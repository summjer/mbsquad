#include "net/server.h"
#include "core/database.h"
#include "core/core.h"
#include <set>
#include <map>

namespace sp {

void registerAnalyticsRoutes(Server& server) {
    auto& db = server.db();
    // GET /api/players/online-chart
    server.get("/api/players/online-chart", [&](Context& ctx) {
        std::string range = ctx.query("range", "1d");

        std::string sqlInterval = "-24 hours";
        std::string groupExpr = "strftime('%Y-%m-%d %H', timestamp)";
        if (range == "3d") { sqlInterval = "-3 days"; }
        else if (range == "7d") { sqlInterval = "-7 days"; groupExpr = "strftime('%Y-%m-%d', timestamp)"; }
        else if (range == "15d") { sqlInterval = "-15 days"; groupExpr = "strftime('%Y-%m-%d', timestamp)"; }
        else if (range == "30d") { sqlInterval = "-30 days"; groupExpr = "strftime('%Y-%m-%d', timestamp)"; }
        else if (range == "6m") { sqlInterval = "-6 months"; groupExpr = "strftime('%Y-%m', timestamp)"; }
        else if (range == "1y") { sqlInterval = "-12 months"; groupExpr = "strftime('%Y-%m', timestamp)"; }

        std::string sql1 = "SELECT " + groupExpr + " as bucket, COUNT(*) as count FROM player_events "
                           "WHERE eventType='join' AND timestamp >= datetime('now', '" + sqlInterval + "') "
                           "GROUP BY bucket ORDER BY bucket";
        std::string sql2 = "SELECT " + groupExpr + " as bucket, COUNT(*) as count FROM player_events "
                           "WHERE eventType IN ('disconnect','leave') AND timestamp >= datetime('now', '" + sqlInterval + "') "
                           "GROUP BY bucket ORDER BY bucket";

        auto joins = db.query(sql1, {});
        auto discs = db.query(sql2, {});

        std::map<std::string, int> jmap, dmap;
        for (auto& r : joins) jmap[r["bucket"]] = std::stoi(r["count"]);
        for (auto& r : discs) dmap[r["bucket"]] = std::stoi(r["count"]);

        std::set<std::string> keys;
        for (auto& [k, _] : jmap) keys.insert(k);
        for (auto& [k, _] : dmap) keys.insert(k);

        nlohmann::json labels = nlohmann::json::array();
        nlohmann::json joinData = nlohmann::json::array();
        nlohmann::json discData = nlohmann::json::array();
        for (auto& k : keys) {
            labels.push_back(k);
            joinData.push_back(jmap.count(k) ? jmap[k] : 0);
            discData.push_back(dmap.count(k) ? dmap[k] : 0);
        }

        return ctx.json({
            {"labels", labels}, {"joins", joinData}, {"disconnects", discData}, {"range", range}
        });
    });

    // GET /api/players/tick-chart
    server.get("/api/players/tick-chart", [&](Context& ctx) {
        std::string range = ctx.query("range", "1h");

        std::string sqlInterval = "-1 hours";
        std::string groupExpr = "strftime('%Y-%m-%d %H:%M', timestamp)";
        if (range == "6h") { sqlInterval = "-6 hours"; groupExpr = "strftime('%Y-%m-%d %H:%M', timestamp)"; }
        else if (range == "24h") { sqlInterval = "-24 hours"; groupExpr = "strftime('%Y-%m-%d %H', timestamp)"; }
        else if (range == "3d") { sqlInterval = "-3 days"; groupExpr = "strftime('%Y-%m-%d %H', timestamp)"; }
        else if (range == "7d") { sqlInterval = "-7 days"; groupExpr = "strftime('%Y-%m-%d', timestamp)"; }
        else if (range == "30d") { sqlInterval = "-30 days"; groupExpr = "strftime('%Y-%m-%d', timestamp)"; }

        std::string sql = "SELECT " + groupExpr + " as bucket, "
                          "AVG(tickRate) as avgTick, MAX(tickRate) as maxTick, MIN(tickRate) as minTick "
                          "FROM server_ticks "
                          "WHERE timestamp >= datetime('now', '" + sqlInterval + "') "
                          "GROUP BY bucket ORDER BY bucket";

        auto rows = db.query(sql, {});

        nlohmann::json labels = nlohmann::json::array();
        nlohmann::json avgData = nlohmann::json::array();
        nlohmann::json maxData = nlohmann::json::array();
        nlohmann::json minData = nlohmann::json::array();
        for (auto& r : rows) {
            labels.push_back(r["bucket"]);
            try { avgData.push_back(std::stod(r["avgTick"])); } catch (...) { avgData.push_back(0); }
            try { maxData.push_back(std::stod(r["maxTick"])); } catch (...) { maxData.push_back(0); }
            try { minData.push_back(std::stod(r["minTick"])); } catch (...) { minData.push_back(0); }
        }

        return ctx.json({
            {"labels", labels}, {"avg", avgData}, {"max", maxData}, {"min", minData}, {"range", range}
        });
    });

    // /api/squad-events -- registered in query_handler.cpp (paginated)

} // registerXxxRoutes
} // namespace sp
