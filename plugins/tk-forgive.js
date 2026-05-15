/**
 * tk-forgive.js — TK 道歉惩罚插件 v1.1
 * v1.1: 改进警告措辞，AdminWarn 失败记日志
 */

module.exports = {
  name: 'TKForgive',
  label: 'TK 道歉',
  desc: 'TK自动惩罚，聊天道歉免踢，超时自动踢出',
  version: '1.1',

  onKill(event, { db, getRcon, log }) {
    if (event.type !== 'teamkill' || !event.killerSteamId) return;

    const sid = parseInt(event.serverId);
    const enabled = _getSettingBool(db, 'tk_forgive_enabled', true);
    if (!enabled) return;

    const secs = _getSettingInt(db, 'tk_forgive_seconds', 180);
    const expiresAt = new Date(Date.now() + secs * 1000).toISOString();

    try {
      db.prepare(
        'INSERT INTO tk_forgive (serverId,killerSteamId,killerName,victimSteamId,victimName,expiresAt) VALUES(?,?,?,?,?,?)'
      ).run(sid, event.killerSteamId, event.killerName || null, event.victimSteamId, event.victimName || null, expiresAt);

      const keywords = _getKeywords(db);
      const kwStr = keywords.join('/');
      const tkPenalty = _getSettingInt(db, 'tk_penalty', 5);
      _warnKiller(getRcon, sid, event.killerSteamId, event.victimName || '队友',
        '你TK了' + (event.victimName || '队友') + '，' + secs + '秒内输入 ' + kwStr + ' 道歉，否则自动踢出。积分 -' + tkPenalty);
    } catch (e) {
      console.error('[TKForgive] 创建记录失败:', e.message);
    }
  },

  onChat(event, { db, getRcon, log }) {
    const enabled = _getSettingBool(db, 'tk_forgive_enabled', true);
    if (!enabled) return;

    const { serverId, steamId, message } = event;
    const sid = parseInt(serverId);

    const keywords = _getKeywords(db);
    const msgLower = (message || '').toLowerCase();
    const matched = keywords.some(kw => msgLower.includes(kw));
    if (!matched) return;

    try {
      const activeTK = db.prepare(
        "SELECT * FROM tk_forgive WHERE serverId=? AND killerSteamId=? AND forgiven=0 AND kicked=0 AND expiresAt > datetime('now')"
      ).all(sid, steamId);

      for (const tk of activeTK) {
        db.prepare('UPDATE tk_forgive SET forgiven=1 WHERE id=?').run(tk.id);
        log('[TKForgive] ' + tk.killerName + ' (' + tk.killerSteamId + ') 已通过聊天 "' + message + '" 道歉');
        _warnKiller(getRcon, sid, tk.killerSteamId, tk.victimName, '你的TK已被原谅，注意友军伤害！');
      }
    } catch (e) {
      console.error('[TKForgive] 道歉检测错误:', e.message);
    }
  },
};

function _getSettingBool(db, key, defaultVal) {
  try {
    const row = db.prepare('SELECT value FROM settings WHERE key=?').get(key);
    if (row) return JSON.parse(row.value) !== 0;
  } catch {}
  return defaultVal;
}

function _getSettingInt(db, key, defaultVal) {
  try {
    const row = db.prepare('SELECT value FROM settings WHERE key=?').get(key);
    if (row) return parseInt(JSON.parse(row.value));
  } catch {}
  return defaultVal;
}

function _getKeywords(db) {
  try {
    const row = db.prepare("SELECT value FROM settings WHERE key='tk_forgive_keywords'").get();
    if (row) return JSON.parse(row.value).split(',').map(k => k.trim().toLowerCase()).filter(Boolean);
  } catch {}
  return ['sor', 'sorry', 'soy'];
}

async function _warnKiller(getRcon, serverId, steamId, victimName, message) {
  try {
    const rcon = await getRcon(serverId);
    await rcon.exec('AdminWarn "' + steamId + '" ' + message);
  } catch (e) {
    console.warn('[TKForgive] AdminWarn 失败 (server=' + serverId + ', steam=' + steamId + '): ' + e.message);
  }
}
