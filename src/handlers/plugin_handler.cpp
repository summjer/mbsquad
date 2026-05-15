#include "handlers/plugin_handler.h"
#include "net/server.h"
#include "core/database.h"
#include "core/core.h"
#include <filesystem>
#include <fstream>

namespace sp {
namespace fs = std::filesystem;

void registerPluginRoutes(Server& server) {
    auto& db = server.db();

    // GET /api/plugins
    server.get("/api/plugins", [&](Context& ctx) {
        auto rows = db.query(
            "SELECT * FROM plugins WHERE userId=? ORDER BY id DESC LIMIT 100",
            {ctx.userId});
        nlohmann::json plugins = nlohmann::json::array();
        for (auto& r : rows) {
            nlohmann::json p;
            for (auto& [k, v] : r) p[k] = v;
            plugins.push_back(p);
        }
        return ctx.json({{"plugins", plugins}});
    });

    // POST /api/plugins
    server.post("/api/plugins", [&](Context& ctx) {
        std::string serverId = jsonStr(ctx.body, "serverId");
        std::string name     = jsonStr(ctx.body, "name");
        std::string version  = jsonStr(ctx.body, "version", "1.0");
        std::string config   = jsonStr(ctx.body, "config", "{}");
        if (name.empty()) return ctx.error("name required");

        db.exec(
            "INSERT INTO plugins (userId,serverId,name,version,config) VALUES(?,?,?,?,?)",
            {ctx.userId, serverId, name, version, config});

        return ctx.json({{"id", db.lastInsertId()}});
    });

    // PUT /api/plugins/:id
    server.put(R"(/api/plugins/(\d+))", [&](Context& ctx) {
        std::string id = ctx.param(1);
        auto existing = db.queryOne(
            "SELECT * FROM plugins WHERE id=? AND userId=?", {id, ctx.userId});
        if (existing.empty()) return ctx.error("Not found", 404);

        if (ctx.body.contains("enabled")) {
            int enabled = ctx.body["enabled"].get<bool>() ? 1 : 0;
            db.exec("UPDATE plugins SET enabled=? WHERE id=?", {std::to_string(enabled), id});
        }
        if (ctx.body.contains("config")) {
            std::string config = ctx.body["config"].dump();
            db.exec("UPDATE plugins SET config=? WHERE id=? AND userId=?",
                    {config, id, ctx.userId});
        }
        return ctx.json({{"message", "Updated"}});
    });

    // DELETE /api/plugins/:id
    server.del(R"(/api/plugins/(\d+))", [&](Context& ctx) {
        std::string id = ctx.param(1);
        db.exec("DELETE FROM plugins WHERE id=? AND userId=?", {id, ctx.userId});
        return ctx.json({{"message", "Deleted"}});
    });

    // GET /api/modules — scan public/js/modules/ directory
    server.get("/api/modules", [&](Context& ctx) {
        nlohmann::json modules = nlohmann::json::array();
        std::string modulesDir = "public/js/modules";
        std::error_code ec;
        if (fs::is_directory(modulesDir, ec)) {
            for (auto& entry : fs::directory_iterator(modulesDir, ec)) {
                if (!entry.is_directory()) continue;
                std::string name = entry.path().filename().string();
                if (name[0] == '.') continue;
                if (fs::exists(entry.path() / "index.js", ec)) {
                    modules.push_back(name);
                }
            }
        }
        return ctx.json({{"modules", modules}});
    });
}

} // namespace sp
