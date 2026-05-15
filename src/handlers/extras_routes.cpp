#include "net/server.h"
#include "core/database.h"
#include "core/core.h"

namespace sp {

void registerExtrasRoutes(Server& server) {
    auto& db = server.db();
    // âââ CDK Management âââ

    // GET /api/cdk
    server.get("/api/cdk", [&](Context& ctx) {
        auto rows = db.query("SELECT * FROM cdk_codes ORDER BY id DESC LIMIT 100", {});
        nlohmann::json codes = nlohmann::json::array();
        for (auto& r : rows) {
            nlohmann::json c;
            for (auto& [k, v] : r) c[k] = v;
            codes.push_back(c);
        }
        return ctx.json({{"codes", codes}});
    });

    // GET /api/cdk/logs
    server.get("/api/cdk/logs", [&](Context& ctx) {
        auto rows = db.query("SELECT * FROM cdk_logs ORDER BY usedAt DESC LIMIT 100", {});
        nlohmann::json logs = nlohmann::json::array();
        for (auto& r : rows) {
            nlohmann::json l;
            for (auto& [k, v] : r) l[k] = v;
            logs.push_back(l);
        }
        return ctx.json({{"logs", logs}});
    });

    // POST /api/cdk/batch â batch generate CDK codes
    server.post("/api/cdk/batch", [&](Context& ctx) {
        if (ctx.role != "server_owner") return ctx.error("Admin only", 403);
        int count      = jsonInt(ctx.body, "count", 1);
        std::string prefix = jsonStr(ctx.body, "prefix", "cdk");
        std::string rewardType  = jsonStr(ctx.body, "rewardType", "points");
        int rewardValue = jsonInt(ctx.body, "rewardValue", 50);
        int maxUses     = jsonInt(ctx.body, "maxUses", 1);

        if (count < 1) count = 1;
        if (count > 100) count = 100;

        nlohmann::json generated = nlohmann::json::array();
        static const char alphanum[] = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ";
        std::random_device rd;
        std::mt19937 gen(rd());
        std::uniform_int_distribution<> dis(0, 35);

        for (int i = 0; i < count; i++) {
            std::string code = prefix + "-";
            for (int j = 0; j < 8; j++) code += alphanum[dis(gen)];
            db.exec(
                "INSERT INTO cdk_codes (code,rewardType,rewardValue,maxUses) VALUES(?,?,?,?)",
                {code, rewardType, std::to_string(rewardValue), std::to_string(maxUses)});
            generated.push_back({{"code", code}, {"id", db.lastInsertId()}});
        }

        return ctx.json({{"generated", generated}});
    });

    // DELETE /api/cdk/:id
    server.del(R"(/api/cdk/(\d+))", [&](Context& ctx) {
        std::string id = ctx.param(1);
        db.exec("DELETE FROM cdk_codes WHERE id=?", {id});
        return ctx.json({{"message", "Deleted"}});
    });

    // âââ Broadcast Messages âââ

    // GET /api/broadcast/messages
    server.get("/api/broadcast/messages", [&](Context& ctx) {
        auto row = db.queryOne("SELECT value FROM settings WHERE key='broadcast_messages'", {});
        nlohmann::json msgs = nlohmann::json::array();
        if (!row.empty()) {
            try { msgs = nlohmann::json::parse(row["value"]); } catch (...) {}
        }
        // Also get interval
        auto intRow = db.queryOne("SELECT value FROM settings WHERE key='broadcast_interval'", {});
        int interval = 300;
        if (!intRow.empty()) { try { interval = std::stoi(intRow["value"]); } catch (...) {} }
        return ctx.json({{"messages", msgs}, {"interval", interval}});
    });

    // POST /api/broadcast/messages
    server.post("/api/broadcast/messages", [&](Context& ctx) {
        if (!ctx.body.contains("messages")) return ctx.error("messages required");
        std::string val = ctx.body["messages"].dump();
        db.exec("INSERT OR REPLACE INTO settings (key, value) VALUES('broadcast_messages', ?)", {val});
        if (ctx.body.contains("interval")) {
            int interval = jsonInt(ctx.body, "interval", 30);
            db.exec("INSERT OR REPLACE INTO settings (key, value) VALUES('broadcast_interval', ?)",
                    {std::to_string(interval)});
        }
        return ctx.json({{"message", "Broadcast messages saved"}});
    });

    // âââ OP Logs âââ

    // POST /api/op/logs â add OP log entry
    server.post("/api/op/logs", [&](Context& ctx) {
        std::string action  = jsonStr(ctx.body, "action", "other");
        std::string target  = jsonStr(ctx.body, "target");
        std::string details = jsonStr(ctx.body, "details");
        std::string serverId = jsonStr(ctx.body, "serverId");

        db.exec(
            "INSERT INTO op_logs (operator,action,target,details,serverId) VALUES(?,?,?,?,?)",
            {ctx.username, action, target, details, serverId});

        return ctx.json({{"id", db.lastInsertId()}, {"message", "Logged"}});
    });

} // registerXxxRoutes
} // namespace sp
