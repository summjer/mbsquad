#include "plugins/plugin.h"
#include "core/shared_utils.h"
#include "net/rcon.h"
#include "core/database.h"
#include "core/core.h"
#include <chrono>
#include <random>
#include <algorithm>

namespace sp {

class ChatCommandsPlugin : public Plugin {
public:
    std::string name() const override { return "ChatCommands"; }

    void handle(const PluginEvent& ev, PluginContext& ctx) override {
        if (ev.type != "chat") return;
        std::string msg = ev.data.value("message", "");
        std::string steamId = ev.data.value("steamId", "");
        std::string playerName = ev.data.value("playerName", "");
        int sid = ev.serverId;
        if (msg.empty() || steamId.empty() || sid == 0) return;

        std::transform(msg.begin(), msg.end(), msg.begin(), ::tolower);
        while (!msg.empty() && (msg.back() == ' ' || msg.back() == '\t' || msg.back() == '\r' || msg.back() == '\n'))
            msg.pop_back();
        while (!msg.empty() && (msg.front() == ' ' || msg.front() == '\t'))
            msg.erase(msg.begin());

        auto cmds = getCommands(ctx.db);
        for (auto& cmd : cmds) {
            if (!cmd.enabled) continue;
            if (msg != cmd.trigger) continue;
            execute(cmd, ctx, sid, steamId, playerName);
            return;
        }
    }

private:
    struct ChatCmd {
        std::string name, trigger, action;
        int cost = 0, reward = 5;
        bool enabled = true;
    };

    std::vector<ChatCmd> getCommands(Database& db) {
        std::string raw = getSetting(db, "chat_commands", "");
        std::vector<ChatCmd> cmds;
        if (raw.empty() || raw[0] != '[') {
            cmds = {{"签到","qd","sign_in",0,5,true},{"抽奖","cj","lottery",0,0,true},
                    {"查询战绩","kd","query_kd",0,0,true},{"查询积分","jf","query_points",0,0,true},
                    {"兑换预留位","duihuan","redeem",50,0,true},{"跳边","tb","switch_team",20,0,true}};
            return cmds;
        }
        try {
            nlohmann::json arr = nlohmann::json::parse(raw);
            for (auto& j : arr) {
                ChatCmd c;
                c.name = j.value("name","");
                c.trigger = j.value("trigger","");
                c.action = j.value("action","");
                c.cost = j.value("cost",0);
                c.reward = j.value("reward",0);
                c.enabled = j.value("enabled",true);
                cmds.push_back(c);
            }
        } catch (...) {}
        return cmds;
    }

    void execute(const ChatCmd& cmd, PluginContext& ctx, int sid,
                 const std::string& steamId, const std::string& playerName) {
        if (cmd.action == "sign_in") doSignIn(ctx, sid, steamId, playerName, cmd.reward > 0 ? cmd.reward : 5);
        else if (cmd.action == "lottery") doLottery(ctx, sid, steamId, playerName);
        else if (cmd.action == "query_kd") doQueryKd(ctx, sid, steamId, playerName);
        else if (cmd.action == "query_points") doQueryPoints(ctx, sid, steamId, playerName);
        else if (cmd.action == "redeem") doRedeem(ctx, sid, steamId, playerName, cmd.cost > 0 ? cmd.cost : 50);
        else if (cmd.action == "switch_team") doSwitchTeam(ctx, sid, steamId, playerName, cmd.cost > 0 ? cmd.cost : 20);
    }

    int getBalance(Database& db, const std::string& steamId, int serverId) {
        auto bal = db.queryOne(
            "SELECT balance FROM points WHERE steamId=? AND (serverId=? OR serverId IS NULL) "
            "ORDER BY serverId DESC LIMIT 1", {steamId, std::to_string(serverId)});
        return safeStoi(bal["balance"]);
    }

    void addBalance(Database& db, int sid, const std::string& steamId,
                    const std::string& playerName, int amount, const std::string& reason) {
        auto existing = db.queryOne(
            "SELECT id FROM points WHERE steamId=? AND (serverId=? OR serverId IS NULL) "
            "ORDER BY serverId DESC LIMIT 1", {steamId, std::to_string(sid)});
        if (!existing.empty()) {
            db.exec("UPDATE points SET balance=balance+?, lifetimeEarned=lifetimeEarned+?, "
                     "lastUpdated=datetime('now') WHERE id=?",
                     {std::to_string(amount), std::to_string(amount), existing["id"]});
        } else {
            db.exec("INSERT INTO points (serverId,steamId,playerName,balance,lifetimeEarned,lastUpdated) "
                     "VALUES(?,?,?,?,?,datetime('now'))",
                     {std::to_string(sid), steamId, playerName,
                      std::to_string(std::max(amount, 0)), std::to_string(std::max(amount, 0))});
        }
        db.exec("INSERT INTO point_logs (serverId,steamId,playerName,amount,reason,operator,createdAt) "
                 "VALUES(?,?,?,?,?,?,datetime('now'))",
                 {std::to_string(sid), steamId, playerName, std::to_string(amount), reason, "系统-口令"});
    }

    void doSignIn(PluginContext& ctx, int sid, const std::string& steamId,
                  const std::string& playerName, int reward) {
        int cooldown = getSettingInt(ctx.db, "sign_in_cooldown", 86400);
        auto row = ctx.db.queryOne(
            "SELECT createdAt FROM point_logs WHERE steamId=? AND serverId=? AND reason='每日签到' "
            "ORDER BY createdAt DESC LIMIT 1", {steamId, std::to_string(sid)});
        if (!row.empty()) {
            auto check = ctx.db.queryOne(
                "SELECT CAST((julianday('now') - julianday(?)) * 86400 AS INTEGER) as elapsed",
                {row["createdAt"]});
            int elapsed = check.empty() ? cooldown : safeStoi(check["elapsed"]);
            if (elapsed < cooldown) {
                int remain = cooldown - elapsed;
                int h = remain / 3600, m = (remain % 3600) / 60, s = remain % 60;
                std::string wait;
                if (h > 0) wait = std::to_string(h) + "小时" + std::to_string(m) + "分";
                else if (m > 0) wait = std::to_string(m) + "分" + std::to_string(s) + "秒";
                else wait = std::to_string(s) + "秒";
                ctx.rcon.send(sid, "AdminWarn \"" + steamId + "\" 今日已签到，下次签到还需等待 " + wait);
                return;
            }
        }
        addBalance(ctx.db, sid, steamId, playerName, reward, "每日签到");
        ctx.rcon.send(sid, "AdminWarn \"" + steamId + "\" 签到成功！积分 +" + std::to_string(reward));
    }

    void doLottery(PluginContext& ctx, int sid, const std::string& steamId,
                   const std::string& playerName) {
        if (!getSettingBool(ctx.db, "lottery_enabled", false)) return;
        int cooldown = getSettingInt(ctx.db, "lottery_cooldown", 3600);
        int minR = getSettingInt(ctx.db, "lottery_min", 5);
        int maxR = getSettingInt(ctx.db, "lottery_max", 30);
        int cost = getSettingInt(ctx.db, "lottery_cost", 0);

        auto row = ctx.db.queryOne(
            "SELECT createdAt FROM point_logs WHERE steamId=? AND serverId=? AND reason LIKE '抽奖%' "
            "ORDER BY createdAt DESC LIMIT 1", {steamId, std::to_string(sid)});
        if (!row.empty()) {
            auto check = ctx.db.queryOne(
                "SELECT CAST((julianday('now') - julianday(?)) * 86400 AS INTEGER) as elapsed",
                {row["createdAt"]});
            int elapsed = check.empty() ? cooldown : safeStoi(check["elapsed"]);
            if (elapsed < cooldown) {
                int remain = cooldown - elapsed;
                int m = remain / 60, s = remain % 60;
                ctx.rcon.send(sid, "AdminWarn \"" + steamId + "\" 抽奖冷却中，还需等待 " +
                              std::to_string(m) + "分" + std::to_string(s) + "秒");
                return;
            }
        }
        if (cost > 0) {
            int balance = getBalance(ctx.db, steamId, sid);
            if (balance < cost) {
                ctx.rcon.send(sid, "AdminWarn \"" + steamId + "\" 积分不足！抽奖需要 " +
                              std::to_string(cost) + "，当前 " + std::to_string(balance));
                return;
            }
            addBalance(ctx.db, sid, steamId, playerName, -cost, "抽奖费用");
        }
        static std::mt19937 rng((unsigned)std::chrono::steady_clock::now().time_since_epoch().count());
        if (maxR < minR) maxR = minR;
        int reward = std::uniform_int_distribution<>(minR, maxR)(rng);
        addBalance(ctx.db, sid, steamId, playerName, reward, "抽奖 +" + std::to_string(reward));
        ctx.rcon.send(sid, "AdminWarn \"" + steamId + "\" 抽奖获得 " + std::to_string(reward) + " 积分！");
    }

    void doQueryKd(PluginContext& ctx, int sid, const std::string& steamId, const std::string&) {
        auto kills = ctx.db.queryOne("SELECT COUNT(*) as c FROM kills WHERE serverId=? AND killer=?",
                                      {std::to_string(sid), steamId});
        auto deaths = ctx.db.queryOne("SELECT COUNT(*) as c FROM kills WHERE serverId=? AND victim=?",
                                       {std::to_string(sid), steamId});
        int k = safeStoi(kills["c"]);
        int d = safeStoi(deaths["c"]);
        std::string ratio;
        if (d > 0) { char buf[32]; snprintf(buf, sizeof(buf), "%.2f", (double)k / d); ratio = buf; }
        else { ratio = k > 0 ? "inf" : "0"; }
        ctx.rcon.send(sid, "AdminWarn \"" + steamId + "\" 战绩: K=" + std::to_string(k) +
                      " D=" + std::to_string(d) + " K/D=" + ratio);
    }

    void doQueryPoints(PluginContext& ctx, int sid, const std::string& steamId, const std::string&) {
        int balance = getBalance(ctx.db, steamId, sid);
        ctx.rcon.send(sid, "AdminWarn \"" + steamId + "\" 当前积分: " + std::to_string(balance));
    }

    void doRedeem(PluginContext& ctx, int sid, const std::string& steamId,
                  const std::string& playerName, int cost) {
        int balance = getBalance(ctx.db, steamId, sid);
        if (balance < cost) {
            ctx.rcon.send(sid, "AdminWarn \"" + steamId + "\" 积分不足！兑换需要 " +
                          std::to_string(cost) + "，当前 " + std::to_string(balance));
            return;
        }
        auto existing = ctx.db.queryOne("SELECT id FROM reserved_slots WHERE serverId=? AND steamId=?",
                                         {std::to_string(sid), steamId});
        if (!existing.empty()) {
            ctx.rcon.send(sid, "AdminWarn \"" + steamId + "\" 你已有预留位，无需重复兑换");
            return;
        }
        addBalance(ctx.db, sid, steamId, playerName, -cost, "兑换预留位");
        int days = getSettingInt(ctx.db, "redeem_days", 0);
        if (days > 0) {
            ctx.db.exec("INSERT INTO reserved_slots (serverId,steamId,playerName,addedBy,expiresAt) "
                         "VALUES(?,?,?,? ,datetime('now', ? || ' days'))",
                         {std::to_string(sid), steamId, playerName, "系统-口令", std::to_string(days)});
        } else {
            ctx.db.exec("INSERT INTO reserved_slots (serverId,steamId,playerName,addedBy) VALUES(?,?,?,?)",
                         {std::to_string(sid), steamId, playerName, "系统-口令"});
        }
        ctx.rcon.send(sid, "AdminWarn \"" + steamId + "\" 兑换成功！预留位已添加，积分 -" + std::to_string(cost));
    }

    void doSwitchTeam(PluginContext& ctx, int sid, const std::string& steamId,
                      const std::string& playerName, int cost) {
        int balance = getBalance(ctx.db, steamId, sid);
        if (balance < cost) {
            ctx.rcon.send(sid, "AdminWarn \"" + steamId + "\" 积分不足！跳边需要 " +
                          std::to_string(cost) + "，当前 " + std::to_string(balance));
            return;
        }
        addBalance(ctx.db, sid, steamId, playerName, -cost, "跳边");
        std::string result = ctx.rcon.send(sid, "AdminForceTeamChange \"" + steamId + "\"");
        if (result.find("Not Found") != std::string::npos || result.find("Error") != std::string::npos) {
            ctx.rcon.send(sid, "AdminWarn \"" + steamId + "\" 积分已扣(-" + std::to_string(cost) +
                          ")，跳边执行失败，请联系管理员");
        } else {
            ctx.rcon.send(sid, "AdminWarn \"" + steamId + "\" 跳边成功！积分 -" + std::to_string(cost));
        }
    }
};

static ChatCommandsPlugin g_chatCommands;
static bool g_regCC = [](){ PluginManager::instance().registerPlugin(&g_chatCommands); return true; }();

} // namespace sp
