#include "net/server.h"
#include "core/database.h"
#include "core/core.h"
#include "../../vendor/httplib.h"

namespace sp {

void registerRelayRoutes(Server& server) {
    auto& db = server.db();
    // Relay ç®¡ç
    // POST /api/relay/register â per-server relay registration (v12)
    server.post("/api/relay/register", [&](Context& ctx) {
        std::string registerCode = jsonStr(ctx.body, "registerCode");
        std::string host = jsonStr(ctx.body, "host");
        int rconPort = ctx.body.value("rconPort", 27015);
        std::string rconPassword = jsonStr(ctx.body, "rconPassword");
        std::string relayUrl = jsonStr(ctx.body, "relayUrl");

        if (registerCode.empty()) return ctx.error("registerCode required");

        // Find server by per-server registerCode
        auto srv = db.queryOne(
            "SELECT id,name,serverApiKey FROM servers WHERE registerCode=?",
            {registerCode});
        if (srv.empty()) return ctx.error("Invalid register code â no matching server", 401);

        // Update server connection info if provided
        if (!host.empty()) {
            db.exec("UPDATE servers SET host=?,rconPort=?,rconPassword=? WHERE id=?",
                    {host, std::to_string(rconPort), rconPassword, srv["id"]});
        }
        if (!relayUrl.empty()) {
            db.exec("UPDATE servers SET relayUrl=? WHERE id=?", {relayUrl, srv["id"]});
        } else if (!host.empty()) {
            std::string autoUrl = "http://" + host + ":18977";
            db.exec("UPDATE servers SET relayUrl=? WHERE id=?", {autoUrl, srv["id"]});
        }

        // Generate a new apiKey for this relay session
        std::string apiKey = "srv_";
        static const char hex[] = "0123456789abcdef";
        std::random_device rd2;
        std::mt19937 gen2(rd2());
        std::uniform_int_distribution<> dis2(0, 15);
        for (int i = 0; i < 40; i++) apiKey += hex[dis2(gen2)];
        db.exec("UPDATE servers SET serverApiKey=? WHERE id=?", {apiKey, srv["id"]});

        return ctx.json({
            {"apiKey", apiKey},
            {"serverId", std::stoll(srv["id"])},
            {"serverName", srv["name"]}
        });
    }, false);

    // GET /api/servers/:id/register-code â per-server register code (v12)
    server.get(R"(/api/servers/(\d+)/register-code)", [&](Context& ctx) {
        std::string serverId = ctx.param(1);
        // Check access
        auto member = db.queryOne("SELECT role FROM server_members WHERE serverId=? AND userId=?",
                                   {serverId, ctx.userId});
        if (member.empty()) return ctx.error("Access denied", 403);

        auto row = db.queryOne("SELECT registerCode FROM servers WHERE id=?", {serverId});
        if (row.empty() || row["registerCode"].empty()) return ctx.json({{"code", nullptr}});
        return ctx.json({{"code", row["registerCode"]}});
    });

    // POST /api/servers/:id/generate-code â per-server register code (v12)
    server.post(R"(/api/servers/(\d+)/generate-code)", [&](Context& ctx) {
        std::string serverId = ctx.param(1);
        auto member = db.queryOne("SELECT role FROM server_members WHERE serverId=? AND userId=?",
                                   {serverId, ctx.userId});
        if (member.empty()) return ctx.error("Access denied", 403);
        if (member["role"] != "owner" && member["role"] != "admin")
            return ctx.error("Only owner/admin can generate register codes", 403);

        std::string code;
        static const char hex[] = "0123456789abcdef";
        std::random_device rd;
        std::mt19937 gen(rd());
        std::uniform_int_distribution<> dis(0, 15);
        for (int i = 0; i < 16; i++) code += hex[dis(gen)];
        db.exec("UPDATE servers SET registerCode=? WHERE id=?", {code, serverId});
        return ctx.json({{"code", code}});
    });

    // GET /api/relay/download â download squad-relay.exe
    server.get("/api/relay/download", [&](Context& ctx) {
        std::string base = std::string(getenv("HOME") ? getenv("HOME") : ".");
        std::string filePath = base + "/squad-panel-cpp/squad-relay.exe";
        std::ifstream file(filePath, std::ios::binary);
        if (!file.is_open()) {
            return ctx.error("squad-relay.exe not found", 404);
        }
        std::string body((std::istreambuf_iterator<char>(file)),
                          std::istreambuf_iterator<char>());
        file.close();
        ctx.res.status = 200;
        ctx.res.set_header("Content-Type", "application/octet-stream");
        ctx.res.set_header("Content-Disposition", "attachment; filename=\"squad-relay.exe\"");
        ctx.res.set_content(body, "application/octet-stream");
    }, false);
    // GET /api/rcon/status
    server.get("/api/rcon/status", [&](Context& ctx) {
        auto servers = db.query("SELECT id, name, host, rconPort FROM servers", {});
        nlohmann::json conns = nlohmann::json::array();
        for (auto& s : servers) {
            conns.push_back({
                {"serverId", std::stoi(s["id"])},
                {"name", s["name"]},
                {"host", s["host"]},
                {"port", std::stoi(s["rconPort"])}
            });
        }
        return ctx.json({{"connections", conns}});
    });

} // registerXxxRoutes
} // namespace sp
