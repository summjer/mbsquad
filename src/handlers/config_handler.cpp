#include "handlers/config_handler.h"
#include "net/server.h"
#include "core/database.h"
#include "core/core.h"
#include <fstream>

namespace sp {

void registerConfigRoutes(Server& server) {
    auto& db = server.db();

    // GET /api/config
    server.get("/api/config", [&](Context& ctx) {
        auto rows = db.query("SELECT * FROM settings", {});
        nlohmann::json config;
        for (auto& r : rows) {
            try { config[r["key"]] = nlohmann::json::parse(r["value"]); }
            catch (...) { config[r["key"]] = r["value"]; }
        }
        return ctx.json({{"config", config}});
    });

    // POST /api/config
    server.post("/api/config", [&](Context& ctx) {
        for (auto& [k, v] : ctx.body.items()) {
            std::string val = v.dump();
            db.exec("INSERT OR REPLACE INTO settings (key,value) VALUES(?,?)", {k, val});
        }
        return ctx.json({{"message", "Saved"}});
    });

    // GET /api/config/tb-code — requires API key auth
    server.get("/api/config/tb-code", [&](Context& ctx) {
        // Validate API key from header or query param
        std::string apiKey;
        if (ctx.req.has_header("X-API-Key")) apiKey = ctx.req.get_header_value("X-API-Key");
        if (apiKey.empty()) apiKey = ctx.query("apiKey");
        if (apiKey.empty()) return ctx.error("API key required", 401);
        auto srv = db.queryOne("SELECT id FROM servers WHERE serverApiKey=?", {apiKey});
        if (srv.empty()) return ctx.error("Invalid API key", 401);

        auto row = db.queryOne("SELECT value FROM settings WHERE key='tb_code'", {});
        std::string code = row.empty() ? "tb" : row["value"];
        try { code = nlohmann::json::parse(code).get<std::string>(); }
        catch (...) {}
        return ctx.json({{"tb_code", code}});
    }, false);

    // GET /api/config/plugin-keys — requires API key auth
    server.get("/api/config/plugin-keys", [&](Context& ctx) {
        std::string apiKey;
        if (ctx.req.has_header("X-API-Key")) apiKey = ctx.req.get_header_value("X-API-Key");
        if (apiKey.empty()) apiKey = ctx.query("apiKey");
        if (apiKey.empty()) return ctx.error("API key required", 401);
        auto srv = db.queryOne("SELECT id FROM servers WHERE serverApiKey=?", {apiKey});
        if (srv.empty()) return ctx.error("Invalid API key", 401);

        auto redeemRow = db.queryOne("SELECT value FROM settings WHERE key='redeem_code'", {});
        auto tbRow = db.queryOne("SELECT value FROM settings WHERE key='tb_code'", {});
        std::string redeem = "duihuan", tb = "tb";
        if (!redeemRow.empty()) {
            try { redeem = nlohmann::json::parse(redeemRow["value"]).get<std::string>(); }
            catch (...) { redeem = redeemRow["value"]; }
        }
        if (!tbRow.empty()) {
            try { tb = nlohmann::json::parse(tbRow["value"]).get<std::string>(); }
            catch (...) { tb = tbRow["value"]; }
        }
        return ctx.json({{"redeem_code", redeem}, {"tb_code", tb}});
    }, false);

    // GET /api/logs — read only last N lines (seek-based)
    server.get("/api/logs", [&](Context& ctx) {
        nlohmann::json logs = nlohmann::json::array();
        std::ifstream f("data/server.log", std::ios::ate | std::ios::binary);
        if (f.is_open()) {
            auto fileSize = f.tellg();
            // Read last 100KB max
            const size_t maxRead = 100 * 1024;
            size_t readSize = static_cast<size_t>(fileSize);
            size_t startPos = 0;
            if (readSize > maxRead) {
                startPos = readSize - maxRead;
                readSize = maxRead;
            }
            f.seekg(static_cast<std::streamoff>(startPos));
            std::string buffer(readSize, '\0');
            f.read(&buffer[0], static_cast<std::streamsize>(readSize));
            // Split into lines, skip first partial line if not at start
            size_t start = 0;
            if (startPos > 0) {
                auto nl = buffer.find('\n');
                if (nl != std::string::npos) start = nl + 1;
            }
            std::string_view sv(buffer.data() + start, buffer.size() - start);
            size_t pos = 0;
            std::vector<std::string> lines;
            while (pos < sv.size()) {
                auto nl = sv.find('\n', pos);
                std::string line;
                if (nl == std::string_view::npos) {
                    line = std::string(sv.substr(pos));
                    pos = sv.size();
                } else {
                    line = std::string(sv.substr(pos, nl - pos));
                    pos = nl + 1;
                }
                if (!line.empty() && line.back() == '\r') line.pop_back();
                if (!line.empty()) lines.push_back(std::move(line));
            }
            // Return last 200 lines
            int startIdx = std::max(0, static_cast<int>(lines.size()) - 200);
            for (int i = startIdx; i < static_cast<int>(lines.size()); i++) {
                logs.push_back(lines[static_cast<size_t>(i)]);
            }
        }
        return ctx.json({{"logs", logs}});
    });

    // GET /api/health (public, no internal details)
    server.get("/api/health", [&](Context& ctx) {
        return ctx.json({
            {"status", "ok"},
            {"version", "1.0.0-cpp"}
        });
    }, false);
}

} // namespace sp
