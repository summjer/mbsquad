#include "handlers/server_handler.h"
#include "core/shared_utils.h"
#include "net/server.h"
#include "net/rcon.h"
#include "core/database.h"
#include "core/auth.h"
#include "core/core.h"

namespace sp {

// ── Helpers ──

// hasServerAccess and getServerRole are now in core/shared_utils.h

void registerServerRoutes(Server& server) {
    auto& db = server.db();

    // GET /api/servers — servers the user has access to (via server_members)
    server.get("/api/servers", [&](Context& ctx) {
        auto rows = db.query(
            "SELECT s.id,s.userId,s.name,s.host,s.rconPort,s.relayUrl,s.notes,"
            "s.connectionMode,s.logPath,s.remoteApiUrl,s.apiType,s.apiConfig,"
            "s.createdAt,s.updatedAt,sm.role as memberRole "
            "FROM servers s "
            "JOIN server_members sm ON s.id = sm.serverId "
            "WHERE sm.userId=? ORDER BY s.id DESC",
            {ctx.userId});

        nlohmann::json servers = nlohmann::json::array();
        for (auto& row : rows) {
            nlohmann::json s;
            s["id"]        = safeStoll(row["id"]);
            s["userId"]    = safeStoll(row["userId"]);
            s["name"]      = row["name"];
            s["host"]      = row["host"];
            s["rconPort"]  = safeStoi(row["rconPort"], 27015);
            s["relayUrl"]  = row["relayUrl"];
            s["notes"]     = row["notes"];
            s["createdAt"] = row["createdAt"];
            s["updatedAt"] = row["updatedAt"];
            s["memberRole"]= row["memberRole"];
            s["connectionMode"] = row.count("connectionMode") ? row["connectionMode"] : "relay";
            s["logPath"] = row.count("logPath") ? row["logPath"] : "";
            s["remoteApiUrl"] = row.count("remoteApiUrl") ? row["remoteApiUrl"] : "";
            s["apiType"] = row.count("apiType") ? row["apiType"] : "self";
            s["apiConfig"] = row.count("apiConfig") ? row["apiConfig"] : "{}";
            servers.push_back(s);
        }
        return ctx.json({{"servers", servers}});
    });

    // POST /api/servers — create server + auto-add creator as owner
    server.post("/api/servers", [&](Context& ctx) {
        std::string name     = jsonStr(ctx.body, "name");
        std::string host     = jsonStr(ctx.body, "host");
        int rconPort         = ctx.body.value("rconPort", 27015);
        std::string password = jsonStr(ctx.body, "rconPassword");
        std::string connectionMode = jsonStr(ctx.body, "connectionMode", "relay");
        std::string logPath  = jsonStr(ctx.body, "logPath");
        std::string remoteApiUrl = jsonStr(ctx.body, "remoteApiUrl");
        std::string remoteApiToken = jsonStr(ctx.body, "remoteApiToken");
        std::string apiType  = jsonStr(ctx.body, "apiType", "self");
        std::string apiConfig = jsonStr(ctx.body, "apiConfig", "{}");

        if (name.empty() || host.empty()) {
            return ctx.error("Name and host required");
        }
        // Password not required for remote_api mode
        if (connectionMode != "remote_api" && connectionMode != "external_api" && password.empty()) {
            return ctx.error("RCON password required for this connection mode");
        }

        std::string apiKey = "srv_" + randomToken(40);
        db.exec(
            "INSERT INTO servers (userId,name,host,rconPort,rconPassword,serverApiKey,"
            "connectionMode,logPath,remoteApiUrl,remoteApiToken,apiType,apiConfig) "
            "VALUES(?,?,?,?,?,?,?,?,?,?,?,?)",
            {ctx.userId, name, host, std::to_string(rconPort), password, apiKey,
             connectionMode, logPath, remoteApiUrl, remoteApiToken, apiType, apiConfig});

        int64_t id = db.lastInsertId();

        // Auto-add creator as server owner
        db.exec(
            "INSERT OR IGNORE INTO server_members (serverId,userId,role) VALUES(?,?,'owner')",
            {std::to_string(id), ctx.userId});

        return ctx.json({{"id", id}, {"apiKey", apiKey}, {"message", "Server added"}});
    });

    // PUT /api/servers/:id — update server (owner/admin only)
    server.put(R"(/api/servers/(\d+))", [&](Context& ctx) {
        std::string serverId = ctx.param(1);
        std::string mRole = getServerRole(db, ctx.userId, serverId);
        if (mRole.empty() || (mRole != "owner" && mRole != "admin"))
            return ctx.error("Access denied", 403);

        auto existing = db.queryOne("SELECT rconPassword FROM servers WHERE id=?", {serverId});
        if (existing.empty()) return ctx.error("Server not found");

        std::string name     = jsonStr(ctx.body, "name");
        std::string host     = jsonStr(ctx.body, "host");
        int rconPort         = ctx.body.value("rconPort", 27015);
        std::string password = jsonStr(ctx.body, "rconPassword");
        if (password.empty()) password = existing["rconPassword"];
        std::string connectionMode = jsonStr(ctx.body, "connectionMode", "relay");
        std::string logPath  = jsonStr(ctx.body, "logPath");
        std::string remoteApiUrl = jsonStr(ctx.body, "remoteApiUrl");
        std::string remoteApiToken = jsonStr(ctx.body, "remoteApiToken");
        std::string apiType  = jsonStr(ctx.body, "apiType", "self");
        std::string apiConfig = jsonStr(ctx.body, "apiConfig", "{}");

        db.exec(
            "UPDATE servers SET name=?,host=?,rconPort=?,rconPassword=?,"
            "connectionMode=?,logPath=?,remoteApiUrl=?,remoteApiToken=?,apiType=?,apiConfig=?,"
            "updatedAt=datetime('now') WHERE id=?",
            {name, host, std::to_string(rconPort), password,
             connectionMode, logPath, remoteApiUrl, remoteApiToken, apiType, apiConfig, serverId});

        if (auto* pool = server.rconPool()) pool->remove(safeStoi(serverId));
        return ctx.json({{"message", "Server updated"}});
    });

    // PUT /api/servers/:id/notes
    server.put(R"(/api/servers/(\d+)/notes)", [&](Context& ctx) {
        std::string serverId = ctx.param(1);
        if (!hasServerAccess(db, ctx.userId, serverId))
            return ctx.error("Access denied", 403);
        std::string notes = jsonStr(ctx.body, "notes");
        db.exec("UPDATE servers SET notes=?,updatedAt=datetime('now') WHERE id=?", {notes, serverId});
        return ctx.json({{"message", "Notes updated"}});
    });

    // DELETE /api/servers/:id — owner only
    server.del(R"(/api/servers/(\d+))", [&](Context& ctx) {
        std::string serverId = ctx.param(1);
        if (getServerRole(db, ctx.userId, serverId) != "owner")
            return ctx.error("Only server owner can delete", 403);

        int sid = safeStoi(serverId);
        db.transaction([&]() {
            const char* tables[] = {
                "players","kills","bans","chat_logs","squad_claims",
                "points","point_logs","reserved_slots","tk_forgive",
                "cdk_codes","cdk_logs","op_logs","plugins",
                "player_events","revives","game_rounds","switch_locks",
                "server_members"
            };
            for (const char* t : tables)
                db.exec(std::string("DELETE FROM ") + t + " WHERE serverId=?", {std::to_string(sid)});
            db.exec("DELETE FROM servers WHERE id=?", {std::to_string(sid)});
        });

        if (auto* pool = server.rconPool()) pool->remove(sid);
        return ctx.json({{"message", "Server deleted"}});
    });

    // POST /api/servers/:id/test
    server.post(R"(/api/servers/(\d+)/test)", [&](Context& ctx) {
        std::string serverId = ctx.param(1);
        if (!hasServerAccess(db, ctx.userId, serverId))
            return ctx.error("Access denied", 403);
        auto* pool = server.rconPool();
        if (!pool) return ctx.error("RCON pool not initialized", 500);
        auto result = pool->test(safeStoi(serverId), Config::get().getInt("RCON_SEND_TIMEOUT", 5000));
        if (result.empty()) return ctx.error("RCON connection failed");
        return ctx.json({{"message", "Connection successful"}, {"response", result}});
    });

    // Server Members API

    // GET /api/servers/:id/members
    server.get(R"(/api/servers/(\d+)/members)", [&](Context& ctx) {
        std::string serverId = ctx.param(1);
        if (!hasServerAccess(db, ctx.userId, serverId))
            return ctx.error("Access denied", 403);

        auto rows = db.query(
            "SELECT sm.id,sm.userId,sm.role,sm.addedAt,u.username,u.steamId "
            "FROM server_members sm JOIN users u ON sm.userId=u.id "
            "WHERE sm.serverId=? ORDER BY CASE sm.role WHEN 'owner' THEN 0 WHEN 'admin' THEN 1 ELSE 2 END, sm.addedAt ASC",
            {serverId});

        nlohmann::json members = nlohmann::json::array();
        for (auto& r : rows) {
            members.push_back({
                {"id", safeStoll(r["id"])},
                {"userId", safeStoll(r["userId"])},
                {"username", r["username"]},
                {"steamId", r["steamId"]},
                {"role", r["role"]},
                {"addedAt", r["addedAt"]}
            });
        }
        return ctx.json({{"members", members}});
    });

    // POST /api/servers/:id/members — add user to server
    server.post(R"(/api/servers/(\d+)/members)", [&](Context& ctx) {
        std::string serverId = ctx.param(1);
        std::string mRole = getServerRole(db, ctx.userId, serverId);
        if (mRole != "owner" && mRole != "admin")
            return ctx.error("Only owner/admin can add members", 403);

        std::string username = jsonStr(ctx.body, "username");
        std::string memberRole = jsonStr(ctx.body, "role", "member");
        if (username.empty()) return ctx.error("username required");
        if (memberRole != "owner" && memberRole != "admin" && memberRole != "member")
            return ctx.error("role must be owner/admin/member");

        if ((memberRole == "owner" || memberRole == "admin") && mRole != "owner")
            return ctx.error("Only server owner can assign admin/owner roles", 403);

        auto user = db.queryOne("SELECT id FROM users WHERE username=? AND status='active'", {username});
        if (user.empty()) return ctx.error("User not found or not active", 404);

        auto existing = db.queryOne("SELECT id FROM server_members WHERE serverId=? AND userId=?",
                                     {serverId, user["id"]});
        if (!existing.empty()) return ctx.error("User is already a member");

        db.exec("INSERT INTO server_members (serverId,userId,role) VALUES(?,?,?)",
                {serverId, user["id"], memberRole});
        return ctx.json({{"message", "Member added"}, {"userId", safeStoll(user["id"])}});
    });

    // PUT /api/servers/:id/members/:userId — change member role (owner only)
    server.put(R"(/api/servers/(\d+)/members/(\d+))", [&](Context& ctx) {
        std::string serverId = ctx.param(1);
        std::string targetUserId = ctx.param(2);
        if (getServerRole(db, ctx.userId, serverId) != "owner")
            return ctx.error("Only server owner can change roles", 403);
        if (targetUserId == ctx.userId)
            return ctx.error("Cannot change own role");

        std::string newRole = jsonStr(ctx.body, "role");
        if (newRole != "owner" && newRole != "admin" && newRole != "member")
            return ctx.error("role must be owner/admin/member");

        auto existing = db.queryOne("SELECT id FROM server_members WHERE serverId=? AND userId=?",
                                     {serverId, targetUserId});
        if (existing.empty()) return ctx.error("Member not found", 404);

        db.exec("UPDATE server_members SET role=? WHERE serverId=? AND userId=?",
                {newRole, serverId, targetUserId});
        return ctx.json({{"message", "Role updated"}});
    });

    // DELETE /api/servers/:id/members/:userId — remove member or self-leave
    server.del(R"(/api/servers/(\d+)/members/(\d+))", [&](Context& ctx) {
        std::string serverId = ctx.param(1);
        std::string targetUserId = ctx.param(2);
        std::string mRole = getServerRole(db, ctx.userId, serverId);

        if (targetUserId == ctx.userId) {
            if (mRole == "owner") return ctx.error("Owner cannot leave their own server");
            db.exec("DELETE FROM server_members WHERE serverId=? AND userId=?", {serverId, targetUserId});
            return ctx.json({{"message", "Left server"}});
        }

        if (mRole != "owner" && mRole != "admin")
            return ctx.error("Only owner/admin can remove members", 403);
        auto tRole = getServerRole(db, targetUserId, serverId);
        if ((tRole == "owner" || tRole == "admin") && mRole != "owner")
            return ctx.error("Only owner can remove admin members", 403);

        db.exec("DELETE FROM server_members WHERE serverId=? AND userId=?", {serverId, targetUserId});
        return ctx.json({{"message", "Member removed"}});
    });
}

} // namespace sp
