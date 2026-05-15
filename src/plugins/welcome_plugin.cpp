#include "plugins/plugin.h"
#include "core/shared_utils.h"
#include "net/rcon.h"
#include "core/database.h"
#include "core/core.h"

namespace sp {

class WelcomePlugin : public Plugin {
public:
    std::string name() const override { return "Welcome"; }

    void handle(const PluginEvent& ev, PluginContext& ctx) override {
        if (ev.type != "join") return;
        if (!getSettingBool(ctx.db, "welcome_enabled", false)) return;

        std::string steamId = ev.data.value("steamId", "");
        std::string playerName = ev.data.value("playerName", "");
        if (steamId.empty()) return;

        std::string msg = getSetting(ctx.db, "welcome_message", "欢迎 {player} 加入服务器！");
        // Replace {player}
        size_t pos;
        while ((pos = msg.find("{player}")) != std::string::npos) {
            msg.replace(pos, 8, playerName.empty() ? "玩家" : playerName);
        }

        ctx.rcon.send(ev.serverId, "AdminWarn \"" + steamId + "\" " + msg);
        ctx.log("[进服欢迎] " + (playerName.empty() ? steamId : playerName) + ": " + msg);
    }
};

static WelcomePlugin g_welcome;
static bool g_regW = [](){ PluginManager::instance().registerPlugin(&g_welcome); return true; }();

} // namespace sp
