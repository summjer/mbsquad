/**
 * switch-lock.js — 防跳边冷却插件
 * 打乱（Scramble）后锁定所有玩家跳边权限，防止被打过去的玩家立刻跳回来。
 */

module.exports = {
  name: 'SwitchLock',
  label: '防跳边锁定',
  desc: '打乱后锁定跳边权限，防止立刻跳回',
  version: '1.1',

  onScramble(ev, { db, log }) {
    const sid = ev.serverId;
    if (!sid) return;

    const duration = _getSettingInt(db, 'switch_lock_minutes', 20);
    const lockedUntil = new Date(Date.now() + duration * 60 * 1000).toISOString();
    try {
      db.prepare(`DELETE FROM switch_locks WHERE serverId=?`).run(sid);
      log('[SwitchLock] 打乱完成，锁定 ' + sid + ' 号服务器所有玩家 ' + duration + ' 分钟');
      db.prepare(
        `INSERT INTO switch_locks (serverId, steamId, lockedUntil, reason) VALUES (?, 'ALL', ?, ?)`
      ).run(sid, lockedUntil, 'scramble_lock_' + sid);
    } catch(e) {
      log('[SwitchLock] 记录锁定失败: ' + e.message);
    }
  },

  onSwitchTeamRequest(ev, { db, log }) {
    const sid = ev.serverId;
    if (!sid) return;

    try {
      const lock = db.prepare(
        `SELECT lockedUntil FROM switch_locks WHERE serverId=? AND lockedUntil > datetime('now') LIMIT 1`
      ).get(sid);

      if (lock) {
        const remaining = Math.ceil((new Date(lock.lockedUntil) - new Date()) / 60000);
        if (remaining > 0) {
          ev.rejected = true;
          ev.reason = 'Scramble lock: ' + remaining + 'min remaining';
          log('[SwitchLock] 拒绝 ' + (ev.steamId || '?') + ' 跳边，剩余 ' + remaining + ' 分钟');
        }
      }
    } catch(e) {}
  }
};

function _getSettingInt(db, key, defaultVal) {
  try {
    const row = db.prepare('SELECT value FROM settings WHERE key=?').get(key);
    if (row) {
      const v = parseInt(row.value);
      if (!isNaN(v)) return v;
    }
  } catch {}
  return defaultVal;
}
