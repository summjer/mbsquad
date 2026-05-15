#include "handlers/admin_handler.h"
#include "core/shared_utils.h"
#include "net/server.h"
#include "net/rcon.h"
#include "core/database.h"
#include "core/auth.h"
#include "core/core.h"
#include <algorithm>
#include <httplib.h>
#include <openssl/crypto.h>


namespace sp {

// Relay admin sync helper (v12: scoped to specific server)
static void relayAdminSync(Database& db, const std::string& steamId,
                           const std::string& action, const std::string& level = "",
                           const std::string& serverId = "", RconPool* rconPool = nullptr) {
    // If serverId specified, sync only that server; otherwise sync all
    std::vector<std::unordered_map<std::string, std::string>> servers;
    if (!serverId.empty()) {
        servers = db.query("SELECT id,relayUrl FROM servers WHERE id=? AND relayUrl IS NOT NULL AND relayUrl != ''", {serverId});
    } else {
        servers = db.query("SELECT id,relayUrl FROM servers WHERE relayUrl IS NOT NULL AND relayUrl != ''", {});
    }
    for (auto& srv : servers) {
        auto ep = parseRelayUrl(srv["relayUrl"]);
        httplib::Client cli(ep.host.c_str(), ep.port);
        cli.set_connection_timeout(Config::get().getInt("RELAY_CONNECT_TIMEOUT", 3));
        cli.set_read_timeout(Config::get().getInt("RELAY_READ_TIMEOUT", 3));
        nlohmann::json body = {{"steamId", steamId}, {"action", action}};
        if (!level.empty()) body["level"] = level;
        auto res = cli.Post("/api/admin-sync", body.dump(), "application/json");
        if (res) {
            LOG_I("AdminSync", "Relay sync " + action + " for " + steamId + " -> " + std::to_string(res->status));
        } else {
            LOG_W("AdminSync", "Relay sync failed for " + steamId + " (relay unreachable)");
        }
    }

    // RCON: apply admin change immediately (don't wait for map change)
    if (rconPool && !steamId.empty()) {
        try {
            int sid = safeStoi(serverId);
            if (action == "add") {
                // Map role to Squad admin group
                std::string grp = (level == "server_owner") ? "Admin" : "Moderator";
                std::string cmd = "AdminAdd \"" + rconSafe(steamId) + "\" " + rconSafe(grp);
                std::string r = rconPool->send(sid, cmd);
                LOG_I("AdminSync", "RCON AdminAdd " + steamId + " " + grp + " -> " + r);
            } else if (action == "remove") {
                std::string cmd = "AdminRemove \"" + rconSafe(steamId) + "\"";
                std::string r = rconPool->send(sid, cmd);
                LOG_I("AdminSync", "RCON AdminRemove " + steamId + " -> " + r);
            }
        } catch (const std::exception& e) {
            LOG_W("AdminSync", "RCON failed for " + steamId + ": " + e.what());
        }
    }
}



// All permission keys and their labels
static const std::unordered_map<std::string, std::string> ALL_PERMISSIONS = {
    {"players",        "玩家管理"},
    {"kick",           "踢人"},
    {"ban",            "封禁管理"},
    {"reserved",       "预留位管理"},
    {"tk",             "TK 管理"},
    {"points",         "积分管理"},
    {"quick_commands", "快捷命令"},
    {"rcon",           "RCON 终端"},
    {"settings",       "服务器设置"},
    {"user_admin",     "用户管理"},
    {"plugins",        "插件管理"},
};

// Default permissions for new OP users
static const std::vector<std::string> DEFAULT_OP_PERMISSIONS = {
    "players", "kick", "ban", "reserved", "tk", "points", "quick_commands",
};

// Helper: check if user is server_owner
static bool isOwner(Context& ctx) {
    return ctx.role == "server_owner";
}

// Helper: get user permissions as JSON
static nlohmann::json getUserPermissionsJson(Database& db, int userId, const std::string& role) {
    nlohmann::json perms;
    if (role == "server_owner") {
        for (auto& [k, _] : ALL_PERMISSIONS) perms[k] = true;
        return perms;
    }

    // Start with defaults
    for (auto& [k, _] : ALL_PERMISSIONS) perms[k] = false;
    for (auto& k : DEFAULT_OP_PERMISSIONS) perms[k] = true;

    // Override from DB
    auto rows = db.query(
        "SELECT permission, granted FROM user_permissions WHERE userId=?",
        {std::to_string(userId)});
    for (auto& r : rows) {
        perms[r["permission"]] = (r["granted"] == "1");
    }
    return perms;
}

void registerAdminRoutes(Server& server) {
    auto& db = server.db();

    // GET /api/admin/users
    server.get("/api/admin/users", [&](Context& ctx) {
        if (!isOwner(ctx)) return ctx.error("Admin only", 403);

        std::string status = ctx.query("status");
        std::vector<std::unordered_map<std::string, std::string>> rows;
        if (!status.empty()) {
            rows = db.query(
                "SELECT id,username,role,status,createdAt,steamId,lastLogin,rejectReason "
                "FROM users WHERE status=? ORDER BY id DESC", {status});
        } else {
            rows = db.query(
                "SELECT id,username,role,status,createdAt,steamId,lastLogin,rejectReason "
                "FROM users ORDER BY id DESC", {});
        }

        nlohmann::json users = nlohmann::json::array();
        for (auto& r : rows) {
            nlohmann::json u;
            for (auto& [k, v] : r) u[k] = v;
            users.push_back(u);
        }
        return ctx.json({{"users", users}});
    });

    // POST /api/admin/users/add
    server.post("/api/admin/users/add", [&](Context& ctx) {
        if (!isOwner(ctx)) return ctx.error("Admin only", 403);

        std::string username = jsonStr(ctx.body, "username");
        std::string password = jsonStr(ctx.body, "password");
        std::string role     = jsonStr(ctx.body, "role", "op");
        std::string steamId  = jsonStr(ctx.body, "steamId");

        if (username.length() < 3 || username.length() > 32)
            return ctx.error("Username must be 3-32 chars");
        if (password.length() < 6)
            return ctx.error("Password min 6 chars");

        if (role != "server_owner" && role != "op") role = "op";
        if ((role == "op" || role == "server_owner") && steamId.empty())
            return ctx.error("OP/owner accounts require Steam ID");

        // Check duplicate
        auto existing = db.queryOne("SELECT id FROM users WHERE username=?", {username});
        if (!existing.empty()) return ctx.error("Username taken");

        std::string hash = Auth::hashPassword(password);
        db.exec(
            "INSERT INTO users (username,passwordHash,role,status,steamId) VALUES(?,?,?,?,?)",
            {username, hash, role, "active", steamId});

        int64_t newId = db.lastInsertId();

        // Insert default permissions for OP
        if (role == "op") {
            // Connection verified by successful query above
            for (auto& p : DEFAULT_OP_PERMISSIONS) {
                db.exec(
                    "INSERT OR IGNORE INTO user_permissions (userId,permission,granted) VALUES(?,?,1)",
                    {std::to_string(newId), p});
            }
        }

        return ctx.json({
            {"message", "User created"},
            {"id", newId},
            {"username", username},
            {"role", role}
        });
    });

    // POST /api/admin/users/:id/approve
    server.post(R"(/api/admin/users/(\d+)/approve)", [&](Context& ctx) {
        if (!isOwner(ctx)) return ctx.error("Admin only", 403);
        std::string userId = ctx.param(1);
        db.exec("UPDATE users SET status='active' WHERE id=? AND status='pending'", {userId});
        return ctx.json({{"message", "Approved"}});
    });

    // POST /api/admin/users/:id/reject
    server.post(R"(/api/admin/users/(\d+)/reject)", [&](Context& ctx) {
        if (!isOwner(ctx)) return ctx.error("Admin only", 403);
        std::string userId = ctx.param(1);
        std::string reason = jsonStr(ctx.body, "reason");
        db.exec("UPDATE users SET status='rejected',rejectReason=? WHERE id=?",
                {reason, userId});
        return ctx.json({{"message", "Rejected"}});
    });

    // DELETE /api/admin/users/:id
    server.del(R"(/api/admin/users/(\d+))", [&](Context& ctx) {
        if (!isOwner(ctx)) return ctx.error("Admin only", 403);
        std::string userId = ctx.param(1);
        if (userId == ctx.userId) return ctx.error("Cannot delete self");

        // Get steamId for relay sync
        auto user = db.queryOne("SELECT steamId FROM users WHERE id=?", {userId});

        db.exec("DELETE FROM sessions WHERE userId=?", {userId});
        db.exec("DELETE FROM user_permissions WHERE userId=?", {userId});
        db.exec("DELETE FROM server_members WHERE userId=?", {userId});
        db.exec("DELETE FROM users WHERE id=?", {userId});

        if (!user.empty() && !user["steamId"].empty()) { relayAdminSync(db, user["steamId"], "remove", "", "", server.rconPool()); }

        return ctx.json({{"message", "Deleted"}});
    });

    // PUT /api/admin/users/:id/role
    server.put(R"(/api/admin/users/(\d+)/role)", [&](Context& ctx) {
        if (!isOwner(ctx)) return ctx.error("Admin only", 403);
        std::string userId = ctx.param(1);
        if (userId == ctx.userId) return ctx.error("Cannot change own role");

        std::string role = jsonStr(ctx.body, "role");
        if (role != "server_owner" && role != "op")
            return ctx.error("Invalid role");

        // Get steamId for relay sync
        auto user = db.queryOne("SELECT steamId FROM users WHERE id=?", {userId});

        db.exec("UPDATE users SET role=? WHERE id=?", {role, userId});

        // Update permissions
        if (role == "server_owner") {
            db.exec("DELETE FROM user_permissions WHERE userId=?", {userId});
        } else if (role == "op") {
            db.exec("DELETE FROM user_permissions WHERE userId=?", {userId});
            for (auto& p : DEFAULT_OP_PERMISSIONS) {
                db.exec(
                    "INSERT OR IGNORE INTO user_permissions (userId,permission,granted) VALUES(?,?,1)",
                    {userId, p});
            }
        }

        if (!user.empty() && !user["steamId"].empty()) { relayAdminSync(db, user["steamId"], "add", role, "", server.rconPool()); }

        return ctx.json({{"message", "Role updated"}});
    });

    // GET /api/admin/users/:id/permissions
    server.get(R"(/api/admin/users/(\d+)/permissions)", [&](Context& ctx) {
        if (!isOwner(ctx)) return ctx.error("Admin only", 403);
        std::string userId = ctx.param(1);

        auto user = db.queryOne("SELECT role FROM users WHERE id=?", {userId});
        if (user.empty()) return ctx.error("User not found", 404);

        auto perms = getUserPermissionsJson(db, safeStoi(userId), user["role"]);
        return ctx.json({{"permissions", perms}});
    });

    // PUT /api/admin/users/:id/permissions
    server.put(R"(/api/admin/users/(\d+)/permissions)", [&](Context& ctx) {
        if (!isOwner(ctx)) return ctx.error("Admin only", 403);
        std::string userId = ctx.param(1);
        if (userId == ctx.userId) return ctx.error("Cannot modify own permissions");

        auto user = db.queryOne("SELECT role FROM users WHERE id=?", {userId});
        if (user.empty()) return ctx.error("User not found", 404);
        if (user["role"] == "server_owner")
            return ctx.error("server_owner always has full permissions");

        if (!ctx.body.contains("permissions") || !ctx.body["permissions"].is_object())
            return ctx.error("permissions object required");

        for (auto& [key, val] : ctx.body["permissions"].items()) {
            if (ALL_PERMISSIONS.find(key) == ALL_PERMISSIONS.end()) continue;
            int granted = val.is_boolean() ? (val.get<bool>() ? 1 : 0) : 0;
            db.exec(
                "INSERT OR REPLACE INTO user_permissions (userId,permission,granted) VALUES(?,?,?)",
                {userId, key, std::to_string(granted)});
        }

        return ctx.json({{"message", "Permissions updated"}});
    });

    // POST /api/admin/users/:id/reset-password
    server.post(R"(/api/admin/users/(\d+)/reset-password)", [&](Context& ctx) {
        if (!isOwner(ctx)) return ctx.error("Admin only", 403);
        std::string userId = ctx.param(1);
        std::string newPassword = jsonStr(ctx.body, "password");
        if (newPassword.length() < 6) return ctx.error("Password min 6 chars");

        std::string hash = Auth::hashPassword(newPassword);
        db.exec("UPDATE users SET passwordHash=? WHERE id=?", {hash, userId});

        return ctx.json({{"message", "Password reset"}});
    });
}

} // namespace sp
