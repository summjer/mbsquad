#include "handlers/tk_handler.h"
#include "net/server.h"
#include "net/rcon.h"
#include "core/database.h"
#include "core/core.h"
#include "core/shared_utils.h"

namespace sp {

void registerTkForgiveRoutes(Server& server) {
    auto& db = server.db();

    // GET /api/tk-forgive
    server.get("/api/tk-forgive", [&](Context& ctx) {
        std::string sid = ctx.query("serverId");
        std::string status = ctx.query("status", "active");
        std::vector<std::unordered_map<std::string, std::string>> rows;

        if (status == "active") {
            if (!sid.empty()) {
                rows = db.query(
                    "SELECT * FROM tk_forgive WHERE serverId=? AND forgiven=0 AND kicked=0 ORDER BY createdAt DESC",
                    {sid});
            } else {
                rows = db.query(
                    "SELECT * FROM tk_forgive WHERE forgiven=0 AND kicked=0 ORDER BY createdAt DESC", {});
            }
        } else {
            if (!sid.empty()) {
                rows = db.query(
                    "SELECT * FROM tk_forgive WHERE serverId=? ORDER BY createdAt DESC LIMIT 100", {sid});
            } else {
                rows = db.query(
                    "SELECT * FROM tk_forgive ORDER BY createdAt DESC LIMIT 100", {});
            }
        }

        nlohmann::json events = nlohmann::json::array();
        for (auto& r : rows) {
            nlohmann::json e;
            for (auto& [k, v] : r) e[k] = v;
            events.push_back(e);
        }
        return ctx.json({{"events", events}});
    });

    // POST /api/tk-forgive
    server.post("/api/tk-forgive", [&](Context& ctx) {
        std::string serverId      = jsonStr(ctx.body, "serverId");
        std::string killerSteamId = jsonStr(ctx.body, "killerSteamId");
        std::string killerName    = jsonStr(ctx.body, "killerName");
        std::string victimSteamId = jsonStr(ctx.body, "victimSteamId");
        std::string victimName    = jsonStr(ctx.body, "victimName");

        if (serverId.empty() || killerSteamId.empty())
            return ctx.error("serverId and killerSteamId required");

        auto secsRow = db.queryOne("SELECT value FROM settings WHERE key='tk_forgive_seconds'", {});
        int secs = Config::get().getInt("TK_FORGIVE_SECONDS_DEFAULT", 180);
        if (!secsRow.empty()) {
            try { secs = std::stoi(secsRow["value"]); } catch (...) {}
        }

        // expiresAt = now + secs
        auto now = std::chrono::system_clock::now();
        auto expires = now + std::chrono::seconds(secs);
        auto t = std::chrono::system_clock::to_time_t(expires);
        std::tm tm_buf;
        localtime_r(&t, &tm_buf);
        char buf[32];
        std::strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &tm_buf);
        std::string expiresAt(buf);

        db.exec(
            "INSERT INTO tk_forgive (serverId,killerSteamId,killerName,victimSteamId,victimName,expiresAt) "
            "VALUES(?,?,?,?,?,?)",
            {serverId, killerSteamId, killerName, victimSteamId, victimName, expiresAt});

        return ctx.json({
            {"id", db.lastInsertId()},
            {"expiresAt", expiresAt},
            {"seconds", secs}
        });
    });

    // DELETE /api/tk-forgive/:id
    server.del(R"(/api/tk-forgive/(\d+))", [&](Context& ctx) {
        std::string id = ctx.param(1);
        auto ev = db.queryOne("SELECT id FROM tk_forgive WHERE id=?", {id});
        if (ev.empty()) return ctx.error("TK record not found");
        db.exec("DELETE FROM tk_forgive WHERE id=?", {id});
        return ctx.json({{"message", "Deleted"}});
    });

    // POST /api/tk-forgive/:id/forgive
    server.post(R"(/api/tk-forgive/(\d+)/forgive)", [&](Context& ctx) {
        std::string id = ctx.param(1);
        auto ev = db.queryOne("SELECT id FROM tk_forgive WHERE id=?", {id});
        if (ev.empty()) return ctx.error("TK record not found");
        db.exec("UPDATE tk_forgive SET forgiven=1 WHERE id=?", {id});
        return ctx.json({{"message", "Forgiven"}});
    });

    // POST /api/tk-forgive/:id/kick
    server.post(R"(/api/tk-forgive/(\d+)/kick)", [&](Context& ctx) {
        std::string id = ctx.param(1);
        auto ev = db.queryOne("SELECT * FROM tk_forgive WHERE id=?", {id});
        if (ev.empty()) return ctx.error("TK record not found");

        db.exec("UPDATE tk_forgive SET kicked=1 WHERE id=?", {id});

        std::string rconResult, rconError;
        if (auto* pool = server.rconPool()) {
            try {
                std::string steamId = ev["killerSteamId"];
                // Strip non-numeric
                std::string clean;
                for (char c : steamId) if (std::isdigit(c)) clean += c;
                rconResult = pool->send(std::stoi(ev["serverId"]),
                    "AdminKick \"" + rconSafe(clean) + "\" TK kick by admin");
            } catch (const std::exception& e) {
                rconError = e.what();
            }
        }

        nlohmann::json resp;
        resp["message"] = "Kicked";
        resp["rcon"] = rconResult;
        resp["rconError"] = rconError;
        return ctx.json(resp);
    });
}

} // namespace sp
