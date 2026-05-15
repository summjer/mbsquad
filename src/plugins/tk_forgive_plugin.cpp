#include "plugins/plugin.h"
#include "core/shared_utils.h"
#include "net/rcon.h"
#include "core/database.h"
#include "core/core.h"
#include <algorithm>

namespace sp {

class TKForgivePlugin : public Plugin {
public:
    std::string name() const override { return "TKForgive"; }

    void handle(const PluginEvent& ev, PluginContext& ctx) override {
        if (ev.type == "kill") handleKill(ev, ctx);
        else if (ev.type == "chat") handleChat(ev, ctx);
    }

private:
    void handleKill(const PluginEvent& ev, PluginContext& ctx) {
        std::string evType = ev.data.value("type", "");
        if (evType != "teamkill") return;
        std::string killerSteamId = ev.data.value("killerSteamId", "");
        if (killerSteamId.empty()) return;

        if (!getSettingBool(ctx.db, "tk_forgive_enabled", true)) return;

        int sid = ev.serverId;
        int secs = getSettingInt(ctx.db, "tk_forgive_seconds", 180);
        std::string killerName = ev.data.value("killerName", "");
        std::string victimName = ev.data.value("victimName", "");
        std::string victimSteamId = ev.data.value("victimSteamId", "");

        // Insert tk_forgive record
        ctx.db.exec("INSERT INTO tk_forgive (serverId,killerSteamId,killerName,victimSteamId,victimName,expiresAt) "
                     "VALUES(?,?,?,?,?,datetime('now', ? || ' seconds'))",
                     {std::to_string(sid), killerSteamId, killerName, victimSteamId, victimName, std::to_string(secs)});

        // Get keywords
        std::string kwRaw = getSetting(ctx.db, "tk_forgive_keywords", "sor,sorry,soy");
        int tkPenalty = getSettingInt(ctx.db, "tk_penalty", 5);

        ctx.rcon.send(sid, "AdminWarn \"" + killerSteamId + "\" 你TK了" + victimName +
                      "，" + std::to_string(secs) + "秒内输入 " + kwRaw +
                      " 道歉，否则自动踢出。积分 -" + std::to_string(tkPenalty));
    }

    void handleChat(const PluginEvent& ev, PluginContext& ctx) {
        if (!getSettingBool(ctx.db, "tk_forgive_enabled", true)) return;
        std::string steamId = ev.data.value("steamId", "");
        std::string message = ev.data.value("message", "");
        if (steamId.empty() || message.empty()) return;

        // Lowercase message
        std::string msgLower = message;
        std::transform(msgLower.begin(), msgLower.end(), msgLower.begin(), ::tolower);

        // Get keywords
        std::string kwRaw = getSetting(ctx.db, "tk_forgive_keywords", "sor,sorry,soy");
        std::vector<std::string> keywords;
        std::string kw;
        std::istringstream ss(kwRaw);
        while (std::getline(ss, kw, ',')) {
            while (!kw.empty() && kw.front() == ' ') kw.erase(kw.begin());
            while (!kw.empty() && kw.back() == ' ') kw.pop_back();
            std::transform(kw.begin(), kw.end(), kw.begin(), ::tolower);
            if (!kw.empty()) keywords.push_back(kw);
        }

        bool matched = false;
        for (auto& k : keywords) {
            if (msgLower.find(k) != std::string::npos) { matched = true; break; }
        }
        if (!matched) return;

        // Find active TK records
        int sid = ev.serverId;
        auto rows = ctx.db.query(
            "SELECT * FROM tk_forgive WHERE serverId=? AND killerSteamId=? AND forgiven=0 AND kicked=0 "
            "AND expiresAt > datetime('now')",
            {std::to_string(sid), steamId});

        for (auto& tk : rows) {
            ctx.db.exec("UPDATE tk_forgive SET forgiven=1 WHERE id=?", {tk["id"]});
            std::string victimName = tk["victimName"];
            ctx.rcon.send(sid, "AdminWarn \"" + steamId + "\" 你的TK已被原谅，注意友军伤害！");
        }
    }
};

static TKForgivePlugin g_tkForgive;
static bool g_regTK = [](){ PluginManager::instance().registerPlugin(&g_tkForgive); return true; }();

} // namespace sp
