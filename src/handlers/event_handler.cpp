#include "handlers/event_handler.h"
#include "net/server.h"
#include "net/ws_handler.h"
#include "net/rcon.h"
#include "core/database.h"
#include "core/auth.h"
#include "core/core.h"
#include "core/log_parser.h"
#include "core/points_service.h"
#include "core/shared_utils.h"
#include "plugins/plugin.h"

#include <unordered_map>
#include <mutex>
#include <chrono>
#include <atomic>

namespace sp {
// ── v12: Relay API key validation ──
// Returns serverId if apiKey is valid, -1 otherwise
static int validateRelayApiKey(Database& db, const httplib::Request& req, const nlohmann::json& body) {
    // Check X-API-Key header first
    std::string apiKey;
    if (req.has_header("X-API-Key")) {
        apiKey = req.get_header_value("X-API-Key");
    }
    // Fallback: apiKey in body
    if (apiKey.empty() && body.contains("apiKey")) {
        apiKey = body["apiKey"].get<std::string>();
    }
    if (apiKey.empty()) return -1;

    auto srv = db.queryOne("SELECT id FROM servers WHERE serverApiKey=?", {apiKey});
    if (srv.empty()) return -1;
    return safeStoi(srv["id"]);
}

// Validate that the claimed serverId matches the apiKey's server
static bool validateRelayServerAccess(Database& db, const httplib::Request& req,
                                       const nlohmann::json& body, int claimedServerId) {
    int actualServerId = validateRelayApiKey(db, req, body);
    if (actualServerId < 0) return false;
    return actualServerId == claimedServerId;
}


// Shared state (module-level)
// Ownership: these are non-owning pointers. Caller (main) must ensure
// the pointed-to objects outlive this module.
static LogParser* g_logParser = nullptr;
static WsHandler* g_wsHandler = nullptr;
static PointsService* g_pointsService = nullptr;
static std::atomic<bool> g_moduleShutdown{false};

// Team map: serverId → (steamId → {name, teamId})
struct TeamMapEntry { std::string name; std::string teamId; };
static std::unordered_map<int, std::unordered_map<std::string, TeamMapEntry>> g_teamMap;
static std::mutex g_teamMtx;

// Live player cache: serverId → players json
struct LiveData {
    nlohmann::json players = nlohmann::json::array();
    std::string updatedAt;
};
static std::unordered_map<int, LiveData> g_liveCache;
static std::mutex g_liveMtx;

// Suicide cooldown
// Suicide cooldown (per-server)
static std::unordered_map<int, int64_t> g_lastSuicideTime;
static std::mutex g_suicideMtx;

// Helpers

static bool isTeamKill(int serverId, const std::string& killerSteamId,
                       const std::string& victimSteamId) {
    std::lock_guard<std::mutex> lk(g_teamMtx);
    auto it = g_teamMap.find(serverId);
    if (it == g_teamMap.end()) return false;
    auto& map = it->second;
    auto kit = map.find(killerSteamId);
    auto vit = map.find(victimSteamId);
    if (kit == map.end() || vit == map.end()) return false;
    return !kit->second.teamId.empty() && kit->second.teamId == vit->second.teamId;
}

static void updateTeamMap(int serverId, const std::vector<PlayerInfo>& players) {
    std::lock_guard<std::mutex> lk(g_teamMtx);
    auto& map = g_teamMap[serverId];
    map.clear();
    for (auto& p : players) {
        map[p.steamId] = {p.name, p.teamId};
    }
}

static nlohmann::json playerToJson(const PlayerInfo& p) {
    return {{"rconId", p.rconId},
            {"steamId", p.steamId},
            {"name", p.name},
            {"teamId", p.teamId},
            {"squadId", p.squadId},
            {"isLeader", p.isLeader}};
}

static std::string resolvePlayerName(Database& db, int serverId,
                                      const std::string& steamId,
                                      const std::string& controllerName) {
    // If not a controller ID, return as-is
    if (controllerName.find("PlayerController") == std::string::npos) {
        return controllerName;
    }
    // Try live players
    {
        std::lock_guard<std::mutex> lk(g_liveMtx);
        auto it = g_liveCache.find(serverId);
        if (it != g_liveCache.end()) {
            for (auto& p : it->second.players) {
                if (p.value("steamId", "") == steamId) {
                    return p.value("name", controllerName);
                }
            }
        }
    }
    // Try DB
    auto row = db.queryOne(
        "SELECT name FROM players WHERE steamId=? ORDER BY lastSeen DESC LIMIT 1",
        {steamId});
    if (!row.empty() && !row["name"].empty()) return row["name"];
    return controllerName;
}


// rconSafe is now in core/shared_utils.h

static std::string findSteamIdByName(int serverId, const std::string& name) {
    nlohmann::json playersCopy;
    {
        std::lock_guard<std::mutex> lk(g_liveMtx);
        auto it = g_liveCache.find(serverId);
        if (it == g_liveCache.end()) return "";
        playersCopy = it->second.players;
    }
    for (auto& p : playersCopy) {
        if (p.value("name", "") == name) return p.value("steamId", "");
    }
    for (auto& p : playersCopy) {
        std::string pname = p.value("name", "");
        if (pname.find(name) != std::string::npos ||
            name.find(pname) != std::string::npos) return p.value("steamId", "");
    }
    return "";
}

// Broadcast helper

static void wsBroadcast(int serverId, const std::string& type,
                        const nlohmann::json& data) {
    if (g_wsHandler) {
        g_wsHandler->broadcast(serverId, type, data.dump());
    }
}

// Register event routes

void registerEventRoutes(Server& server) {
    auto& db = server.db();

    // POST /api/events/playerlist-raw
    server.post("/api/events/playerlist-raw", [&](Context& ctx) {
        std::string serverIdStr = jsonStr(ctx.body, "serverId");
        if (serverIdStr.empty()) return ctx.error("serverId required");
        int serverId = safeStoi(serverIdStr);
        if (!validateRelayServerAccess(db, ctx.req, ctx.body, serverId)) return ctx.error("Unauthorized: apiKey does not match serverId", 401);

        std::string raw = jsonStr(ctx.body, "raw");
        if (raw.empty()) return ctx.json({{"success", true}});

        auto players = g_logParser->parsePlayerList(raw);
        auto timestamp = sp::nowISO();

        // Update live cache
        {
            std::lock_guard<std::mutex> lk(g_liveMtx);
            auto& live = g_liveCache[serverId];
            live.players = nlohmann::json::array();
            for (auto& p : players) {
                live.players.push_back(playerToJson(p));
            }
            live.updatedAt = timestamp;
        }

        // Update team map
        updateTeamMap(serverId, players);

        // Broadcast players_updated
        {
            std::lock_guard<std::mutex> lk(g_liveMtx);
            auto& live = g_liveCache[serverId];
            wsBroadcast(serverId, "players_updated", {
                {"type", "players_updated"},
                {"serverId", serverId},
                {"players", live.players},
                {"teamNames", nlohmann::json::object()}
            });
        }

        // Batch insert players into DB (transaction)
        db.transaction([&]() {
            for (auto& p : players) {
                db.exec(
                    "INSERT INTO players (serverId, steamId, name, playtime, lastSeen, firstSeen) "
                    "VALUES (?, ?, ?, 0, datetime('now'), "
                    "COALESCE((SELECT firstSeen FROM players WHERE serverId=? AND steamId=?), datetime('now'))) "
                    "ON CONFLICT(serverId, steamId) DO UPDATE SET name=?, lastSeen=datetime('now')",
                    {std::to_string(serverId), p.steamId, p.name,
                     std::to_string(serverId), p.steamId, p.name});
            }
        });

        return ctx.json({{"success", true}, {"playerCount", players.size()}});
    }, false);

    // POST /api/events/chat
    server.post("/api/events/chat", [&](Context& ctx) {
        std::string serverIdStr = jsonStr(ctx.body, "serverId");
        if (serverIdStr.empty()) return ctx.error("serverId required");
        int serverId = safeStoi(serverIdStr);
        if (!validateRelayServerAccess(db, ctx.req, ctx.body, serverId)) return ctx.error("Unauthorized: apiKey does not match serverId", 401);
        auto timestamp = sp::nowISO();

        // Raw mode: parse from raw log text
        std::string raw = jsonStr(ctx.body, "raw");
        std::string steamId = jsonStr(ctx.body, "steamId");

        if (!raw.empty() && steamId.empty()) {
            ChatEvent chat;
            if (LogParser::parseChatRaw(raw, chat)) {
                // Dedup check: 5 seconds
                auto dup = db.queryOne(
                    "SELECT id FROM chat_logs WHERE serverId=? AND steamId=? "
                    "AND message=? AND timestamp > datetime('now', '-5 seconds') LIMIT 1",
                    {std::to_string(serverId), chat.steamId, chat.message});
                if (dup.empty()) {
                    db.exec(
                        "INSERT INTO chat_logs (serverId,playerName,steamId,message,type,timestamp) "
                        "VALUES(?,?,?,?,?,?)",
                        {std::to_string(serverId), chat.playerName, chat.steamId,
                         chat.message, chat.chatType, timestamp});
                }

                wsBroadcast(serverId, "chat", {
                    {"type", "chat"},
                    {"serverId", serverId},
                    {"playerName", chat.playerName},
                    {"steamId", chat.steamId},
                    {"message", chat.message},
                    {"chatType", chat.chatType},
                    {"timestamp", timestamp}
                });
                return ctx.json({{"message", "ok"}});
            }
            return ctx.error("Cannot parse chat message");
        }

        // Direct mode: steamId + message provided
        std::string message = jsonStr(ctx.body, "message");
        std::string playerName = jsonStr(ctx.body, "playerName");
        std::string chatType = jsonStr(ctx.body, "chatType", "chat");

        if (steamId.empty() || message.empty()) {
            return ctx.error("steamId and message required");
        }

        // Dedup
        auto dup = db.queryOne(
            "SELECT id FROM chat_logs WHERE serverId=? AND steamId=? "
            "AND message=? AND timestamp > datetime('now', '-5 seconds') LIMIT 1",
            {std::to_string(serverId), steamId, message});
        if (dup.empty()) {
            db.exec(
                "INSERT INTO chat_logs (serverId,playerName,steamId,message,type,timestamp) "
                "VALUES(?,?,?,?,?,?)",
                {std::to_string(serverId), playerName, steamId, message, chatType, timestamp});
        }

        wsBroadcast(serverId, "chat", {
            {"type", "chat"},
            {"serverId", serverId},
            {"playerName", playerName},
            {"steamId", steamId},
            {"message", message},
            {"chatType", chatType},
            {"timestamp", timestamp}
        });

        return ctx.json({{"message", "ok"}});
    }, false);

    // POST /api/events/kill
    server.post("/api/events/kill", [&](Context& ctx) {
        std::string serverIdStr = jsonStr(ctx.body, "serverId");
        if (serverIdStr.empty()) return ctx.error("serverId required");
        int serverId = safeStoi(serverIdStr);
        if (!validateRelayServerAccess(db, ctx.req, ctx.body, serverId)) return ctx.error("Unauthorized: apiKey does not match serverId", 401);

        std::string victimSteamId = jsonStr(ctx.body, "victimSteamId");
        std::string killerSteamId = jsonStr(ctx.body, "killerSteamId");
        std::string victimName = jsonStr(ctx.body, "victimName");
        std::string killerName = jsonStr(ctx.body, "killerName");
        std::string weapon = jsonStr(ctx.body, "weapon");
        auto timestamp = sp::nowISO();

        if (victimSteamId.empty()) return ctx.error("victimSteamId required");

        // TK detection
        bool tk = false;
        if (!killerSteamId.empty() && killerSteamId != victimSteamId) {
            tk = isTeamKill(serverId, killerSteamId, victimSteamId);
        }

        std::string eventType = tk ? "teamkill" : "kill";

        // Insert kill record
        db.exec(
            "INSERT INTO kills (serverId,killer,victim,weapon,timestamp) VALUES(?,?,?,?,?)",
            {std::to_string(serverId),
             killerSteamId.empty() ? killerName : killerSteamId,
             victimSteamId, weapon, timestamp});

        // Auto-score
        if (g_pointsService) {
            bool isSelfKill = (!killerSteamId.empty() && killerSteamId == victimSteamId);
            if (isSelfKill) {
                g_pointsService->scoreSuicide(serverId, victimSteamId, victimName);
            } else if (!killerSteamId.empty()) {
                g_pointsService->scoreKill(serverId, killerSteamId, killerName, tk);
            }
        }

        // Broadcast
        wsBroadcast(serverId, eventType, {
            {"type", eventType},
            {"serverId", serverId},
            {"killerSteamId", killerSteamId},
            {"killerName", killerName},
            {"victimSteamId", victimSteamId},
            {"victimName", victimName},
            {"weapon", weapon},
            {"timestamp", timestamp}
        });

        return ctx.json({{"message", "ok"}});
    }, false);

    // POST /api/events/revive
    server.post("/api/events/revive", [&](Context& ctx) {
        std::string serverIdStr = jsonStr(ctx.body, "serverId");
        if (serverIdStr.empty()) return ctx.error("serverId required");
        int serverId = safeStoi(serverIdStr);
        if (!validateRelayServerAccess(db, ctx.req, ctx.body, serverId)) return ctx.error("Unauthorized: apiKey does not match serverId", 401);

        std::string revivedSteamId = jsonStr(ctx.body, "revivedSteamId");
        std::string reviverSteamId = jsonStr(ctx.body, "reviverSteamId");
        std::string revivedName = jsonStr(ctx.body, "revivedName");
        std::string reviverName = jsonStr(ctx.body, "reviverName");
        auto timestamp = sp::nowISO();

        if (revivedSteamId.empty()) return ctx.error("revivedSteamId required");

        // Insert revive record
        db.exec(
            "INSERT INTO revives (serverId,reviverSteamId,reviverName,"
            "revivedSteamId,revivedName,timestamp) VALUES(?,?,?,?,?,?)",
            {std::to_string(serverId), reviverSteamId, reviverName,
             revivedSteamId, revivedName, timestamp});

        // Auto-score
        if (g_pointsService) {
            g_pointsService->scoreRevive(serverId, reviverSteamId, reviverName);
        }

        // Broadcast
        wsBroadcast(serverId, "revive", {
            {"type", "revive"},
            {"serverId", serverId},
            {"revivedSteamId", revivedSteamId},
            {"revivedName", revivedName},
            {"reviverSteamId", reviverSteamId},
            {"reviverName", reviverName},
            {"timestamp", timestamp}
        });

        // Notify both players via RCON
        if (!revivedSteamId.empty() && !reviverName.empty()) {
            if (auto* pool = server.rconPool()) {
                std::string cmd = "AdminWarn \"" + rconSafe(revivedSteamId) + "\" " +
                                  rconSafe(reviverName) + " 救了你";
                pool->send(serverId, cmd);
            }
        }

        // Notify reviver about +3 points
        if (!reviverSteamId.empty() && !revivedName.empty()) {
            if (auto* pool = server.rconPool()) {
                std::string cmd2 = "AdminWarn \"" + rconSafe(reviverSteamId) + "\" 救了 " + rconSafe(revivedName) + "，积分 " + std::to_string(getSettingInt(db, "points_revive", 1));
                pool->send(serverId, cmd2);
            }
        }

        return ctx.json({{"message", "ok"}});
    }, false);

    // POST /api/events/wound
    server.post("/api/events/wound", [&](Context& ctx) {
        std::string serverIdStr = jsonStr(ctx.body, "serverId");
        if (serverIdStr.empty()) return ctx.error("serverId required");
        int serverId = safeStoi(serverIdStr);
        if (!validateRelayServerAccess(db, ctx.req, ctx.body, serverId)) return ctx.error("Unauthorized: apiKey does not match serverId", 401);
        auto timestamp = sp::nowISO();

        std::string victimName = jsonStr(ctx.body, "victimName");
        std::string attackerSteamId = jsonStr(ctx.body, "attackerSteamId");
        std::string weapon = jsonStr(ctx.body, "weapon");

        wsBroadcast(serverId, "wound", {
            {"type", "wound"},
            {"serverId", serverId},
            {"victimName", victimName},
            {"attackerSteamId", attackerSteamId},
            {"weapon", weapon},
            {"timestamp", timestamp}
        });

        return ctx.json({{"message", "ok"}});
    }, false);

    // POST /api/events/admin-camera
    server.post("/api/events/admin-camera", [&](Context& ctx) {
        std::string serverIdStr = jsonStr(ctx.body, "serverId");
        if (serverIdStr.empty()) return ctx.error("serverId required");
        int serverId = safeStoi(serverIdStr);
        if (!validateRelayServerAccess(db, ctx.req, ctx.body, serverId)) return ctx.error("Unauthorized: apiKey does not match serverId", 401);

        std::string playerName = jsonStr(ctx.body, "playerName");
        std::string action = jsonStr(ctx.body, "action");
        if (playerName.empty() || action.empty()) {
            return ctx.error("playerName and action required");
        }

        auto timestamp = sp::nowISO();

        if (action == "possessed") {
            db.exec(
                "INSERT INTO op_logs (operator,action,target,details,serverId,createdAt) "
                "VALUES(?,'fly',?,?,?,datetime('now'))",
                {"系统(自动)", playerName,
                 "系统(自动) 在 " + timestamp + " 执行了飞天（" + playerName + "）进入",
                 std::to_string(serverId)});
        } else {
            auto last = db.queryOne(
                "SELECT id, details FROM op_logs WHERE action='fly' AND target=? "
                "AND serverId=? ORDER BY id DESC LIMIT 1",
                {playerName, std::to_string(serverId)});
            if (!last.empty() && last["details"].find("进入") != std::string::npos) {
                std::string details = last["details"];
                auto pos = details.find("进入");
                if (pos != std::string::npos) {
                    details.replace(pos, 6, "到 " + timestamp + " 离开");
                }
                db.exec("UPDATE op_logs SET details=? WHERE id=?",
                        {details, last["id"]});
            } else {
                db.exec(
                    "INSERT INTO op_logs (operator,action,target,details,serverId,createdAt) "
                    "VALUES(?,'fly',?,?,?,datetime('now'))",
                    {"系统(自动)", playerName,
                     "系统(自动) 在 " + timestamp + " 执行了飞天（" + playerName + "）离开",
                     std::to_string(serverId)});
            }
        }

        return ctx.json({{"success", true}});
    }, false);

    // POST /api/events/raw
    // Receive raw log lines from relay, parse and dispatch
    server.post("/api/events/raw", [&](Context& ctx) {
        std::string serverIdStr = jsonStr(ctx.body, "serverId");
        if (serverIdStr.empty()) return ctx.error("serverId required");
        int serverId = safeStoi(serverIdStr);
        if (!validateRelayServerAccess(db, ctx.req, ctx.body, serverId)) return ctx.error("Unauthorized: apiKey does not match serverId", 401);

        // Accept either array of lines or single line
        std::vector<std::string> lines;
        if (ctx.body.contains("lines") && ctx.body["lines"].is_array()) {
            for (auto& l : ctx.body["lines"]) {
                lines.push_back(l.get<std::string>());
            }
        } else if (ctx.body.contains("line")) {
            lines.push_back(jsonStr(ctx.body, "line"));
        }

        if (lines.empty()) return ctx.json({{"success", true}, {"processed", 0}});

        int killCount = 0, woundCount = 0, reviveCount = 0, chatCount = 0;
        auto timestamp = sp::nowISO();
        auto nowMs = std::chrono::steady_clock::now().time_since_epoch();
        int64_t nowEpoch = std::chrono::duration_cast<std::chrono::milliseconds>(nowMs).count();

        for (auto& line : lines) {
            if (line.size() < 10) continue;

            auto events = g_logParser->processLine(line, serverId);

            for (auto& ev : events) {
                if (ev.type == "kill") {
                    // Resolve names
                    std::string killerName = ev.fields["killerName"];
                    std::string killerSteamId = ev.fields["killerSteamId"];
                    std::string victimName = ev.fields["victimName"];
                    std::string weapon = ev.fields["weapon"];

                    // Try to find victim steamId from live cache
                    std::string victimSteamId;
                    if (!victimName.empty()) {
                        victimSteamId = findSteamIdByName(serverId, victimName);
                    }

                    // Resolve killer name if it's a controller ID
                    if (killerName.find("PlayerController") != std::string::npos &&
                        !killerSteamId.empty()) {
                        killerName = resolvePlayerName(db, serverId, killerSteamId, killerName);
                    }

                    // Check TK
                    bool tk = false;
                    bool isSelfKill = (!killerSteamId.empty() && killerSteamId == victimSteamId);
                    if (!isSelfKill && !killerSteamId.empty() && !victimSteamId.empty()) {
                        tk = isTeamKill(serverId, killerSteamId, victimSteamId);
                    }

                    // Insert kill
                    db.exec(
                        "INSERT INTO kills (serverId,killer,victim,weapon,timestamp) VALUES(?,?,?,?,?)",
                        {std::to_string(serverId), killerSteamId, victimSteamId,
                         weapon, timestamp});

                    // Auto-score
                    if (g_pointsService) {
                        if (isSelfKill) {
                            g_pointsService->scoreSuicide(serverId, victimSteamId, victimName);
                        } else {
                            g_pointsService->scoreKill(serverId, killerSteamId, killerName, tk);
                        }
                    }

                    // AdminWarn notifications for kill/suicide/TK
                    if (auto* pool = server.rconPool()) {
                        if (!isSelfKill && !tk && !killerSteamId.empty()) {
                            pool->send(serverId, "AdminWarn \"" + rconSafe(killerSteamId) + "\" 你使用 " + rconSafe(weapon) + " 击杀了 " + rconSafe(victimName.empty() ? "未知" : victimName) + "，积分 " + std::to_string(getSettingInt(db, "points_kill", 2)));
                        }
                        if (!isSelfKill && !victimSteamId.empty()) {
                            pool->send(serverId, "AdminWarn \"" + rconSafe(victimSteamId) + "\" 你被 " + rconSafe(killerName.empty() ? "未知" : killerName) + " 使用 " + rconSafe(weapon) + " 击杀，积分 " + std::to_string(getSettingInt(db, "points_suicide", -2)));
                        }
                        if (tk && !killerSteamId.empty()) {
                            pool->send(serverId, "AdminWarn \"" + rconSafe(killerSteamId) + "\" 你TK了" + rconSafe(victimName.empty() ? "队友" : victimName) + "，积分 " + std::to_string(getSettingInt(db, "points_tk", -5)));
                        }
                    }

                    // Plugin dispatch for kill events
                    {
                        PluginContext pctx{db, *server.rconPool(),
                            [&](int s, const std::string& st) { if (auto* p = server.rconPool()) p->send(s, "AdminWarn \"" + st + "\""); },
                            [](const std::string& m) { LOG_I("Plugin", m); }
                        };
                        PluginEvent pev;
                        pev.type = "kill";
                        pev.serverId = serverId;
                        std::string evType = tk ? "teamkill" : (isSelfKill ? "suicide" : "kill");
                        pev.data = {{"type", evType}, {"killerName", killerName}, {"killerSteamId", killerSteamId},
                                    {"victimName", victimName}, {"victimSteamId", victimSteamId}, {"weapon", weapon}};
                        PluginManager::instance().dispatch(pev, pctx);
                    }

                    std::string evType = tk ? "teamkill" : (isSelfKill ? "suicide" : "kill");
                    wsBroadcast(serverId, evType, {
                        {"type", evType},
                        {"serverId", serverId},
                        {"killerName", killerName},
                        {"killerSteamId", killerSteamId},
                        {"victimName", victimName},
                        {"victimSteamId", victimSteamId},
                        {"weapon", weapon},
                        {"timestamp", timestamp}
                    });
                    killCount++;

                } else if (ev.type == "suicide") {
                    std::string victimName = ev.fields["victimName"];
                    std::string victimSteamId = ev.fields["victimSteamId"];

                    // Suicide cooldown (10 seconds)
                    {
                        std::lock_guard<std::mutex> lk(g_suicideMtx);
                        if (nowEpoch - g_lastSuicideTime[serverId] < Config::get().getInt("SUICIDE_DEDUP_WINDOW_MS", 10000)) continue;
                        g_lastSuicideTime[serverId] = nowEpoch;
                    }

                    // Try to resolve steamId
                    if (victimSteamId.empty() && !victimName.empty()) {
                        victimSteamId = findSteamIdByName(serverId, victimName);
                    }

                    if (g_pointsService && !victimSteamId.empty()) {
                        g_pointsService->scoreSuicide(serverId, victimSteamId, victimName);
                    }

                    // Notify via RCON
                    if (!victimSteamId.empty()) {
                        if (auto* pool = server.rconPool()) {
                            pool->send(serverId,
                                       "AdminWarn \"" + rconSafe(victimSteamId) + "\" 你自杀了，积分 " + std::to_string(getSettingInt(db, "points_suicide", -2)));
                        }
                    }

                    wsBroadcast(serverId, "suicide", {
                        {"type", "suicide"},
                        {"serverId", serverId},
                        {"victimName", victimName},
                        {"victimSteamId", victimSteamId},
                        {"timestamp", timestamp}
                    });
                    killCount++;

                } else if (ev.type == "wound") {
                    std::string victimName = ev.fields["victimName"];
                    std::string attackerSteamId = ev.fields["attackerSteamId"];
                    std::string weapon = ev.fields["weapon"];

                    // Find victim steamId
                    std::string victimSteamId = findSteamIdByName(serverId, victimName);

                    // Notify via RCON
                    if (auto* pool = server.rconPool()) {
                        if (!attackerSteamId.empty()) {
                            pool->send(serverId,
                                       "AdminWarn \"" + rconSafe(attackerSteamId) + "\" 你使用 " +
                                       rconSafe(weapon) + " 击倒了 " + rconSafe(victimName));
                        }
                        if (!victimSteamId.empty()) {
                            std::string killerName = ev.fields["attackerName"];
                            if (killerName.find("PlayerController") != std::string::npos &&
                                !attackerSteamId.empty()) {
                                killerName = resolvePlayerName(db, serverId, attackerSteamId, killerName);
                            }
                            pool->send(serverId,
                                       "AdminWarn \"" + rconSafe(victimSteamId) + "\" 你被 " +
                                       rconSafe(killerName) + " 使用 " + rconSafe(weapon) + " 击倒");
                        }
                    }

                    wsBroadcast(serverId, "wound", {
                        {"type", "wound"},
                        {"serverId", serverId},
                        {"victimName", victimName},
                        {"victimSteamId", victimSteamId},
                        {"attackerSteamId", attackerSteamId},
                        {"weapon", weapon},
                        {"timestamp", timestamp}
                    });
                    woundCount++;

                } else if (ev.type == "revive") {
                    std::string reviverName = ev.fields["reviverName"];
                    std::string reviverSteamId = ev.fields["reviverSteamId"];
                    std::string revivedName = ev.fields["revivedName"];
                    std::string revivedSteamId = ev.fields["revivedSteamId"];

                    db.exec(
                        "INSERT INTO revives (serverId,reviverSteamId,reviverName,"
                        "revivedSteamId,revivedName,timestamp) VALUES(?,?,?,?,?,?)",
                        {std::to_string(serverId), reviverSteamId, reviverName,
                         revivedSteamId, revivedName, timestamp});

                    if (g_pointsService) {
                        g_pointsService->scoreRevive(serverId, reviverSteamId, reviverName);
                    }

                    // Notify via RCON
                    if (!revivedSteamId.empty() && !reviverName.empty()) {
                        if (auto* pool = server.rconPool()) {
                            pool->send(serverId,
                                       "AdminWarn \"" + rconSafe(revivedSteamId) + "\" 你被 " +
                                       rconSafe(reviverName) + " 救活了");
                        }
                    }

                    // Notify reviver about points
                    if (!reviverSteamId.empty() && !revivedName.empty()) {
                        if (auto* pool = server.rconPool()) {
                            pool->send(serverId,
                                       "AdminWarn \"" + rconSafe(reviverSteamId) + "\" ä½ æäº " +
                                       rconSafe(revivedName) + "，积分 " + std::to_string(getSettingInt(db, "points_revive", 1)));
                        }
                    }

                    wsBroadcast(serverId, "revive", {
                        {"type", "revive"},
                        {"serverId", serverId},
                        {"reviverName", reviverName},
                        {"reviverSteamId", reviverSteamId},
                        {"revivedName", revivedName},
                        {"revivedSteamId", revivedSteamId},
                        {"timestamp", timestamp}
                    });
                    reviveCount++;

                    // Plugin dispatch for revive
                    {
                        PluginContext pctx{db, *server.rconPool(),
                            [&](int s, const std::string& st) { if (auto* p = server.rconPool()) p->send(s, "AdminWarn \"" + st + "\""); },
                            [](const std::string& m) { LOG_I("Plugin", m); }
                        };
                        PluginEvent pev;
                        pev.type = "revive";
                        pev.serverId = serverId;
                        pev.data = {{"reviverName", reviverName}, {"reviverSteamId", reviverSteamId},
                                    {"revivedName", revivedName}, {"revivedSteamId", revivedSteamId}};
                        PluginManager::instance().dispatch(pev, pctx);
                    }

                } else if (ev.type == "chat") {
                    std::string playerName = ev.fields["playerName"];
                    std::string steamId = ev.fields["steamId"];
                    std::string message = ev.fields["message"];

                    // Dedup
                    auto dup = db.queryOne(
                        "SELECT id FROM chat_logs WHERE serverId=? AND steamId=? "
                        "AND message=? AND timestamp > datetime('now', '-5 seconds') LIMIT 1",
                        {std::to_string(serverId), steamId, message});
                    if (dup.empty()) {
                        db.exec(
                            "INSERT INTO chat_logs (serverId,playerName,steamId,message,type,timestamp) "
                            "VALUES(?,?,?,?,?,?)",
                            {std::to_string(serverId), playerName, steamId,
                             message, "all", timestamp});
                    }

                    wsBroadcast(serverId, "chat", {
                        {"type", "chat"},
                        {"serverId", serverId},
                        {"playerName", playerName},
                        {"steamId", steamId},
                        {"message", message},
                        {"timestamp", timestamp}
                    });
                    chatCount++;

                    // Plugin dispatch for chat
                    {
                        PluginContext pctx{db, *server.rconPool(),
                            [&](int s, const std::string& st) { if (auto* p = server.rconPool()) p->send(s, "AdminWarn \"" + st + "\""); },
                            [](const std::string& m) { LOG_I("Plugin", m); }
                        };
                        PluginEvent pev;
                        pev.type = "chat";
                        pev.serverId = serverId;
                        pev.data = {{"playerName", playerName}, {"steamId", steamId}, {"message", message}};
                        PluginManager::instance().dispatch(pev, pctx);
                    }
                }
            }
        }

        return ctx.json({{"success", true},
                         {"results", {{"kill", killCount}, {"wound", woundCount},
                                      {"revive", reviveCount}, {"chat", chatCount}}}});
    }, false);

    LOG_I("Events", "Event routes registered");
}

// Module init (called from main)

void initEventModule(LogParser* parser, WsHandler* ws, PointsService* pts) {
    g_logParser = parser;
    g_wsHandler = ws;
    g_pointsService = pts;
    g_moduleShutdown = false;
}

void shutdownEventModule() {
    g_moduleShutdown = true;
    g_logParser = nullptr;
    g_wsHandler = nullptr;
    g_pointsService = nullptr;
}

} // namespace sp
