#include "handlers/points_handler.h"
#include "net/server.h"
#include "net/rcon.h"
#include "core/database.h"
#include "core/auth.h"
#include "core/core.h"
#include "core/shared_utils.h"

namespace sp {

void registerPointsRoutes(Server& server) {
    auto& db = server.db();

    // GET /api/points
    server.get("/api/points", [&](Context& ctx) {
        std::string q = ctx.query("q");
        std::vector<std::unordered_map<std::string, std::string>> rows;
        if (!q.empty()) {
            rows = db.query(
                "SELECT * FROM points WHERE steamId LIKE ? OR playerName LIKE ? "
                "ORDER BY balance DESC",
                {"%" + q + "%", "%" + q + "%"});
        } else {
            rows = db.query(
                "SELECT * FROM points ORDER BY balance DESC LIMIT 200");
        }

        nlohmann::json points = nlohmann::json::array();
        for (auto& row : rows) {
            nlohmann::json p;
            p["id"]             = row["id"].empty() ? 0 : std::stoll(row["id"]);
            p["serverId"]       = row["serverId"].empty() ? 0 : std::stoi(row["serverId"]);
            p["steamId"]        = row["steamId"];
            p["playerName"]     = row["playerName"];
            p["balance"]        = row["balance"].empty() ? 0 : std::stoi(row["balance"]);
            p["lifetimeEarned"] = row["lifetimeEarned"].empty() ? 0 : std::stoi(row["lifetimeEarned"]);
            p["lastUpdated"]    = row["lastUpdated"];
            points.push_back(p);
        }
        return ctx.json({{"points", points}});
    });

    // GET /api/points/leaderboard
    server.get("/api/points/leaderboard", [&](Context& ctx) {
        // Public — no auth required; override auth for this route
        int page = std::max(1, std::atoi(ctx.query("page", "1").c_str()));
        int pageSize = std::min(100, std::max(1, std::atoi(ctx.query("page_size", "20").c_str())));
        int offset = (page - 1) * pageSize;

        auto totalRow = db.queryOne("SELECT COUNT(*) as c FROM points");
        int total = safeStoi(totalRow["c"]);

        auto rows = db.query(
            "SELECT steamId, playerName, balance, lifetimeEarned "
            "FROM points ORDER BY balance DESC LIMIT ? OFFSET ?",
            {std::to_string(pageSize), std::to_string(offset)});

        nlohmann::json data = nlohmann::json::array();
        for (auto& row : rows) {
            nlohmann::json p;
            p["steamId"]        = row["steamId"];
            p["playerName"]     = row["playerName"];
            p["balance"]        = safeStoi(row["balance"]);
            p["lifetimeEarned"] = safeStoi(row["lifetimeEarned"]);
            data.push_back(p);
        }

        return ctx.json({
            {"data", data},
            {"page", page},
            {"page_size", pageSize},
            {"total", total},
            {"total_pages", (total + pageSize - 1) / pageSize}
        });
    }, false);

    // POST /api/points/adjust
    server.post("/api/points/adjust", [&](Context& ctx) {
        std::string steamId    = jsonStr(ctx.body, "steamId");
        std::string serverIdStr = jsonStr(ctx.body, "serverId");
        std::string playerName = jsonStr(ctx.body, "playerName");
        int amount             = jsonInt(ctx.body, "amount");
        std::string reason     = jsonStr(ctx.body, "reason", "manual");

        if (steamId.empty()) return ctx.error("steamId required");
        if (amount == 0) return ctx.error("amount must be non-zero");

        int serverId = safeStoi(serverIdStr);

        auto existing = db.queryOne(
            "SELECT id, balance FROM points WHERE steamId=? LIMIT 1",
            {steamId});

        if (!existing.empty()) {
            int newBal = safeStoi(existing["balance"]) + amount;
            if (newBal < 0) return ctx.error("积分不足");
            int earned = (amount > 0) ? amount : 0;
            db.exec(
                "UPDATE points SET balance=?, lifetimeEarned=lifetimeEarned+?, "
                "playerName=COALESCE(?,playerName), lastUpdated=datetime('now') WHERE id=?",
                {std::to_string(newBal), std::to_string(earned),
                 playerName.empty() ? "" : playerName, existing["id"]});
        } else {
            if (amount < 0) return ctx.error("该玩家无积分记录，无法扣减");
            int earned = (amount > 0) ? amount : 0;
            db.exec(
                "INSERT INTO points (serverId,steamId,playerName,balance,lifetimeEarned) "
                "VALUES(?,?,?,?,?)",
                {std::to_string(serverId), steamId,
                 playerName.empty() ? "" : playerName,
                 std::to_string(amount), std::to_string(earned)});
        }

        db.exec(
            "INSERT INTO point_logs (serverId,steamId,playerName,amount,reason,operator) "
            "VALUES(?,?,?,?,?,?)",
            {std::to_string(serverId), steamId,
             playerName.empty() ? "" : playerName,
             std::to_string(amount), reason, ctx.authenticated ? ctx.username : "系统"});

        return ctx.json({{"message", "操作成功"}});
    });

    // GET /api/points/logs
    server.get("/api/points/logs", [&](Context& ctx) {
        std::string serverIdStr = ctx.query("serverId");
        std::vector<std::unordered_map<std::string, std::string>> rows;
        if (!serverIdStr.empty()) {
            rows = db.query(
                "SELECT * FROM point_logs WHERE serverId=? "
                "ORDER BY createdAt DESC LIMIT 200",
                {serverIdStr});
        } else {
            rows = db.query(
                "SELECT * FROM point_logs ORDER BY createdAt DESC LIMIT 200");
        }

        nlohmann::json logs = nlohmann::json::array();
        for (auto& row : rows) {
            nlohmann::json l;
            l["id"]         = safeStoll(row["id"]);
            l["serverId"]   = safeStoi(row["serverId"]);
            l["steamId"]    = row["steamId"];
            l["playerName"] = row["playerName"];
            l["amount"]     = safeStoi(row["amount"]);
            l["reason"]     = row["reason"];
            l["operator"]   = row["operator"];
            l["createdAt"]  = row["createdAt"];
            logs.push_back(l);
        }
        return ctx.json({{"logs", logs}});
    });

    LOG_I("Points", "Points routes registered");
}

} // namespace sp
