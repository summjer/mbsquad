#include "handlers/ban_handler.h"
#include "core/shared_utils.h"
#include "net/server.h"
#include "net/rcon.h"
#include "core/database.h"
#include "core/auth.h"
#include "core/core.h"
#include <httplib.h>

namespace sp {

void registerBanRoutes(Server& server) {
    auto& db = server.db();

    // GET /api/bans
    server.get("/api/bans", [&](Context& ctx) {
        std::string sid = ctx.query("serverId");
        std::vector<std::unordered_map<std::string, std::string>> rows;

        if (!sid.empty()) {
            rows = db.query(
                "SELECT * FROM bans WHERE serverId=? ORDER BY createdAt DESC", {sid});
        } else {
            rows = db.query(
                "SELECT * FROM bans ORDER BY createdAt DESC LIMIT 50", {});
        }

        nlohmann::json bans = nlohmann::json::array();
        for (auto& r : rows) {
            nlohmann::json b;
            for (auto& [k, v] : r) b[k] = v;
            bans.push_back(b);
        }
        return ctx.json({{"bans", bans}});
    });

    // POST /api/bans
    server.post("/api/bans", [&](Context& ctx) {
        std::string serverId = jsonStr(ctx.body, "serverId");
        std::string steamId  = jsonStr(ctx.body, "steamId");
        std::string playerName = jsonStr(ctx.body, "playerName");
        std::string reason   = jsonStr(ctx.body, "reason");
        int duration         = jsonInt(ctx.body, "duration", 0);

        if (serverId.empty() || steamId.empty()) {
            return ctx.error("serverId and steamId required");
        }

        db.exec(
            "INSERT INTO bans (serverId,steamId,playerName,reason,bannedBy,duration) "
            "VALUES(?,?,?,?,?,?)",
            {serverId, steamId, playerName, reason, ctx.username, std::to_string(duration)});

        // RCON ban
        std::string rconResult;
        std::string rconError;
        if (auto* pool = server.rconPool()) {
            try {
                std::string cmd = "AdminBan \"" + rconSafe(steamId) + "\" " + std::to_string(duration)
                    + " " + rconSafe(reason.empty() ? "Banned by panel" : reason);
                rconResult = pool->send(std::stoi(serverId), cmd);
            } catch (const std::exception& e) {
                rconError = e.what();
            }
        }

        return ctx.json({
            {"id", db.lastInsertId()},
            {"rcon", rconResult},
            {"rconError", rconError}
        });
    });

    // DELETE /api/bans/:id
    server.del(R"(/api/bans/(\d+))", [&](Context& ctx) {
        std::string banId = ctx.param(1);
        auto ban = db.queryOne("SELECT * FROM bans WHERE id=?", {banId});
        if (ban.empty()) return ctx.error("Ban not found");

        // Get server relay info for unban
        auto srv = db.queryOne(
            "SELECT relayUrl, relayApiKey FROM servers WHERE id=?",
            {ban["serverId"]});

        std::string relayError;
        if (!srv["relayUrl"].empty() && !srv["relayApiKey"].empty()) {
            // Parse relayUrl to get host:port
            auto ep = parseRelayUrl(srv["relayUrl"]);
            httplib::Client cli(ep.host.c_str(), ep.port);
            cli.set_connection_timeout(Config::get().getInt("RELAY_CONNECT_TIMEOUT", 3));
            cli.set_read_timeout(Config::get().getInt("RELAY_READ_TIMEOUT", 3));
            nlohmann::json unbanBody = {{"steamId", ban["steamId"]}};
            auto res = cli.Post("/api/unban", {{"X-API-Key", srv["relayApiKey"]}},
                                unbanBody.dump(), "application/json");
            if (res && res->status == 200) {
                LOG_I("Unban", "Relay unban OK for " + ban["steamId"]);
            } else {
                relayError = res ? ("Relay returned " + std::to_string(res->status)) : "Relay unreachable";
                LOG_W("Unban", "Relay unban failed: " + relayError);
            }
        }

        db.exec("DELETE FROM bans WHERE id=?", {banId});

        nlohmann::json resp;
        resp["message"] = "Unbanned";
        if (!relayError.empty()) resp["relayError"] = relayError;
        return ctx.json(resp);
    });

}

} // namespace sp
