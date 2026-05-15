#pragma once
#include "net/server.h"
#include "net/rcon.h"
#include "core/database.h"
#include "core/core.h"
#include <httplib.h>

namespace sp {

// Extracts host and port from URLs like "http://1.2.3.4:18977"
struct RelayEndpoint {
    std::string host;
    int port;
};

inline RelayEndpoint parseRelayUrl(const std::string& url, int defaultPort = Config::get().getInt("DEFAULT_RELAY_PORT", 18977)) {
    RelayEndpoint ep;
    ep.port = defaultPort;
    
    std::string rest = url;
    auto p = rest.find("://");
    if (p != std::string::npos) rest = rest.substr(p + 3);
    
    auto colon = rest.find(':');
    if (colon != std::string::npos) {
        ep.host = rest.substr(0, colon);
        auto slash = rest.find('/');
        std::string portStr = rest.substr(colon + 1, slash != std::string::npos ? slash - colon - 1 : std::string::npos);
        try { ep.port = std::stoi(portStr); } catch (...) {}
    } else {
        auto slash = rest.find('/');
        ep.host = slash != std::string::npos ? rest.substr(0, slash) : rest;
    }
    return ep;
}


inline httplib::Client makeRelayClient(const std::string& relayUrl, int defaultPort = Config::get().getInt("DEFAULT_RELAY_PORT", 18977)) {
    auto ep = parseRelayUrl(relayUrl, defaultPort);
    httplib::Client cli(ep.host.c_str(), ep.port);
    cli.set_connection_timeout(Config::get().getInt("RELAY_CONNECT_TIMEOUT", 3));
    cli.set_read_timeout(Config::get().getInt("RELAY_READ_TIMEOUT", 3));
    return cli;
}


inline nlohmann::json rowsToJson(const std::vector<std::unordered_map<std::string, std::string>>& rows) {
    nlohmann::json arr = nlohmann::json::array();
    for (auto& r : rows) {
        nlohmann::json item;
        for (auto& [k, v] : r) item[k] = v;
        arr.push_back(item);
    }
    return arr;
}


inline nlohmann::json rowToJson(const std::unordered_map<std::string, std::string>& row) {
    nlohmann::json item;
    for (auto& [k, v] : row) item[k] = v;
    return item;
}


inline int safeStoi(const std::string& s, int fallback = 0) {
    if (s.empty()) return fallback;
    try { return std::stoi(s); } catch (...) { return fallback; }
}

inline int64_t safeStoll(const std::string& s, int64_t fallback = 0) {
    if (s.empty()) return fallback;
    try { return std::stoll(s); } catch (...) { return fallback; }
}


struct PageParams {
    int page;
    int pageSize;
    int offset;
    int total;
    int totalPages;
};

inline PageParams parsePageParams(Context& ctx, int maxSize = 100) {
    PageParams pp;
    pp.page = std::max(1, safeStoi(ctx.query("page", "1"), 1));
    pp.pageSize = std::min(maxSize, std::max(1, safeStoi(ctx.query("page_size", "50"), 50)));
    pp.offset = (pp.page - 1) * pp.pageSize;
    pp.total = 0;
    pp.totalPages = 0;
    return pp;
}

inline nlohmann::json pagedResponse(const nlohmann::json& data, const PageParams& pp) {
    return {
        {"data", data},
        {"page", pp.page},
        {"page_size", pp.pageSize},
        {"total", pp.total},
        {"total_pages", pp.totalPages}
    };
}


inline bool hasServerAccess(Database& db, const std::string& userId, const std::string& serverId) {
    auto row = db.queryOne(
        "SELECT role FROM server_members WHERE serverId=? AND userId=?",
        {serverId, userId});
    return !row.empty();
}

inline std::string getServerRole(Database& db, const std::string& userId, const std::string& serverId) {
    auto row = db.queryOne(
        "SELECT role FROM server_members WHERE serverId=? AND userId=?",
        {serverId, userId});
    return row.empty() ? "" : row["role"];
}


inline std::string rconSafe(const std::string& s) {
    std::string result;
    result.reserve(s.size());
    for (size_t i = 0; i < s.size(); i++) {
        char ch = s[i];
        if (ch == '"') result += "\\\"";
        else if (ch == '\\') result += "\\\\";
        else result += ch;
    }
    return result;
}


inline void adminWarn(RconPool* pool, int serverId, const std::string& steamId, const std::string& message) {
    if (!pool || steamId.empty()) return;
    try {
        pool->send(serverId, "AdminWarn \"" + rconSafe(steamId) + "\" " + rconSafe(message));
    } catch (...) {}
}


inline int getPointsBalance(Database& db, const std::string& steamId, int serverId = 0) {
    auto row = db.queryOne(
        "SELECT balance FROM points WHERE steamId=? AND (serverId=? OR serverId IS NULL) "
        "ORDER BY serverId DESC LIMIT 1",
        {steamId, std::to_string(serverId)});
    return row.empty() ? 0 : safeStoi(row["balance"]);
}


inline bool adjustPoints(Database& db, int serverId, const std::string& steamId,
                          const std::string& playerName, int amount, const std::string& reason,
                          const std::string& op = "system") {
    auto existing = db.queryOne(
        "SELECT id, balance FROM points WHERE steamId=? AND (serverId=? OR serverId IS NULL) "
        "ORDER BY serverId DESC LIMIT 1",
        {steamId, std::to_string(serverId)});
    
    if (!existing.empty()) {
        int newBal = safeStoi(existing["balance"]) + amount;
        if (newBal < 0) return false;
        int earned = (amount > 0) ? amount : 0;
        db.exec("UPDATE points SET balance=?, lifetimeEarned=lifetimeEarned+?, "
                "playerName=COALESCE(?,playerName), lastUpdated=datetime('now') WHERE id=?",
                {std::to_string(newBal), std::to_string(earned),
                 playerName.empty() ? "" : playerName, existing["id"]});
    } else {
        if (amount < 0) return false;
        int earned = (amount > 0) ? amount : 0;
        db.exec("INSERT INTO points (serverId,steamId,playerName,balance,lifetimeEarned) VALUES(?,?,?,?,?)",
                {std::to_string(serverId), steamId, playerName.empty() ? "" : playerName,
                 std::to_string(amount), std::to_string(earned)});
    }
    
    db.exec("INSERT INTO point_logs (serverId,steamId,playerName,amount,reason,operator) VALUES(?,?,?,?,?,?)",
            {std::to_string(serverId), steamId, playerName.empty() ? "" : playerName,
             std::to_string(amount), reason, op});
    return true;
}


inline std::string getSetting(Database& db, const std::string& key, const std::string& fallback = "") {
    try {
        auto row = db.queryOne("SELECT value FROM settings WHERE key=?", {key});
        if (!row.empty()) {
            std::string v = row["value"];
            if (v.size() >= 2 && v[0] == '"' && v.back() == '"') return v.substr(1, v.size()-2);
            return v;
        }
    } catch (...) {}
    return fallback;
}

inline int getSettingInt(Database& db, const std::string& key, int fallback = 0) {
    try {
        auto row = db.queryOne("SELECT value FROM settings WHERE key=?", {key});
        if (!row.empty()) {
            std::string v = row["value"];
            if (v.size() >= 2 && v[0] == '"') v = v.substr(1, v.size()-2);
            try { return std::stoi(v); } catch (...) {}
        }
    } catch (...) {}
    return fallback;
}

inline bool getSettingBool(Database& db, const std::string& key, bool fallback = false) {
    return getSettingInt(db, key, fallback ? 1 : 0) != 0;
}

} // namespace sp
