/**
 * welcome.js — 进服欢迎插件
 * 
 * 玩家加入服务器时发送自定义欢迎语。
 * 支持变量: {player} = 玩家名
 */
module.exports = {
  name: 'Welcome',
  label: '进服欢迎',
  desc: '玩家加入服务器时发送自定义欢迎语',
  version: '1.0',

  async onJoin(event, { db, getRcon, log }) {
    try {
      const enabled = parseInt(_getSetting(db, 'welcome_enabled', 0));
      if (!enabled) return;
      if (!event.steamId) return;

      const msg = _getSetting(db, 'welcome_message', '欢迎 {player} 加入服务器！');
      const text = msg.replace(/{player}/g, event.playerName || '玩家');

      const rcon = await getRcon(event.serverId);
      await rcon.exec('AdminWarn "' + event.steamId + '" ' + text);
      log('[进服欢迎] ' + (event.playerName || event.steamId) + ': ' + text);
    } catch (e) {
      console.warn('[进服欢迎] 发送失败:', e.message);
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
