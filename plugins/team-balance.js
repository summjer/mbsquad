/**
 * team-balance.js — 队伍平衡插件
 * 
 * 每次收到玩家列表时检查双方人数差，超过阈值自动将多余玩家跳到对面。
 * 只在游戏进行中生效，通过 RCON ListPlayers 获取实时数据。
 */
module.exports = {
  name: 'TeamBalance',
  label: '队伍平衡',
  desc: '双方人数差超过阈值自动跳边平衡',
  version: '1.0',

  async onPlayerList(event, { db, getRcon, log }) {
    try {
      const enabled = parseInt(_getSetting(db, 'team_balance_enabled', 0));
      if (!enabled) return;

      const threshold = parseInt(_getSetting(db, 'team_balance_threshold', 3));
      const { serverId, players } = event;
      if (!players || players.length < 4) return;

      const team1 = players.filter(p => p.teamIndex === 1);
      const team2 = players.filter(p => p.teamIndex === 2);
      const diff = Math.abs(team1.length - team2.length);

      if (diff < threshold) return;

      // 从人多的一方跳到人少的一方
      const biggerTeam = team1.length > team2.length ? team1 : team2;
      const smallerTeam = team1.length > team2.length ? team2 : team1;
      const toMove = Math.floor(diff / 2);

      log('[队伍平衡] 差距=' + diff + ' 阈值=' + threshold + ' 需跳边' + toMove + '人');

      // 从没有小队的玩家开始跳（优先跳散人）— 每条命令新建连接（Squad 每连接只支持一条命令）
      const unsorted = biggerTeam.filter(p => !p.squadId || p.squadId === '-1' || p.squadId === 'N/A');
      const sorted = biggerTeam.filter(p => p.squadId && p.squadId !== '-1' && p.squadId !== 'N/A');
      const toMoveList = [...unsorted, ...sorted].slice(0, toMove);

      for (const p of toMoveList) {
        try {
          const rc = await getRcon(serverId);
          await rc.exec('AdminForceTeamChange "' + p.steamId + '"');
          log('[队伍平衡] 跳边: ' + p.name + ' (ID:' + p.rconId + ')');
        } catch (e) {
          log('[队伍平衡] 跳边失败: ' + p.name + ' - ' + e.message);
        }
        await new Promise(r => setTimeout(r, 500));
      }
    } catch (e) {
      console.warn('[队伍平衡] 处理异常:', e.message);
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
