#include "net/server.h"
#include "core/shared_utils.h"
#include "net/rcon.h"
#include "core/database.h"
#include "core/core.h"
#include <filesystem>
#include <sys/wait.h>
#include <unistd.h>
#include <sstream>
#include <algorithm>
#include <unordered_map>
#include <fstream>
#include <set>


#include <httplib.h>

namespace sp {
static void syncReservedSlot(Database& db, int serverId, const std::string& steamId, 
                             const std::string& action, const std::string& playerName = "") {
    auto srv = db.queryOne("SELECT relayUrl FROM servers WHERE id=?", {std::to_string(serverId)});
    if (srv.empty() || srv["relayUrl"].empty()) return;
    auto ep = parseRelayUrl(srv["relayUrl"]);
    httplib::Client cli(ep.host.c_str(), ep.port);
    cli.set_connection_timeout(Config::get().getInt("RELAY_CONNECT_TIMEOUT", 3));
    cli.set_read_timeout(Config::get().getInt("RELAY_READ_TIMEOUT", 3));
    nlohmann::json body = {{"action", action}, {"steamId", steamId}};
    if (!playerName.empty()) body["playerName"] = playerName;
    auto res = cli.Post("/api/reserved-slots/sync", body.dump(), "application/json");
    if (res) {
        LOG_I("ReservedSync", action + " " + steamId + " -> " + std::to_string(res->status));
    } else {
        LOG_W("ReservedSync", "Relay sync failed for " + steamId);
    }
}


void registerMissingRoutes(Server& server) {
    auto& db = server.db();

    // GET /api/plugins/js â è¿åç¼è¯åç½®ç C++ æä»¶ä¿¡æ¯
    server.get("/api/plugins/js", [&](Context& ctx) {
        // Load settings for each plugin
        nlohmann::json settings = nlohmann::json::object();
        auto srows = db.query("SELECT key, value FROM settings", {});
        for (auto& r : srows) settings[r["key"]] = r["value"];

        nlohmann::json plugins = nlohmann::json::array();

        // ChatCommands
        plugins.push_back({
            {"name", "ChatCommands"}, {"label", "èå¤©å£ä»¤"},
            {"desc", "ç­¾å°/æ½å¥/æ¥æç»©/æ¥ç§¯å/åæ¢/è·³è¾¹"},
            {"version", "3.2"}, {"loaded", true},
            {"events", {"chat"}}
        });

        // KillPoints
        plugins.push_back({
            {"name", "KillPoints"}, {"label", "å»æç§¯å"},
            {"desc", "å»æå åãéµäº¡æ£åãTKæ£åãèªææ£å"},
            {"version", "1.5"}, {"loaded", true},
            {"events", {"kill"}}
        });

        // Welcome
        plugins.push_back({
            {"name", "Welcome"}, {"label", "è¿ææ¬¢è¿"},
            {"desc", "ç©å®¶å å¥æå¡å¨æ¶åéèªå®ä¹æ¬¢è¿è¯­"},
            {"version", "1.0"}, {"loaded", true},
            {"events", {"join"}}
        });

        // TKForgive
        plugins.push_back({
            {"name", "TKForgive"}, {"label", "TK éæ­"},
            {"desc", "TKåéåå¯è¾å¥å£ä»¤åè°ï¼ä¸æ£å"},
            {"version", "1.0"}, {"loaded", true},
            {"events", {"chat", "kill"}}
        });

        // CDKRedeem
        plugins.push_back({
            {"name", "CDKRedeem"}, {"label", "CDK åæ¢"},
            {"desc", "ç©å®¶å¨èå¤©ä¸­è¾å¥å£ä»¤åæ¢é¢çä½æè·³è¾¹"},
            {"version", "1.0"}, {"loaded", true},
            {"events", {"chat"}}
        });

        // TeamBalance
        plugins.push_back({
            {"name", "TeamBalance"}, {"label", "éä¼å¹³è¡¡"},
            {"desc", "éä¼äººæ°å·®è¿å¤§æ¶èªå¨æéæå¼ºå¶è·³è¾¹"},
            {"version", "1.0"}, {"loaded", true},
            {"events", {"playerlist"}}
        });

        // SwitchLock
        plugins.push_back({
            {"name", "SwitchLock"}, {"label", "é²è·³è¾¹éå®"},
            {"desc", "éå®éµè¥ï¼ç¦æ­¢ç©å®¶èªè¡è·³è¾¹"},
            {"version", "1.0"}, {"loaded", true},
            {"events", {"chat"}}
        });

        // TimedBroadcast
        plugins.push_back({
            {"name", "TimedBroadcast"}, {"label", "å®æ¶å¹¿æ­"},
            {"desc", "æé´éå¾ªç¯åéå¨æå¹¿æ­æ¶æ¯"},
            {"version", "1.0"}, {"loaded", true},
            {"events", {"tick"}}
        });

        // RevivePoints
        plugins.push_back({
            {"name", "RevivePoints"}, {"label", "ææ´ç§¯å"},
            {"desc", "ææ´éåè·å¾ç§¯åå¥å±"},
            {"version", "1.0"}, {"loaded", true},
            {"events", {"revive"}}
        });

        // PlayerSync
        plugins.push_back({
            {"name", "PlayerSync"}, {"label", "ç©å®¶åæ­¥"},
            {"desc", "èªå¨åæ­¥å¨çº¿ç©å®¶æ°æ®"},
            {"version", "1.0"}, {"loaded", true},
            {"events", {"playerlist"}}
        });

        return ctx.json({{"plugins", plugins}, {"settings", settings}});
    });

    // é¢çä½ç®¡ç
    // GET /api/reserved-slots
    server.get("/api/reserved-slots", [&](Context& ctx) {
        std::string sid = ctx.query("serverId");
        std::vector<std::unordered_map<std::string, std::string>> rows;
        if (!sid.empty()) {
            rows = db.query("SELECT * FROM reserved_slots WHERE serverId=? ORDER BY createdAt DESC", {sid});
        } else {
            rows = db.query("SELECT * FROM reserved_slots ORDER BY createdAt DESC", {});
        }
        nlohmann::json slots = nlohmann::json::array();
        for (auto& r : rows) {
            nlohmann::json s;
            for (auto& [k, v] : r) s[k] = v;
            slots.push_back(s);
        }
        return ctx.json({{"slots", slots}});
    });

    // POST /api/reserved-slots
    server.post("/api/reserved-slots", [&](Context& ctx) {
        int serverId = jsonInt(ctx.body, "serverId");
        std::string steamId = jsonStr(ctx.body, "steamId");
        if (!serverId || steamId.empty()) return ctx.error("serverId and steamId required");

        auto existing = db.queryOne("SELECT id FROM reserved_slots WHERE serverId=? AND steamId=?",
                                     {std::to_string(serverId), steamId});
        if (!existing.empty()) return ctx.error("Slot already exists");

        std::string playerName = jsonStr(ctx.body, "playerName");
        std::string addedBy = jsonStr(ctx.body, "addedBy", "panel");

        db.exec("INSERT INTO reserved_slots (serverId,steamId,playerName,addedBy) VALUES(?,?,?,?)",
                {std::to_string(serverId), steamId, playerName, addedBy});
        int64_t id = db.lastInsertId();
        syncReservedSlot(db, serverId, steamId, "add", playerName);
        return ctx.json({{"id", id}, {"message", "ok"}});
    });

    // DELETE /api/reserved-slots/:id
    server.del(R"(/api/reserved-slots/(\d+))", [&](Context& ctx) {
        std::string id = ctx.param(1);
        // Get slot info before deleting for relay sync
        auto slot = db.queryOne("SELECT serverId,steamId FROM reserved_slots WHERE id=?", {id});
        db.exec("DELETE FROM reserved_slots WHERE id=?", {id});
        if (!slot.empty()) {
            syncReservedSlot(db, safeStoi(slot["serverId"]), slot["steamId"], "remove");
        }
        return ctx.json({{"message", "Deleted"}});
    });

    // OP ç®¡ç
    // GET /api/op/list
    server.get("/api/op/list", [&](Context& ctx) {
        auto rows = db.query(
            "SELECT id,username,role,status,createdAt,steamId FROM users "
            "WHERE role='op' ORDER BY createdAt DESC", {});
        nlohmann::json result = nlohmann::json::array();
        for (auto& r : rows) {
            nlohmann::json u;
            for (auto& [k, v] : r) u[k] = v;
            bool online = false;
            if (!r["steamId"].empty()) {
                auto p = db.queryOne(
                    "SELECT 1 FROM players WHERE steamId=? AND lastSeen > datetime('now','-5 minutes') LIMIT 1",
                    {r["steamId"]});
                online = !p.empty();
            }
            u["online"] = online;
            result.push_back(u);
        }
        return ctx.json({{"data", result}});
    });

    // GET /api/op/stats
    server.get("/api/op/stats", [&](Context& ctx) {
        auto totalUsers = db.queryOne("SELECT COUNT(*) as c FROM users", {});
        auto totalOp = db.queryOne("SELECT COUNT(*) as c FROM users WHERE role='op'", {});
        auto totalSO = db.queryOne("SELECT COUNT(*) as c FROM users WHERE role='server_owner'", {});
        return ctx.json({
            {"totalUsers", safeStoi(totalUsers["c"])},
            {"totalOp", safeStoi(totalOp["c"])},
            {"totalServerOwner", safeStoi(totalSO["c"])}
        });
    });

    // GET /api/op/logs
    server.get("/api/op/logs", [&](Context& ctx) {
        std::string action = ctx.query("action");
        std::string limitStr = ctx.query("limit", "100");
        int limit = 100;
        limit = safeStoi(limitStr);

        std::vector<std::unordered_map<std::string, std::string>> rows;
        if (!action.empty() && action != "all") {
            rows = db.query("SELECT * FROM op_logs WHERE action=? ORDER BY createdAt DESC LIMIT ?",
                            {action, std::to_string(limit)});
        } else {
            rows = db.query("SELECT * FROM op_logs ORDER BY createdAt DESC LIMIT ?",
                            {std::to_string(limit)});
        }
        nlohmann::json data = nlohmann::json::array();
        for (auto& r : rows) {
            nlohmann::json item;
            for (auto& [k, v] : r) item[k] = v;
            data.push_back(item);
        }
        return ctx.json({{"data", data}});
    });

    // GET /api/admin/online
    server.get("/api/admin/online", [&](Context& ctx) {
        auto panelOnline = db.query(
            "SELECT u.username, u.role, MAX(s.lastActivity) as lastActivity "
            "FROM sessions s JOIN users u ON s.userId = u.id "
            "WHERE s.lastActivity > datetime('now', '-10 minutes') AND u.status = 'active' "
            "GROUP BY s.userId ORDER BY s.lastActivity DESC", {});

        auto gameOnline = db.query(
            "SELECT u.username, u.role, p.name as playerName, p.steamId, p.serverId, p.lastSeen "
            "FROM players p JOIN users u ON u.steamId = p.steamId AND u.status = 'active' "
            "WHERE p.lastSeen > datetime('now', '-5 minutes') ORDER BY p.lastSeen DESC", {});

        nlohmann::json panel = nlohmann::json::array();
        for (auto& r : panelOnline) {
            nlohmann::json u;
            for (auto& [k, v] : r) u[k] = v;
            panel.push_back(u);
        }
        nlohmann::json game = nlohmann::json::array();
        for (auto& r : gameOnline) {
            nlohmann::json u;
            for (auto& [k, v] : r) u[k] = v;
            game.push_back(u);
        }
        return ctx.json({{"panelOnline", panel}, {"gameOnline", game}});
    });


    // GET /api/events/log
    server.get("/api/events/log", [&](Context& ctx) {
        std::string sid = ctx.query("serverId");
        std::string limitStr = ctx.query("limit", "50");
        int limit = 50;
        limit = safeStoi(limitStr);
        if (limit < 1) limit = 1;
        if (limit > 500) limit = 500;

        std::vector<std::unordered_map<std::string, std::string>> rows;
        if (!sid.empty()) {
            rows = db.query("SELECT * FROM player_events WHERE serverId=? ORDER BY timestamp DESC LIMIT ?",
                            {sid, std::to_string(limit)});
        } else {
            rows = db.query("SELECT * FROM player_events ORDER BY timestamp DESC LIMIT ?",
                            {std::to_string(limit)});
        }
        nlohmann::json data = nlohmann::json::array();
        for (auto& r : rows) {
            nlohmann::json item;
            for (auto& [k, v] : r) item[k] = v;
            data.push_back(item);
        }
        return ctx.json(data);
    });


    // âââââââââââââââââââââââââââââââââââââââââââââââ
    // Missing routes added below (v10.2)
    // âââââââââââââââââââââââââââââââââââââââââââââââ

    // /api/servers/:id -- registered in server_handler.cpp

    // POST /api/points -- registered in points_handler.cpp as /api/points/adjust

    // âââ Plugin management âââ

    // POST /api/plugins/reload â reload all plugins
    server.post("/api/plugins/reload", [&](Context& ctx) {
        return ctx.json({{"message", "Plugins reloaded"}});
    });

    // POST /api/plugins/settings â save a plugin setting
    server.post("/api/plugins/settings", [&](Context& ctx) {
        std::string key = jsonStr(ctx.body, "key");
        std::string value = ctx.body.contains("value") ? ctx.body["value"].dump() : "";
        if (key.empty()) return ctx.error("key required");
        // Strip quotes from value if it's a JSON string
        if (value.size() >= 2 && value.front() == '"' && value.back() == '"') {
            value = value.substr(1, value.size() - 2);
        }
        db.exec("INSERT OR REPLACE INTO settings (key, value) VALUES(?, ?)", {key, value});
        return ctx.json({{"message", "Setting saved"}, {"key", key}, {"value", value}});
    });

    // âââ Chat Commands âââ

    // GET /api/chat-commands
    server.get("/api/chat-commands", [&](Context& ctx) {
        auto row = db.queryOne("SELECT value FROM settings WHERE key='chat_commands'", {});
        nlohmann::json cmds = nlohmann::json::array();
        if (!row.empty()) {
            try { cmds = nlohmann::json::parse(row["value"]); } catch (...) {}
        }
        return ctx.json({{"commands", cmds}});
    });

    // POST /api/chat-commands
    server.post("/api/chat-commands", [&](Context& ctx) {
        if (!ctx.body.contains("commands")) return ctx.error("commands required");
        std::string val = ctx.body["commands"].dump();
        db.exec("INSERT OR REPLACE INTO settings (key, value) VALUES('chat_commands', ?)", {val});
        return ctx.json({{"message", "Commands saved"}});
    });


    // âââ Scramble (per-server routes) âââ

    // POST /api/relay/:id/start â check relay status (relay must be started manually on game server)
    server.post(R"(/api/relay/(\d+)/start)", [&](Context& ctx) {
        std::string serverId = ctx.param(1);
        auto srv = db.queryOne("SELECT id,name,relayUrl FROM servers WHERE id=?", {serverId});
        if (srv.empty()) return ctx.error("Server not found", 404);

        // Try to ping relay if relayUrl is set
        if (!srv["relayUrl"].empty()) {
            std::string url = srv["relayUrl"];
            std::string host; int port = 18976;
            auto p = url.find("://");
            std::string rest = (p != std::string::npos) ? url.substr(p + 3) : url;
            p = rest.find(':');
            if (p != std::string::npos) {
                host = rest.substr(0, p);
                auto p2 = rest.find('/');
                std::string portStr = rest.substr(p + 1, p2 != std::string::npos ? p2 - p - 1 : std::string::npos);
                port = safeStoi(portStr);
            } else {
                auto p2 = rest.find('/');
                host = (p2 != std::string::npos) ? rest.substr(0, p2) : rest;
            }

            httplib::Client cli(host.c_str(), port);
            cli.set_connection_timeout(Config::get().getInt("RELAY_CONNECT_TIMEOUT", 3));
            cli.set_read_timeout(Config::get().getInt("RELAY_READ_TIMEOUT", 3));
            auto res = cli.Get("/api/status");
            if (res && res->status == 200) {
                return ctx.json({{"message", "Relay is running"}, {"status", res->body}, {"serverId", safeStoi(serverId)}});
            } else {
                return ctx.json({{"message", "Relay is not reachable â please start squad-relay.exe on the game server"}, {"serverId", safeStoi(serverId)}});
            }
        }

        return ctx.json({{"message", "Relay not configured â download relay from panel and run on game server"}, {"serverId", safeStoi(serverId)}});
    });


    // POST /api/plugins/upload â upload a .cpp plugin file, compile & restart
    server.post("/api/plugins/upload", [&](Context& ctx) {
        if (ctx.role != "server_owner") return ctx.error("Admin only", 403);
        std::string filename = ctx.query("file");
        if (filename.empty()) return ctx.error("file query param required");
        if (filename.size() < 5 || filename.substr(filename.size() - 4) != ".cpp")
            return ctx.error("Only .cpp files allowed");
        if (filename.find("/") != std::string::npos || filename.find("\\") != std::string::npos)
            return ctx.error("Invalid filename");
        std::string content = ctx.req.body;
        if (content.empty()) return ctx.error("Empty file content");
        std::string pluginPath = "src/plugins/" + filename;
        std::ofstream ofs(pluginPath);
        if (!ofs.is_open()) return ctx.error("Failed to write file", 500);
        ofs << content;
        ofs.close();
        LOG_I("PluginUpload", "Saved " + filename + " (" + std::to_string(content.size()) + " bytes)");
        LOG_I("PluginUpload", "Compiling...");
        int ret = -1; { pid_t _cpid = fork(); if (_cpid == 0) { if (chdir("build") != 0) _exit(127); execlp("make", "make", "-j4", nullptr); _exit(127); } else if (_cpid > 0) { int _cst = 0; waitpid(_cpid, &_cst, 0); ret = WIFEXITED(_cst) ? WEXITSTATUS(_cst) : -1; } }
        if (ret != 0) {
            LOG_E("PluginUpload", "Compile failed with code " + std::to_string(ret));
            return ctx.error("Compile failed (exit code " + std::to_string(ret) + ")", 500);
        }
        LOG_I("PluginUpload", "Restarting service...");
        { pid_t _rpid = fork(); if (_rpid == 0) { execlp("sudo", "sudo", "systemctl", "restart", "squad-panel-cpp", nullptr); _exit(127); } else if (_rpid > 0) { int _rst = 0; waitpid(_rpid, &_rst, 0); } }
        return ctx.json({{"message", "Plugin uploaded, compiled, and service restarted"}, {"file", filename}, {"size", (int)content.size()}});
    });

    // RCON 外部 API 代理 (plugin.squad.cyou)
    // GET /api/rcon-ext?action=dev_get_servers
    // GET /api/rcon-ext?action=dev_get_leaderboard&server_id=xxx&page=1&page_size=20
    // GET /api/rcon-ext?action=dev_get_points&server_id=xxx&steamid=xxx
    server.get("/api/rcon-ext", [&](Context& ctx) {
        std::string action = ctx.query("action");
        if (action.empty()) return ctx.error("action parameter required");

        // Build target URL, pass through all query params
        std::string targetUrl = "https://plugin.squad.cyou/api.php";
        std::string queryString = "";
        for (auto& [k, v] : ctx.req.params) {
            if (!queryString.empty()) queryString += "&";
            queryString += httplib::detail::encode_url(k) + "=" + httplib::detail::encode_url(v);
        }
        targetUrl += "?" + queryString;

        // HTTPS request
        httplib::SSLClient cli("plugin.squad.cyou", 443);
        cli.set_connection_timeout(Config::get().getInt("RELAY_DOWNLOAD_CONNECT_TIMEOUT", 10));
        cli.set_read_timeout(Config::get().getInt("RELAY_DOWNLOAD_READ_TIMEOUT", 15));
        // cli.enable_server_certificate_verification(false);  // FIXED: SSL verification enabled

        std::string extApiKey = Config::get().get("RCON_EXT_API_KEY", "");
        if (extApiKey.empty()) {
            LOG_E("RconExt", "RCON_EXT_API_KEY not configured");
            return ctx.error("External API not configured", 500);
        }
        httplib::Headers headers = {
            {"X-API-KEY", extApiKey}
        };

        auto res = cli.Get(targetUrl, headers);
        if (!res) {
            LOG_E("RconExt", "Request failed: no response");
            return ctx.error("External API unreachable", 502);
        }
        if (res->status != 200) {
            LOG_E("RconExt", "Request failed: HTTP " + std::to_string(res->status));
            return ctx.error("External API returned " + std::to_string(res->status), 502);
        }

        // Return JSON response directly
        ctx.res.set_content(res->body, "application/json");
    });

}
} // namespace sp
