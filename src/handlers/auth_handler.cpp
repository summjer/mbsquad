#include "handlers/auth_handler.h"
#include "net/server.h"
#include "core/database.h"
#include "core/auth.h"
#include "core/core.h"
#include "core/shared_utils.h"
#include <algorithm>

namespace sp {

void registerAuthRoutes(Server& server) {
    auto& db = server.db();
    auto& auth = server.auth();

    // POST /api/auth/register
    server.post("/api/auth/register", [&](Context& ctx) {
        std::string username = jsonStr(ctx.body, "username");
        std::string password = jsonStr(ctx.body, "password");
        std::string steamId  = jsonStr(ctx.body, "steamId");

        if (username.empty() || password.empty()) {
            return ctx.error("username and password required");
        }
        if (username.size() < 3 || username.size() > 32) {
            return ctx.error("Username 3-32 chars");
        }
        if (password.size() < 6) {
            return ctx.error("Password min 6 chars");
        }

        // Check if username taken
        auto existing = db.queryOne("SELECT id FROM users WHERE username=?", {username});
        if (!existing.empty()) {
            return ctx.error("Username taken");
        }

        // SteamID validation (76561198xxxxxxxxx format, 17 digits)
        if (!steamId.empty()) {
            if (steamId.size() != 17 ||
                steamId.substr(0, 8) != "76561198" ||
                !std::all_of(steamId.begin(), steamId.end(), ::isdigit)) {
                return ctx.error("SteamID format error");
            }
        }

        // First user is server_owner, others are pending
        auto countStr = db.queryScalar("SELECT COUNT(*) FROM users");
        int count = safeStoi(countStr);
        std::string role   = (count == 0) ? "server_owner" : "pending";
        std::string status = (count == 0) ? "active" : "pending";

        std::string hash = Auth::hashPassword(password);
        db.exec("INSERT INTO users (username,passwordHash,role,status,steamId) VALUES(?,?,?,?,?)",
                {username, hash, role, status, steamId});
        int64_t uid = db.lastInsertId();

        if (status == "active") {
            // Insert default permissions for non-owner
            if (role != "server_owner") {
                auth.insertDefaultPermissions(static_cast<int>(uid));
            }

            std::string token = Auth::generateToken();
            std::string expires = /* now + 7 days */
                db.queryScalar("SELECT datetime('now','+7 days')");
            db.exec("INSERT INTO sessions (token,userId,expiresAt) VALUES(?,?,?)",
                    {token, std::to_string(uid), expires});

            nlohmann::json resp = {
                {"message", "Admin created"},
                {"token", token},
                {"user", {{"id", uid}, {"username", username}, {"role", role}}}
            };
            return ctx.json(resp);
        }

        return ctx.json({{"message", "Waiting for admin approval"}, {"status", "pending"}});
    }, false);  // No auth required for registration

    // POST /api/auth/login
    server.post("/api/auth/login", [&](Context& ctx) {
        auto ip = ctx.req.remote_addr;
        if (!auth.checkRateLimit(ip)) {
            return ctx.error("Too many attempts", 429);
        }

        std::string username = jsonStr(ctx.body, "username");
        std::string password = jsonStr(ctx.body, "password");
        if (username.empty() || password.empty()) {
            return ctx.error("username and password required");
        }

        auto user = db.queryOne("SELECT * FROM users WHERE username=?", {username});
        if (user.empty() || !Auth::verifyPassword(password, user["passwordHash"])) {
            return ctx.error("Invalid credentials", 401);
        }
        if (user["status"] == "pending") {
            return ctx.error("Account pending approval", 403);
        }
        if (user["status"] == "rejected") {
            return ctx.error("Account rejected", 403);
        }

        std::string token = Auth::generateToken();
        std::string expires = db.queryScalar("SELECT datetime('now','+7 days')");
        db.exec("INSERT INTO sessions (token,userId,expiresAt) VALUES(?,?,?)",
                {token, user["id"], expires});
        db.exec("UPDATE users SET lastLogin=datetime('now') WHERE id=?", {user["id"]});

        nlohmann::json resp = {
            {"token", token},
            {"user", {
                {"id", safeStoll(user["id"])},
                {"username", user["username"]},
                {"role", user["role"]}
            }}
        };
        return ctx.json(resp);
    }, false);  // No auth required for login

    // POST /api/auth/logout
    server.post("/api/auth/logout", [&](Context& ctx) {
        auto authHeader = ctx.req.get_header_value("Authorization");
        if (!authHeader.empty()) {
            std::string token = authHeader;
            if (token.size() > 7 && token.substr(0, 7) == "Bearer ") {
                token = token.substr(7);
            }
            db.exec("DELETE FROM sessions WHERE token=?", {token});
        }
        return ctx.json({{"message", "ok"}});
    });

    // GET /api/auth/me
    server.get("/api/auth/me", [&](Context& ctx) {
        auto permissions = auth.getUserPermissions(
            safeStoi(ctx.userId), ctx.role);

        nlohmann::json perms = nlohmann::json::object();
        for (auto& [k, v] : permissions.perms) {
            perms[k] = v;
        }

        nlohmann::json resp = {
            {"user", {
                {"id", safeStoll(ctx.userId)},
                {"username", ctx.username},
                {"role", ctx.role}
            }},
            {"permissions", perms}
        };
        return ctx.json(resp);
    });
}

} // namespace sp
