/**
 * player-sync.js — 玩家同步插件
 * 
 * 功能：同步玩家列表到数据库
 * 事件：onPlayerList
 */

module.exports = {
  name: 'PlayerSync',
  label: '玩家同步',
  desc: '在线玩家列表同步到面板数据库',
  version: '1.0',

  onPlayerList(event, { db }) {
    const { serverId, raw } = event;
    if (!serverId || !raw) return;
    const sid = parseInt(serverId);

    const lines = raw.split('\n').filter(Boolean);
    let count = 0;

    for (const line of lines) {
      const steamM = line.match(/SteamID:\s*(\d+)/i);
      const nameM = line.match(/Name:\s*([^|]+)/i);
      if (steamM && nameM) {
        const cleanName = nameM[1].trim().replace(/\[.*?\]/g, '').trim();
        try {
          db.prepare(
            "INSERT OR REPLACE INTO players (serverId,steamId,name,playtime,firstSeen,lastSeen) VALUES(?,?,?,COALESCE((SELECT playtime FROM players WHERE serverId=? AND steamId=?),0),COALESCE((SELECT firstSeen FROM players WHERE serverId=? AND steamId=?),datetime('now')),datetime('now'))"
          ).run(sid, steamM[1], cleanName, sid, steamM[1], sid, steamM[1]);
          count++;
        } catch (e) {
          console.error('[PlayerSync] 同步失败:', e.message);
        }
      }
    }
  },
};
