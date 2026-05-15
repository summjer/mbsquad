#include "handlers/player_handler.h"
#include "net/server.h"
#include "net/rcon.h"
#include "core/database.h"
#include "core/auth.h"
#include "core/core.h"
#include "core/shared_utils.h"

#include <unordered_map>
#include <mutex>

namespace sp {

// In-memory live player cache

// Live cache is in event_handler.cpp
void registerPlayerRoutes(Server& server) {
    auto& db = server.db();

    // GET /api/players — from DB
    server.get("/api/players", [&](Context& ctx) {
        std::string serverId = ctx.query("serverId");
        std::vector<std::unordered_map<std::string, std::string>> rows;
        if (!serverId.empty()) {
            rows = db.query(
                "SELECT * FROM players WHERE serverId=? ORDER BY lastSeen DESC LIMIT 100",
                {serverId});
        } else {
            rows = db.query("SELECT * FROM players ORDER BY lastSeen DESC LIMIT 100");
        }

        nlohmann::json players = nlohmann::json::array();
        for (auto& row : rows) {
            nlohmann::json p;
            p["id"]        = safeStoll(row["id"]);
            p["serverId"]  = safeStoi(row["serverId"]);
            p["steamId"]   = row["steamId"];
            p["name"]      = row["name"];
            p["playtime"]  = safeStoi(row["playtime"]);
            p["firstSeen"] = row["firstSeen"];
            p["lastSeen"]  = row["lastSeen"];
            players.push_back(p);
        }
        return ctx.json({{"players", players}});
    });

    // POST /api/players/kick
    server.post("/api/players/kick", [&](Context& ctx) {
        std::string serverId   = jsonStr(ctx.body, "serverId");
        std::string steamId    = jsonStr(ctx.body, "steamId");
        std::string reason     = jsonStr(ctx.body, "reason", "Kicked by admin");
        std::string playerName = jsonStr(ctx.body, "playerName");

        if (serverId.empty() || steamId.empty()) {
            return ctx.error("serverId and steamId required");
        }

        auto* pool = server.rconPool();
        if (!pool) return ctx.error("RCON not available", 500);

        std::string cmd = "AdminKick \"" + rconSafe(steamId) + "\" " + rconSafe(reason);
        auto result = pool->send(std::stoi(serverId), cmd);
        if (result.empty()) {
            return ctx.error("RCON command failed", 500);
        }

        // Log operation
        db.exec(
            "INSERT INTO op_logs (operator,action,target,details,serverId) VALUES(?,?,?,?,?)",
            {ctx.username, "other", playerName.empty() ? steamId : playerName,
             ctx.username + " kicked " + playerName + " SteamID: " + steamId +
             " reason: " + reason,
             serverId});

        return ctx.json({{"result", result}});
    });

    // POST /api/players/switch-team
    server.post("/api/players/switch-team", [&](Context& ctx) {
        std::string serverId   = jsonStr(ctx.body, "serverId");
        std::string steamId    = jsonStr(ctx.body, "steamId");
        std::string playerName = jsonStr(ctx.body, "playerName");

        if (serverId.empty() || steamId.empty()) {
            return ctx.error("serverId and steamId required");
        }

        auto* pool = server.rconPool();
        if (!pool) return ctx.error("RCON not available", 500);

        std::string cmd = "AdminForceTeamChange \"" + rconSafe(steamId) + "\"";
        auto result = pool->send(std::stoi(serverId), cmd);
        if (result.empty()) {
            return ctx.error("RCON command failed", 500);
        }

        // Log operation
        db.exec(
            "INSERT INTO op_logs (operator,action,target,details,serverId) VALUES(?,?,?,?,?)",
            {ctx.username, "other", playerName.empty() ? steamId : playerName,
             ctx.username + " switched team for " + playerName + " SteamID: " + steamId,
             serverId});

        return ctx.json({{"result", result}});
    });

    // POST /api/players/refresh — pull players via RCON ListPlayers
    server.post("/api/players/refresh", [&](Context& ctx) {
        std::string serverId = jsonStr(ctx.body, "serverId");
        if (serverId.empty()) return ctx.error("serverId required");

        auto* pool = server.rconPool();
        if (!pool) return ctx.error("RCON not available", 500);

        std::string result;
        try {
            result = pool->send(std::stoi(serverId), "ListPlayers");
        } catch (const std::exception& e) {
            return ctx.error(std::string("RCON error: ") + e.what(), 500);
        }

        return ctx.json({{"message", "Refreshed"}, {"result", result}});
    });

    // POST /api/players/afk-kick — kick AFK players
    server.post("/api/players/afk-kick", [&](Context& ctx) {
        std::string serverId = jsonStr(ctx.body, "serverId");
        if (serverId.empty()) return ctx.error("serverId required");

        auto* pool = server.rconPool();
        if (!pool) return ctx.error("RCON not available", 500);

        // Get current players
        std::string listResult;
        try {
            listResult = pool->send(std::stoi(serverId), "ListPlayers");
        } catch (const std::exception& e) {
            return ctx.error(std::string("RCON error: ") + e.what(), 500);
        }

        // Check afk_kick settings
        auto afkEnabled = db.queryOne("SELECT value FROM settings WHERE key='afk_kick_enabled'", {});
        if (afkEnabled.empty() || afkEnabled["value"] != "1") {
            return ctx.json({{"message", "AFK kick not enabled"}, {"kicked", 0}});
        }

        auto afkSecs = db.queryOne("SELECT value FROM settings WHERE key='afk_kick_seconds'", {});
        int threshold = Config::get().getInt("AFK_KICK_SECONDS", 300);
        if (!afkSecs.empty()) { try { threshold = std::stoi(afkSecs["value"]); } catch (...) {} }

        // Find AFK players (no kills in threshold period)
        auto afkPlayers = db.query(
            "SELECT steamId, name FROM players WHERE serverId=? "
            "AND (lastSeen < datetime('now', ?) OR lastSeen IS NULL)",
            {serverId, "-" + std::to_string(threshold) + " seconds"});

        int kicked = 0;
        for (auto& p : afkPlayers) {
            try {
                pool->send(std::stoi(serverId),
                    "AdminKick \"" + rconSafe(p["steamId"]) + "\" AFK too long");
                kicked++;
            } catch (...) {}
        }

        return ctx.json({{"message", "Done"}, {"kicked", kicked}});
    });

}

} // namespace sp

