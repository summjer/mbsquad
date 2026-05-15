#include "plugins/plugin.h"
#include "core/shared_utils.h"
#include "net/rcon.h"
#include "core/database.h"
#include "core/core.h"
#include <algorithm>

namespace sp {

class CDKRedeemPlugin : public Plugin {
public:
    std::string name() const override { return "CDKRedeem"; }

    void handle(const PluginEvent& ev, PluginContext& ctx) override {
        if (ev.type != "chat") return;
        if (!getSettingBool(ctx.db, "cdk_enabled", false)) return;

        std::string prefix = getSetting(ctx.db, "cdk_prefix", "cdk");
        std::string msg = ev.data.value("message", "");
        std::string steamId = ev.data.value("steamId", "");
        std::string playerName = ev.data.value("playerName", "");
        int sid = ev.serverId;
        if (msg.empty() || steamId.empty()) return;

        // Check prefix match (case insensitive)
        std::string msgLower = msg, prefixLower = prefix;
        std::transform(msgLower.begin(), msgLower.end(), msgLower.begin(), ::tolower);
        std::transform(prefixLower.begin(), prefixLower.end(), prefixLower.begin(), ::tolower);

        if (msgLower.find(prefixLower + " ") != 0 && msgLower.find(prefixLower + "\t") != 0) return;

        std::string code = msg.substr(prefix.size());
        while (!code.empty() && (code.front() == ' ' || code.front() == '\t')) code.erase(code.begin());
        while (!code.empty() && (code.back() == ' ' || code.back() == '\t' || code.back() == '\r' || code.back() == '\n')) code.pop_back();
        std::transform(code.begin(), code.end(), code.begin(), ::toupper);

        if (code.empty()) return;

        // Find CDK
        auto cdk = ctx.db.queryOne("SELECT * FROM cdk_codes WHERE code=?", {code});
        if (cdk.empty()) {
            ctx.rcon.send(sid, "AdminWarn \"" + steamId + "\" 激活码不存在或已失效");
            return;
        }

        // Check expiry
        std::string expiresAt = cdk["expiresAt"];
        if (!expiresAt.empty()) {
            auto exp = ctx.db.queryOne("SELECT CASE WHEN ? < datetime('now') THEN 1 ELSE 0 END as expired", {expiresAt});
            if (!exp.empty() && exp["expired"] == "1") {
                ctx.rcon.send(sid, "AdminWarn \"" + steamId + "\" 该激活码已过期");
                return;
            }
        }

        // Check uses
        int usedCount = safeStoi(cdk["usedCount"]);
        int maxUses = safeStoi(cdk["maxUses"]);
        if (usedCount >= maxUses) {
            ctx.rcon.send(sid, "AdminWarn \"" + steamId + "\" 该激活码已被使用完毕");
            return;
        }

        // Check server restriction
        std::string cdkServerId = cdk["serverId"];
        if (!cdkServerId.empty() && cdkServerId != "0" && cdkServerId != std::to_string(sid)) {
            ctx.rcon.send(sid, "AdminWarn \"" + steamId + "\" 该激活码不适用于当前服务器");
            return;
        }

        // Check if already used
        auto used = ctx.db.queryOne("SELECT id FROM cdk_logs WHERE code=? AND steamId=?", {code, steamId});
        if (!used.empty()) {
            ctx.rcon.send(sid, "AdminWarn \"" + steamId + "\" 你已经使用过该激活码了");
            return;
        }

        // Grant reward
        std::string rewardType = cdk["rewardType"];
        int rewardValue = safeStoi(cdk["rewardValue"]);

        if (rewardType == "points") {
            auto existing = ctx.db.queryOne("SELECT id FROM points WHERE steamId=? AND (serverId=? OR serverId IS NULL) ORDER BY serverId DESC LIMIT 1",
                                             {steamId, std::to_string(sid)});
            if (!existing.empty()) {
                ctx.db.exec("UPDATE points SET balance=balance+?, lifetimeEarned=lifetimeEarned+?, lastUpdated=datetime('now') WHERE id=?",
                             {std::to_string(rewardValue), std::to_string(rewardValue), existing["id"]});
            } else {
                ctx.db.exec("INSERT INTO points (serverId,steamId,playerName,balance,lifetimeEarned,lastUpdated) VALUES(?,?,?,?,?,datetime('now'))",
                             {std::to_string(sid), steamId, playerName, std::to_string(rewardValue), std::to_string(rewardValue)});
            }
            ctx.db.exec("INSERT INTO point_logs (serverId,steamId,playerName,amount,reason,operator,createdAt) VALUES(?,?,?,?,?,?,datetime('now'))",
                         {std::to_string(sid), steamId, playerName, std::to_string(rewardValue), "CDK兑换:" + code, "系统-CDK"});
        } else if (rewardType == "reserved") {
            auto existing = ctx.db.queryOne("SELECT id FROM reserved_slots WHERE serverId=? AND steamId=?", {std::to_string(sid), steamId});
            if (existing.empty()) {
                if (rewardValue > 0) {
                    ctx.db.exec("INSERT INTO reserved_slots (serverId,steamId,playerName,addedBy,expiresAt) VALUES(?,?,?,? ,datetime('now', ? || ' days'))",
                                 {std::to_string(sid), steamId, playerName, "系统-CDK", std::to_string(rewardValue)});
                } else {
                    ctx.db.exec("INSERT INTO reserved_slots (serverId,steamId,playerName,addedBy) VALUES(?,?,?,?)",
                                 {std::to_string(sid), steamId, playerName, "系统-CDK"});
                }
            }
        }

        // Record usage
        ctx.db.exec("UPDATE cdk_codes SET usedCount=usedCount+1 WHERE id=?", {cdk["id"]});
        ctx.db.exec("INSERT INTO cdk_logs (code,serverId,steamId,playerName,rewardType,rewardValue,usedAt) VALUES(?,?,?,?,?,?,datetime('now'))",
                     {code, std::to_string(sid), steamId, playerName, rewardType, std::to_string(rewardValue)});

        std::string rewardText = rewardType == "points"
            ? std::to_string(rewardValue) + "积分"
            : "预留位(" + std::to_string(rewardValue) + "天)";
        ctx.rcon.send(sid, "AdminWarn \"" + steamId + "\" 激活码兑换成功！获得 " + rewardText);
        ctx.log("[CDK] " + playerName + " 兑换 " + code + " 成功: " + rewardText);
    }
};

static CDKRedeemPlugin g_cdk;
static bool g_regCDK = [](){ PluginManager::instance().registerPlugin(&g_cdk); return true; }();

} // namespace sp
