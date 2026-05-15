#include "plugins/plugin.h"
#include "core/shared_utils.h"
#include "net/rcon.h"
#include "core/database.h"
#include "core/core.h"

namespace sp {

class SwitchLockPlugin : public Plugin {
public:
    std::string name() const override { return "SwitchLock"; }

    void handle(const PluginEvent& ev, PluginContext& ctx) override {
        if (ev.type != "scramble") return;
        int sid = ev.serverId;
        if (sid == 0) return;

        int duration = getSettingInt(ctx.db, "switch_lock_minutes", 20);

        ctx.db.exec("DELETE FROM switch_locks WHERE serverId=?", {std::to_string(sid)});
        ctx.db.exec("INSERT INTO switch_locks (serverId,steamId,lockedUntil,reason) "
                     "VALUES(?,'ALL',datetime('now', ? || ' minutes'),?)",
                     {std::to_string(sid), std::to_string(duration), "scramble_lock_" + std::to_string(sid)});
        ctx.log("[SwitchLock] 打乱完成，锁定服务器" + std::to_string(sid) + " 所有玩家 " +
                std::to_string(duration) + " 分钟");
    }
};

static SwitchLockPlugin g_switchLock;
static bool g_regSL = [](){ PluginManager::instance().registerPlugin(&g_switchLock); return true; }();

} // namespace sp
