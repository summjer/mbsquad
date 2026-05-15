/**
 * timed-broadcast.js — 定时广播插件
 * 
 * 按配置的间隔自动循环发送广播消息。
 * 由 server.js 中的定时器驱动 tick() 调用。
 */
module.exports = {
  name: 'TimedBroadcast',
  label: '定时广播',
  desc: '按间隔循环发送全服广播消息',
  version: '1.0',

  init({ db }) {
    this._index = 0; // 当前消息索引
    this._lastTick = 0;
  },

  // 由外部定时器调用
  async tick({ db, getRcon, log }) {
    try {
      const enabled = parseInt(_getSetting(db, 'broadcast_enabled', 0));
      if (!enabled) return;

      const interval = parseInt(_getSetting(db, 'broadcast_interval', 300)) * 1000; // 秒→毫秒
      const now = Date.now();
      if (now - this._lastTick < interval) return;
      this._lastTick = now;

      let messages = _getSetting(db, 'broadcast_messages', []);
      if (typeof messages === 'string') { try { messages = JSON.parse(messages); } catch { messages = []; } }
      if (!messages.length) return;

      // 获取目标服务器
      let servers = _getSetting(db, 'broadcast_servers', 'all');
      if (typeof servers === 'string' && servers !== 'all') { try { servers = JSON.parse(servers); } catch { servers = 'all'; } }

      const msg = messages[this._index % messages.length];
      this._index++;

      // 获取需要广播的服务器列表
      let serverIds = [];
      if (servers === 'all') {
        const rows = db.prepare('SELECT id FROM servers').all();
        serverIds = rows.map(r => r.id);
      } else if (Array.isArray(servers)) {
        serverIds = servers;
      }

      for (const sid of serverIds) {
        try {
          const rcon = await getRcon(sid);
          await rcon.exec('AdminBroadcast "' + msg + '"');
        } catch (e) {
          log('[定时广播] 服务器' + sid + ' 发送失败: ' + e.message);
        }
      }
      log('[定时广播] 已发送: ' + msg);
    } catch (e) {
      console.warn('[定时广播] 异常:', e.message);
    }
  },
};

function _getSetting(db, key, fallback) {
  try {
    const row = db.prepare('SELECT value FROM settings WHERE key=?').get(key);
    if (row) {
      try { return JSON.parse(row.value); } catch { return row.value; }
    }
  } catch {}
  return fallback;
}
