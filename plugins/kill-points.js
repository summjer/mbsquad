/**
 * kill-points.js — 击杀积分插件 v1.5
 * v1.5: 新增自杀判定（type==='suicide'），只扣分不加分
 * v1.4: 使用统一的 points-service.js
 */

const { addPoints } = require('../src/lib/points-service');

module.exports = {
  name: 'KillPoints',
  label: '击杀积分',
  desc: '击杀加分、阵亡扣分、TK扣分、自杀扣分',
  version: '1.5',

  defaultConfig() {
    return { kill_reward: 3, death_penalty: 2, tk_penalty: 5 };
  },

  onKill(event, { db, getRcon, log }) {
    const { serverId, killerSteamId, killerName, victimSteamId, victimName, type } = event;
    const sid = parseInt(serverId);

    const killReward = _getSettingInt(db, 'kill_reward', 3);
    const deathPenalty = _getSettingInt(db, 'death_penalty', 2);
    const tkPenalty = _getSettingInt(db, 'tk_penalty', 5);
    const weapon = event.weapon || '未知武器';

    if (type === 'suicide') {
      // 自杀：只扣阵亡分，不加击杀分
      if (killerSteamId) {
        addPoints(db, sid, killerSteamId, killerName, -deathPenalty, '自杀', '系统-插件');
        _warnPlayer(getRcon, sid, killerSteamId,
          '你自杀身亡，积分 -' + deathPenalty);
      }
    } else if (type === 'teamkill') {
      // TK 扣分（通知由 tk-forgive 插件负责）
      if (killerSteamId) {
        addPoints(db, sid, killerSteamId, killerName, -tkPenalty, '友军击杀', '系统-插件');
      }
    } else {
      // 击杀者加分 + 被杀者扣分 + 通知双方
      if (killerSteamId) {
        addPoints(db, sid, killerSteamId, killerName, killReward, '击杀', '系统-插件');
        _warnPlayer(getRcon, sid, killerSteamId,
          '你使用 ' + weapon + ' 击杀了 ' + (victimName || '未知') + '，积分 +' + killReward);
      }
      if (victimSteamId) {
        addPoints(db, sid, victimSteamId, victimName, -deathPenalty, '阵亡', '系统-插件');
        _warnPlayer(getRcon, sid, victimSteamId,
          '你被 ' + (killerName || '未知') + ' 使用 ' + weapon + ' 击杀，积分 -' + deathPenalty);
      }
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

async function _warnPlayer(getRcon, serverId, steamId, message) {
  try {
    const rcon = await getRcon(serverId);
    await rcon.exec('AdminWarn "' + steamId + '" ' + message);
  } catch (e) {
    console.warn('[KillPoints] AdminWarn 失败 (server=' + serverId + ', steam=' + steamId + '): ' + e.message);
  }
}
