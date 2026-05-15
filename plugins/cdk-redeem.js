/**
 * cdk-redeem.js — CDK 激活码插件
 * 
 * 玩家在聊天输入兑换口令（如 "cdk CODE"）来激活 CDK 码，获得积分或预留位。
 */
module.exports = {
  name: 'CDKRedeem',
  label: 'CDK激活码',
  desc: '生成激活码，玩家聊天输入兑换积分或预留位',
  version: '1.0',

  async onChat(event, { db, getRcon, log }) {
    try {
      const enabled = parseInt(_getSetting(db, 'cdk_enabled', 0));
      if (!enabled) return;

      const prefix = _getSetting(db, 'cdk_prefix', 'cdk');
      const msg = (event.message || '').trim();

      // 匹配格式: "cdk CODE" 或 "CDK CODE"
      const re = new RegExp('^' + prefix.replace(/[.*+?^${}()|[\]\\]/g, '\\$&') + '\\s+(\\S+)', 'i');
      const match = msg.match(re);
      if (!match) return;

      const code = match[1].toUpperCase();
      const sid = event.serverId;
      const steamId = event.steamId;
      const playerName = event.playerName;

      // 查找 CDK
      const cdk = db.prepare('SELECT * FROM cdk_codes WHERE code=?').get(code);
      if (!cdk) {
        log('[CDK] ' + playerName + ' 输入无效码: ' + code);
        await _warn(getRcon, sid, steamId, '激活码不存在或已失效');
        return;
      }

      // 检查过期
      if (cdk.expiresAt && new Date(cdk.expiresAt) < new Date()) {
        await _warn(getRcon, sid, steamId, '该激活码已过期');
        return;
      }

      // 检查使用次数
      if (cdk.usedCount >= cdk.maxUses) {
        await _warn(getRcon, sid, steamId, '该激活码已被使用完毕');
        return;
      }

      // 检查服务器限制
      if (cdk.serverId && cdk.serverId !== sid) {
        await _warn(getRcon, sid, steamId, '该激活码不适用于当前服务器');
        return;
      }

      // 检查是否已使用过
      const used = db.prepare('SELECT id FROM cdk_logs WHERE code=? AND steamId=?').get(code, steamId);
      if (used) {
        await _warn(getRcon, sid, steamId, '你已经使用过该激活码了');
        return;
      }

      // 发放奖励
      if (cdk.rewardType === 'points') {
        // 积分奖励
        const existing = db.prepare('SELECT id, balance FROM points WHERE steamId=? AND (serverId=? OR serverId IS NULL) ORDER BY serverId DESC LIMIT 1').get(steamId, sid);
        if (existing) {
          db.prepare("UPDATE points SET balance=balance+?, lifetimeEarned=lifetimeEarned+?, lastUpdated=datetime('now') WHERE id=?")
            .run(cdk.rewardValue, cdk.rewardValue, existing.id);
        } else {
          db.prepare("INSERT INTO points (serverId, steamId, playerName, balance, lifetimeEarned, lastUpdated) VALUES (?, ?, ?, ?, ?, datetime('now'))")
            .run(sid, steamId, playerName, cdk.rewardValue, cdk.rewardValue);
        }
        db.prepare("INSERT INTO point_logs (serverId, steamId, playerName, amount, reason, operator) VALUES (?, ?, ?, ?, ?, ?)")
          .run(sid, steamId, playerName, cdk.rewardValue, 'CDK兑换:' + code, '系统-CDK');
      } else if (cdk.rewardType === 'reserved') {
        // 预留位奖励
        const existing = db.prepare('SELECT id FROM reserved_slots WHERE serverId=? AND steamId=?').get(sid, steamId);
        if (!existing) {
          let expiresAt = null;
          if (cdk.rewardValue > 0) {
            const d = new Date();
            d.setDate(d.getDate() + cdk.rewardValue);
            expiresAt = d.toISOString().replace('T', ' ').substring(0, 19);
          }
          db.prepare("INSERT INTO reserved_slots (serverId, steamId, playerName, addedBy, expiresAt) VALUES (?, ?, ?, ?, ?)")
            .run(sid, steamId, playerName, '系统-CDK', expiresAt);
        }
      }

      // 记录使用
      db.prepare("UPDATE cdk_codes SET usedCount=usedCount+1 WHERE id=?").run(cdk.id);
      db.prepare("INSERT INTO cdk_logs (code, serverId, steamId, playerName, rewardType, rewardValue) VALUES (?, ?, ?, ?, ?, ?)")
        .run(code, sid, steamId, playerName, cdk.rewardType, cdk.rewardValue);

      const rewardText = cdk.rewardType === 'points' ? cdk.rewardValue + '积分' : '预留位(' + cdk.rewardValue + '天)';
      log('[CDK] ' + playerName + ' 兑换 ' + code + ' 成功: ' + rewardText);
      await _warn(getRcon, sid, steamId, '激活码兑换成功！获得 ' + rewardText);
    } catch (e) {
      console.warn('[CDK] 异常:', e.message);
    }
  },
};

async function _warn(getRcon, serverId, steamId, msg) {
  try {
    const rcon = await getRcon(serverId);
    await rcon.exec('AdminWarn "' + steamId + '" ' + msg);
  } catch {}
}

function _getSetting(db, key, fallback) {
  try {
    const row = db.prepare('SELECT value FROM settings WHERE key=?').get(key);
    if (row) {
      try { return JSON.parse(row.value); } catch { return row.value; }
    }
  } catch {}
  return fallback;
}
