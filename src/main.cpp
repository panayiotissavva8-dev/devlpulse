#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <thread>
#include <chrono>
#include <mutex>
#include <vector>
#include <unordered_map>
#include <unordered_set>
#include <optional>
#include <ctime>
#include <cstdlib>

#include <crow.h>
#include <crow/middlewares/cookie_parser.h>
#include <sqlite3.h>
#include <cpr/cpr.h>
#include <nlohmann/json.hpp>
#include <openssl/sha.h>
#include <openssl/hmac.h>
#include <openssl/evp.h>

#include "db/schema.h"
#include "utils/security.h"
#include "services/user_service.h"
#include "services/github_service.h"
#include "services/ws_manager.h"

using json = nlohmann::json;

// ═══════════════════════════════════════════════════════════════
//  GLOBALS
// ═══════════════════════════════════════════════════════════════
sqlite3* db = nullptr;

// WebSocket connection map — global to avoid static-local capture warnings
std::unordered_map<void*, std::string> ws_conn_map;
std::mutex ws_conn_mutex;

// ═══════════════════════════════════════════════════════════════
//  HELPER: Load env
// ═══════════════════════════════════════════════════════════════
void loadEnv() {
    std::ifstream f("secret.env");
    if (!f.is_open()) { std::cerr << "[ENV] secret.env not found\n"; return; }
    std::string line;
    while (std::getline(f, line)) {
        auto pos = line.find('=');
        if (pos == std::string::npos || line[0] == '#') continue;
        setenv(line.substr(0,pos).c_str(), line.substr(pos+1).c_str(), 1);
    }
}

// ═══════════════════════════════════════════════════════════════
//  HELPER: Env getter with fallback
// ═══════════════════════════════════════════════════════════════
inline std::string env(const char* key, const std::string& def = "") {
    const char* v = getenv(key);
    return v ? v : def;
}

// ═══════════════════════════════════════════════════════════════
//  HELPER: Serve HTML file
// ═══════════════════════════════════════════════════════════════
crow::response serveFile(const std::string& path,
                          const std::string& mime = "text/html") {
    std::ifstream f(path, std::ios::binary);
    if (!f.is_open()) return crow::response(404, "Not found");
    std::ostringstream ss; ss << f.rdbuf();
    crow::response res(ss.str());
    res.add_header("Content-Type", mime);
    return res;
}

// ═══════════════════════════════════════════════════════════════
//  HELPER: Security response headers
// ═══════════════════════════════════════════════════════════════
void addSecurityHeaders(crow::response& res) {
    res.add_header("X-Content-Type-Options",  "nosniff");
    res.add_header("X-Frame-Options",         "SAMEORIGIN");
    res.add_header("X-XSS-Protection",        "1; mode=block");
    res.add_header("Referrer-Policy",         "strict-origin-when-cross-origin");
    res.add_header("Strict-Transport-Security","max-age=63072000; includeSubDomains");
    res.add_header("Content-Security-Policy",
        "default-src 'self'; "
        "script-src 'self' 'unsafe-inline'; "
        "style-src 'self' 'unsafe-inline' https://fonts.googleapis.com; "
        "font-src 'self' https://fonts.gstatic.com; "
        "img-src 'self' https://avatars.githubusercontent.com data:; "
        "connect-src 'self' wss:;");
}

// ═══════════════════════════════════════════════════════════════
//  HELPER: Extract session token from HttpOnly cookie
// ═══════════════════════════════════════════════════════════════
std::string getSessionToken(const crow::request& req) {
    std::string cookie = req.get_header_value("Cookie");
    const std::string key = "dp_session=";
    auto pos = cookie.find(key);
    if (pos == std::string::npos) return "";
    auto end = cookie.find(';', pos);
    std::string tok = cookie.substr(pos + key.size(),
        end == std::string::npos ? std::string::npos : end - pos - key.size());
    if (tok.size() != 64) return "";
    return tok;
}

// ═══════════════════════════════════════════════════════════════
//  HELPER: Authenticate request → returns user_id or nullopt
// ═══════════════════════════════════════════════════════════════
std::optional<int> authenticate(const crow::request& req) {
    return UserService::validateSession(db, getSessionToken(req));
}

// ═══════════════════════════════════════════════════════════════
//  HELPER: Rate limit check → sends 429 if exceeded
// ═══════════════════════════════════════════════════════════════
bool rateLimited(const crow::request& req, crow::response& res,
                 int max = 60, int window = 60) {
    std::string ip = req.get_header_value("X-Forwarded-For");
    if (ip.empty()) ip = req.remote_ip_address;
    std::string key = Security::sha256(ip).substr(0, 16);
    auto [allowed, remaining] = Security::checkRateLimit(db, key, max, window);
    res.add_header("X-RateLimit-Limit",     std::to_string(max));
    res.add_header("X-RateLimit-Remaining", std::to_string(remaining));
    if (!allowed) {
        res.code = 429;
        res.write(R"({"error":"Too many requests"})");
        res.add_header("Content-Type", "application/json");
        res.add_header("Retry-After", std::to_string(window));
        res.end();
        return true;
    }
    return false;
}

// ═══════════════════════════════════════════════════════════════
//  HELPER: JSON error response
// ═══════════════════════════════════════════════════════════════
crow::response jsonError(int code, const std::string& msg) {
    crow::response res(code, json({{"error", msg}}).dump());
    res.add_header("Content-Type", "application/json");
    return res;
}

// ═══════════════════════════════════════════════════════════════
//  HELPER: JSON success response
// ═══════════════════════════════════════════════════════════════
crow::response jsonOk(const json& data) {
    crow::response res(200, data.dump());
    res.add_header("Content-Type", "application/json");
    return res;
}

// ═══════════════════════════════════════════════════════════════
//  HELPER: CSRF token from header
// ═══════════════════════════════════════════════════════════════
bool csrfValid(const crow::request& req) {
    std::string hdr    = req.get_header_value("X-CSRF-Token");
    std::string cookie = req.get_header_value("Cookie");
    const std::string key = "dp_csrf=";
    auto pos = cookie.find(key);
    if (pos == std::string::npos || hdr.empty()) return false;
    auto end = cookie.find(';', pos);
    std::string ck = cookie.substr(pos + key.size(),
        end == std::string::npos ? std::string::npos : end - pos - key.size());
    return Security::safeCompare(hdr, ck) && hdr.size() == 32;
}

// ═══════════════════════════════════════════════════════════════
//  HELPER: Set session cookie
// ═══════════════════════════════════════════════════════════════
void setSessionCookie(crow::response& res, const std::string& token) {
    res.add_header("Set-Cookie",
        "dp_session=" + token +
        "; Path=/; HttpOnly; SameSite=Lax; Max-Age=86400");
}

void clearSessionCookie(crow::response& res) {
    res.add_header("Set-Cookie",
        "dp_session=; Path=/; HttpOnly; SameSite=Lax; Max-Age=0");
}

void setCsrfCookie(crow::response& res, const std::string& token) {
    res.add_header("Set-Cookie",
        "dp_csrf=" + token +
        "; Path=/; SameSite=Lax; Max-Age=86400");
}

// ═══════════════════════════════════════════════════════════════
//  HELPER: Build profile JSON from DB
// ═══════════════════════════════════════════════════════════════
std::optional<json> buildProfileJson(const std::string& username) {
    auto user = UserService::findByUsername(db, username);
    if (!user) return std::nullopt;
    if (!user->is_public) return std::nullopt;

    auto stats    = UserService::getStats(db, user->user_id);
    auto activity = UserService::getActivity(db, user->user_id, 10);

    json langs = json::array();
    if (stats && !stats->languages_json.empty()) {
        try { langs = json::parse(stats->languages_json); } catch(...) {}
    }

    json acts = json::array();
    for (auto& a : activity) {
        acts.push_back({
            {"repo",       a.repo},
            {"message",    a.message},
            {"language",   a.language},
            {"commit_sha", a.commit_sha},
            {"pushed_at",  a.pushed_at}
        });
    }

    return json{
        {"user", {
            {"username",      user->username},
            {"display_name",  user->display_name},
            {"avatar_url",    user->avatar_url},
            {"bio",           user->bio},
            {"location",      user->location},
            {"role",          user->role},
            {"github_url",    user->github_url},
            {"theme",         user->theme},
            {"public",        user->is_public}
        }},
        {"stats", stats ? json{
            {"total_commits",     stats->total_commits},
            {"streak_days",       stats->streak_days},
            {"best_streak",       stats->best_streak},
            {"repos_count",       stats->repos_count},
            {"hours_coded",       stats->hours_coded},
            {"commits_today",     stats->commits_today},
            {"repos_this_month",  stats->repos_this_month},
            {"hours_this_week",   stats->hours_this_week},
            {"top_language",      stats->top_language}
        } : json(nullptr)},
        {"languages", langs},
        {"activity",  acts}
    };
}

// ═══════════════════════════════════════════════════════════════
//  HELPER: Generate SVG badge for /badge/<username>
//  NOTE: Raw string delimiters use R"SVG(...)SVG" to avoid
//        the compiler misreading rgba(x,x,x,0.N)>" as C++
// ═══════════════════════════════════════════════════════════════
std::string buildBadgeSvg(const User& u, const StatsCache& sc,
                          const std::string& app_url) {

    auto esc = [](const std::string& s) {
        std::string out;
        out.reserve(s.size());
        for (char c : s) {
            if      (c == '<')  out += "&lt;";
            else if (c == '>')  out += "&gt;";
            else if (c == '&')  out += "&amp;";
            else if (c == '"')  out += "&quot;";
            else                out += c;
        }
        return out;
    };

    std::string name = esc(u.display_name.empty() ? u.username : u.display_name);

    std::string bio = u.bio;
    if (bio.size() > 40) bio = bio.substr(0, 40);
    bio = esc(bio);

    std::string commits = std::to_string(sc.total_commits);
    std::string streak  = std::to_string(sc.streak_days);
    std::string repos   = std::to_string(sc.repos_count);

    char hours_buf[16];
    snprintf(hours_buf, sizeof(hours_buf), "%.1f", sc.hours_coded);
    std::string hours = hours_buf;

    std::string profile_url = esc(app_url + "/u/" + u.username);
    std::string avatar = esc(u.avatar_url);

    std::string svg;
    svg.reserve(3000);

    // Part 1: everything up to the avatar href
    svg += R"SVG(<?xml version="1.0" encoding="UTF-8"?>
<svg xmlns="http://www.w3.org/2000/svg"
     xmlns:xlink="http://www.w3.org/1999/xlink"
     width="420" height="130" viewBox="0 0 420 130">

  <defs>
    <linearGradient id="bg" x1="0" y1="0" x2="1" y2="0">
      <stop offset="0%" stop-color="#1e1b4b"/>
      <stop offset="100%" stop-color="#1e3a8a"/>
    </linearGradient>

    <linearGradient id="accent" x1="0" y1="0" x2="1" y2="0">
      <stop offset="0%" stop-color="#6366f1"/>
      <stop offset="100%" stop-color="#2563eb"/>
    </linearGradient>

    <clipPath id="avatar-clip">
      <circle cx="54" cy="65" r="36"/>
    </clipPath>
  </defs>

  <rect width="420" height="130" rx="14" fill="url(#bg)"/>
  <rect x="0" y="118" width="420" height="12" fill="url(#accent)" opacity="0.8"/>

  <!-- Avatar -->
  <image href=")SVG";

    svg += avatar;

    // Part 2: after avatar href, up to name text node
    svg += R"SVG(" x="18" y="29" width="72" height="72"
         clip-path="url(#avatar-clip)"
         preserveAspectRatio="xMidYMid slice"/>

  <circle cx="54" cy="65" r="36" fill="none" stroke="#4f46e5" stroke-width="2"/>

  <!-- Live indicator -->
  <circle cx="82" cy="37" r="7" fill="#111827"/>
  <circle cx="82" cy="37" r="4.5" fill="#22c55e"/>

  <!-- Name -->
  <text x="108" y="50" font-family="system-ui,sans-serif"
        font-size="17" font-weight="700" fill="#ffffff">)SVG";

    svg += name;

    // Part 3: bio text element open tag
    svg += R"SVG(</text>

  <!-- Bio -->
  <text x="108" y="68" font-family="system-ui,sans-serif"
        font-size="11" fill="rgba(255,255,255,0.6)">)SVG";

    svg += bio;

    // Part 4: stats line 1 open tag
    svg += R"SVG(</text>

  <!-- Stats -->
  <text x="108" y="93" font-family="system-ui,sans-serif"
        font-size="12" fill="rgba(255,255,255,0.9)">)SVG";

    svg += commits;
    svg += " commits  \xe2\x80\xa2  "; // bullet character
    svg += streak;

    // Part 5: stats line 2 open tag
    svg += R"SVG( day streak</text>

  <text x="108" y="110" font-family="system-ui,sans-serif"
        font-size="12" fill="rgba(255,255,255,0.7)">)SVG";

    svg += repos;
    svg += " repos  \xe2\x80\xa2  ";
    svg += hours;

    // Part 6: profile url text element open tag
    svg += R"SVG( hours coded</text>

  <!-- Profile URL -->
  <text x="18" y="113" font-family="system-ui,sans-serif"
        font-size="9" fill="rgba(255,255,255,0.35)">)SVG";

    svg += profile_url;

    // Part 7: closing link + svg
    svg += R"SVG(</text>

  <a href=")SVG";
    svg += profile_url;
    svg += R"SVG(">
    <rect width="420" height="130" rx="14" fill="transparent"/>
  </a>

</svg>
)SVG";

    return svg;
}

// ═══════════════════════════════════════════════════════════════
//  BACKGROUND: Stats refresh loop (every 10 min)
// ═══════════════════════════════════════════════════════════════
void statsRefreshLoop() {
    while (true) {
        std::this_thread::sleep_for(std::chrono::minutes(10));
        std::cout << "[Refresh] Updating stats cache...\n";

        sqlite3_stmt* stmt;
        sqlite3_prepare_v2(db,
            "SELECT user_id, username FROM users", -1, &stmt, nullptr);

        while (sqlite3_step(stmt) == SQLITE_ROW) {
            int uid = sqlite3_column_int(stmt, 0);
            const char* p = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
            if (!p) continue;
            std::string username(p);

            auto sc   = GitHubService::fetchUserStats(username);
            auto acts = GitHubService::fetchActivity(username);

            auto existing = UserService::getStats(db, uid);
            if (existing) {
                sc.hours_coded      = existing->hours_coded;
                sc.hours_this_week  = existing->hours_this_week;
                sc.streak_days      = existing->streak_days;
                sc.best_streak      = existing->best_streak;
                sc.commits_today    = existing->commits_today;
            }

            UserService::upsertStats(db, uid, sc);
            for (auto& a : acts)
                UserService::insertActivity(db, uid, a);

            std::cout << "[Refresh] Updated: " << username << "\n";
        }
        sqlite3_finalize(stmt);
    }
}

// ═══════════════════════════════════════════════════════════════
//  MAIN
// ═══════════════════════════════════════════════════════════════
int main() {
    loadEnv();

    const std::string DB_PATH = env("DB_PATH", "data/devpulse.sqlite");
    if (sqlite3_open(DB_PATH.c_str(), &db) != SQLITE_OK) {
        std::cerr << "[DB] Cannot open: " << sqlite3_errmsg(db) << "\n";
        return 1;
    }
    initializeDatabase(db);

    std::thread(statsRefreshLoop).detach();

    crow::App<crow::CookieParser> app;

    // ═══════════════════════════════════════════════════════════
    //  STATIC FILES
    // ═══════════════════════════════════════════════════════════
    CROW_ROUTE(app, "/web/<path>")([](const std::string& path) {
        std::string full = "web/" + path;
        std::string mime = "application/octet-stream";
        auto dot = full.rfind('.');
        if (dot != std::string::npos) {
            std::string ext = full.substr(dot);
            if (ext == ".css")   mime = "text/css";
            else if (ext == ".js")    mime = "application/javascript";
            else if (ext == ".png")   mime = "image/png";
            else if (ext == ".svg")   mime = "image/svg+xml";
            else if (ext == ".ico")   mime = "image/x-icon";
            else if (ext == ".woff2") mime = "font/woff2";
        }
        return serveFile(full, mime);
    });

    // ═══════════════════════════════════════════════════════════
    //  PAGES
    // ═══════════════════════════════════════════════════════════
    CROW_ROUTE(app, "/")([](const crow::request& req) {
        auto res = serveFile("web/html/index.html");
        addSecurityHeaders(res);
        std::string csrf = Security::generateToken(16);
        res.add_header("Set-Cookie",
            "dp_csrf=" + csrf + "; Path=/; SameSite=Lax; Max-Age=86400");
        std::string body = res.body;
        size_t pos = body.find("__CSRF__");
        if (pos != std::string::npos) body.replace(pos, 8, csrf);
        res.body = body;
        return res;
    });

    CROW_ROUTE(app, "/dashboard")([](const crow::request& req) {
        auto uid = authenticate(req);
        if (!uid) {
            crow::response res(302);
            res.add_header("Location", "/");
            return res;
        }
        auto res = serveFile("web/html/dashboard.html");
        addSecurityHeaders(res);
        return res;
    });

    CROW_ROUTE(app, "/u/<string>")([](const crow::request& req,
                                      const std::string& username) {
        if (!Security::isValidUsername(username))
            return jsonError(400, "Invalid username");
        auto user = UserService::findByUsername(db, Security::sanitize(username, 39));
        if (!user || !user->is_public) return crow::response(404);
        auto res = serveFile("web/html/profile.html");
        addSecurityHeaders(res);
        return res;
    });

    CROW_ROUTE(app, "/embed/<string>")([](const crow::request& req,
                                          const std::string& username) {
        if (!Security::isValidUsername(username))
            return crow::response(400);
        auto res = serveFile("web/html/embed.html");
        res.add_header("X-Frame-Options",  "ALLOWALL");
        res.add_header("Content-Security-Policy", "default-src 'self' 'unsafe-inline';");
        return res;
    });

    // ═══════════════════════════════════════════════════════════
    //  AUTH: GitHub OAuth
    // ═══════════════════════════════════════════════════════════
    CROW_ROUTE(app, "/auth/github")([](const crow::request& req) {
        crow::response res(302);
        std::string state = Security::generateToken(16);
        res.add_header("Set-Cookie",
    "dp_oauth_state=" + state +
    "; Path=/; HttpOnly; SameSite=Lax; Max-Age=300");
        res.add_header("Location",
            GitHubService::oauthRedirectUrl(
                env("GITHUB_CLIENT_ID"),
                env("APP_URL", "http://localhost:8080"),
                state));
        return res;
    });

    CROW_ROUTE(app, "/auth/github/callback")([](const crow::request& req) {
        auto params = req.url_params;
        std::string code  = params.get("code")  ? params.get("code")  : "";
        std::string state = params.get("state") ? params.get("state") : "";

        std::string cookie = req.get_header_value("Cookie");
        const std::string sk = "dp_oauth_state=";
        auto sp = cookie.find(sk);
        std::string stored_state;
        if (sp != std::string::npos) {
            auto se = cookie.find(';', sp);
            stored_state = cookie.substr(sp + sk.size(),
                se == std::string::npos ? std::string::npos : se - sp - sk.size());
        }

        if (code.empty() || !Security::safeCompare(state, stored_state)) {
            crow::response res(302);
            res.add_header("Location", "/?error=oauth_failed");
            return res;
        }

        std::string access_token = GitHubService::exchangeCode(
            code, env("GITHUB_CLIENT_ID"), env("GITHUB_CLIENT_SECRET"));
        if (access_token.empty()) {
            crow::response res(302);
            res.add_header("Location", "/?error=token_failed");
            return res;
        }

        auto gh_user = GitHubService::fetchAuthUser(access_token);
        if (!gh_user) {
            crow::response res(302);
            res.add_header("Location", "/?error=user_failed");
            return res;
        }

        int uid = UserService::upsertUser(db, *gh_user);
        if (uid <= 0) {
            crow::response res(302);
            res.add_header("Location", "/?error=db_failed");
            return res;
        }

        std::string uname = gh_user->username;
        std::thread([uname, uid]() {
            auto sc   = GitHubService::fetchUserStats(uname, "");
            auto acts = GitHubService::fetchActivity(uname);
            UserService::upsertStats(db, uid, sc);
            for (auto& a : acts) UserService::insertActivity(db, uid, a);
        }).detach();

        std::string ip_hash = Security::sha256(
            req.get_header_value("X-Forwarded-For").empty()
                ? req.remote_ip_address
                : req.get_header_value("X-Forwarded-For")).substr(0, 16);
        std::string token = UserService::createSession(db, uid, ip_hash);

        std::cout << "[Auth] Token created: '" << token << "' length=" << token.size() << "\n";

        crow::response res(302);
        setSessionCookie(res, token);
        res.add_header("Set-Cookie",
            "dp_oauth_state=; Path=/; HttpOnly; Max-Age=0");
        res.add_header("Location", "/dashboard");
        return res;
    });

    CROW_ROUTE(app, "/auth/logout").methods("POST"_method)
    ([](const crow::request& req) {
        if (!csrfValid(req)) return jsonError(403, "Invalid CSRF");
        std::string tok = getSessionToken(req);
        if (!tok.empty()) UserService::deleteSession(db, tok);
        crow::response res(302);
        clearSessionCookie(res);
        res.add_header("Location", "/");
        return res;
    });

    // ═══════════════════════════════════════════════════════════
    //  API: /api/me
    // ═══════════════════════════════════════════════════════════
    CROW_ROUTE(app, "/api/me")([](const crow::request& req) {
        auto uid = authenticate(req);
        if (!uid) return jsonError(401, "Unauthorized");

        sqlite3_stmt* s;
        sqlite3_prepare_v2(db,
            "SELECT username FROM users WHERE user_id=?", -1, &s, nullptr);
        sqlite3_bind_int(s, 1, *uid);
        std::string uname;
        if (sqlite3_step(s) == SQLITE_ROW) {
            const char* p = reinterpret_cast<const char*>(sqlite3_column_text(s, 0));
            if (p) uname = p;
        }
        sqlite3_finalize(s);

        auto profile = buildProfileJson(uname);
        if (!profile) {
            auto user = UserService::findByUsername(db, uname);
            if (!user) return jsonError(404, "User not found");
            return jsonOk({{"user", {
                {"username",     user->username},
                {"display_name", user->display_name},
                {"avatar_url",   user->avatar_url},
                {"bio",          user->bio},
                {"theme",        user->theme},
                {"public",       user->is_public}
            }}});
        }
        return jsonOk(*profile);
    });

    CROW_ROUTE(app, "/api/me/theme").methods("PATCH"_method)
    ([](const crow::request& req) {
        if (!csrfValid(req)) return jsonError(403, "Invalid CSRF");
        auto uid = authenticate(req);
        if (!uid) return jsonError(401, "Unauthorized");
        try {
            auto body  = json::parse(req.body);
            std::string theme = body.value("theme", "dark");
            UserService::updateTheme(db, *uid, theme);
            return jsonOk({{"theme", theme}});
        } catch (...) { return jsonError(400, "Invalid JSON"); }
    });

    CROW_ROUTE(app, "/api/me/public").methods("PATCH"_method)
    ([](const crow::request& req) {
        if (!csrfValid(req)) return jsonError(403, "Invalid CSRF");
        auto uid = authenticate(req);
        if (!uid) return jsonError(401, "Unauthorized");
        try {
            auto body = json::parse(req.body);
            bool pub  = body.value("public", true);
            UserService::updatePublic(db, *uid, pub);
            return jsonOk({{"public", pub}});
        } catch (...) { return jsonError(400, "Invalid JSON"); }
    });

    // ═══════════════════════════════════════════════════════════
    //  API: /api/profile/<username>
    // ═══════════════════════════════════════════════════════════
    CROW_ROUTE(app, "/api/profile/<string>")([](const crow::request& req,
                                                 const std::string& username) {
        crow::response res(200);
        if (rateLimited(req, res, 120, 60)) return res;
        if (!Security::isValidUsername(username)) return jsonError(400, "Invalid username");
        auto profile = buildProfileJson(Security::sanitize(username, 39));
        if (!profile) return jsonError(404, "User not found or private");
        return jsonOk(*profile);
    });

    // ═══════════════════════════════════════════════════════════
    //  API: /api/stats/<username>
    // ═══════════════════════════════════════════════════════════
    CROW_ROUTE(app, "/api/stats/<string>")([](const crow::request& req,
                                               const std::string& username) {
        crow::response res(200);
        if (rateLimited(req, res, 120, 60)) return res;
        if (!Security::isValidUsername(username)) return jsonError(400, "Invalid username");

        auto user = UserService::findByUsername(db, Security::sanitize(username, 39));
        if (!user || !user->is_public) return jsonError(404, "Not found");

        auto sc = UserService::getStats(db, user->user_id);
        if (!sc) return jsonOk({{"total_commits",0},{"streak_days",0},
                                 {"repos_count",0},{"hours_coded",0.0}});

        json langs = json::array();
        try { langs = json::parse(sc->languages_json); } catch (...) {}

        return jsonOk({
            {"total_commits",    sc->total_commits},
            {"streak_days",      sc->streak_days},
            {"best_streak",      sc->best_streak},
            {"repos_count",      sc->repos_count},
            {"hours_coded",      sc->hours_coded},
            {"commits_today",    sc->commits_today},
            {"repos_this_month", sc->repos_this_month},
            {"hours_this_week",  sc->hours_this_week},
            {"top_language",     sc->top_language},
            {"languages",        langs}
        });
    });

    // ═══════════════════════════════════════════════════════════
    //  API: /api/activity/<username>
    // ═══════════════════════════════════════════════════════════
    CROW_ROUTE(app, "/api/activity/<string>")([](const crow::request& req,
                                                  const std::string& username) {
        crow::response res(200);
        if (rateLimited(req, res, 120, 60)) return res;
        if (!Security::isValidUsername(username)) return jsonError(400, "Invalid username");

        auto user = UserService::findByUsername(db, Security::sanitize(username, 39));
        if (!user || !user->is_public) return jsonError(404, "Not found");

        int limit = 10;
        const char* lp = req.url_params.get("limit");
        if (lp) limit = std::max(1, std::min(50, atoi(lp)));

        auto acts = UserService::getActivity(db, user->user_id, limit);
        json arr  = json::array();
        for (auto& a : acts) arr.push_back({
            {"repo",       a.repo},
            {"message",    a.message},
            {"language",   a.language},
            {"commit_sha", a.commit_sha},
            {"pushed_at",  a.pushed_at}
        });
        return jsonOk({{"activity", arr}});
    });

    // ═══════════════════════════════════════════════════════════
    //  API: /api/search?q=<query>
    // ═══════════════════════════════════════════════════════════
    CROW_ROUTE(app, "/api/search")([](const crow::request& req) {
        crow::response res(200);
        if (rateLimited(req, res, 30, 60)) return res;

        const char* qp = req.url_params.get("q");
        if (!qp || std::string(qp).empty()) return jsonError(400, "Missing q");
        std::string q = Security::sanitize(std::string(qp), 50);

        auto users = UserService::searchUsers(db, q);
        json arr   = json::array();
        for (auto& u : users) arr.push_back({
            {"username",     u.username},
            {"display_name", u.display_name},
            {"avatar_url",   u.avatar_url},
            {"bio",          u.bio}
        });
        return jsonOk({{"users", arr}});
    });

    // ═══════════════════════════════════════════════════════════
    //  API: /api/viewers/<username>
    // ═══════════════════════════════════════════════════════════
    CROW_ROUTE(app, "/api/viewers/<string>")([](const std::string& username) {
        if (!Security::isValidUsername(username)) return jsonError(400, "Invalid");
        int count = WSManager::instance().viewerCount(username);
        return jsonOk({{"viewers", count}});
    });

    // ═══════════════════════════════════════════════════════════
    //  BADGE: /badge/<username>
    // ═══════════════════════════════════════════════════════════
    CROW_ROUTE(app, "/badge/<string>")([](const crow::request& req,
                                          const std::string& username) {
        crow::response res(200);
        if (rateLimited(req, res, 200, 60)) return res;
        if (!Security::isValidUsername(username)) return crow::response(400);

        auto user = UserService::findByUsername(db, Security::sanitize(username, 39));
        if (!user) return crow::response(404);

        StatsCache sc;
        auto cached = UserService::getStats(db, user->user_id);
        if (cached) sc = *cached;

        std::string svg = buildBadgeSvg(*user, sc, env("APP_URL","https://devpulse.fly.dev"));
        crow::response badge_res(svg);
        badge_res.add_header("Content-Type",  "image/svg+xml");
        badge_res.add_header("Cache-Control", "no-cache, max-age=300");
        badge_res.add_header("ETag", std::to_string(sc.last_updated));
        return badge_res;
    });

    // ═══════════════════════════════════════════════════════════
    //  WEBHOOK: /webhook/<username>/github
    // ═══════════════════════════════════════════════════════════
    CROW_ROUTE(app, "/webhook/<string>/github").methods("POST"_method)
    ([](const crow::request& req, const std::string& username) {
        crow::response res(200);
        if (rateLimited(req, res, 10, 10)) return res;
        if (!Security::isValidUsername(username)) return crow::response(400);

        auto user = UserService::findByUsername(db, Security::sanitize(username, 39));
        if (!user) return crow::response(404);

        std::string sig = req.get_header_value("X-Hub-Signature-256");
        if (sig.empty()) return crow::response(401);

        std::string expected = Security::hmacSha256(user->webhook_secret, req.body);
        if (!Security::safeCompare(sig, expected)) {
            std::cerr << "[Webhook] HMAC mismatch for " << username << "\n";
            return crow::response(401);
        }

        try {
            auto body = json::parse(req.body);
            if (!body.contains("commits") || !body["commits"].is_array())
                return crow::response(204);

            std::string repo    = body["repository"].value("full_name", "");
            auto& commits       = body["commits"];
            int count           = (int)commits.size();
            std::string message = commits[0].value("message", "");
            message = message.substr(0, message.find('\n'));
            message = Security::sanitize(message, 200);
            repo    = Security::sanitize(repo, 200);

            ActivityItem a;
            a.repo      = repo;
            a.message   = message;
            a.pushed_at = Security::nowSec();
            UserService::insertActivity(db, user->user_id, a);

            auto sc = UserService::getStats(db, user->user_id);


              // Recalculate streak based on today having activity
int today = (int)(Security::nowSec() / 86400);
int last  = sc->last_updated > 0 ? (int)(sc->last_updated / 86400) : 0;
if (today > last) {
    sc->streak_days += 1;
    if (sc->streak_days > sc->best_streak)
        sc->best_streak = sc->streak_days;
    UserService::upsertStats(db, user->user_id, *sc);
}

            if (sc) {
    sc->commits_today   += count;
    sc->total_commits   += count;
    sc->hours_coded     += count * 0.25;   // ~15 min per commit
    sc->hours_this_week += count * 0.25;
    sc->last_updated     = Security::nowSec();
    UserService::upsertStats(db, user->user_id, *sc);
}


            broadcastCommit(username, repo, message, a.language, count);
            if (sc) broadcastStatsUpdate(username, sc->total_commits, sc->streak_days);

            std::cout << "[Webhook] " << username << " pushed " << count
                      << " commit(s) to " << repo << "\n";
        } catch (const std::exception& e) {
            std::cerr << "[Webhook] Parse error: " << e.what() << "\n";
            return crow::response(400);
        }

        return crow::response(200, "ok");
    });

    // ═══════════════════════════════════════════════════════════
    //  WEBSOCKET: /ws/<username>
    //
    //  Crow does NOT forward path params into WS handlers.
    //  Fix: client sends {"type":"join","username":"..."} as
    //  its very first message, which registers the connection.
    //  ws_conn_map/ws_conn_mutex are global (above main()).
    // ═══════════════════════════════════════════════════════════
    CROW_WEBSOCKET_ROUTE(app, "/ws/<string>")
    .onopen([](crow::websocket::connection& conn) {
        std::lock_guard<std::mutex> lock(ws_conn_mutex);
        ws_conn_map[&conn] = ""; // pending join handshake
    })
    .onclose([](crow::websocket::connection& conn,
                const std::string& /*reason*/) {
        std::string username;
        {
            std::lock_guard<std::mutex> lock(ws_conn_mutex);
            auto it = ws_conn_map.find(&conn);
            if (it != ws_conn_map.end()) {
                username = it->second;
                ws_conn_map.erase(it);
            }
        }
        if (!username.empty()) {
            WSManager::instance().remove(username, &conn);
            std::cout << "[WS] Disconnected: " << username << "\n";
        }
    })
    .onmessage([](crow::websocket::connection& conn,
                  const std::string& data,
                  bool is_binary) {
        if (is_binary) return;

        // Look up username for this connection
        std::string username;
        {
            std::lock_guard<std::mutex> lock(ws_conn_mutex);
            auto it = ws_conn_map.find(&conn);
            if (it != ws_conn_map.end()) username = it->second;
        }

        // ── Handshake: first message must be join ──────────────
        if (username.empty()) {
            try {
                auto j = json::parse(data);
                if (j.value("type", "") != "join") return;
                std::string uname = j.value("username", "");
                if (!Security::isValidUsername(uname)) {
                    conn.close("Invalid username");
                    return;
                }
                {
                    std::lock_guard<std::mutex> lock(ws_conn_mutex);
                    ws_conn_map[&conn] = uname;
                }
                WSManager::instance().add(uname, &conn);

                // Send initial snapshot
                auto user = UserService::findByUsername(db, uname);
                if (user) {
                    auto sc   = UserService::getStats(db, user->user_id);
                    auto acts = UserService::getActivity(db, user->user_id, 5);
                    json snap;
                    snap["type"] = "snapshot";
                    if (sc) snap["stats"] = {
                        {"total_commits", sc->total_commits},
                        {"streak_days",   sc->streak_days},
                        {"repos_count",   sc->repos_count},
                        {"hours_coded",   sc->hours_coded}
                    };
                    json arr = json::array();
                    for (auto& a : acts) arr.push_back({
                        {"repo",      a.repo},
                        {"message",   a.message},
                        {"pushed_at", a.pushed_at}
                    });
                    snap["activity"] = arr;
                    try { conn.send_text(snap.dump()); } catch(...) {}
                }
                std::cout << "[WS] Joined: " << uname << " ("
                          << WSManager::instance().viewerCount(uname) << " viewers)\n";
            } catch(...) {}
            return;
        }

        // ── Normal messages ────────────────────────────────────
        if (data == "ping") {
            try { conn.send_text("pong"); } catch(...) {}
            return;
        }
        if (data == "refresh") {
            auto user = UserService::findByUsername(db, username);
            if (!user) return;
            auto sc = UserService::getStats(db, user->user_id);
            if (!sc) return;
            try {
                conn.send_text(json({
                    {"type",          "stats_update"},
                    {"total_commits", sc->total_commits},
                    {"streak_days",   sc->streak_days},
                    {"repos_count",   sc->repos_count},
                    {"hours_coded",   sc->hours_coded}
                }).dump());
            } catch(...) {}
        }
    });

    // ═══════════════════════════════════════════════════════════
    //  404 FALLBACK
    // ═══════════════════════════════════════════════════════════
    CROW_CATCHALL_ROUTE(app)([](const crow::request& req) {
        auto res = serveFile("web/html/404.html");
        res.code = 404;
        addSecurityHeaders(res);
        return res;
    });

    // ═══════════════════════════════════════════════════════════
    //  RUN
    // ═══════════════════════════════════════════════════════════
    int port = std::stoi(env("PORT", "8080"));
    std::cout << "[DevPulse] Starting on port " << port << "\n";
    app.bindaddr("0.0.0.0").port(port).multithreaded().run();

    sqlite3_close(db);
    return 0;
}