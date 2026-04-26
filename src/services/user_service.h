#pragma once
#include <sqlite3.h>
#include <string>
#include <optional>
#include "../utils/security.h"
 
struct User {
    int         user_id     = 0;
    std::string github_id;
    std::string username;
    std::string display_name;
    std::string avatar_url;
    std::string bio;
    std::string location;
    std::string role;
    std::string github_url;
    std::string theme;
    std::string webhook_secret;
    bool        is_public   = true;
    long long   created_at  = 0;
};
 
struct StatsCache {
    int         total_commits     = 0;
    int         streak_days       = 0;
    int         best_streak       = 0;
    int         repos_count       = 0;
    double      hours_coded       = 0;
    int         commits_today     = 0;
    int         repos_this_month  = 0;
    double      hours_this_week   = 0;
    std::string top_language;
    std::string languages_json;
    long long   last_updated      = 0;
};
 
struct ActivityItem {
    int         id         = 0;
    std::string repo;
    std::string message;
    std::string language;
    std::string commit_sha;
    long long   pushed_at  = 0;
};
 
namespace UserService {
 
inline std::optional<User> findByGithubId(sqlite3* db,
                                            const std::string& github_id) {
    sqlite3_stmt* s;
    sqlite3_prepare_v2(db,
        "SELECT user_id,github_id,username,display_name,avatar_url,bio,"
        "location,role,github_url,theme,webhook_secret,public,created_at"
        " FROM users WHERE github_id=?", -1, &s, nullptr);
    sqlite3_bind_text(s, 1, github_id.c_str(), -1, SQLITE_TRANSIENT);
    std::optional<User> result;
    if (sqlite3_step(s) == SQLITE_ROW) {
        User u;
        u.user_id      = sqlite3_column_int(s, 0);
        auto txt = [&](int col) -> std::string {
            auto p = sqlite3_column_text(s, col);
            return p ? reinterpret_cast<const char*>(p) : "";
        };
        u.github_id    = txt(1); u.username    = txt(2);
        u.display_name = txt(3); u.avatar_url  = txt(4);
        u.bio          = txt(5); u.location    = txt(6);
        u.role         = txt(7); u.github_url  = txt(8);
        u.theme        = txt(9); u.webhook_secret = txt(10);
        u.is_public    = sqlite3_column_int(s, 11);
        u.created_at   = sqlite3_column_int64(s, 12);
        result = u;
    }
    sqlite3_finalize(s);
    return result;
}
 
inline std::optional<User> findByUsername(sqlite3* db,
                                           const std::string& username) {
    sqlite3_stmt* s;
    sqlite3_prepare_v2(db,
        "SELECT user_id,github_id,username,display_name,avatar_url,bio,"
        "location,role,github_url,theme,webhook_secret,public,created_at"
        " FROM users WHERE username=?", -1, &s, nullptr);
    sqlite3_bind_text(s, 1, username.c_str(), -1, SQLITE_TRANSIENT);
    std::optional<User> result;
    if (sqlite3_step(s) == SQLITE_ROW) {
        User u;
        u.user_id = sqlite3_column_int(s, 0);
        auto txt = [&](int col) -> std::string {
            auto p = sqlite3_column_text(s, col);
            return p ? reinterpret_cast<const char*>(p) : "";
        };
        u.github_id = txt(1); u.username = txt(2);
        u.display_name = txt(3); u.avatar_url = txt(4);
        u.bio = txt(5); u.location = txt(6);
        u.role = txt(7); u.github_url = txt(8);
        u.theme = txt(9); u.webhook_secret = txt(10);
        u.is_public = sqlite3_column_int(s, 11);
        u.created_at = sqlite3_column_int64(s, 12);
        result = u;
    }
    sqlite3_finalize(s);
    return result;
}
 
inline int upsertUser(sqlite3* db, const User& u) {
    std::string webhook = Security::generateToken(32);
    sqlite3_stmt* s;
    sqlite3_prepare_v2(db, R"(
        INSERT INTO users(github_id,username,display_name,avatar_url,bio,
                          location,role,github_url,webhook_secret,created_at)
        VALUES(?,?,?,?,?,?,?,?,?,?)
        ON CONFLICT(github_id) DO UPDATE SET
            username=excluded.username,
            display_name=excluded.display_name,
            avatar_url=excluded.avatar_url,
            bio=excluded.bio,
            location=excluded.location,
            github_url=excluded.github_url
        RETURNING user_id
    )", -1, &s, nullptr);
    sqlite3_bind_text(s, 1, u.github_id.c_str(),    -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(s, 2, u.username.c_str(),     -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(s, 3, u.display_name.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(s, 4, u.avatar_url.c_str(),   -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(s, 5, u.bio.c_str(),          -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(s, 6, u.location.c_str(),     -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(s, 7, u.role.c_str(),         -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(s, 8, u.github_url.c_str(),   -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(s, 9, webhook.c_str(),         -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(s, 10, Security::nowSec());
    int uid = 0;
    if (sqlite3_step(s) == SQLITE_ROW) uid = sqlite3_column_int(s, 0);
    sqlite3_finalize(s);
    if (!uid) uid = (int)sqlite3_last_insert_rowid(db);
    return uid;
}
 
inline bool updateTheme(sqlite3* db, int user_id, const std::string& theme) {
    if (theme != "dark" && theme != "light") return false;
    sqlite3_stmt* s;
    sqlite3_prepare_v2(db,
        "UPDATE users SET theme=? WHERE user_id=?", -1, &s, nullptr);
    sqlite3_bind_text(s, 1, theme.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(s, 2, user_id);
    sqlite3_step(s);
    sqlite3_finalize(s);
    return true;
}
 
inline bool updatePublic(sqlite3* db, int user_id, bool pub) {
    sqlite3_stmt* s;
    sqlite3_prepare_v2(db,
        "UPDATE users SET public=? WHERE user_id=?", -1, &s, nullptr);
    sqlite3_bind_int(s, 1, pub ? 1 : 0);
    sqlite3_bind_int(s, 2, user_id);
    sqlite3_step(s);
    sqlite3_finalize(s);
    return true;
}
 
// ── Stats ─────────────────────────────────────────────────────
inline std::optional<StatsCache> getStats(sqlite3* db, int user_id) {
    sqlite3_stmt* s;
    sqlite3_prepare_v2(db,
        "SELECT total_commits,streak_days,best_streak,repos_count,hours_coded,"
        "commits_today,repos_this_month,hours_this_week,top_language,"
        "languages_json,last_updated FROM stats_cache WHERE user_id=?",
        -1, &s, nullptr);
    sqlite3_bind_int(s, 1, user_id);
    std::optional<StatsCache> result;
    if (sqlite3_step(s) == SQLITE_ROW) {
        StatsCache sc;
        sc.total_commits    = sqlite3_column_int(s, 0);
        sc.streak_days      = sqlite3_column_int(s, 1);
        sc.best_streak      = sqlite3_column_int(s, 2);
        sc.repos_count      = sqlite3_column_int(s, 3);
        sc.hours_coded      = sqlite3_column_double(s, 4);
        sc.commits_today    = sqlite3_column_int(s, 5);
        sc.repos_this_month = sqlite3_column_int(s, 6);
        sc.hours_this_week  = sqlite3_column_double(s, 7);
        auto txt = [&](int col) -> std::string {
            auto p = sqlite3_column_text(s, col);
            return p ? reinterpret_cast<const char*>(p) : "";
        };
        sc.top_language    = txt(8);
        sc.languages_json  = txt(9);
        sc.last_updated    = sqlite3_column_int64(s, 10);
        result = sc;
    }
    sqlite3_finalize(s);
    return result;
}
 
inline void upsertStats(sqlite3* db, int user_id, const StatsCache& sc) {
    sqlite3_stmt* s;
    sqlite3_prepare_v2(db, R"(
        INSERT INTO stats_cache(user_id,total_commits,streak_days,best_streak,
            repos_count,hours_coded,commits_today,repos_this_month,
            hours_this_week,top_language,languages_json,last_updated)
        VALUES(?,?,?,?,?,?,?,?,?,?,?,?)
        ON CONFLICT(user_id) DO UPDATE SET
            total_commits=excluded.total_commits,
            streak_days=excluded.streak_days,
            best_streak=excluded.best_streak,
            repos_count=excluded.repos_count,
            hours_coded=excluded.hours_coded,
            commits_today=excluded.commits_today,
            repos_this_month=excluded.repos_this_month,
            hours_this_week=excluded.hours_this_week,
            top_language=excluded.top_language,
            languages_json=excluded.languages_json,
            last_updated=excluded.last_updated
    )", -1, &s, nullptr);
    sqlite3_bind_int(s, 1, user_id);
    sqlite3_bind_int(s, 2, sc.total_commits);
    sqlite3_bind_int(s, 3, sc.streak_days);
    sqlite3_bind_int(s, 4, sc.best_streak);
    sqlite3_bind_int(s, 5, sc.repos_count);
    sqlite3_bind_double(s, 6, sc.hours_coded);
    sqlite3_bind_int(s, 7, sc.commits_today);
    sqlite3_bind_int(s, 8, sc.repos_this_month);
    sqlite3_bind_double(s, 9, sc.hours_this_week);
    sqlite3_bind_text(s, 10, sc.top_language.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(s, 11, sc.languages_json.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(s, 12, Security::nowSec());
    sqlite3_step(s);
    sqlite3_finalize(s);
}
 
// ── Activity ──────────────────────────────────────────────────
inline void insertActivity(sqlite3* db, int user_id,
                            const ActivityItem& a) {
    sqlite3_stmt* s;
    sqlite3_prepare_v2(db,
        "INSERT INTO activity_feed(user_id,repo,message,language,commit_sha,pushed_at)"
        " VALUES(?,?,?,?,?,?)", -1, &s, nullptr);
    sqlite3_bind_int(s, 1, user_id);
    sqlite3_bind_text(s, 2, a.repo.c_str(),       -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(s, 3, a.message.c_str(),    -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(s, 4, a.language.c_str(),   -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(s, 5, a.commit_sha.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(s, 6, a.pushed_at);
    sqlite3_step(s);
    sqlite3_finalize(s);
    // Keep only latest 100 per user
    sqlite3_exec(db,
        ("DELETE FROM activity_feed WHERE user_id=" + std::to_string(user_id) +
         " AND id NOT IN (SELECT id FROM activity_feed WHERE user_id=" +
         std::to_string(user_id) + " ORDER BY pushed_at DESC LIMIT 100)").c_str(),
        nullptr, nullptr, nullptr);
}
 
inline std::vector<ActivityItem> getActivity(sqlite3* db,
                                              int user_id,
                                              int limit = 10) {
    sqlite3_stmt* s;
    sqlite3_prepare_v2(db,
        "SELECT id,repo,message,language,commit_sha,pushed_at"
        " FROM activity_feed WHERE user_id=?"
        " ORDER BY pushed_at DESC LIMIT ?",
        -1, &s, nullptr);
    sqlite3_bind_int(s, 1, user_id);
    sqlite3_bind_int(s, 2, limit);
    std::vector<ActivityItem> items;
    while (sqlite3_step(s) == SQLITE_ROW) {
        ActivityItem a;
        a.id         = sqlite3_column_int(s, 0);
        auto txt = [&](int col) -> std::string {
            auto p = sqlite3_column_text(s, col);
            return p ? reinterpret_cast<const char*>(p) : "";
        };
        a.repo       = txt(1); a.message   = txt(2);
        a.language   = txt(3); a.commit_sha = txt(4);
        a.pushed_at  = sqlite3_column_int64(s, 5);
        items.push_back(a);
    }
    sqlite3_finalize(s);
    return items;
}
 
// ── Sessions ──────────────────────────────────────────────────
inline std::string createSession(sqlite3* db, int user_id,
                                  const std::string& ip_hash) {
    // Clean expired sessions
    sqlite3_exec(db,
        ("DELETE FROM sessions WHERE expires_at < " +
         std::to_string(Security::nowSec())).c_str(),
        nullptr, nullptr, nullptr);
 
    std::string token = Security::generateToken(32);
    sqlite3_stmt* s;
    sqlite3_prepare_v2(db,
        "INSERT INTO sessions(token,user_id,created_at,expires_at,ip_hash)"
        " VALUES(?,?,?,?,?)", -1, &s, nullptr);
    sqlite3_bind_text(s, 1, token.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(s, 2, user_id);
    sqlite3_bind_int64(s, 3, Security::nowSec());
    sqlite3_bind_int64(s, 4, Security::sessionExpiry());
    sqlite3_bind_text(s, 5, ip_hash.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_step(s);
    sqlite3_finalize(s);
    return token;
}
 
inline std::optional<int> validateSession(sqlite3* db,
                                           const std::string& token) {
    if (token.empty() || token.size() > 128) return std::nullopt;
    sqlite3_stmt* s;
    sqlite3_prepare_v2(db,
        "SELECT user_id FROM sessions"
        " WHERE token=? AND expires_at > ?",
        -1, &s, nullptr);
    sqlite3_bind_text(s, 1, token.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(s, 2, Security::nowSec());
    std::optional<int> uid;
    if (sqlite3_step(s) == SQLITE_ROW) uid = sqlite3_column_int(s, 0);
    sqlite3_finalize(s);
    return uid;
}
 
inline void deleteSession(sqlite3* db, const std::string& token) {
    sqlite3_stmt* s;
    sqlite3_prepare_v2(db,
        "DELETE FROM sessions WHERE token=?", -1, &s, nullptr);
    sqlite3_bind_text(s, 1, token.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_step(s);
    sqlite3_finalize(s);
}
 
// ── Search ────────────────────────────────────────────────────
inline std::vector<User> searchUsers(sqlite3* db, const std::string& query) {
    sqlite3_stmt* s;
    std::string pattern = "%" + query.substr(0, 50) + "%";
    sqlite3_prepare_v2(db,
        "SELECT user_id,github_id,username,display_name,avatar_url,bio,"
        "location,role,github_url,theme,webhook_secret,public,created_at"
        " FROM users WHERE public=1 AND"
        " (username LIKE ? OR display_name LIKE ?)"
        " LIMIT 10", -1, &s, nullptr);
    sqlite3_bind_text(s, 1, pattern.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(s, 2, pattern.c_str(), -1, SQLITE_TRANSIENT);
    std::vector<User> results;
    while (sqlite3_step(s) == SQLITE_ROW) {
        User u;
        u.user_id = sqlite3_column_int(s, 0);
        auto txt = [&](int col) -> std::string {
            auto p = sqlite3_column_text(s, col);
            return p ? reinterpret_cast<const char*>(p) : "";
        };
        u.github_id = txt(1); u.username = txt(2);
        u.display_name = txt(3); u.avatar_url = txt(4);
        u.bio = txt(5); u.location = txt(6);
        u.role = txt(7); u.github_url = txt(8);
        u.theme = txt(9); u.webhook_secret = txt(10);
        u.is_public = sqlite3_column_int(s, 11);
        u.created_at = sqlite3_column_int64(s, 12);
        results.push_back(u);
    }
    sqlite3_finalize(s);
    return results;
}
 
} // namespace UserService
