#pragma once
#include <string>
#include <sstream>
#include <iomanip>
#include <chrono>
#include <random>
#include <regex>
#include <openssl/sha.h>
#include <openssl/hmac.h>
#include <openssl/evp.h>
#include <sqlite3.h>

namespace Security {

// ── Token generation ─────────────────────────────────────────
inline std::string generateToken(size_t bytes = 32) {
    std::random_device rd;
    std::mt19937_64 gen(rd());
    std::uniform_int_distribution<uint64_t> dis;
    std::ostringstream ss;
    for (size_t i = 0; i < (bytes / 8) + 1; i++)
        ss << std::hex << std::setw(16) << std::setfill('0') << dis(gen);
    return ss.str().substr(0, bytes * 2);
}

// ── SHA-256 hash ──────────────────────────────────────────────
inline std::string sha256(const std::string& input) {
    unsigned char hash[SHA256_DIGEST_LENGTH];
    SHA256(reinterpret_cast<const unsigned char*>(input.c_str()),
           input.size(), hash);
    std::ostringstream ss;
    for (int i = 0; i < SHA256_DIGEST_LENGTH; i++)
        ss << std::hex << std::setw(2) << std::setfill('0') << (int)hash[i];
    return ss.str();
}

// ── HMAC-SHA256 (webhook verification) ───────────────────────
inline std::string hmacSha256(const std::string& key,
                               const std::string& data) {
    unsigned char hash[EVP_MAX_MD_SIZE];
    unsigned int  len = 0;
    HMAC(EVP_sha256(),
         key.c_str(),  (int)key.size(),
         reinterpret_cast<const unsigned char*>(data.c_str()),
         data.size(), hash, &len);
    std::ostringstream ss;
    for (unsigned int i = 0; i < len; i++)
        ss << std::hex << std::setw(2) << std::setfill('0') << (int)hash[i];
    return "sha256=" + ss.str();
}

// ── Constant-time compare ─────────────────────────────────────
inline bool safeCompare(const std::string& a, const std::string& b) {
    if (a.size() != b.size()) return false;
    volatile int diff = 0;
    for (size_t i = 0; i < a.size(); i++) diff |= (a[i] ^ b[i]);
    return diff == 0;
}

// ── Input sanitization ────────────────────────────────────────
inline std::string sanitize(const std::string& s, size_t maxLen = 256) {
    if (s.empty()) return "";
    std::string out = s.substr(0, maxLen);
    // Strip null bytes and control chars (keep printable + common unicode)
    out.erase(std::remove_if(out.begin(), out.end(),
        [](unsigned char c){ return c < 0x09 || c == 0x0b || c == 0x0c ||
                                    (c >= 0x0e && c < 0x20); }), out.end());
    return out;
}

inline bool isValidUsername(const std::string& s) {
    static const std::regex re("^[a-zA-Z0-9_\\-]{1,39}$");
    return std::regex_match(s, re);
}

inline bool isValidUrl(const std::string& s) {
    static const std::regex re("^https?://[^\\s]{1,2048}$");
    return std::regex_match(s, re);
}

// ── Rate limiting (SQLite backed, per IP hash) ────────────────
struct RateLimitResult { bool allowed; int remaining; };

inline RateLimitResult checkRateLimit(sqlite3* db,
                                       const std::string& key,
                                       int maxRequests,
                                       int windowSeconds) {
    long long now = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();

    sqlite3_stmt* stmt;
    // Get or create window
    sqlite3_prepare_v2(db,
        "INSERT INTO rate_limits(key, count, window_start) VALUES(?,1,?)"
        " ON CONFLICT(key) DO UPDATE SET"
        "   count = CASE WHEN (? - window_start) > ? THEN 1 ELSE count+1 END,"
        "   window_start = CASE WHEN (? - window_start) > ? THEN ? ELSE window_start END",
        -1, &stmt, nullptr);
    sqlite3_bind_text(stmt, 1, key.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(stmt, 2, now);
    sqlite3_bind_int64(stmt, 3, now); sqlite3_bind_int(stmt, 4, windowSeconds);
    sqlite3_bind_int64(stmt, 5, now); sqlite3_bind_int(stmt, 6, windowSeconds);
    sqlite3_bind_int64(stmt, 7, now);
    sqlite3_step(stmt);
    sqlite3_finalize(stmt);

    // Read count
    sqlite3_prepare_v2(db,
        "SELECT count FROM rate_limits WHERE key=?", -1, &stmt, nullptr);
    sqlite3_bind_text(stmt, 1, key.c_str(), -1, SQLITE_TRANSIENT);
    int count = 0;
    if (sqlite3_step(stmt) == SQLITE_ROW) count = sqlite3_column_int(stmt, 0);
    sqlite3_finalize(stmt);

    int remaining = std::max(0, maxRequests - count);
    return { count <= maxRequests, remaining };
}

// ── Session expiry (24h) ──────────────────────────────────────
inline long long sessionExpiry() {
    return std::chrono::duration_cast<std::chrono::seconds>(
               std::chrono::system_clock::now().time_since_epoch()).count()
           + 86400;
}

inline long long nowSec() {
    return std::chrono::duration_cast<std::chrono::seconds>(
               std::chrono::system_clock::now().time_since_epoch()).count();
}

} // namespace Security