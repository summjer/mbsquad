#include "tasks.h"
#include "core/database.h"
#include "core/core.h"
#include "net/rcon.h"
#include "plugins/plugin.h"
#include "core/shared_utils.h"
#include "core/log_parser.h"
#include <chrono>
#include <thread>

namespace sp {

void TimedTasks::start(Database& db, RconPool& rcon) {
    running_ = true;
    threads_.emplace_back(&TimedTasks::autoRefreshLoop, this, std::ref(db), std::ref(rcon));
    threads_.emplace_back(&TimedTasks::broadcastLoop, this, std::ref(db), std::ref(rcon));
    threads_.emplace_back(&TimedTasks::tkCheckerLoop, this, std::ref(db), std::ref(rcon));
    threads_.emplace_back(&TimedTasks::sessionCleanupLoop, this, std::ref(db));
    LOG_I("Tasks", "Started 4 background tasks");
}

void TimedTasks::stop() {
    running_ = false;
    for (auto& t : threads_) {
        if (t.joinable()) t.join();
    }
    threads_.clear();
    LOG_I("Tasks", "Stopped all background tasks");
}

void TimedTasks::autoRefreshLoop(Database& db, RconPool& rcon) {
    LOG_I("AutoRefresh", "Started (" + std::to_string(getSettingInt(db, "auto_refresh_interval", 2)) + "s interval)");
    int tickCounter = 0;
    while (running_) {
        try {
            rcon.healthCheck();
            auto servers = db.query("SELECT id FROM servers", {});
            for (auto& s : servers) {
                if (!running_) break;
                int sid = std::stoi(s["id"]);
                try {
                    std::string raw = rcon.send(sid, "ListPlayers", Config::get().getInt("RCON_SEND_TIMEOUT", 5000));
                    if (!raw.empty() && raw.find("Error") == std::string::npos &&
                        raw.find("not connected") == std::string::npos) {
                        LogParser lp;
                        auto players = lp.parsePlayerList(raw);
                        nlohmann::json playersJson = nlohmann::json::array();
                        for (auto& p : players) {
                            if (p.steamId.empty()) continue;
                            playersJson.push_back({
                                {"steamId", p.steamId}, {"name", p.name},
                                {"teamId", p.teamId}, {"squadId", p.squadId},
                                {"rconId", p.rconId}, {"isLeader", p.isLeader},
                                {"teamIndex", std::stoi(p.teamId.empty() ? "0" : p.teamId)}
                            });
                            try {
                                db.exec("INSERT OR REPLACE INTO players "
                                        "(serverId,steamId,name,playtime,firstSeen,lastSeen) "
                                        "VALUES(?,?,?,COALESCE((SELECT playtime FROM players WHERE serverId=? AND steamId=?),0),"
                                        "COALESCE((SELECT firstSeen FROM players WHERE serverId=? AND steamId=?),datetime('now')),"
                                        "datetime('now'))",
                                        {std::to_string(sid), p.steamId, p.name,
                                         std::to_string(sid), p.steamId,
                                         std::to_string(sid), p.steamId});
                            } catch (...) {}
                        }
                        PluginContext pctx{db, rcon,
                            [&](int sv, const std::string& st) { rcon.send(sv, "AdminWarn \"" + rconSafe(st) + "\""); },
                            [](const std::string& m) { LOG_I("Plugin", m); }
                        };
                        PluginEvent pev;
                        pev.type = "playerlist";
                        pev.serverId = sid;
                        pev.data = {{"players", playersJson}};
                        PluginManager::instance().dispatch(pev, pctx);

                        // Tick rate collection (every 10 iterations = ~20s)
                        if (++tickCounter % 10 == 0) {
                            try {
                                std::string stats = rcon.send(sid, "ShowServerStats", Config::get().getInt("RCON_SEND_TIMEOUT", 3000));
                                if (!stats.empty() && stats.find("Error") == std::string::npos) {
                                    double tickRate = 0;
                                    auto pos = stats.find("ick");
                                    if (pos != std::string::npos) {
                                        std::string sub = stats.substr(pos);
                                        auto col = sub.find(':');
                                        if (col != std::string::npos) {
                                            try { tickRate = std::stod(sub.substr(col + 1)); } catch (...) {}
                                        }
                                    }
                                    if (tickRate <= 0) {
                                        std::string num;
                                        bool dot = false;
                                        for (char ch : stats) {
                                            if (ch >= '0' && ch <= '9') num += ch;
                                            else if (ch == '.' && !dot && !num.empty()) { num += ch; dot = true; }
                                            else if (!num.empty()) break;
                                        }
                                        if (!num.empty()) try { tickRate = std::stod(num); } catch (...) {}
                                    }
                                    if (tickRate > 0 && tickRate < Config::get().getInt("TICK_RATE_MAX", 200)) {
                                        db.exec("INSERT INTO server_ticks (serverId,tickRate,timestamp) VALUES(?,?,datetime('now'))",
                                                {std::to_string(sid), std::to_string(tickRate)});
                                    }
                                }
                            } catch (...) {}
                        }
                    }
                } catch (...) {}
            }
        } catch (...) {}
        std::this_thread::sleep_for(std::chrono::seconds(getSettingInt(db, "auto_refresh_interval", 2)));
    }
}

void TimedTasks::broadcastLoop(Database& db, RconPool& rcon) {
    LOG_I("TimedBroadcast", "Started (30s tick)");
    int msgIndex = 0;
    int64_t lastBroadcast = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now().time_since_epoch()).count();
    while (running_) {
        try {
            if (!getSettingBool(db, "broadcast_enabled", false)) {
                std::this_thread::sleep_for(std::chrono::seconds(30));
                continue;
            }
            int intervalSec = getSettingInt(db, "broadcast_interval", 300);
            auto nowMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now().time_since_epoch()).count();
            if (nowMs - lastBroadcast < (int64_t)intervalSec * 1000) {
                std::this_thread::sleep_for(std::chrono::seconds(30));
                continue;
            }
            lastBroadcast = nowMs;
            std::string msgRaw = getSetting(db, "broadcast_messages", "[]");
            nlohmann::json messages;
            try { messages = nlohmann::json::parse(msgRaw); } catch (...) {
                std::this_thread::sleep_for(std::chrono::seconds(30)); continue;
            }
            if (!messages.is_array() || messages.empty()) {
                std::this_thread::sleep_for(std::chrono::seconds(30)); continue;
            }
            std::string msg = messages[msgIndex % messages.size()].get<std::string>();
            msgIndex++;
            auto servers = db.query("SELECT id FROM servers", {});
            for (auto& s : servers) {
                int sid = std::stoi(s["id"]);
                try { rcon.send(sid, "AdminBroadcast \"" + rconSafe(msg) + "\"", 5000); } catch (...) {}
            }
            LOG_I("TimedBroadcast", "Sent: " + msg);
        } catch (...) {}
        std::this_thread::sleep_for(std::chrono::seconds(30));
    }
}

void TimedTasks::tkCheckerLoop(Database& db, RconPool& rcon) {
    LOG_I("TKChecker", "Started (" + std::to_string(getSettingInt(db, "tk_check_interval_sec", 15)) + "s interval)");
    while (running_) {
        try {
            auto rows = db.query(
                "SELECT * FROM tk_forgive WHERE forgiven=0 AND kicked=0 AND expiresAt <= datetime('now')", {});
            for (auto& tk : rows) {
                int sid = std::stoi(tk["serverId"]);
                std::string killerSteamId = tk["killerSteamId"];
                std::string killerName = tk["killerName"];
                try {
                    std::string kickMsg = getSetting(db, "tk_forgive_kick_msg",
                        "你因TK队友且未道歉被自动踢出");
                    rcon.send(sid, "AdminKick \"" + rconSafe(killerSteamId) + "\" " + rconSafe(kickMsg), 5000);
                } catch (...) {}
                db.exec("UPDATE tk_forgive SET kicked=1 WHERE id=?", {tk["id"]});
                LOG_I("TKChecker", "Kicked " + killerName + " for expired TK");
            }
        } catch (...) {}
        std::this_thread::sleep_for(std::chrono::seconds(getSettingInt(db, "tk_check_interval_sec", 15)));
    }
}

void TimedTasks::sessionCleanupLoop(Database& db) {
    LOG_I("SessionCleanup", "Started (" + std::to_string(getSettingInt(db, "session_cleanup_interval_sec", 300)) + "s interval)");
    while (running_) {
        try {
            db.exec("DELETE FROM sessions WHERE expiresAt < datetime('now')");
            db.exec("DELETE FROM point_logs WHERE createdAt < datetime('now', '-90 days')");
            LOG_D("SessionCleanup", "Cleaned expired sessions and old logs");
        } catch (...) {}
        std::this_thread::sleep_for(std::chrono::seconds(getSettingInt(db, "session_cleanup_interval_sec", 300)));
    }
}

} // namespace sp