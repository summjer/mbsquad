#include "handlers/rcon_handler.h"
#include <regex>
#include "net/server.h"
#include "net/rcon.h"
#include "core/database.h"
#include "core/core.h"
#include "core/shared_utils.h"

namespace sp {

void registerRconRoutes(Server& server) {
    // POST /api/rcon/send
    server.post("/api/rcon/send", [&](Context& ctx) {
        std::string serverId = jsonStr(ctx.body, "serverId");
        std::string command = jsonStr(ctx.body, "command");
        if (serverId.empty() || command.empty())
            return ctx.error("serverId and command required");

        auto* pool = server.rconPool();
        if (!pool) return ctx.error("RCON pool not initialized", 500);

        std::string result;
        try {
            result = pool->send(safeStoi(serverId), command);
        } catch (const std::exception& e) {
            return ctx.error(std::string("RCON error: ") + e.what(), 500);
        }
        if (result.empty()) {
            return ctx.error("RCON: server unreachable or no response", 502);
        }

        return ctx.json({{"result", result}});
    });

    // POST /api/rcon/test
    server.post("/api/rcon/test", [&](Context& ctx) {
        std::string serverId = jsonStr(ctx.body, "serverId");
        std::string host = jsonStr(ctx.body, "host");
        int port = jsonInt(ctx.body, "port");
        std::string password = jsonStr(ctx.body, "password");

        std::string result;
        try {
            if (!serverId.empty()) {
                auto* pool = server.rconPool();
                if (!pool) return ctx.error("RCON pool not initialized", 500);
                result = pool->send(safeStoi(serverId), "ListPlayers");
                if (result.empty()) return ctx.json({{"success", false}, {"error", "RCON no response"}});
            } else if (!host.empty() && port > 0 && !password.empty()) {
                // Temporary connection test
                RconClient tmp(host, port, password);
                if (!tmp.connect(Config::get().getInt("RCON_SEND_TIMEOUT", 5000))) {
                    return ctx.json({{"success", false}, {"error", "Connection failed"}});
                }
                result = tmp.execute("ListPlayers", Config::get().getInt("RCON_SEND_TIMEOUT", 5000));
                tmp.disconnect();
            } else {
                return ctx.error("Provide serverId or host+port+password");
            }
        } catch (const std::exception& e) {
            return ctx.json({{"success", false}, {"error", e.what()}});
        }

        return ctx.json({{"success", true}, {"result", result}});
    });
}

} // namespace sp
