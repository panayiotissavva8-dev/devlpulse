#include "services/user_service.h"
#include "utils/security.h"
#include <iostream>

// ── RAII statement wrapper ────────────────────────────────────────────────────
struct Stmt {
    sqlite3_stmt* s = nullptr;
    Stmt(sqlite3* db, const char* sql) { sqlite3_prepare_v2(db, sql, -1, &s, nullptr); }
    ~Stmt() { sqlite3_finalize(s); }
    operator sqlite3_stmt*() { return s; }
};
static std::string col(sqlite3_stmt* s, int i) {
    const char* p = reinterpret_cast<const char*>(sqlite3_column_text(s, i));
    return p ? p : "";
}

namespace UserService {

// ── Sessions ──────────────────────────────────────────────────────────────────

std::optional<int> validateSession(sqlite3* db, const std::string& token) {
    if (token.empty()) return std::nullopt;
    Stmt s(db, "SELECT user_id FROM sessions "
               "WHERE token=? AND expires_at > strftime('%s','now')");
    sqlite3_bind_text(s, 1, token.c_str(), -1, SQLITE_STATIC);
    if (sqlite3_step(s) == SQLITE_ROW) return sqlite3_column_int(s, 0);
    return std::nullopt;
}

std::string createSession(sqlite3* db, int user_id, const std::string& ip_hash) {
    std::string token = Security::generateToken(32); // 64-char hex
    Stmt s(db, "INSERT INTO sessions(token,user_id,ip_hash,expires_at) "
               "VALUES(?,?,?,strftime('%s','now')+86400)");
    sqlite3_bind_text(s, 1, token.c_str(),   -1, SQLITE_STATIC);
    sqlite3_bind_int (s, 2, user_id);
    sqlite3_bind_text(s, 3, ip_hash.c_str(), -1, SQLITE_STATIC);
    sqlite3_step(s);
    return token;
}

void deleteSession(sqlite3* db, const std::string& token) {
    Stmt s(db, "DELETE FROM sessions WHERE token=?");
    sqlite3_bind_text(s, 1, token.c_str(), -1, SQLITE_STATIC);
    sqlite3_step(s);
}

// ── Users ─────────────────────────────────────────────────────────────────────

int upsertUser(sqlite3* db, const GitHubUser& gh) {
    // Try INSERT OR REPLACE approach with RETURNING
    Stmt s(db, R"(
        INSERT INTO users(username,display_name,avatar_url,bio,location,github_url)
        VALUES(?,?,?,?,?,?)
        ON CONFLICT(username) DO UPDATE SET
            display_name=excluded.display_name,
            avatar_url=excluded.avatar_url,
            bio=excluded.bio,
            location=excluded.location,
            github_url=excluded.github_url
        RETURNING user_id
    )");
    sqlite3_bind_text(s, 1, gh.username.c_str(),     -1, SQLITE_STATIC);
    sqlite3_bind_text(s, 2, gh.display_name.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_text(s, 3, gh.avatar_url.c_str(),   -1, SQLITE_STATIC);
    sqlite3_bind_text(s, 4, gh.bio.c_str(),          -1, SQLITE_STATIC);
    sqlite3_bind_text(s, 5, gh.location.c_str(),     -1, SQLITE_STATIC);
    sqlite3_bind_text(s, 6, gh.github_url.c_str(),   -1, SQLITE_STATIC);
    if (sqlite3_step(s) == SQLITE_ROW) return sqlite3_column_int(s, 0);
    // Fallback select
    Stmt sel(db, "SELECT user_id FROM users WHERE username=?");
    sqlite3_bind_text(sel, 1, gh.username.c_str(), -1, SQLITE_STATIC);
    if (sqlite3_step(sel) == SQLITE_ROW) return sqlite3_column_int(sel, 0);
    return -1;
}

std::optional<User> findByUsername(sqlite3* db, const std::string& username) {
    Stmt s(db, "SELECT user_id,username,display_name,avatar_url,bio,"
               "location,role,github_url,theme,is_public,webhook_secret "
               "FROM users WHERE username=?");
    sqlite3_bind_text(s, 1, username.c_str(), -1, SQLITE_STATIC);
    if (sqlite3_step(s) != SQLITE_ROW) return std::nullopt;
    User u;
    u.user_id        = sqlite3_column_int(s, 0);
    u.username       = col(s, 1);
    u.display_name   = col(s, 2);
    u.avatar_url     = col(s, 3);
    u.bio            = col(s, 4);
    u.location       = col(s, 5);
    u.role           = col(s, 6);
    u.github_url     = col(s, 7);
    u.theme          = col(s, 8);
    u.is_public      = sqlite3_column_int(s, 9) != 0;
    u.webhook_secret = col(s, 10);
    return u;
}

std::vector<User> searchUsers(sqlite3* db, const std::string& q) {
    std::string pat = "%" + q + "%";
    Stmt s(db, "SELECT user_id,username,display_name,avatar_url,bio "
               "FROM users WHERE is_public=1 "
               "AND (username LIKE ? OR display_name LIKE ? OR bio LIKE ?) LIMIT 20");
    sqlite3_bind_text(s, 1, pat.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(s, 2, pat.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(s, 3, pat.c_str(), -1, SQLITE_TRANSIENT);
    std::vector<User> out;
    while (sqlite3_step(s) == SQLITE_ROW) {
        User u;
        u.user_id      = sqlite3_column_int(s, 0);
        u.username     = col(s, 1);
        u.display_name = col(s, 2);
        u.avatar_url   = col(s, 3);
        u.bio          = col(s, 4);
        out.push_back(std::move(u));
    }
    return out;
}

void updateTheme(sqlite3* db, int user_id, const std::string& theme) {
    Stmt s(db, "UPDATE users SET theme=? WHERE user_id=?");
    sqlite3_bind_text(s, 1, theme.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_int (s, 2, user_id);
    sqlite3_step(s);
}

void updatePublic(sqlite3* db, int user_id, bool pub) {
    Stmt s(db, "UPDATE users SET is_public=? WHERE user_id=?");
    sqlite3_bind_int(s, 1, pub ? 1 : 0);
    sqlite3_bind_int(s, 2, user_id);
    sqlite3_step(s);
}

// ── Stats ─────────────────────────────────────────────────────────────────────

std::optional<StatsCache> getStats(sqlite3* db, int user_id) {
    Stmt s(db, "SELECT total_commits,streak_days,best_streak,repos_count,"
               "hours_coded,commits_today,repos_this_month,hours_this_week,"
               "top_language,languages_json,last_updated "
               "FROM stats_cache WHERE user_id=?");
    sqlite3_bind_int(s, 1, user_id);
    if (sqlite3_step(s) != SQLITE_ROW) return std::nullopt;
    StatsCache sc;
    sc.total_commits    = sqlite3_column_int   (s, 0);
    sc.streak_days      = sqlite3_column_int   (s, 1);
    sc.best_streak      = sqlite3_column_int   (s, 2);
    sc.repos_count      = sqlite3_column_int   (s, 3);
    sc.hours_coded      = sqlite3_column_double(s, 4);
    sc.commits_today    = sqlite3_column_int   (s, 5);
    sc.repos_this_month = sqlite3_column_int   (s, 6);
    sc.hours_this_week  = sqlite3_column_double(s, 7);
    sc.top_language     = col(s, 8);
    sc.languages_json   = col(s, 9);
    sc.last_updated     = sqlite3_column_int64 (s, 10);
    return sc;
}

void upsertStats(sqlite3* db, int user_id, const StatsCache& sc) {
    Stmt s(db, R"(
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
    )");
    sqlite3_bind_int   (s,  1, user_id);
    sqlite3_bind_int   (s,  2, sc.total_commits);
    sqlite3_bind_int   (s,  3, sc.streak_days);
    sqlite3_bind_int   (s,  4, sc.best_streak);
    sqlite3_bind_int   (s,  5, sc.repos_count);
    sqlite3_bind_double(s,  6, sc.hours_coded);
    sqlite3_bind_int   (s,  7, sc.commits_today);
    sqlite3_bind_int   (s,  8, sc.repos_this_month);
    sqlite3_bind_double(s,  9, sc.hours_this_week);
    sqlite3_bind_text  (s, 10, sc.top_language.c_str(),   -1, SQLITE_STATIC);
    sqlite3_bind_text  (s, 11, sc.languages_json.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_int64 (s, 12, sc.last_updated);
    sqlite3_step(s);
}

// ── Activity ──────────────────────────────────────────────────────────────────

std::vector<ActivityItem> getActivity(sqlite3* db, int user_id, int limit) {
    Stmt s(db, "SELECT repo,message,language,commit_sha,pushed_at "
               "FROM activity WHERE user_id=? ORDER BY pushed_at DESC LIMIT ?");
    sqlite3_bind_int(s, 1, user_id);
    sqlite3_bind_int(s, 2, limit);
    std::vector<ActivityItem> out;
    while (sqlite3_step(s) == SQLITE_ROW) {
        ActivityItem a;
        a.repo       = col(s, 0);
        a.message    = col(s, 1);
        a.language   = col(s, 2);
        a.commit_sha = col(s, 3);
        a.pushed_at  = sqlite3_column_int64(s, 4);
        out.push_back(std::move(a));
    }
    return out;
}

void insertActivity(sqlite3* db, int user_id, const ActivityItem& a) {
    Stmt s(db, "INSERT INTO activity(user_id,repo,message,language,commit_sha,pushed_at) "
               "VALUES(?,?,?,?,?,?)");
    sqlite3_bind_int  (s, 1, user_id);
    sqlite3_bind_text (s, 2, a.repo.c_str(),       -1, SQLITE_STATIC);
    sqlite3_bind_text (s, 3, a.message.c_str(),    -1, SQLITE_STATIC);
    sqlite3_bind_text (s, 4, a.language.c_str(),   -1, SQLITE_STATIC);
    sqlite3_bind_text (s, 5, a.commit_sha.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_int64(s, 6, a.pushed_at);
    sqlite3_step(s);
}

} // namespace UserService