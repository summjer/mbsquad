/**
 * revive-points.js — 救援积分插件 v1.3
 * v1.3: 使用统一的 points-service.js
 */

const { addPoints } = require('../src/lib/points-service');

module.exports = {
  name: 'RevivePoints',
  label: '救援积分',
  desc: '救援队友获得积分奖励',
  version: '1.3',

  onRevive(event, { db, getRcon, log }) {
    const { serverId, reviverSteamId, reviverName, revivedSteamId } = event;
    if (!serverId || !reviverSteamId) return;
    const sid = parseInt(serverId);

    const reward = _getSettingInt(db, 'revive_reward', 3);

    try {
      addPoints(db, sid, reviverSteamId, reviverName, reward, '救援队友', '系统-插件');

      // 通知救援者
      if (reviverSteamId) {
        _warn(getRcon, sid, reviverSteamId, '你救援了队友，积分 +' + reward);
      }
    } catch (e) {
      console.error('[RevivePoints] 积分处理错误:', e.message);
    }
  },
};

function _getSettingInt(db, key, defaultVal) {
  try {
    const row = db.prepare('SELECT value FROM settings WHERE key=?').get(key);
    if (row) return parseInt(JSON.parse(row.value));
  } catch {}
  return defaultVal;
}

async function _warn(getRcon, serverId, steamId, message) {
  try {
    const rcon = await getRcon(serverId);
    await rcon.exec('AdminWarn "' + steamId + '" ' + message);
  } catch (e) {
    console.warn('[RevivePoints] AdminWarn 失败 (server=' + serverId + ', steam=' + steamId + '): ' + e.message);
  }
}
