#include "handlers/misc_handler.h"
#include "net/server.h"
#include "net/rcon.h"
#include "core/database.h"
#include "core/core.h"
#include <algorithm>
#include <numeric>
#include <fstream>
#include <sys/wait.h>
#include <unistd.h>

namespace sp {

// Snake Draft
struct ScoredPlayer {
    std::string steamId, name, rconId;
    double playtime, kills, deaths, kd;
    double normPlaytime, normKD, score;
    int currentTeam;
};

static void snakeAssign(std::vector<ScoredPlayer>& sorted,
    std::vector<ScoredPlayer>& team1, std::vector<ScoredPlayer>& team2) {
    for (size_t i = 0; i < sorted.size(); i++) {
        size_t group = i / 2;
        bool toTeam1;
        if (group % 2 == 0) {
            toTeam1 = (i % 2 == 0);
        } else {
            toTeam1 = (i % 2 == 1);
        }
        if (toTeam1) team1.push_back(sorted[i]);
        else team2.push_back(sorted[i]);
    }
}

void registerScrambleRoutes(Server& server) {
    auto& db = server.db();

    // POST /api/scramble
    server.post("/api/scramble", [&](Context& ctx) {
        std::string serverId = jsonStr(ctx.body, "serverId");
        if (serverId.empty()) return ctx.error("serverId required");

        auto* pool = server.rconPool();
        if (!pool) return ctx.error("RCON pool not initialized", 500);

        // Get current players via RCON
        std::string listResult;
        try {
            listResult = pool->send(std::stoi(serverId), "ListPlayers");
        } catch (const std::exception& e) {
            return ctx.error(std::string("RCON ListPlayers failed: ") + e.what(), 500);
        }

        // Parse player list (simplified: extract SteamID and Name patterns)
        std::vector<ScoredPlayer> online;
        // Format: "ID: X - SteamID: 7656... - Name: PlayerName - Team: 1"
        std::istringstream stream(listResult);
        std::string line;
        while (std::getline(stream, line)) {
            if (line.find("SteamID:") == std::string::npos) continue;

            ScoredPlayer p;
            p.currentTeam = 0;

            // Extract SteamID
            auto steamPos = line.find("SteamID:");
            auto steamEnd = line.find("-", steamPos + 9);
            if (steamEnd == std::string::npos) steamEnd = line.find("\n", steamPos + 9);
            if (steamEnd == std::string::npos) steamEnd = line.size();
            p.steamId = line.substr(steamPos + 9, steamEnd - steamPos - 9);
            // trim
            while (!p.steamId.empty() && p.steamId.back() == ' ') p.steamId.pop_back();
            while (!p.steamId.empty() && p.steamId.front() == ' ') p.steamId.erase(p.steamId.begin());

            // Extract Name
            auto namePos = line.find("Name:");
            auto nameEnd = line.find("-", namePos + 6);
            if (nameEnd == std::string::npos) nameEnd = line.size();
            if (namePos != std::string::npos) {
                p.name = line.substr(namePos + 6, nameEnd - namePos - 6);
                while (!p.name.empty() && p.name.back() == ' ') p.name.pop_back();
                while (!p.name.empty() && p.name.front() == ' ') p.name.erase(p.name.begin());
            }

            // Extract Team
            auto teamPos = line.find("Team:");
            if (teamPos != std::string::npos) {
                std::string teamStr = line.substr(teamPos + 6);
                while (!teamStr.empty() && teamStr.front() == ' ') teamStr.erase(teamStr.begin());
                p.currentTeam = std::stoi(teamStr);
            }

            if (!p.steamId.empty()) online.push_back(p);
        }

        if (online.size() < 4) return ctx.error("Need at least 4 players to scramble");

        // Build steamId list for DB query
        std::string placeholders;
        std::vector<std::string> params;
        for (size_t i = 0; i < online.size(); i++) {
            if (i > 0) placeholders += ",";
            placeholders += "?";
            params.push_back(online[i].steamId);
        }

        // Query playtime
        std::vector<std::string> ptParams = {serverId};
        ptParams.insert(ptParams.end(), params.begin(), params.end());
        auto ptRows = db.query(
            "SELECT steamId, playtime FROM players WHERE serverId=? AND steamId IN (" + placeholders + ")",
            ptParams);
        std::unordered_map<std::string, double> playtimeMap;
        for (auto& r : ptRows) playtimeMap[r["steamId"]] = std::stod(r["playtime"]);

        // Query kills
        std::string namePlaceholders;
        std::vector<std::string> names;
        for (size_t i = 0; i < online.size(); i++) {
            if (i > 0) namePlaceholders += ",";
            namePlaceholders += "?";
            names.push_back(online[i].name);
        }
        std::vector<std::string> killParams = {serverId};
        killParams.insert(killParams.end(), names.begin(), names.end());
        auto killRows = db.query(
            "SELECT killer AS name, COUNT(*) AS kills FROM kills WHERE serverId=? AND killer IN ("
            + namePlaceholders + ") GROUP BY killer",
            killParams);
        std::unordered_map<std::string, double> killMap;
        for (auto& r : killRows) killMap[r["name"]] = std::stod(r["kills"]);

        std::vector<std::string> deathParams = {serverId};
        deathParams.insert(deathParams.end(), names.begin(), names.end());
        auto deathRows = db.query(
            "SELECT victim AS name, COUNT(*) AS deaths FROM kills WHERE serverId=? AND victim IN ("
            + namePlaceholders + ") GROUP BY victim",
            deathParams);
        std::unordered_map<std::string, double> deathMap;
        for (auto& r : deathRows) deathMap[r["name"]] = std::stod(r["deaths"]);

        // Calculate scores
        double maxPlaytime = 0, maxKD = 0;
        for (auto& p : online) {
            p.playtime = playtimeMap.count(p.steamId) ? playtimeMap[p.steamId] : 0;
            p.kills = killMap.count(p.name) ? killMap[p.name] : 0;
            p.deaths = deathMap.count(p.name) ? deathMap[p.name] : 0;
            p.kd = p.deaths > 0 ? p.kills / p.deaths : (p.kills > 0 ? p.kills : 0);
            if (p.playtime > maxPlaytime) maxPlaytime = p.playtime;
            if (p.kd > maxKD) maxKD = p.kd;
        }
        if (maxPlaytime == 0) maxPlaytime = 1;
        if (maxKD == 0) maxKD = 1;

        for (auto& p : online) {
            p.normPlaytime = p.playtime / maxPlaytime;
            p.normKD = p.kd / maxKD;
            p.score = p.normPlaytime * 0.5 + p.normKD * 0.5;
        }

        // Sort by score descending
        std::sort(online.begin(), online.end(),
            [](const ScoredPlayer& a, const ScoredPlayer& b) { return a.score > b.score; });

        // Snake draft assign
        std::vector<ScoredPlayer> team1, team2;
        snakeAssign(online, team1, team2);

        // Find who needs to switch
        std::vector<ScoredPlayer> needSwitch;
        for (auto& p : team1) {
            if (p.currentTeam == 2) needSwitch.push_back(p);
        }
        for (auto& p : team2) {
            if (p.currentTeam == 1) needSwitch.push_back(p);
        }

        // Execute RCON switches
        int switched = 0, failed = 0;
        for (auto& p : needSwitch) {
            try {
                pool->send(std::stoi(serverId),
                    "AdminForceTeamChange \"" + p.steamId + "\"");
                switched++;
            } catch (const std::exception& e) {
                failed++;
                LOG_W("Scramble", "Failed to switch " + p.name + ": " + e.what());
            }
        }

        nlohmann::json t1Names = nlohmann::json::array();
        nlohmann::json t2Names = nlohmann::json::array();
        for (auto& p : team1) t1Names.push_back(p.name);
        for (auto& p : team2) t2Names.push_back(p.name);

        nlohmann::json resp;
        resp["success"] = true;
        resp["switched"] = switched;
        if (failed > 0) resp["failed"] = failed;
        resp["total"] = (int)online.size();
        resp["team1"] = t1Names;
        resp["team2"] = t2Names;
        return ctx.json(resp);
    });
}

// Plugin Compilation
void registerBuildRoutes(Server& server) {
    auto& db = server.db();

    // POST /api/build/check — check if compilation is available
    server.post("/api/build/check", [&](Context& ctx) {
        // Check if gcc/g++ is available
        std::string compiler = jsonStr(ctx.body, "compiler", "g++");
        std::string checkCmd = compiler + " --version 2>&1";
        int ret = -1; { pid_t _mpid = fork(); if (_mpid == 0) { execlp("sh", "sh", "-c", checkCmd.c_str(), nullptr); _exit(127); } else if (_mpid > 0) { int _mst = 0; waitpid(_mpid, &_mst, 0); ret = WIFEXITED(_mst) ? WEXITSTATUS(_mst) : -1; } }
        return ctx.json({
            {"available", ret == 0},
            {"compiler", compiler}
        });
    });

    // GET /api/build/list — list builds
    server.get("/api/build/list", [&](Context& ctx) {
        auto rows = db.query(
            "SELECT id,userId,filename,status,createdAt FROM plugin_sources WHERE userId=? ORDER BY id DESC",
            {ctx.userId});
        nlohmann::json builds = nlohmann::json::array();
        for (auto& r : rows) {
            nlohmann::json b;
            for (auto& [k, v] : r) b[k] = v;
            builds.push_back(b);
        }
        return ctx.json({{"builds", builds}});
    });

    // GET /api/build/download/:id — download compiled plugin
    server.get(R"(/api/build/download/(\d+))", [&](Context& ctx) {
        std::string id = ctx.param(1);
        auto row = db.queryOne("SELECT * FROM plugin_sources WHERE id=?", {id});
        if (row.empty()) return ctx.error("Not found", 404);

        // In a real implementation, serve the compiled .so file
        return ctx.error("Download not yet implemented in C++ version");
    });
}

} // namespace sp
