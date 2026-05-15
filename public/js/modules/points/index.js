 
// ─── points.js — 积分系统 ───


let ctx;
export function init(_ctx) { ctx = _ctx; }

export async function renderPoints(el) {
  var html = '<div class="sub-tabs">' +
    '<div class="sub-tab' + (ctx.pointsSubTab === 'leaderboard' ? ' active' : '') + '" data-action="switchPointsTab" data-param="leaderboard">排行榜</div>' +
    '<div class="sub-tab' + (ctx.pointsSubTab === 'ranking' ? ' active' : '') + '" data-action="switchPointsTab" data-param="ranking">积分查询</div>' +
    '<div class="sub-tab' + (ctx.pointsSubTab === 'award' ? ' active' : '') + '" data-action="switchPointsTab" data-param="award">发放积分</div>' +
    '<div class="sub-tab' + (ctx.pointsSubTab === 'logs' ? ' active' : '') + '" data-action="switchPointsTab" data-param="logs">积分日志</div>' +
    '<div class="sub-tab' + (ctx.pointsSubTab === 'redeem' ? ' active' : '') + '" data-action="switchPointsTab" data-param="redeem">兑换设置</div>' +
  '</div>';

  if (ctx.pointsSubTab === 'leaderboard') {
    html += ctx.renderRefreshBar('points-leaderboard', '');
    var lb = await ctx.api('/points/leaderboard');
    var data = lb.data || [];
    html += data.length
      ? '<table><tr><th>排名</th><th>玩家名</th><th>Steam ID</th><th>当前积分</th><th>累计获得</th></tr>' +
        data.map(function(p, i) {
          var rank = (lb.page - 1) * lb.page_size + i + 1;
          return '<tr><td>' + rank + '</td><td>' + ctx.esc(p.playerName || '-') + '</td><td>' + ctx.esc(p.steamId) + '</td><td><b style="color:var(--accent)">' + p.balance + '</b></td><td>' + p.lifetimeEarned + '</td></tr>';
        }).join('') + '</table>'
      : '<div class="empty">暂无积分数据</div>';
    if (lb.total_pages > 1) {
      html += '<div style="display:flex;gap:8px;justify-content:center;margin-top:16px">';
      if (lb.page > 1) html += '<button class="btn btn-sm" data-action="loadLeaderboardPage" data-param="1">首页</button>';
      if (lb.page > 1) html += '<button class="btn btn-sm" data-action="loadLeaderboardPage" data-param="' + (lb.page - 1) + '">上一页</button>';
      html += '<span style="padding:8px 12px;color:var(--text2)">第 ' + lb.page + ' / ' + lb.total_pages + ' 页</span>';
      if (lb.page < lb.total_pages) html += '<button class="btn btn-sm" data-action="loadLeaderboardPage" data-param="' + (lb.page + 1) + '">下一页</button>';
      html += '</div>';
    }

  } else if (ctx.pointsSubTab === 'ranking') {
    html += ctx.renderRefreshBar('points-ranking', '<input id="points-search" placeholder="搜索 SteamID 或玩家名..." style="flex:1;min-width:200px;padding:8px 12px;border-radius:6px;border:1px solid var(--border);font-size:14px;outline:none" onkeydown="if(event.key===\'Enter\')searchPoints()"><button class="btn" data-action="searchPoints">搜索</button>');
    var d = await ctx.api('/points');
    var pts = d.points || [];
    html += pts.length
      ? '<table><tr><th>排名</th><th>玩家名</th><th>Steam ID</th><th>当前积分</th><th>累计获得</th><th>最后更新</th></tr>' +
        pts.map(function(p, i) {
          return '<tr><td>' + (i + 1) + '</td><td>' + ctx.esc(p.playerName || '-') + '</td><td>' + ctx.esc(p.steamId) + '</td><td><b style="color:var(--accent)">' + p.balance + '</b></td><td>' + p.lifetimeEarned + '</td><td>' + p.lastUpdated + '</td></tr>';
        }).join('') + '</table>'
      : '<div class="empty">暂无积分数据</div>';

  } else if (ctx.pointsSubTab === 'award') {
    html += '<div class="card"><h3>发放/扣除积分</h3>' +
      '<div class="form-row">' +
        '<div class="form-group"><label>Steam ID</label><input id="pt-steam" placeholder="76561198xxxxxxxx"></div>' +
        '<div class="form-group"><label>玩家名 (可选)</label><input id="pt-name" placeholder="玩家昵称"></div>' +
      '</div>' +
      '<div class="form-row">' +
        '<div class="form-group"><label>积分 (正数=发放, 负数=扣除)</label><input id="pt-amount" type="number" placeholder="100"></div>' +
        '<div class="form-group"><label>原因</label><input id="pt-reason" placeholder="击杀奖励 / 违规扣分 等"></div>' +
      '</div>' +
      '<button class="btn btn-primary" data-action="awardPoints">确认</button></div>';

  } else if (ctx.pointsSubTab === 'logs') {
    var l = await ctx.api('/points/logs');
    var logs = l.logs || [];
    html += ctx.renderRefreshBar('points-logs');
    html += logs.length
      ? '<table><tr><th>玩家</th><th>Steam ID</th><th>积分变动</th><th>原因</th><th>操作人</th><th>时间</th></tr>' +
        logs.map(function(x) {
          var color = x.amount > 0 ? 'var(--green)' : 'var(--red)';
          var prefix = x.amount > 0 ? '+' : '';
          return '<tr><td>' + ctx.esc(x.playerName || '-') + '</td><td>' + ctx.esc(x.steamId) + '</td><td style="color:' + color + ';font-weight:700">' + prefix + x.amount + '</td><td>' + ctx.esc(x.reason || '-') + '</td><td>' + ctx.esc(x.operator || '-') + '</td><td>' + x.createdAt + '</td></tr>';
        }).join('') + '</table>'
      : '<div class="empty">暂无积分操作记录</div>';

  } else if (ctx.pointsSubTab === 'redeem') {
    var cfg = await ctx.api('/config');
    var cfgData = cfg.config || {};
    var redeemCode = cfgData.redeem_code || '';
    var redeemCost = cfgData.redeem_cost || '50';
    var redeemDays = cfgData.redeem_days || '0';
    var tbCost = cfgData.tb_cost || '20';
    var tbCode = cfgData.tb_code || '';
    var signInCooldown = cfgData.sign_in_cooldown || '86400';
    var lotteryEnabled = cfgData.lottery_enabled !== '0' && cfgData.lottery_enabled !== 0;
    var lotteryCooldown = cfgData.lottery_cooldown || '3600';
    var lotteryMin = cfgData.lottery_min || '5';
    var lotteryMax = cfgData.lottery_max || '30';
    var lotteryCost = cfgData.lottery_cost || '0';
    var shareGroup = cfgData.points_share_group || '[]';
    function _pv(v) { if (typeof v === 'string' && v.startsWith('"')) { try { return JSON.parse(v); } catch(e) {} } return v; }
    redeemCode = _pv(redeemCode); redeemCost = _pv(redeemCost); tbCost = _pv(tbCost); tbCode = _pv(tbCode);
    signInCooldown = _pv(signInCooldown); lotteryCooldown = _pv(lotteryCooldown); lotteryMin = _pv(lotteryMin);
    lotteryMax = _pv(lotteryMax); lotteryCost = _pv(lotteryCost); shareGroup = _pv(shareGroup);
    if (typeof shareGroup !== 'string') shareGroup = JSON.stringify(shareGroup);

    // 兑换口令
    html += '<div class="card" style="margin-bottom:16px"><h3>兑换预留位口令</h3>';
    html += '<p style="color:var(--text2);font-size:13px;margin-bottom:12px">玩家在游戏中输入口令，自动扣除积分并获得预留位。C 插件需配合使用。</p>';
    html += '<div class="form-row">';
    html += '<div class="form-group"><label>兑换口令</label><input id="redeem-code" value="' + ctx.esc(redeemCode) + '" placeholder="如：兑换VIP" style="width:100%"></div>';
    html += '<div class="form-group"><label>消耗积分</label><input id="redeem-cost" type="number" value="' + ctx.esc(String(redeemCost)) + '" placeholder="50" style="width:100%"></div>';
    html += '</div>';
    html += '<div class="form-group" style="max-width:300px"><label>预留位有效期（天，0=永久）</label><input id="redeem-days" type="number" value="' + ctx.esc(String(redeemDays)) + '" placeholder="0" style="width:100%"><p style="color:var(--text2);font-size:12px;margin-top:4px">0=永久有效，过期自动删除预留位</p></div>';
    html += '<button class="btn btn-primary" data-action="saveRedeemConfig">保存兑换配置</button> <button class="btn" data-action="resetRedeemConfig" style="margin-left:8px">重置</button>';
    if (redeemCode) {
      var daysInfo = redeemDays > 0 ? '，有效期 <b style="color:var(--accent)">' + ctx.esc(String(redeemDays)) + '</b> 天' : '，<b style="color:var(--accent)">永久</b>';
      html += '<div style="margin-top:12px;padding:10px 14px;background:var(--bg2);border-radius:6px;font-size:13px;color:var(--text2)">当前口令：<b style="color:var(--accent)">' + ctx.esc(redeemCode) + '</b>，消耗 <b style="color:var(--accent)">' + ctx.esc(String(redeemCost)) + '</b> 积分' + daysInfo + '</div>';
    }
    html += '</div>';

    // 跳边命令
    html += '<div class="card"><h3>跳边命令</h3>';
    html += '<p style="color:var(--text2);font-size:13px;margin-bottom:12px">玩家在游戏中输入跳边口令，自动扣除积分并执行跳边。C 插件需配合使用。</p>';
    html += '<div class="form-row">';
    html += '<div class="form-group"><label>跳边口令</label><input id="tb-code" value="' + ctx.esc(tbCode) + '" placeholder="如：tb" style="width:100%"></div>';
    html += '<div class="form-group"><label>消耗积分</label><input id="tb-cost" type="number" value="' + ctx.esc(String(tbCost)) + '" placeholder="20" style="width:100%"></div>';
    html += '</div>';
    html += '<button class="btn btn-primary" data-action="saveTbConfig">保存跳边配置</button> <button class="btn" data-action="resetTbConfig" style="margin-left:8px">重置</button>';
    if (tbCode) {
      html += '<div style="margin-top:12px;padding:10px 14px;background:var(--bg2);border-radius:6px;font-size:13px;color:var(--text2)">当前口令：<b style="color:var(--accent)">' + ctx.esc(tbCode) + '</b>，消耗 <b style="color:var(--accent)">' + ctx.esc(String(tbCost)) + '</b> 积分</div>';
    }
    html += '</div>';

    // Sign-in cooldown
    html += '<div class="card" style="margin-top:16px"><h3>签到冷却设置</h3>';
    html += '<div class="form-group" style="max-width:300px"><label>冷却时间（秒）</label><input id="signin-cooldown" type="number" value="' + ctx.esc(String(signInCooldown)) + '" placeholder="86400" style="width:100%"><p style="color:var(--text2);font-size:12px;margin-top:4px">86400=24小时, 3600=1小时</p></div>';
    html += '<button class="btn btn-primary" data-action="saveSignInConfig" style="margin-top:8px">保存</button></div>';

    // Lottery settings
    html += '<div class="card" style="margin-top:16px"><h3>抽奖设置</h3>';
    html += '<div class="form-row">';
    html += '<div class="form-group"><label>冷却时间（秒）</label><input id="lottery-cooldown" type="number" value="' + ctx.esc(String(lotteryCooldown)) + '" placeholder="3600" style="width:100%"></div>';
    html += '<div class="form-group"><label>抽奖费用</label><input id="lottery-cost" type="number" value="' + ctx.esc(String(lotteryCost)) + '" placeholder="0" style="width:100%"><p style="color:var(--text2);font-size:12px;margin-top:4px">0=免费</p></div>';
    html += '</div>';
    html += '<div class="form-row">';
    html += '<div class="form-group"><label>最低奖励</label><input id="lottery-min" type="number" value="' + ctx.esc(String(lotteryMin)) + '" placeholder="5" style="width:100%"></div>';
    html += '<div class="form-group"><label>最高奖励</label><input id="lottery-max" type="number" value="' + ctx.esc(String(lotteryMax)) + '" placeholder="30" style="width:100%"></div>';
    html += '</div>';
    html += '<div style="margin-bottom:12px"><label style="display:flex;align-items:center;gap:8px;cursor:pointer"><input type="checkbox" id="lottery-enabled" ' + (lotteryEnabled ? 'checked' : '') + '> 启用抽奖</label></div>';
    html += '<button class="btn btn-primary" data-action="saveLotteryConfig">保存</button></div>';

    // Cross-server sharing
    html += '<div class="card" style="margin-top:16px"><h3>跨服积分共享</h3>';
    html += '<p style="color:var(--text2);font-size:13px;margin-bottom:12px">格式: [[1,2],[3,4]] 表示服务器1和2共享积分池，3和4共享积分池。扣分时自动从同组服务器补差额。</p>';
    html += '<div class="form-group"><label>共享组配置（JSON）</label><textarea id="share-group" rows="3" style="width:100%;padding:8px;border-radius:6px;border:1px solid var(--border);font-family:monospace;font-size:13px;resize:vertical">' + ctx.esc(shareGroup) + '</textarea></div>';
    html += '<button class="btn btn-primary" data-action="saveShareGroup" style="margin-top:8px">保存</button></div>';
  }
  el.innerHTML = html;
}

export function switchPointsTab(tab) {
  ctx.stopAutoRefresh('points-' + ctx.pointsSubTab);
  ctx.setPointsSubTab(tab);
  import('../../app.js?v=1775700005').then(function(m) { m.render(); });
}

export async function searchPoints() {
  var q = document.getElementById('points-search').value;
  var d = await ctx.api('/points?q=' + encodeURIComponent(q));
  var pts = d.points || [];
  var html = '<div style="margin-bottom:16px;display:flex;gap:12px;align-items:center">' +
    '<input id="points-search" value="' + ctx.esc(q) + '" placeholder="搜索 SteamID 或玩家名..." style="flex:1;padding:8px 12px;border-radius:6px;border:1px solid var(--border);font-size:14px;outline:none" onkeydown="if(event.key===\'Enter\')searchPoints()">' +
    '<button class="btn" data-action="searchPoints">搜索</button></div>';
  html += pts.length
    ? '<table><tr><th>排名</th><th>玩家名</th><th>Steam ID</th><th>当前积分</th><th>累计获得</th><th>最后更新</th></tr>' +
      pts.map(function(p, i) {
        return '<tr><td>' + (i + 1) + '</td><td>' + ctx.esc(p.playerName || '-') + '</td><td>' + ctx.esc(p.steamId) + '</td><td><b style="color:var(--accent)">' + p.balance + '</b></td><td>' + p.lifetimeEarned + '</td><td>' + p.lastUpdated + '</td></tr>';
      }).join('') + '</table>'
    : '<div class="empty">没有找到匹配的积分记录</div>';
  ctx.setPointsSubTab('ranking');
  import('../../app.js?v=1775700005').then(function(m) { m.render(); });
}

export async function awardPoints() {
  var steam = document.getElementById('pt-steam').value;
  var name = document.getElementById('pt-name').value;
  var amount = parseInt(document.getElementById('pt-amount').value);
  var reason = document.getElementById('pt-reason').value;
  if (!steam || isNaN(amount)) return ctx.toast('请填写 Steam ID 和积分', 'error');
  var r = await ctx.api('/points', { method: 'POST', body: { steamId: steam, playerName: name || null, amount: amount, reason: reason || 'manual', serverId: ctx.currentPlayerServerId || null } });
  if (r.error) return ctx.toast(r.error, 'error');
  ctx.toast(amount > 0 ? '已发放 ' + amount + ' 积分' : '已扣除 ' + Math.abs(amount) + ' 积分');
  document.getElementById('pt-steam').value = '';
  document.getElementById('pt-name').value = '';
  document.getElementById('pt-amount').value = '';
  document.getElementById('pt-reason').value = '';
}

export async function saveRedeemConfig() {
  var code = document.getElementById('redeem-code').value.trim();
  var cost = parseInt(document.getElementById('redeem-cost').value) || 50;
  var days = parseInt(document.getElementById('redeem-days').value) || 0;
  if (!code) return ctx.toast('请输入兑换口令', 'error');
  if (cost < 1) return ctx.toast('消耗积分必须大于0', 'error');
  var r = await ctx.api('/config', { method: 'POST', body: { redeem_code: code, redeem_cost: cost, redeem_days: days } });
  if (r.error) return ctx.toast(r.error, 'error');
  ctx.toast('兑换配置已保存');
  import('../../app.js?v=1775700005').then(function(m) { m.render(); });
}

export async function saveTbConfig() {
  var code = document.getElementById('tb-code').value.trim();
  var cost = parseInt(document.getElementById('tb-cost').value) || 20;
  if (!code) return ctx.toast('请输入跳边口令', 'error');
  if (cost < 1) return ctx.toast('消耗积分必须大于0', 'error');
  var r = await ctx.api('/config', { method: 'POST', body: { tb_code: code, tb_cost: cost } });
  if (r.error) return ctx.toast(r.error, 'error');
  ctx.toast('跳边配置已保存');
  import('../../app.js?v=1775700005').then(function(m) { m.render(); });
}

export async function resetRedeemConfig() {
  if (!confirm('确定要清空兑换口令吗？清空后玩家将无法通过口令兑换预留位。')) return;
  var r = await ctx.api('/config', { method: 'POST', body: { redeem_code: '', redeem_cost: 50 } });
  if (r.error) return ctx.toast(r.error, 'error');
  ctx.toast('兑换配置已重置');
  import('../../app.js?v=1775700005').then(function(m) { m.render(); });
}

export async function resetTbConfig() {
  if (!confirm('确定要清空跳边口令吗？清空后玩家将无法通过口令跳边。')) return;
  var r = await ctx.api('/config', { method: 'POST', body: { tb_code: '', tb_cost: 20 } });
  if (r.error) return ctx.toast(r.error, 'error');
  ctx.toast('跳边配置已重置');
  import('../../app.js?v=1775700005').then(function(m) { m.render(); });
}

export async function saveSignInConfig() {
  var cooldown = parseInt(document.getElementById('signin-cooldown').value) || 86400;
  if (cooldown < 60) return ctx.toast('冷却时间至少60秒', 'error');
  var r = await ctx.api('/config', { method: 'POST', body: { sign_in_cooldown: cooldown } });
  if (r.error) return ctx.toast(r.error, 'error');
  ctx.toast('签到配置已保存');
}

export async function saveLotteryConfig() {
  var cooldown = parseInt(document.getElementById('lottery-cooldown').value) || 3600;
  var cost = parseInt(document.getElementById('lottery-cost').value) || 0;
  var minR = parseInt(document.getElementById('lottery-min').value) || 5;
  var maxR = parseInt(document.getElementById('lottery-max').value) || 30;
  var enabled = document.getElementById('lottery-enabled').checked ? 1 : 0;
  if (minR > maxR) return ctx.toast('最低奖励不能大于最高奖励', 'error');
  if (cooldown < 60) return ctx.toast('冷却时间至少60秒', 'error');
  var r = await ctx.api('/config', { method: 'POST', body: { lottery_enabled: enabled, lottery_cooldown: cooldown, lottery_cost: cost, lottery_min: minR, lottery_max: maxR } });
  if (r.error) return ctx.toast(r.error, 'error');
  ctx.toast('抽奖配置已保存');
}

export async function saveShareGroup() {
  var raw = document.getElementById('share-group').value.trim();
  try { if (raw) JSON.parse(raw); } catch(e) { return ctx.toast('JSON格式错误: ' + e.message, 'error'); }
  var r = await ctx.api('/config', { method: 'POST', body: { points_share_group: raw || '[]' } });
  if (r.error) return ctx.toast(r.error, 'error');
  ctx.toast('跨服共享配置已保存');
}

export async function loadLeaderboardPage(page) {
  var lb = await ctx.api('/points/leaderboard?page=' + page);
  var data = lb.data || [];
  var html = data.length
    ? '<table><tr><th>排名</th><th>玩家名</th><th>Steam ID</th><th>当前积分</th><th>累计获得</th></tr>' +
      data.map(function(p, i) {
        var rank = (lb.page - 1) * lb.page_size + i + 1;
        return '<tr><td>' + rank + '</td><td>' + ctx.esc(p.playerName || '-') + '</td><td>' + ctx.esc(p.steamId) + '</td><td><b style="color:var(--accent)">' + p.balance + '</b></td><td>' + p.lifetimeEarned + '</td></tr>';
      }).join('') + '</table>'
    : '<div class="empty">暂无积分数据</div>';
  if (lb.total_pages > 1) {
    html += '<div style="display:flex;gap:8px;justify-content:center;margin-top:16px">';
    if (lb.page > 1) html += '<button class="btn btn-sm" data-action="loadLeaderboardPage" data-param="1">首页</button>';
    if (lb.page > 1) html += '<button class="btn btn-sm" data-action="loadLeaderboardPage" data-param="' + (lb.page - 1) + '">上一页</button>';
    html += '<span style="padding:8px 12px;color:var(--text2)">第 ' + lb.page + ' / ' + lb.total_pages + ' 页</span>';
    if (lb.page < lb.total_pages) html += '<button class="btn btn-sm" data-action="loadLeaderboardPage" data-param="' + (lb.page + 1) + '">下一页</button>';
    html += '</div>';
  }
  var el = document.querySelector('#content table') || document.querySelector('#content .empty');
  if (el && el.parentElement) {
    var container = el.parentElement;
    container.innerHTML = html;
  }
}

// ─── Module Contract ───
export const manifest = { id: 'points', label: '积分', icon: '⭐', section: 'data', order: 3, permissions: ['points'] };
export const pages = { 'points': renderPoints };
export const actions = { switchPointsTab, searchPoints, awardPoints, saveRedeemConfig, saveTbConfig, resetRedeemConfig, resetTbConfig, saveSignInConfig, saveLotteryConfig, saveShareGroup, loadLeaderboardPage };
