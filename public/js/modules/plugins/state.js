// modules/plugins/state.js — 插件模块私有状态

export const PLUGIN_SCHEMAS = {
  ChatCommands: [
    { key: "redeem_code",  label: "兑换口令",       type: "text",   default: "duihuan" },
    { key: "redeem_cost",  label: "兑换消耗积分",   type: "number", default: 50 },
    { key: "redeem_days",  label: "预留位有效期（天）", type: "number", default: 0 },
    { key: "tb_code",      label: "跳边口令",       type: "text",   default: "tb" },
    { key: "tb_cost",      label: "跳边扣分",       type: "number", default: 20 },
    { key: "sign_in_cooldown", label: "签到冷却（秒）", type: "number", default: 86400 },
    { key: "lottery_enabled",  label: "启用抽奖",       type: "bool",   default: true },
    { key: "lottery_cooldown", label: "抽奖冷却（秒）", type: "number", default: 3600 },
    { key: "lottery_cost",     label: "抽奖费用",       type: "number", default: 0 },
    { key: "lottery_min",      label: "最低奖励",       type: "number", default: 5 },
    { key: "lottery_max",      label: "最高奖励",       type: "number", default: 30 },
  ],
  KillPoints: [
    { key: "kill_reward",    label: "击杀加分",   type: "number", default: 3 },
    { key: "death_penalty",  label: "阵亡扣分",   type: "number", default: 2 },
    { key: "tk_penalty",     label: "TK 扣分",    type: "number", default: 5 },
  ],
  PlayerSync: [],
  RevivePoints: [
    { key: "revive_reward", label: "救援加分", type: "number", default: 3 },
  ],
  TKForgive: [
    { key: "tk_forgive_enabled",   label: "启用 TK 道歉",     type: "bool",   default: true },
    { key: "tk_forgive_seconds",   label: "道歉超时（秒）",    type: "number", default: 180 },
    { key: "tk_forgive_keywords",  label: "道歉关键词",        type: "text",   default: "sor,sorry,soy" },
    { key: "tk_forgive_kick_msg",  label: "踢出提示消息",      type: "text",   default: "TK超时未道歉，自动踢出" },
    { key: "tk_check_interval",    label: "TK检查间隔（毫秒）", type: "number", default: 15000 },
  ],
  Welcome: [
    { key: "welcome_enabled",   label: "启用进服欢迎",  type: "bool",   default: false },
    { key: "welcome_message",   label: "欢迎消息",      type: "text",   default: "欢迎 {player} 加入服务器！" },
  ],
  TeamBalance: [
    { key: "team_balance_enabled",    label: "启用队伍平衡",  type: "bool",   default: false },
    { key: "team_balance_threshold",  label: "人数差阈值",    type: "number", default: 3 },
  ],
  SwitchLock: [
    { key: "switch_lock_minutes",  label: "锁定时长（分钟）", type: "number", default: 20 },
  ],
  TimedBroadcast: [
    { key: "broadcast_enabled",   label: "启用定时广播",      type: "bool",   default: false },
    { key: "broadcast_interval",  label: "广播间隔（秒）",    type: "number", default: 300 },
  ],
  CDKRedeem: [
    { key: "cdk_enabled",  label: "启用 CDK 兑换",  type: "bool",   default: false },
    { key: "cdk_prefix",   label: "聊天触发前缀",    type: "text",   default: "cdk" },
  ],
};
