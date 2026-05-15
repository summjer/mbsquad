#include "handlers/query_handler.h"
#include "core/shared_utils.h"
#include "net/server.h"
#include "net/rcon.h"
#include "core/database.h"
#include "core/auth.h"
#include "core/core.h"
#include <algorithm>
#include <set>

namespace sp {

// Table name whitelist to prevent SQL injection
static bool isValidTable(const std::string& table) {
    static const std::set<std::string> validTables = {
        "revives", "player_events", "game_rounds", "squad_claims",
        "kills", "bans", "chat_logs", "points", "point_logs",
        "players", "tk_forgive", "server_ticks", "reserved_slots",
        "switch_locks", "server_presets", "op_logs", "cdk_logs",
        "plugins", "plugin_sources", "settings", "sessions",
        "servers", "server_members", "users", "user_permissions"
    };
    return validTables.count(table) > 0;
}

// Helper: build paginated query
static void paginatedQuery(Database& db, Context& ctx,
    const std::string& table, const std::string& extraWhere,
    const std::vector<std::string>& extraParams,
    const std::vector<std::string>& keywordFields) {

    if (!isValidTable(table)) {
        return ctx.error("Invalid table name", 400);
    }

    int page = std::stoi(ctx.query("page", "1"));
    int pageSize = std::min(100, std::stoi(ctx.query("page_size", "50")));
    if (page < 1) page = 1;
    if (pageSize < 1) pageSize = 50;
    int offset = (page - 1) * pageSize;

    std::string sid = ctx.query("serverId");
    std::string keyword = ctx.query("keyword");
    std::string type = ctx.query("type");

    std::string where = "1=1";
    std::vector<std::string> params;

    if (!sid.empty() && sid != "all") {
        where += " AND serverId=?";
        params.push_back(sid);
    }
    if (!type.empty() && (type == "join" || type == "leave")) {
        where += " AND eventType=?";
        params.push_back(type);
    }
    if (!extraWhere.empty()) {
        where += " " + extraWhere;
    }
    for (auto& p : extraParams) params.push_back(p);
    if (!keyword.empty()) {
        for (size_t i = 0; i < keywordFields.size(); i++) {
            where += (i == 0 ? " AND (" : " OR ") + keywordFields[i] + " LIKE ?";
            params.push_back("%" + keyword + "%");
        }
        if (!keywordFields.empty()) where += ")";
    }

    auto countRow = db.queryOne(
        "SELECT COUNT(*) as c FROM " + table + " WHERE " + where, params);
    int total = std::stoi(countRow["c"]);

    std::string dataSql = "SELECT * FROM " + table + " WHERE " + where
        + " ORDER BY id DESC LIMIT ? OFFSET ?";
    params.push_back(std::to_string(pageSize));
    params.push_back(std::to_string(offset));

    auto rows = db.query(dataSql, params);
    nlohmann::json data = nlohmann::json::array();
    for (auto& r : rows) {
        nlohmann::json item;
        for (auto& [k, v] : r) item[k] = v;
        data.push_back(item);
    }

    return ctx.json({
        {"data", data},
        {"page", page},
        {"total", total},
        {"total_pages", (total + pageSize - 1) / pageSize}
    });
}

// rowsToJson is now in core/shared_utils.h

void registerQueryRoutes(Server& server) {
    auto& db = server.db();

    // Revives
    server.get("/api/revives", [&](Context& ctx) {
        paginatedQuery(db, ctx, "revives", "",
            {}, {"reviverName", "reviverSteamId", "revivedName", "revivedSteamId"});
    });

    // Player Events
    server.get("/api/player-events", [&](Context& ctx) {
        paginatedQuery(db, ctx, "player_events", "",
            {}, {"playerName", "steamId"});
    });

    // Game Rounds
    server.get("/api/game-rounds", [&](Context& ctx) {
        paginatedQuery(db, ctx, "game_rounds", "",
            {}, {"map"});
    });

    // Squad Events
    server.get("/api/squad-events", [&](Context& ctx) {
        paginatedQuery(db, ctx, "squad_claims", "",
            {}, {"creatorName", "creatorSteamId", "squadName"});
    });

    // POST /api/events/squad (public, for relay push)
    server.post("/api/events/squad", [&](Context& ctx) {
        std::string squadName      = jsonStr(ctx.body, "squadName");
        std::string creatorName    = jsonStr(ctx.body, "creatorName");
        std::string creatorSteamId = jsonStr(ctx.body, "creatorSteamId");
        std::string teamIndex      = jsonStr(ctx.body, "teamIndex");
        std::string squadId        = jsonStr(ctx.body, "squadId");
        std::string serverId       = jsonStr(ctx.body, "serverId");

        if (squadName.empty() || creatorName.empty())
            return ctx.error("squadName and creatorName required");

        db.exec(
            "INSERT INTO squad_claims (serverId,teamId,squadId,squadName,creatorSteamId,creatorName,createdAt) "
            "VALUES(?,?,?,?,?,?,datetime('now'))",
            {serverId, teamIndex, squadId, squadName, creatorSteamId, creatorName});

        return ctx.json({{"success", true}, {"id", db.lastInsertId()}});
    });

    // DELETE /api/squad-events/:id
    server.del(R"(/api/squad-events/(\d+))", [&](Context& ctx) {
        std::string id = ctx.param(1);
        db.exec("DELETE FROM squad_claims WHERE id=?", {id});
        return ctx.json({{"success", true}});
    });

    // Claims
    server.get("/api/claims", [&](Context& ctx) {
        std::string sid = ctx.query("serverId");
        if (sid.empty()) return ctx.error("serverId required");

        auto rows = db.query(
            "SELECT * FROM squad_claims WHERE serverId=? ORDER BY createdAt ASC", {sid});

        nlohmann::json byTeam;
        for (auto& r : rows) {
            std::string team = r["teamId"].empty() ? "unknown" : r["teamId"];
            if (!byTeam.contains(team)) byTeam[team] = nlohmann::json::array();
            nlohmann::json item;
            for (auto& [k, v] : r) item[k] = v;
            byTeam[team].push_back(item);
        }
        return ctx.json({{"claims", rowsToJson(rows)}, {"byTeam", byTeam}});
    });

    // POST /api/claims/clear
    server.post("/api/claims/clear", [&](Context& ctx) {
        std::string sid = jsonStr(ctx.body, "serverId");
        if (sid.empty()) return ctx.error("serverId required");
        db.exec("DELETE FROM squad_claims WHERE serverId=?", {sid});
        return ctx.json({{"success", true}});
    });

    // Switch Lock
    server.get(R"(/api/switch-lock/(\d+))", [&](Context& ctx) {
        std::string sid = ctx.param(1);
        auto rows = db.query(
            "SELECT * FROM switch_locks WHERE serverId=? ORDER BY lockedUntil ASC", {sid});
        return ctx.json({{"locks", rowsToJson(rows)}});
    });

    server.post(R"(/api/switch-lock/unlock/(\d+))", [&](Context& ctx) {
        std::string sid = ctx.param(1);
        std::string steamId = jsonStr(ctx.body, "steamId");

        if (!steamId.empty()) {
            db.exec("DELETE FROM switch_locks WHERE serverId=? AND steamId=?", {sid, steamId});
        } else {
            db.exec("DELETE FROM switch_locks WHERE serverId=?", {sid});
        }
        return ctx.json({{"success", true}});
    });

    // Presets
    server.get("/api/presets", [&](Context& ctx) {
        auto rows = db.query(
            "SELECT id, name, config, createdAt FROM server_presets WHERE userId=? ORDER BY id DESC",
            {ctx.userId});
        nlohmann::json presets = nlohmann::json::array();
        for (auto& r : rows) {
            nlohmann::json p;
            for (auto& [k, v] : r) {
                if (k == "config") {
                    try { p[k] = nlohmann::json::parse(v); }
                    catch (...) { p[k] = v; }
                } else {
                    p[k] = v;
                }
            }
            presets.push_back(p);
        }
        return ctx.json({{"presets", presets}});
    });

    server.post("/api/presets", [&](Context& ctx) {
        std::string name = jsonStr(ctx.body, "name");
        std::string config = ctx.body.contains("config") ? ctx.body["config"].dump() : "{}";
        if (name.empty()) return ctx.error("name required");

        db.exec("INSERT INTO server_presets (userId,name,config) VALUES(?,?,?)",
                {ctx.userId, name, config});
        return ctx.json({{"id", db.lastInsertId()}, {"message", "Preset created"}});
    });

    server.put(R"(/api/presets/(\d+))", [&](Context& ctx) {
        std::string id = ctx.param(1);
        auto existing = db.queryOne(
            "SELECT id FROM server_presets WHERE id=? AND userId=?", {id, ctx.userId});
        if (existing.empty()) return ctx.error("Not found", 404);

        std::string name = jsonStr(ctx.body, "name");
        std::string config = ctx.body.contains("config") ? ctx.body["config"].dump() : "{}";
        if (name.empty()) return ctx.error("name required");

        db.exec("UPDATE server_presets SET name=?,config=? WHERE id=?",
                {name, config, id});
        return ctx.json({{"message", "Preset updated"}});
    });

    server.del(R"(/api/presets/(\d+))", [&](Context& ctx) {
        std::string id = ctx.param(1);
        auto existing = db.queryOne(
            "SELECT id FROM server_presets WHERE id=? AND userId=?", {id, ctx.userId});
        if (existing.empty()) return ctx.error("Not found", 404);

        db.exec("DELETE FROM server_presets WHERE id=?", {id});
        return ctx.json({{"message", "Preset deleted"}});
    });

    // Relay Auth (v12: per-server apiKey binding)
    server.post("/api/relay/auth", [&](Context& ctx) {
        std::string apiKey = jsonStr(ctx.body, "apiKey");

        if (apiKey.empty()) return ctx.error("apiKey required");

        auto srv = db.queryOne(
            "SELECT id,name,host,rconPort,rconPassword,serverApiKey FROM servers WHERE serverApiKey=?",
            {apiKey});
        if (srv.empty()) return ctx.error("Invalid apiKey — server not found", 401);

        auto relayToken = db.queryOne(
            "SELECT value FROM settings WHERE key='relay_token'", {});
        if (relayToken.empty()) return ctx.error("Relay token not initialized", 500);

        // Auto-update relayUrl if provided
        std::string relayUrl = jsonStr(ctx.body, "relayUrl");
        if (!relayUrl.empty() && relayUrl.substr(0, 4) == "http") {
            db.exec("UPDATE servers SET relayUrl=? WHERE id=?", {relayUrl, srv["id"]});
        }

        // Store relay version if provided
        std::string relayVersion = jsonStr(ctx.body, "version");
        if (!relayVersion.empty()) {
            db.exec("INSERT OR REPLACE INTO settings (key, value) VALUES('relay_version', ?)", {relayVersion});
        }

        std::string token = relayToken["value"];
        try { token = nlohmann::json::parse(token).get<std::string>(); }
        catch (...) {}

        return ctx.json({
            {"token", token},
            {"serverId", std::stoll(srv["id"])},
            {"serverName", srv["name"]},
            {"rcon", {
                {"host", srv["host"]},
                {"port", srv["rconPort"].empty() ? 27015 : std::stoi(srv["rconPort"])},
                {"password", srv["rconPassword"]}
            }}
        });
    }, false);

    // GET /api/relay/version
    server.get("/api/relay/version", [&](Context& ctx) {
        // Return stored relay version (updated by relay on auth) or default
        auto row = db.queryOne("SELECT value FROM settings WHERE key='relay_version'", {});
        std::string ver = row.empty() ? "1.0-cpp" : row["value"];
        return ctx.json({{"version", ver}});
    }, false);

    // GET /api/relay/register-status
    server.get("/api/relay/register-status", [&](Context& ctx) {
        auto row = db.queryOne("SELECT value FROM settings WHERE key='relay_register_code'", {});
        return ctx.json({
            {"code", row.empty() ? nullptr : row["value"]},
            {"used", row.empty()}
        });
    }, false);

    // Plugin Checks
    server.get("/api/plugin-checks", [&](Context& ctx) {
        if (ctx.role != "server_owner") return ctx.error("Admin only", 403);

        nlohmann::json checks = nlohmann::json::array();

        // kill-points
        try {
            auto kc = db.queryOne("SELECT COUNT(*) as c FROM kills", {});
            auto pl = db.queryOne(
                "SELECT COUNT(*) as c FROM point_logs WHERE reason LIKE '%击杀%' OR reason LIKE '%阵亡%'",
                {});
            checks.push_back({
                {"plugin", "kill-points"},
                {"status", std::stoi(kc["c"]) > 0 ? (std::stoi(pl["c"]) > 0 ? "ok" : "data_partial") : "no_data"},
                {"details", {{"killRecords", std::stoi(kc["c"])}, {"pointLogs", std::stoi(pl["c"])}}}
            });
        } catch (...) {}

        // chat-commands
        try {
            auto cc = db.queryOne("SELECT COUNT(*) as c FROM chat_logs", {});
            auto sc = db.queryOne("SELECT value FROM settings WHERE key='sign_in_cooldown'", {});
            checks.push_back({
                {"plugin", "chat-commands"},
                {"status", !sc.empty() ? "ok" : "config_missing"},
                {"details", {{"chatRecords", std::stoi(cc["c"])}, {"signInConfigured", !sc.empty()}}}
            });
        } catch (...) {}

        // revive-points
        try {
            auto rl = db.queryOne(
                "SELECT COUNT(*) as c FROM point_logs WHERE reason LIKE '%救援%'", {});
            checks.push_back({
                {"plugin", "revive-points"},
                {"status", std::stoi(rl["c"]) > 0 ? "ok" : "no_data"},
                {"details", {{"revivePointLogs", std::stoi(rl["c"])}}}
            });
        } catch (...) {}

        // tk-forgive
        try {
            auto tc = db.queryOne("SELECT COUNT(*) as c FROM tk_forgive", {});
            auto tk = db.queryOne("SELECT value FROM settings WHERE key='tk_forgive_enabled'", {});
            checks.push_back({
                {"plugin", "tk-forgive"},
                {"status", !tk.empty() ? "ok" : "config_missing"},
                {"details", {{"tkRecords", std::stoi(tc["c"])}, {"enabled", !tk.empty()}}}
            });
        } catch (...) {}

        // player-sync
        try {
            auto pc = db.queryOne("SELECT COUNT(*) as c FROM players", {});
            checks.push_back({
                {"plugin", "player-sync"},
                {"status", std::stoi(pc["c"]) > 0 ? "ok" : "no_data"},
                {"details", {{"playerRecords", std::stoi(pc["c"])}}}
            });
        } catch (...) {}

        return ctx.json({{"checks", checks}});
    });

    // Player Database
    server.get("/api/player-database", [&](Context& ctx) {
        int page = std::stoi(ctx.query("page", "1"));
        int pageSize = std::min(100, std::stoi(ctx.query("page_size", "20")));
        int offset = (page - 1) * pageSize;
        std::string keyword = ctx.query("keyword");

        std::string where = "1=1";
        std::vector<std::string> params;
        if (!keyword.empty()) {
            where += " AND (p.name LIKE ? OR p.steamId LIKE ?)";
            params.push_back("%" + keyword + "%");
            params.push_back("%" + keyword + "%");
        }

        auto countRow = db.queryOne(
            "SELECT COUNT(*) as c FROM (" 
            "SELECT steamId FROM players GROUP BY steamId"
            ") sub WHERE " + where, params);
        int total = std::stoi(countRow["c"]);

        std::string dataSql = 
            "SELECT p.steamId, "
            "  (SELECT name FROM players p2 WHERE p2.steamId=p.steamId ORDER BY lastSeen DESC LIMIT 1) as name, "
            "  MIN(p.firstSeen) as firstSeen, MAX(p.lastSeen) as lastSeen, "
            "  COALESCE(pt.totalBalance,0) as totalBalance, "
            "  COALESCE(kl.totalKills,0) as totalKills "
            "FROM players p "
            "LEFT JOIN (SELECT steamId,SUM(balance) as totalBalance FROM points GROUP BY steamId) pt ON p.steamId=pt.steamId "
            "LEFT JOIN (SELECT killer as steamId,COUNT(*) as totalKills FROM kills GROUP BY killer) kl ON p.steamId=kl.steamId "
            "GROUP BY p.steamId "
            "ORDER BY lastSeen DESC LIMIT ? OFFSET ?";

        params.push_back(std::to_string(pageSize));
        params.push_back(std::to_string(offset));

        auto rows = db.query(dataSql, params);
        nlohmann::json data = nlohmann::json::array();
        for (auto& r : rows) {
            nlohmann::json item;
            for (auto& [k, v] : r) item[k] = v;
            data.push_back(item);
        }

        return ctx.json({
            {"data", data},
            {"page", page},
            {"total", total},
            {"total_pages", (total + pageSize - 1) / pageSize}
        });
    });
}

} // namespace sp
