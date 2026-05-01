#include "services/github_service.h"
#include <cpr/cpr.h>
#include <nlohmann/json.hpp>
#include <iostream>
#include <sstream>
#include <ctime>

using json = nlohmann::json;

namespace GitHubService {

std::string oauthRedirectUrl(const std::string& client_id,
                              const std::string& app_url,
                              const std::string& state) {
    std::string callback = app_url + "/auth/github/callback";
    return "https://github.com/login/oauth/authorize"
           "?client_id=" + client_id +
           "&redirect_uri=" + callback +
           "&scope=read:user,repo" +
           "&state=" + state;
}

std::string exchangeCode(const std::string& code,
                          const std::string& client_id,
                          const std::string& client_secret) {
    auto r = cpr::Post(
        cpr::Url{"https://github.com/login/oauth/access_token"},
        cpr::Header{{"Accept", "application/json"}},
        cpr::Payload{
            {"client_id",     client_id},
            {"client_secret", client_secret},
            {"code",          code}
        }
    );
    if (r.status_code != 200) return "";
    try {
        auto j = json::parse(r.text);
        return j.value("access_token", "");
    } catch (...) { return ""; }
}

std::optional<GitHubUser> fetchAuthUser(const std::string& access_token) {
    auto r = cpr::Get(
        cpr::Url{"https://api.github.com/user"},
        cpr::Header{
            {"Authorization", "Bearer " + access_token},
            {"Accept",        "application/vnd.github+json"},
            {"User-Agent",    "DevPulse/1.0"}
        }
    );
    if (r.status_code != 200) return std::nullopt;
    try {
        auto j = json::parse(r.text);
        GitHubUser u;
        u.username     = j.value("login",      "");
        u.display_name = j.value("name",       u.username);
        u.avatar_url   = j.value("avatar_url", "");
        u.bio          = j.value("bio",         "");
        u.location     = j.value("location",   "");
        u.github_url   = j.value("html_url",   "");
        if (u.username.empty()) return std::nullopt;
        return u;
    } catch (...) { return std::nullopt; }
}

StatsCache fetchUserStats(const std::string& username, const std::string& token) {
    StatsCache sc;
    sc.last_updated = (long)time(nullptr);

    cpr::Header hdr{
        {"Accept",     "application/vnd.github+json"},
        {"User-Agent", "DevPulse/1.0"}
    };
    if (!token.empty()) hdr["Authorization"] = "Bearer " + token;

    // Repos
    auto r = cpr::Get(
        cpr::Url{"https://api.github.com/users/" + username + "/repos"},
        cpr::Parameters{{"per_page","100"},{"sort","updated"}},
        hdr
    );
    if (r.status_code != 200) return sc;
    try {
        auto repos = json::parse(r.text);
        sc.repos_count = (int)repos.size();

        std::map<std::string,int> lang_counts;
        long now = (long)time(nullptr);
        long month_ago = now - 30*24*3600;

        for (auto& repo : repos) {
            std::string lang = repo.value("language", "");
            if (!lang.empty()) lang_counts[lang]++;

            std::string pushed = repo.value("pushed_at", "");
            if (!pushed.empty()) {
                struct tm tm{}; strptime(pushed.c_str(), "%Y-%m-%dT%H:%M:%SZ", &tm);
                long t = (long)timegm(&tm);
                if (t > month_ago) sc.repos_this_month++;
            }
        }
        // Build languages JSON
        json langs = json::array();
        int  best_count = 0;
        for (auto& [lang, cnt] : lang_counts) {
            langs.push_back({{"name", lang}, {"count", cnt}});
            if (cnt > best_count) { best_count = cnt; sc.top_language = lang; }
        }
        sc.languages_json = langs.dump();
    } catch (...) {}

    return sc;
}

std::vector<ActivityItem> fetchActivity(const std::string& username,
                                         const std::string& token) {
    cpr::Header hdr{
        {"Accept",     "application/vnd.github+json"},
        {"User-Agent", "DevPulse/1.0"}
    };
    if (!token.empty()) hdr["Authorization"] = "Bearer " + token;

    auto r = cpr::Get(
        cpr::Url{"https://api.github.com/users/" + username + "/events"},
        cpr::Parameters{{"per_page","30"}},
        hdr
    );
    std::vector<ActivityItem> out;
    if (r.status_code != 200) return out;
    try {
        auto events = json::parse(r.text);
        for (auto& ev : events) {
            if (ev.value("type","") != "PushEvent") continue;
            auto& payload = ev["payload"];
            auto& commits = payload["commits"];
            if (!commits.is_array() || commits.empty()) continue;

            ActivityItem a;
            a.repo    = ev.contains("repo") ? ev["repo"].value("name","") : "";
            a.message = commits[0].value("message","");
            // Trim to first line
            auto nl = a.message.find('\n');
            if (nl != std::string::npos) a.message = a.message.substr(0, nl);
            a.commit_sha = commits[0].value("sha","").substr(0,7);

            std::string ts = ev.value("created_at","");
            if (!ts.empty()) {
                struct tm tm{}; strptime(ts.c_str(), "%Y-%m-%dT%H:%M:%SZ", &tm);
                a.pushed_at = (long)timegm(&tm);
            }
            out.push_back(std::move(a));
            if (out.size() >= 20) break;
        }
    } catch (...) {}
    return out;
}

} // namespace GitHubService