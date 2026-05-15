#include "handlers/chat_handler.h"
#include "net/server.h"
#include "core/database.h"
#include "core/core.h"
#include "core/shared_utils.h"

namespace sp {

void registerChatRoutes(Server& server) {
    auto& db = server.db();

    // GET /api/chat
    server.get("/api/chat", [&](Context& ctx) {
        std::string sid     = ctx.query("serverId");
        std::string keyword = ctx.query("keyword");
        std::string type    = ctx.query("type");
        int page            = safeStoi(ctx.query("page", "1"), 1);
        int pageSize        = safeStoi(ctx.query("page_size", "50"), 50);
        int offset          = (page - 1) * pageSize;

        std::string where = "WHERE 1=1";
        std::vector<std::string> params;

        if (!sid.empty() && sid != "all") {
            where += " AND serverId=?";
            params.push_back(sid);
        }
        if (!type.empty() && type != "all") {
            where += " AND type=?";
            params.push_back(type);
        }
        if (!keyword.empty()) {
            where += " AND (playerName LIKE ? OR steamId LIKE ? OR message LIKE ?)";
            std::string kw = "%" + keyword + "%";
            params.push_back(kw);
            params.push_back(kw);
            params.push_back(kw);
        }

        // Count total
        std::string countSql = "SELECT COUNT(*) as c FROM chat_logs " + where;
        auto countRow = db.queryOne(countSql, params);
        int total = safeStoi(countRow["c"]);

        // Query rows
        std::string dataSql = "SELECT * FROM chat_logs " + where
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

        int totalPages = (total + pageSize - 1) / pageSize;
        return ctx.json({
            {"data", data},
            {"total", total},
            {"total_pages", totalPages},
            {"page", page}
        });
    });
}

} // namespace sp
