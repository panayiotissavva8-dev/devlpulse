#pragma once
#include <crow.h>
#include <unordered_map>
#include <vector>
#include <mutex>
#include <string>
#include <nlohmann/json.hpp>
 
using json = nlohmann::json;
 
// ── Thread-safe WebSocket connection registry ─────────────────
class WSManager {
public:
    static WSManager& instance() {
        static WSManager mgr;
        return mgr;
    }
 
    void add(const std::string& username,
             crow::websocket::connection* conn) {
        std::lock_guard<std::mutex> lock(mtx_);
        conns_[username].push_back(conn);
    }
 
    void remove(const std::string& username,
                crow::websocket::connection* conn) {
        std::lock_guard<std::mutex> lock(mtx_);
        auto it = conns_.find(username);
        if (it == conns_.end()) return;
        auto& vec = it->second;
        vec.erase(std::remove(vec.begin(), vec.end(), conn), vec.end());
        if (vec.empty()) conns_.erase(it);
    }
 
    // Broadcast JSON to all viewers of a profile
    void broadcast(const std::string& username, const json& payload) {
        std::string msg = payload.dump();
        std::lock_guard<std::mutex> lock(mtx_);
        auto it = conns_.find(username);
        if (it == conns_.end()) return;
        for (auto* conn : it->second) {
            try { conn->send_text(msg); }
            catch (...) { /* dead connection — cleaned on close */ }
        }
    }
 
    int viewerCount(const std::string& username) {
        std::lock_guard<std::mutex> lock(mtx_);
        auto it = conns_.find(username);
        return it == conns_.end() ? 0 : (int)it->second.size();
    }
 
private:
    WSManager() = default;
    std::unordered_map<std::string,
        std::vector<crow::websocket::connection*>> conns_;
    std::mutex mtx_;
};
 
// ── Broadcast helpers ─────────────────────────────────────────
inline void broadcastCommit(const std::string& username,
                             const std::string& repo,
                             const std::string& message,
                             const std::string& language,
                             int count) {
    WSManager::instance().broadcast(username, {
        {"type",     "commit"},
        {"pusher",   username},
        {"repo",     repo},
        {"message",  message},
        {"language", language},
        {"count",    count}
    });
}
 
inline void broadcastStatsUpdate(const std::string& username,
                                  int total_commits,
                                  int streak) {
    WSManager::instance().broadcast(username, {
        {"type",          "stats_update"},
        {"total_commits", total_commits},
        {"streak_days",   streak}
    });
}
 