#include "plugins/plugin.h"
#include "core/shared_utils.h"
#include "net/rcon.h"
#include "core/database.h"
#include "core/core.h"
#include <thread>
#include <chrono>

namespace sp {

class TeamBalancePlugin : public Plugin {
public:
    std::string name() const override { return "TeamBalance"; }

    void handle(const PluginEvent& ev, PluginContext& ctx) override {
        if (ev.type != "playerlist") return;
        if (!getSettingBool(ctx.db, "team_balance_enabled", false)) return;

        int threshold = getSettingInt(ctx.db, "team_balance_threshold", 3);
        int sid = ev.serverId;

        // ev.data should contain players array
        if (!ev.data.contains("players") || !ev.data["players"].is_array()) return;
        auto& players = ev.data["players"];
        if (players.size() < 4) return;

        int team1 = 0, team2 = 0;
        for (auto& p : players) {
            int tid = p.value("teamIndex", 0);
            if (tid == 1) team1++;
            else if (tid == 2) team2++;
        }

        int diff = std::abs(team1 - team2);
        if (diff < threshold) return;

        ctx.log("[队伍平衡] 服务器" + std::to_string(sid) + " 差距=" + std::to_string(diff) +
                " 阈值=" + std::to_string(threshold));

        bool team1Bigger = team1 > team2;
        int toMove = diff / 2;

        // Collect players from bigger team, prefer those without squads
        struct MoveCandidate {
            std::string steamId, name, squadId;
            bool hasSquad;
        };
        std::vector<MoveCandidate> unsorted, sorted;

        for (auto& p : players) {
            int tid = p.value("teamIndex", 0);
            if ((team1Bigger && tid != 1) || (!team1Bigger && tid != 2)) continue;
            MoveCandidate c;
            c.steamId = p.value("steamId", "");
            c.name = p.value("name", "");
            c.squadId = p.value("squadId", "");
            c.hasSquad = !c.squadId.empty() && c.squadId != "-1" && c.squadId != "N/A";
            if (c.steamId.empty()) continue;
            if (c.hasSquad) sorted.push_back(c);
            else unsorted.push_back(c);
        }

        int moved = 0;
        for (auto& c : unsorted) {
            if (moved >= toMove) break;
            ctx.rcon.send(sid, "AdminForceTeamChange \"" + c.steamId + "\"");
            ctx.log("[队伍平衡] 跳边: " + c.name);
            moved++;
            std::this_thread::sleep_for(std::chrono::milliseconds(Config::get().getInt("TEAM_SWITCH_DELAY_MS", 500)));
        }
        for (auto& c : sorted) {
            if (moved >= toMove) break;
            ctx.rcon.send(sid, "AdminForceTeamChange \"" + c.steamId + "\"");
            ctx.log("[队伍平衡] 跳边: " + c.name);
            moved++;
            std::this_thread::sleep_for(std::chrono::milliseconds(Config::get().getInt("TEAM_SWITCH_DELAY_MS", 500)));
        }
    }
};

static TeamBalancePlugin g_teamBalance;
static bool g_regTB = [](){ PluginManager::instance().registerPlugin(&g_teamBalance); return true; }();

} // namespace sp
