// team_handler.cpp
#include "handlers/team_handler.h"
#include "net/server.h"
#include "core/database.h"
#include "core/core.h"

#include <openssl/rand.h>

namespace sp {

static std::string randomCode(int len) {
    static const char chars[] = "ABCDEFGHJKLMNPQRSTUVWXYZ23456789";
    std::string out;
    out.reserve(len);
    unsigned char buf[64];
    RAND_bytes(buf, len);
    for (int i = 0; i < len; i++) {
        out += chars[buf[i] % (sizeof(chars) - 1)];
    }
    return out;
}

void registerTeamRoutes(Server& server) {
    auto& db = server.db();

    server.post("/api/teams", [&](Context& ctx) {
        std::string name = jsonStr(ctx.body, "name");
        if (name.empty() || name.size() > 50)
            return ctx.error("Team name required (max 50 chars)");
        auto user = db.queryOne("SELECT teamId FROM users WHERE id=?", {ctx.userId});
        if (!user.empty() && user.count("teamId") && !user["teamId"].empty() && user["teamId"] != "NULL")
            return ctx.error("You already belong to a team");
        db.exec("INSERT INTO teams (name, ownerId) VALUES (?, ?)", {name, ctx.userId});
        int64_t teamId = db.lastInsertId();
        db.exec("UPDATE users SET teamId=? WHERE id=?", {std::to_string(teamId), ctx.userId});
        return ctx.json({{"message", "Team created"}, {"teamId", teamId}, {"name", name}});
    });

    server.get("/api/teams", [&](Context& ctx) {
        auto user = db.queryOne("SELECT teamId FROM users WHERE id=?", {ctx.userId});
        if (user.empty() || user["teamId"].empty() || user["teamId"] == "NULL")
            return ctx.json({{"team", nullptr}});
        std::string teamId = user["teamId"];
        auto team = db.queryOne("SELECT * FROM teams WHERE id=?", {teamId});
        if (team.empty()) return ctx.json({{"team", nullptr}});
        auto members = db.query("SELECT id,username,role,steamId,createdAt FROM users WHERE teamId=?", {teamId});
        nlohmann::json membersArr = nlohmann::json::array();
        for (auto& m : members) {
            membersArr.push_back({{"id", std::stoll(m["id"])}, {"username", m["username"]},
                {"role", m["role"]}, {"steamId", m.count("steamId") ? m["steamId"] : ""}});
        }
        auto codes = db.query("SELECT id,code,createdAt,expiresAt FROM team_invite_codes "
            "WHERE teamId=? AND usedBy IS NULL AND expiresAt > datetime('now') ORDER BY id DESC", {teamId});
        nlohmann::json codesArr = nlohmann::json::array();
        for (auto& c : codes) codesArr.push_back({{"id", std::stoll(c["id"])}, {"code", c["code"]}, {"expiresAt", c["expiresAt"]}});
        auto reqs = db.query("SELECT r.id,r.userId,r.createdAt,u.username FROM team_join_requests r "
            "JOIN users u ON r.userId=u.id WHERE r.teamId=? AND r.status='pending' ORDER BY r.id DESC", {teamId});
        nlohmann::json reqArr = nlohmann::json::array();
        for (auto& r : reqs) reqArr.push_back({{"id", std::stoll(r["id"])}, {"userId", std::stoll(r["userId"])}, {"username", r["username"]}, {"createdAt", r["createdAt"]}});
        bool isTeamOwner = (team["ownerId"] == ctx.userId);
        return ctx.json({{"team", {{"id", std::stoll(team["id"])}, {"name", team["name"]}, {"ownerId", std::stoll(team["ownerId"])}, {"isOwner", isTeamOwner}, {"members", membersArr}, {"inviteCodes", codesArr}, {"joinRequests", reqArr}}}});
    });

    server.post("/api/teams/invite-code", [&](Context& ctx) {
        auto user = db.queryOne("SELECT teamId FROM users WHERE id=?", {ctx.userId});
        if (user.empty() || user["teamId"].empty() || user["teamId"] == "NULL") return ctx.error("You don't belong to any team");
        auto team = db.queryOne("SELECT ownerId FROM teams WHERE id=?", {user["teamId"]});
        if (team.empty() || team["ownerId"] != ctx.userId) return ctx.error("Only team owner can generate invite codes");
        int hours = 24;
        if (ctx.body.contains("hours") && ctx.body["hours"].is_number()) { hours = ctx.body["hours"].get<int>(); if (hours < 1 || hours > 168) hours = 24; }
        std::string code = randomCode(8);
        db.exec("INSERT INTO team_invite_codes (teamId, code, createdBy, expiresAt) VALUES (?, ?, ?, datetime('now', '+' || ? || ' hours'))", {user["teamId"], code, ctx.userId, std::to_string(hours)});
        return ctx.json({{"code", code}, {"expiresIn", std::to_string(hours) + "h"}});
    });

    server.post("/api/teams/join-by-code", [&](Context& ctx) {
        std::string code = jsonStr(ctx.body, "code");
        if (code.empty()) return ctx.error("Invite code required");
        auto user = db.queryOne("SELECT teamId FROM users WHERE id=?", {ctx.userId});
        if (!user.empty() && user.count("teamId") && !user["teamId"].empty() && user["teamId"] != "NULL") return ctx.error("You already belong to a team. Leave first.");
        auto invite = db.queryOne("SELECT id,teamId FROM team_invite_codes WHERE code=? AND usedBy IS NULL AND expiresAt > datetime('now')", {code});
        if (invite.empty()) return ctx.error("Invalid or expired invite code");
        db.exec("UPDATE team_invite_codes SET usedBy=?, usedAt=datetime('now') WHERE id=?", {ctx.userId, invite["id"]});
        db.exec("UPDATE users SET teamId=? WHERE id=?", {invite["teamId"], ctx.userId});
        auto tname = db.queryOne("SELECT name FROM teams WHERE id=?", {invite["teamId"]});
        return ctx.json({{"message", "Joined team"}, {"teamName", tname["name"]}});
    });

    server.post(R"(/api/teams/(\d+)/join-request)", [&](Context& ctx) {
        std::string teamId = ctx.param(1);
        auto user = db.queryOne("SELECT teamId FROM users WHERE id=?", {ctx.userId});
        if (!user.empty() && user.count("teamId") && !user["teamId"].empty() && user["teamId"] != "NULL") return ctx.error("You already belong to a team");
        auto team = db.queryOne("SELECT id FROM teams WHERE id=?", {teamId});
        if (team.empty()) return ctx.error("Team not found");
        db.exec("INSERT OR REPLACE INTO team_join_requests (teamId, userId, status) VALUES (?, ?, 'pending')", {teamId, ctx.userId});
        return ctx.json({{"message", "Join request submitted"}});
    });

    server.get("/api/teams/available", [&](Context& ctx) {
        auto rows = db.query("SELECT t.id,t.name,u.username as ownerName,COUNT(m.id) as memberCount FROM teams t JOIN users u ON t.ownerId=u.id LEFT JOIN users m ON m.teamId=t.id GROUP BY t.id ORDER BY t.id DESC", {});
        nlohmann::json arr = nlohmann::json::array();
        for (auto& r : rows) arr.push_back({{"id", std::stoll(r["id"])}, {"name", r["name"]}, {"ownerName", r["ownerName"]}, {"memberCount", std::stoi(r["memberCount"])}});
        return ctx.json({{"teams", arr}});
    });

    server.post(R"(/api/teams/requests/(\d+)/approve)", [&](Context& ctx) {
        std::string reqId = ctx.param(1);
        auto user = db.queryOne("SELECT teamId FROM users WHERE id=?", {ctx.userId});
        if (user.empty() || user["teamId"].empty() || user["teamId"] == "NULL") return ctx.error("No team");
        auto team = db.queryOne("SELECT ownerId FROM teams WHERE id=?", {user["teamId"]});
        if (team.empty() || team["ownerId"] != ctx.userId) return ctx.error("Only team owner", 403);
        auto req = db.queryOne("SELECT userId,status FROM team_join_requests WHERE id=? AND teamId=?", {reqId, user["teamId"]});
        if (req.empty() || req["status"] != "pending") return ctx.error("Request not found or already resolved");
        auto target = db.queryOne("SELECT teamId FROM users WHERE id=?", {req["userId"]});
        if (!target.empty() && target.count("teamId") && !target["teamId"].empty() && target["teamId"] != "NULL") {
            db.exec("UPDATE team_join_requests SET status='rejected', resolvedAt=datetime('now') WHERE id=?", {reqId});
            return ctx.error("User already joined another team");
        }
        db.exec("UPDATE team_join_requests SET status='approved', resolvedAt=datetime('now') WHERE id=?", {reqId});
        db.exec("UPDATE users SET teamId=? WHERE id=?", {user["teamId"], req["userId"]});
        return ctx.json({{"message", "Approved"}});
    });

    server.post(R"(/api/teams/requests/(\d+)/reject)", [&](Context& ctx) {
        std::string reqId = ctx.param(1);
        auto user = db.queryOne("SELECT teamId FROM users WHERE id=?", {ctx.userId});
        if (user.empty() || user["teamId"].empty() || user["teamId"] == "NULL") return ctx.error("No team");
        auto team = db.queryOne("SELECT ownerId FROM teams WHERE id=?", {user["teamId"]});
        if (team.empty() || team["ownerId"] != ctx.userId) return ctx.error("Only team owner", 403);
        db.exec("UPDATE team_join_requests SET status='rejected', resolvedAt=datetime('now') WHERE id=? AND teamId=?", {reqId, user["teamId"]});
        return ctx.json({{"message", "Rejected"}});
    });

    server.del(R"(/api/teams/members/(\d+))", [&](Context& ctx) {
        std::string targetId = ctx.param(1);
        auto user = db.queryOne("SELECT teamId FROM users WHERE id=?", {ctx.userId});
        if (user.empty() || user["teamId"].empty() || user["teamId"] == "NULL") return ctx.error("No team");
        auto team = db.queryOne("SELECT ownerId FROM teams WHERE id=?", {user["teamId"]});
        if (team.empty()) return ctx.error("Team not found");
        bool isTeamOwner = (team["ownerId"] == ctx.userId);
        bool isSelf = (targetId == ctx.userId);
        if (!isTeamOwner && !isSelf) return ctx.error("Only team owner can remove members", 403);
        if (isTeamOwner && isSelf) return ctx.error("Team owner cannot leave");
        db.exec("UPDATE users SET teamId=NULL WHERE id=? AND teamId=?", {targetId, user["teamId"]});
        return ctx.json({{"message", isSelf ? "Left team" : "Member removed"}});
    });

    server.del(R"(/api/teams/invite-code/(\d+))", [&](Context& ctx) {
        std::string codeId = ctx.param(1);
        auto user = db.queryOne("SELECT teamId FROM users WHERE id=?", {ctx.userId});
        if (user.empty() || user["teamId"].empty() || user["teamId"] == "NULL") return ctx.error("No team");
        auto team = db.queryOne("SELECT ownerId FROM teams WHERE id=?", {user["teamId"]});
        if (team.empty() || team["ownerId"] != ctx.userId) return ctx.error("Only team owner", 403);
        db.exec("DELETE FROM team_invite_codes WHERE id=? AND teamId=?", {codeId, user["teamId"]});
        return ctx.json({{"message", "Revoked"}});
    });
}

} // namespace sp
