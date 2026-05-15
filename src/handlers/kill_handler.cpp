#include "handlers/kill_handler.h"
#include "net/server.h"
#include "core/database.h"
#include "core/core.h"

namespace sp {

void registerKillRoutes(Server& server) {
    auto& db = server.db();

    // GET /api/kills
    server.get("/api/kills", [&](Context& ctx) {
        std::string sid = ctx.query("serverId");
        int limit       = std::stoi(ctx.query("limit", "100"));

        std::vector<std::unordered_map<std::string, std::string>> rows;
        if (!sid.empty()) {
            rows = db.query(
                "SELECT * FROM kills WHERE serverId=? ORDER BY timestamp DESC LIMIT ?",
                {sid, std::to_string(limit)});
        } else {
            rows = db.query(
                "SELECT * FROM kills ORDER BY timestamp DESC LIMIT ?",
                {std::to_string(limit)});
        }

        nlohmann::json kills = nlohmann::json::array();
        for (auto& r : rows) {
            nlohmann::json k;
            for (auto& [k2, v] : r) k[k2] = v;
            kills.push_back(k);
        }
        return ctx.json({{"kills", kills}});
    });

    // GET /api/kills/stats
    server.get("/api/kills/stats", [&](Context& ctx) {
        std::string sid = ctx.query("serverId");
        if (sid.empty()) return ctx.error("serverId required");

        auto total = db.queryOne(
            "SELECT COUNT(*) as c FROM kills WHERE serverId=?", {sid});
        // Suicide: killer == victim (same steamId stored in both columns)
        auto suicides = db.queryOne(
            "SELECT COUNT(*) as c FROM kills WHERE serverId=? AND killer=victim", {sid});
        // TK detection requires team map (in-memory), count from chat_logs type='teamkill' as approximation
        // or count kills where killer and victim are on the same team (requires join)
        // For now return 0 — TK stats are tracked via WebSocket events in real-time
        auto tkRow = db.queryOne("SELECT COUNT(*) as c FROM point_logs WHERE serverId=? AND reason LIKE '%TK%'", {sid});
        int teamKills = std::stoi(tkRow["c"]);

        return ctx.json({
            {"totalKills", std::stoi(total["c"])},
            {"teamKills", teamKills},
            {"suicides", std::stoi(suicides["c"])}
        });
    });
}

} // namespace sp
