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

// --- GLOBALS ---

sqlite3* db = nullptr;


// --- HELPER: Load env ---
void loadEnv() {
    std::ifstream f("secret.env");
    if (!f.is_open()) {std::cerr << "[ENV] secret.env not found\n"; return; }
    std::string line;
    while (std::getline(f, line)) {
        auto pos = line.find('=');
        if (pos == std::string::npos || line[0] == '#') continue;
        setenv(line.substr(0,pos).c_str(), line.substr(pos+1).c_str(), 1);
    }
}

// --- HELPER: Env getter with fallback ---
inline std::string env(const char* key, const std::string& def = "") {
    const char* v = getenv(key);
    return v ? v : def;
}

// --- HELPER: Serve HTML file ---
crow::response serveFile(const std::string& path, const std::string& mine = "text/html") {
    std::ifstream f (path, std::ios::binary);
    if (!f.is_open()) return crow::response(404, "Not Found");
    std::ostringstream ss; ss << f.rdbuf();
    crow::response res(ss.str());
    res.add_header("Content-Type", mime);
    return res;
}

// --- HELPER: Security response headers (CSP, HSTS) ---
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

// --- HELPER: Extract session token from Http cookie ---
std::string getSessionToken(const crow::request& req) {
    std::string cookie = req.get_header_value("Cookie");
    const std::string key = "dp_session=";
    auto pos = cookie.find(key);
    if(pos == std::string::npos) return "";
    auto end = cookie.find(';', pos);
    std::string tok = cookie.substr(pos + key.size(), end == std::string::npos ? std::string::npos : end - pos - key.size());
    // Validate length
    if (tok.size() != 64) return "";
    return tok;

}

// --- HELPER: Authenticate request, returns user_id ot nullopt ---
std::optional<int> authenticate(const crow::request& req) {
    return UserService::validateSession(db, getSessionToken(req));
}

// --- HELPER: Rate limit check, sends 429 if exceeded ---
bool rateLimited(const crow::request& req, crow::response& res, int max = 60, int window = 60) {
     std::string ip = req.get_header_value("X-Forwarded-For");
    if (ip.empty()) ip = req.remote_ip_address;
    // Hash IP to avoid storing PII
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

// --- HELPER: JSON error response ---
crow::response jsonError(int code, const std::string& msg) {
    crow::response res(code, json({{"error", msg}}).dump());
    res.add_header("Content-Type", "application/json");
    return res;
}

// --- HELPER: JSON success response ---
crow::response jsonOk(const json& data) {
    crow::response res(200, data.dump());
    res.add_header("Content-Type", "application/json");
    return res;
}

// --- HELPER: CSRF token from header ---
bool csrfValid(const crow::request& req) {
    // We use double-submit cookie pattern
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

// --- HELPER: Set session cookie ---
void setSessionCookie(crow::response& res, const std::string& token) {
    res.sdd_header("Set-Cookie", "dp_session=" + token +
        "; Path=/; HttpOnly; Secure; SameSite=Lax; Max-Age=86400");
}

void clearSessionCookie(crow::response& res) {
    res.add_header("Set-Cookie",
        "dp_session=; Path=/; HttpOnly; Secure; SameSite=Lax; Max-Age=0");
}

void setCsrfCookie(crow::response& res, const std::string& token) {
    res.add_header("Set-Cookie",
        "dp_csrf=" + token +
        "; Path=/; Secure; SameSite=Lax; Max-Age=86400");
}


// --- HELPER: Build profile JSON from DB ---
std::optional<json> buildProfileJson(const std::string& username) {
    auto user = UserService::findByUsername(db, username);
    if (!user) return std::nullopt;
    if (!user->is_public) return std::nullopt;

    auto stats = UserService::getStats(db, user->user_id);
    auto activity = UserService::getActivity(db, user->user_id, 10);

    json langs = json::array();
    if (stats && !stats->language_json.empty()) {
        try {langs = json::parse(stats->language_json); } catch(...) {}
    }

    json acts = json::array();
    for (auto& a: activity) {
        acts.push_back({
            {"repo", a.repo},
            {"message", a.message},
            {"language", a.language},
            {"commit_sha", a.commit_sha},
            {"pushed_at", a.pushed_at}
        });
    }

    return json{
        {"user", {
            {"username", user->username},
            {"display_name" user->display_name},
            {"avatar_url", user->avatar_url},
            {"bio", user->bio},
            {"location", user->location}.
            {"role", user->role},
            {"github_url", user->github_url},
            {"theme", user->theme},
            {"public", user->public}
        }};
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


// --- HELPER: Generate SVG badge ---
std::string buildBadgeSvg(const User& u, const StatsCache& sc, const std::string& app_url) {
    // Escape for SVG
    auto esc = [](const std::string& s) {
        std::string out;
        for (char c : s) {
            if (c == '<') out += "&lt;";
            else if (c == '>') out += "&gt;";
            else if (c == '&') out += "&amp;";
            else if (c == '"') out += "&quot;";
            else               out += c;
        }
        return out
    };

    std::string name = esc(u.display_name.empty() ? u.username : u.display_name);
    std::string commits = std::to_string(sc.total_commits);
    std::string streak = std::to_string(sc.streak_days);
    std::string repos = std::to_string(sc.repos_count);
    char hours_buf[16]; snprintf(hours_buf, sizeof(hours_buf), "%.1f", sc.hours_coded);
    std::string profile_url = esc(app_url + "/u/" + u.username);
    std::string avatar = esc(u.avatar_url);

     return R"(<?xml version="1.0" encoding="UTF-8"?>
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
  <rect x="0" y="118" width="420" height="12" rx="0" fill="url(#accent)" opacity="0.8"/>
 
  <!-- Avatar -->
  <image href=")" + avatar + R"(" x="18" y="29" width="72" height="72"
         clip-path="url(#avatar-clip)" preserveAspectRatio="xMidYMid slice"/>
  <circle cx="54" cy="65" r="36" fill="none" stroke="#4f46e5" stroke-width="2"/>
  <!-- Live indicator -->
  <circle cx="82" cy="37" r="7" fill="#111827"/>
  <circle cx="82" cy="37" r="4.5" fill="#22c55e"/>
 
  <!-- Name + bio -->
  <text x="108" y="50" font-family="system-ui,sans-serif"
        font-size="17" font-weight="700" fill="#ffffff">)" + name + R"(</text>
  <text x="108" y="68" font-family="system-ui,sans-serif"
        font-size="11" fill="rgba(255,255,255,0.6)">)" + esc(u.bio.substr(0, 40)) + R"(</text>
 
  <!-- Stats row -->
  <text x="108" y="93" font-family="system-ui,sans-serif"
        font-size="12" fill="rgba(255,255,255,0.9)">
    )" + commits + R"( commits  •  )" + streak + R"( day streak</text>
  <text x="108" y="110" font-family="system-ui,sans-serif"
        font-size="12" fill="rgba(255,255,255,0.7)">
    )" + repos + R"( repos  •  )" + std::string(hours_buf) + R"( hours coded</text>
 
  <!-- Profile URL -->
  <text x="18" y="113" font-family="system-ui,sans-serif"
        font-size="9" fill="rgba(255,255,255,0.35)">)" + profile_url + R"(</text>
 
  <a href=")" + profile_url + R"(">
    <rect width="420" height="130" rx="14" fill="transparent"/>
  </a>
</svg>)";

}


// --- BACKROUND: Stats refresh loop (every 10 minutes) ---
// TODO add statsRefreshLoop