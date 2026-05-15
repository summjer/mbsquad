/**
 * chat-commands.js — 聊天口令插件 v3.3
 *
 * v3.3: 使用统一的 points-service.js
 */

const { addPoints, getBalance, deductPoints } = require('../src/lib/points-service');

module.exports = {
  name: 'ChatCommands',
  label: '聊天口令',
  desc: '玩家在聊天中输入口令兑换预留位或跳边',
  version: '3.2',

  onChat(event, { db, getRcon, log }) {
    const { serverId, steamId, playerName, message } = event;
    const sid = parseInt(serverId);
    const msg = (message || '').trim().toLowerCase();
    if (!msg || !steamId || !sid) return;

    const commands = _getCommands(db);
    for (const cmd of commands) {
      if (!cmd.enabled) continue;
      if (msg !== cmd.trigger.toLowerCase()) continue;
      _execute(cmd, db, getRcon, sid, steamId, playerName, log);
      return;
    }
  },
};

function _getCommands(db) {
  try {
    const row = db.prepare("SELECT value FROM settings WHERE key='chat_commands'").get();
    if (row) return JSON.parse(row.value);
  } catch {}
  return [
    { name: '签到', trigger: 'qd', action: 'sign_in', cost: 0, reward: 5, enabled: true },
    { name: '抽奖', trigger: 'cj', action: 'lottery', cost: 0, enabled: true },
    { name: '查询战绩', trigger: 'kd', action: 'query_kd', cost: 0, enabled: true },
    { name: '查询积分', trigger: 'jf', action: 'query_points', cost: 0, enabled: true },
    { name: '兑换预留位', trigger: 'duihuan', action: 'redeem', cost: 50, enabled: true },
    { name: '跳边', trigger: 'tb', action: 'switch_team', cost: 20, enabled: true },
  ];
}

function _getSetting(db, key, fallback) {
  try {
    const row = db.prepare('SELECT value FROM settings WHERE key=?').get(key);
    if (row) return JSON.parse(row.value);
  } catch {}
  return fallback;
}

// RCON 私信（失败记日志）
async function _warn(getRcon, serverId, steamId, message) {
  try {
    const rcon = await getRcon(serverId);
    await rcon.exec(`AdminWarn "${steamId}" ${message}`);
  } catch (e) {
    console.warn(`[口令] AdminWarn 失败 (server=${serverId}, steam=${steamId}): ${e.message}`);
  }
}

function _execute(cmd, db, getRcon, sid, steamId, playerName, log) {
  switch (cmd.action) {
    case 'sign_in':      return _signIn(db, getRcon, sid, steamId, playerName, cmd.reward || 5, log);
    case 'lottery':      return _lottery(db, getRcon, sid, steamId, playerName, log);
    case 'query_kd':     return _queryKd(db, getRcon, sid, steamId, playerName, log);
    case 'query_points': return _queryPoints(db, getRcon, sid, steamId, playerName, log);
    case 'redeem':       return _redeem(db, getRcon, sid, steamId, playerName, cmd.cost || 50, log);
    case 'switch_team':  return _switchTeam(db, getRcon, sid, steamId, playerName, cmd.cost || 20, log);
  }
}

// 签到
function _signIn(db, getRcon, sid, steamId, playerName, reward, log) {
  try {
    const cooldown = parseInt(_getSetting(db, 'sign_in_cooldown', 86400));
    const row = db.prepare(
      "SELECT createdAt FROM point_logs WHERE steamId=? AND serverId=? AND reason='每日签到' ORDER BY createdAt DESC LIMIT 1"
    ).get(steamId, sid);
    if (row) {
      const elapsed = (Date.now() - new Date(row.createdAt + 'Z').getTime()) / 1000;
      if (elapsed < cooldown) {
        const remain = Math.ceil(cooldown - elapsed);
        const h = Math.floor(remain / 3600);
        const m = Math.floor((remain % 3600) / 60);
        const s = remain % 60;
        let wait = '';
        if (h > 0) wait = h + '小时' + m + '分';
        else if (m > 0) wait = m + '分' + s + '秒';
        else wait = s + '秒';
        log('[口令] ' + playerName + ' 签到冷却中，还需等待 ' + wait);
        _warn(getRcon, sid, steamId, '今日已签到，下次签到还需等待 ' + wait);
        return;
      }
    }
    addPoints(db, sid, steamId, playerName, reward, '每日签到', '系统-口令');
    log('[口令] ' + playerName + ' 签到成功，+' + reward + '积分');
    _warn(getRcon, sid, steamId, '签到成功！积分 +' + reward);
  } catch (e) { console.error('[口令] 签到处理异常:', e.message); }
}

// 抽奖
function _lottery(db, getRcon, sid, steamId, playerName, log) {
  try {
    const enabled = _getSetting(db, 'lottery_enabled', 1);
    if (!enabled) { log('[口令] ' + playerName + ' 抽奖功能未开启'); return; }

    const cooldown = parseInt(_getSetting(db, 'lottery_cooldown', 3600));
    const row = db.prepare(
      "SELECT createdAt FROM point_logs WHERE steamId=? AND serverId=? AND reason LIKE '抽奖%' ORDER BY createdAt DESC LIMIT 1"
    ).get(steamId, sid);
    if (row) {
      const elapsed = (Date.now() - new Date(row.createdAt + 'Z').getTime()) / 1000;
      if (elapsed < cooldown) {
        const remain = Math.ceil(cooldown - elapsed);
        const m = Math.floor(remain / 60);
        const s = remain % 60;
        let wait = m > 0 ? m + '分' + s + '秒' : s + '秒';
        log('[口令] ' + playerName + ' 抽奖冷却中，还需等待 ' + wait);
        _warn(getRcon, sid, steamId, '抽奖冷却中，还需等待 ' + wait);
        return;
      }
    }

    const min = parseInt(_getSetting(db, 'lottery_min', 5));
    const max = parseInt(_getSetting(db, 'lottery_max', 30));
    const cost = parseInt(_getSetting(db, 'lottery_cost', 0));

    if (cost > 0) {
      if (!deductPoints(db, sid, steamId, playerName, cost, '抽奖费用', '系统-抽奖')) {
        const p = getBalance(db, steamId);
        const have = p ? p.balance : 0;
        log('[口令] ' + playerName + ' 积分不足，抽奖需要 ' + cost + ' 积分');
        _warn(getRcon, sid, steamId, '积分不足！抽奖需要 ' + cost + '，当前 ' + have);
        return;
      }
    }

    const reward = Math.floor(Math.random() * (max - min + 1)) + min;
    addPoints(db, sid, steamId, playerName, reward, '抽奖 +' + reward, '系统-口令');
    log('[口令] ' + playerName + ' 抽奖获得 ' + reward + ' 积分！');
    _warn(getRcon, sid, steamId, '抽奖获得 ' + reward + ' 积分！');
  } catch (e) { console.error('[口令] 抽奖处理异常:', e.message); }
}

// 查询战绩
function _queryKd(db, getRcon, sid, steamId, playerName, log) {
  try {
    const kills = db.prepare('SELECT COUNT(*) as c FROM kills WHERE serverId=? AND killer=?').get(sid, steamId);
    const deaths = db.prepare('SELECT COUNT(*) as c FROM kills WHERE serverId=? AND victim=?').get(sid, steamId);
    const k = kills ? kills.c : 0;
    const d = deaths ? deaths.c : 0;
    const ratio = d > 0 ? (k / d).toFixed(2) : k > 0 ? '∞' : '0';
    log('[口令] ' + playerName + ' 战绩: K=' + k + ' D=' + d + ' K/D=' + ratio);
    _warn(getRcon, sid, steamId, '战绩: K=' + k + ' D=' + d + ' K/D=' + ratio);
  } catch (e) { console.error('[口令] 战绩查询异常:', e.message); }
}

// 查询积分
function _queryPoints(db, getRcon, sid, steamId, playerName, log) {
  try {
    const p = getBalance(db, steamId);
    const balance = p ? p.balance : 0;
    log('[口令] ' + playerName + ' 当前积分: ' + balance);
    _warn(getRcon, sid, steamId, '当前积分: ' + balance);
  } catch (e) { console.error('[口令] 积分查询异常:', e.message); }
}

// 兑换预留位
function _redeem(db, getRcon, sid, steamId, playerName, cost, log) {
  try {
    const p = getBalance(db, steamId);
    if (!p || p.balance < cost) {
      const have = p ? p.balance : 0;
      log('[口令] ' + playerName + ' 积分不足(需' + cost + '，当前' + have + ')');
      _warn(getRcon, sid, steamId, '积分不足！兑换需要 ' + cost + '，当前 ' + have);
      return;
    }
    const existing = db.prepare('SELECT id FROM reserved_slots WHERE serverId=? AND steamId=?').get(sid, steamId);
    if (existing) {
      log('[口令] ' + playerName + ' 已有预留位');
      _warn(getRcon, sid, steamId, '你已有预留位，无需重复兑换');
      return;
    }
    deductPoints(db, sid, steamId, playerName, cost, '兑换预留位', '系统-口令');
    const redeemDays = parseInt(_getSetting(db, 'redeem_days', 0));
    let expiresAt = null;
    if (redeemDays > 0) {
      const d = new Date();
      d.setDate(d.getDate() + redeemDays);
      expiresAt = d.toISOString().replace('T', ' ').substring(0, 19);
    }
    db.prepare('INSERT INTO reserved_slots (serverId,steamId,playerName,addedBy,expiresAt) VALUES(?,?,?,?,?)')
      .run(sid, steamId, playerName, '系统-口令', expiresAt);
    log('[口令] ' + playerName + ' 兑换预留位成功，-' + cost + '积分');
    _warn(getRcon, sid, steamId, '兑换成功！预留位已添加，积分 -' + cost);
  } catch (e) { console.error('[口令] 兑换处理异常:', e.message); }
}

// 跳边
async function _switchTeam(db, getRcon, sid, steamId, playerName, cost, log) {
  try {
    const p = getBalance(db, steamId);
    if (!p || p.balance < cost) {
      const have = p ? p.balance : 0;
      log('[口令] ' + playerName + ' 积分不足(需' + cost + '，当前' + have + ')');
      _warn(getRcon, sid, steamId, '积分不足！跳边需要 ' + cost + '，当前 ' + have);
      return;
    }
    deductPoints(db, sid, steamId, playerName, cost, '跳边', '系统-口令');
    try {
      const c = await getRcon(sid);
      const raw = await c.exec('ListPlayers');
      for (const line of raw.split('\n')) {
        const sm = line.match(/steam:\s*(\d{17})/i);
        const idM = line.match(/^ID:\s*(\d+)/i);
        if (sm && sm[1] === steamId && idM) {
          await c.exec('AdminForceTeamChange "' + sm[1] + '"');
          log('[口令] ' + playerName + ' 跳边成功，-' + cost + '积分');
          _warn(getRcon, sid, steamId, '跳边成功！积分 -' + cost);
          return;
        }
      }
      // 玩家不在在线列表
      log('[口令] ' + playerName + ' 扣分成功，未找到在线玩家');
      _warn(getRcon, sid, steamId, '积分已扣(-' + cost + ')，但未找到你的在线记录，请确认是否在游戏中');
    } catch (e) {
      log('[口令] ' + playerName + ' 扣分成功，RCON失败: ' + e.message);
      _warn(getRcon, sid, steamId, '积分已扣(-' + cost + ')，跳边执行失败，请联系管理员');
    }
  } catch (e) { console.error('[口令] 跳边处理异常:', e.message); }
}
