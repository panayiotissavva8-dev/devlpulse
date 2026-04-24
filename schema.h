#pragma once
#include <sqlite3.h>
#include <iostream>

inline void initializeDatabase(sqlite3* db) {
    char* err = nullptr;

    const char* pragmas = R"(
    PRAGMA journal_mode = WAL;
    |PRAGMA foreign_keys = ON;
    PRAGMA secure_delete = ON;
)";

const char* users = R"(
CREATE TABLE IF NOT EXISTS users (
    user_id INTERGER PRIMARY KEY AUTOINCREMENT,
    github_id TEXT NOT NULL UNIQUE,
    username TEXT NOT NULL,
    display_name TEXT,
    avatar_url TEXT,
    bio TEXT,
    location TEXT,
    role, TEXT DEFAULT 'student',
    github_url TEXT,
    public INTERGER DEFAULT 1,
    theme TEXT DEFAULT 'dark',
    webhook_secret TEXT,
    created_at INTERGER NOT NULL,
    );)";


const char* sessions = R"(
CREATE TABLE IF NOT EXISTS sessions(
    token TEXT PRIMARY KEY,
    user_id INTERGER NOT NULL,
    created_at INTERGER NOT NULL,
    expires_at INTERGER NOT NULL,
    ip_hash TEXT,
    FOREIGN KEY(user_id) REFERENCES users(user_id) ON DELETE CASCADE
     );)";


const char* stats = R"(
CREATE TABLE IF NOT EXISTS stats_cache(
    user_id INTERGER NOT NULL UNIQUE,
    total_commits INTERGER DEFAULT 0,
    streak_days INTERGER DEFAULT 0,
    best_streak INTERGER DEFAULT 0,
    repos_count INTERGER DEFAULT 0,
    hours_coded INTERGER DEFAULT 0,
    commits_today INTERGER DEFAULT 0,
    repos_this_month INTERGER DEFAULT 0,
    hours_this_week INTERGER DEFAULT 0,
    top_language TEXT,
    languages_json TEXT,
    last_updated INTERGER NOT NULL,
    FOREIGN KEY(user_id) REFERENCES users(user_id) ON DELETE CASCADE
     );)";


const char* activity = R"(
CREATE TABLE IF NOT EXISTS activity_feed(
    id INTERGER PRIMARY KEY AUTOINCREMENT,
    user_id INTERGER NOT NULL,
    repo TEXT NOT NULL,
    message TEXT NOT NULL,
    language TEXT,
    commit_sha TEXT,
    pushed_at INTERGER NOT NULL,
    FOREIGN KEY(user_id) REFERENCES users(user_id) ON DELETE CASCADE
    );)";


const char* rate_limits = R"(
CREATE TABLE IF NOT EXISTS rate_limits(
    key TEXT PRIMARY KEY,
    count INTERGER DEFAULT 0,
    window_start INTERGER NOT NULL
    );)";


const char* indexes = R"(
CREATE INDEX IF NOT EXISTS idx_sessions_user ON sessions(user_id);
CREATE INDEX IF NOT EXISTS idx_activity_user ON activity_feed(user_id);
CREATE INDEX IF NOT EXISTS idx_activity_time ON activity_feed(pushed_at DESC);
CREATE INDEX IF NOT EXISTS idx_rate_key ON rate_limits(key);
)";

for (auto sql : {pragmas, users, sessions, stats, activity, rate_limits, indexes}) {
    sqlite3_exec(db, sql, nullptr, nullptr, &err);
    if(err) {
        std::cerr << "[DB] " << err << "\n";
        sqlite3_free(err);
        err = nullptr;
    }
}


}