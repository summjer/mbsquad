/**
 * modules/players/index.js — 玩家库（跨服务器聚合）
 */

let ctx;
export function init(_ctx) { ctx = _ctx; }

// ── 状态 ──
let pData = { data: [], total: 0, total_pages: 1 };
let pPage = 1;
let pServerId = 'all';
let pSort = 'lastSeen';
let pOrder = 'desc';
let pKeyword = '';
let pStats = null; // 统计概览

// ── 主渲染 ──
export async function renderPlayers(el) {
  el.innerHTML =
    '<div class="players-page">' +
      '<div class="players-header"><h2>玩家库</h2></div>' +
      buildSearchBar() +
      '<div class="players-stats" id="players-stats"></div>' +
      '<div class="players-table-wrap" id="players-table"></div>' +
      '<div class="players-pagination" id="players-pagination"></div>' +
    '</div>';

  loadPlayers();
}

function buildSearchBar() {
  var serverOpts = '<option value="all"' + (pServerId === 'all' ? ' selected' : '') + '>全部服务器</option>';
  if (ctx.servers) {
    for (var i = 0; i < ctx.servers.length; i++) {
      var s = ctx.servers[i];
      serverOpts += '<option value="' + s.id + '"' + (String(s.id) === String(pServerId) ? ' selected' : '') + '>' + esc(s.name || s.host) + '</option>';
    }
  }

  return '<div class="players-controls">' +
    '<input type="text" id="players-kw" class="players-input" placeholder="搜索玩家名 / SteamID / IP" value="' + escAttr(pKeyword) + '" onkeydown="if(event.key===\'Enter\')window._playersSearch()">' +
    '<select id="players-server" class="players-select" data-action="playersChangeServer">' + serverOpts + '</select>' +
    '<select id="players-sort" class="players-select" data-action="playersChangeSort">' +
      '<option value="lastSeen"' + (pSort === 'lastSeen' ? ' selected' : '') + '>最近在线</option>' +
      '<option value="firstSeen"' + (pSort === 'firstSeen' ? ' selected' : '') + '>首次进服</option>' +
      '<option value="balance"' + (pSort === 'balance' ? ' selected' : '') + '>积分</option>' +
      '<option value="totalKills"' + (pSort === 'totalKills' ? ' selected' : '') + '>击杀数</option>' +
      '<option value="joinCount"' + (pSort === 'joinCount' ? ' selected' : '') + '>进服次数</option>' +
    '</select>' +
    '<button class="btn btn-sm" data-action="playersToggleOrder" title="排序方向">' + (pOrder === 'desc' ? '↓ 降序' : '↑ 升序') + '</button>' +
    '<button class="btn btn-sm" data-action="playersRefresh" title="刷新">↻</button>' +
  '</div>';
}

// ── 数据加载 ──
async function loadPlayers() {
  var tableEl = document.getElementById('players-table');
  var statsEl = document.getElementById('players-stats');
  if (!tableEl) return;
  tableEl.innerHTML = '<div class="players-loading">加载中...</div>';

  try {
    var params = 'page=' + pPage + '&page_size=20&sort=' + pSort + '&order=' + pOrder;
    if (pServerId && pServerId !== 'all') params += '&serverId=' + encodeURIComponent(pServerId);
    if (pKeyword) params += '&keyword=' + encodeURIComponent(pKeyword);

    var d = await ctx.api('/player-database?' + params);
    if (d.error) {
      tableEl.innerHTML = '<div class="players-error">加载失败: ' + esc(d.error) + '</div>';
      return;
    }

    pData = d;
    renderStats(statsEl, d);
    renderTable(tableEl, d);
    renderPagination(document.getElementById('players-pagination'), d);
  } catch (e) {
    tableEl.innerHTML = '<div class="players-error">加载失败: ' + esc(e.message) + '</div>';
  }
}

// ── 统计卡片 ──
function renderStats(el, d) {
  if (!el) return;
  var total = d.total || 0;
  var rows = d.data || [];
  var todayActive = 0;
  var totalKills = 0;
  var kdSum = 0;
  var kdCount = 0;
  var now = new Date();
  var todayStart = new Date(now.getFullYear(), now.getMonth(), now.getDate()).getTime();

  for (var i = 0; i < rows.length; i++) {
    var r = rows[i];
    if (r.lastSeen) {
      var t = new Date(r.lastSeen + (r.lastSeen.indexOf('Z') !== -1 ? '' : 'Z')).getTime();
      if (t >= todayStart) todayActive++;
    }
    totalKills += r.totalKills || 0;
    if (r.kdRatio > 0) { kdSum += r.kdRatio; kdCount++; }
  }

  el.innerHTML =
    '<div class="players-stat-cards">' +
      '<div class="players-stat-card"><div class="players-stat-val">' + fmtNum(total) + '</div><div class="players-stat-label">总玩家</div></div>' +
      '<div class="players-stat-card"><div class="players-stat-val">' + fmtNum(totalKills) + '</div><div class="players-stat-label">总击杀</div></div>' +
      '<div class="players-stat-card"><div class="players-stat-val">' + (kdCount ? (kdSum / kdCount).toFixed(2) : '0') + '</div><div class="players-stat-label">平均 K/D</div></div>' +
    '</div>';
}

// ── 表格 ──
function renderTable(el, d) {
  var rows = d.data || [];
  if (!rows.length) {
    el.innerHTML = '<div class="players-empty">暂无玩家数据</div>';
    return;
  }

  var html =
    '<div class="players-table-scroll"><table class="players-table">' +
    '<thead><tr>' +
      '<th>玩家名</th>' +
      '<th>Steam ID</th>' +
      '<th>IP</th>' +
      '<th>积分</th>' +
      '<th>K/D</th>' +
      '<th>击杀</th>' +
      '<th>阵亡</th>' +
      '<th>进服次数</th>' +
      '<th>最后在线</th>' +
      '<th>首次进服</th>' +
      '<th>操作</th>' +
    '</tr></thead><tbody>';

  for (var i = 0; i < rows.length; i++) {
    var r = rows[i];
    var rowClass = i % 2 === 1 ? 'players-row-alt' : '';
    var kdClass = r.kdRatio >= 1 ? 'players-kd-good' : (r.kdRatio > 0 ? 'players-kd-bad' : '');
    var kdText = r.kdRatio != null ? r.kdRatio.toFixed(2) : '0';

    html += '<tr class="' + rowClass + '">' +
      '<td class="players-name">' + esc(r.name || '-') + '</td>' +
      '<td class="font-mono players-steam">' + esc(r.steamId || '-') + '</td>' +
      '<td class="font-mono">' + esc(r.lastIp || '-') + '</td>' +
      '<td class="players-balance">' + fmtNum(r.totalBalance || 0) + '</td>' +
      '<td class="' + kdClass + '">' + kdText + '</td>' +
      '<td>' + (r.totalKills || 0) + '</td>' +
      '<td>' + (r.totalDeaths || 0) + '</td>' +
      '<td>' + (r.joinCount || 0) + '</td>' +
      '<td>' + fmtTime(r.lastSeen) + '</td>' +
      '<td>' + fmtTime(r.firstSeen) + '</td>' +
      '<td class="players-actions">' +
        '<button class="btn btn-sm" data-action="playersDetail" data-param="' + escAttr(r.steamId) + '">详情</button> ' +
        '<a class="btn btn-sm" href="https://steamcommunity.com/profiles/' + escAttr(r.steamId) + '" target="_blank" rel="noopener">Steam</a>' +
      '</td>' +
    '</tr>';
  }

  html += '</tbody></table></div>';
  el.innerHTML = html;
}

// ── 分页 ──
function renderPagination(el, d) {
  if (!el) return;
  var page = d.page || 1;
  var tp = d.total_pages || 1;
  var total = d.total || 0;

  if (tp <= 1) {
    el.innerHTML = '<div class="players-pagination-info">共 ' + fmtNum(total) + ' 名玩家</div>';
    return;
  }

  el.innerHTML =
    '<div class="players-pagination-controls">' +
      '<button class="btn btn-sm" ' + (page <= 1 ? 'disabled' : '') + ' data-action="playersGoPage" data-param="' + (page - 1) + '">◀ 上一页</button>' +
      '<div class="players-pagination-info">第 <strong>' + page + '</strong> / ' + tp + ' 页（共 ' + fmtNum(total) + ' 名）</div>' +
      '<button class="btn btn-sm" ' + (page >= tp ? 'disabled' : '') + ' data-action="playersGoPage" data-param="' + (page + 1) + '">下一页 ▶</button>' +
    '</div>';
}

// ── 详情弹窗 ──
export async function playersDetail(steamId) {
  var modal = document.getElementById('modal-container');
  if (!modal) return;
  modal.innerHTML =
    '<div class="modal-overlay" data-action="playersCloseDetail"><div class="modal modal-lg" onclick="event.stopPropagation()">' +
      '<div class="modal-header"><h3>玩家详情</h3><button class="btn btn-sm" data-action="playersCloseDetail">✕</button></div>' +
      '<div id="players-detail-body" class="players-detail-body"><div class="players-loading">加载中...</div></div>' +
    '</div></div>';

  try {
    var d = await ctx.api('/player-database/' + encodeURIComponent(steamId));
    if (d.error) {
      document.getElementById('players-detail-body').innerHTML = '<div class="players-error">' + esc(d.error) + '</div>';
      return;
    }
    renderDetail(document.getElementById('players-detail-body'), d);
  } catch (e) {
    document.getElementById('players-detail-body').innerHTML = '<div class="players-error">加载失败</div>';
  }
}

function renderDetail(el, d) {
  var kdClass = d.kdRatio >= 1 ? 'players-kd-good' : (d.kdRatio > 0 ? 'players-kd-bad' : '');
  var html =
    '<div class="players-detail-header">' +
      '<div class="players-detail-name">' + esc(d.name) + '</div>' +
      '<div class="players-detail-steam font-mono">' + esc(d.steamId) + '</div>' +
      '<a class="btn btn-sm" href="https://steamcommunity.com/profiles/' + escAttr(d.steamId) + '" target="_blank" rel="noopener">Steam 个人资料</a>' +
    '</div>';

  // 基本信息
  html += '<div class="players-detail-section"><h4>基本信息</h4>' +
    '<div class="players-detail-grid">' +
      '<div class="players-detail-item"><span class="players-detail-label">首次进服</span><span>' + fmtTime(d.firstSeen) + '</span></div>' +
      '<div class="players-detail-item"><span class="players-detail-label">最后在线</span><span>' + fmtTime(d.lastSeen) + '</span></div>' +
      '<div class="players-detail-item"><span class="players-detail-label">IP</span><span class="font-mono">' + esc(d.lastIp || '-') + '</span></div>' +
      '<div class="players-detail-item"><span class="players-detail-label">总积分</span><span class="players-balance">' + fmtNum(d.totalBalance) + '</span></div>' +
      '<div class="players-detail-item"><span class="players-detail-label">累计获得</span><span>' + fmtNum(d.totalLifetimeEarned) + '</span></div>' +
      '<div class="players-detail-item"><span class="players-detail-label">击杀</span><span>' + d.totalKills + '</span></div>' +
      '<div class="players-detail-item"><span class="players-detail-label">阵亡</span><span>' + d.totalDeaths + '</span></div>' +
      '<div class="players-detail-item"><span class="players-detail-label">K/D</span><span class="' + kdClass + '">' + d.kdRatio.toFixed(2) + '</span></div>' +
    '</div></div>';

  // 各服务器数据
  if (d.servers && d.servers.length) {
    html += '<div class="players-detail-section"><h4>服务器记录</h4>' +
      '<table class="players-table"><thead><tr><th>服务器</th><th>IP</th><th>最后在线</th></tr></thead><tbody>';
    for (var i = 0; i < d.servers.length; i++) {
      var s = d.servers[i];
      html += '<tr class="' + (i % 2 === 1 ? 'players-row-alt' : '') + '">' +
        '<td>' + esc(s.serverName || '-') + '</td>' +
        '<td class="font-mono">' + esc(s.lastIp || '-') + '</td>' +
        '<td>' + fmtTime(s.lastSeen) + '</td>' +
      '</tr>';
    }
    html += '</tbody></table></div>';
  }

  // 各服务器积分
  if (d.points && d.points.length) {
    html += '<div class="players-detail-section"><h4>积分明细</h4>' +
      '<table class="players-table"><thead><tr><th>服务器</th><th>当前积分</th><th>累计获得</th><th>更新时间</th></tr></thead><tbody>';
    for (var i = 0; i < d.points.length; i++) {
      var p = d.points[i];
      html += '<tr class="' + (i % 2 === 1 ? 'players-row-alt' : '') + '">' +
        '<td>' + esc(p.serverName || '-') + '</td>' +
        '<td class="players-balance">' + fmtNum(p.balance) + '</td>' +
        '<td>' + fmtNum(p.lifetimeEarned) + '</td>' +
        '<td>' + fmtTime(p.lastUpdated) + '</td>' +
      '</tr>';
    }
    html += '</tbody></table></div>';
  }

  // 最近进出记录
  if (d.recentEvents && d.recentEvents.length) {
    html += '<div class="players-detail-section"><h4>最近进出记录</h4>' +
      '<table class="players-table"><thead><tr><th>服务器</th><th>事件</th><th>时间</th></tr></thead><tbody>';
    for (var i = 0; i < d.recentEvents.length; i++) {
      var ev = d.recentEvents[i];
      var isJoin = ev.eventType === 'join';
      var badge = isJoin
        ? '<span class="players-badge-join">↓ 加入</span>'
        : '<span class="players-badge-leave">↑ 离开</span>';
      html += '<tr class="' + (i % 2 === 1 ? 'players-row-alt' : '') + '">' +
        '<td>' + esc(ev.serverName || '-') + '</td>' +
        '<td>' + badge + '</td>' +
        '<td>' + fmtTime(ev.timestamp) + '</td>' +
      '</tr>';
    }
    html += '</tbody></table></div>';
  }

  // 最近击杀记录
  if (d.recentKills && d.recentKills.length) {
    html += '<div class="players-detail-section"><h4>最近击杀记录</h4>' +
      '<table class="players-table"><thead><tr><th>服务器</th><th>关系</th><th>击杀者</th><th>受害者</th><th>武器</th><th>时间</th></tr></thead><tbody>';
    for (var i = 0; i < d.recentKills.length; i++) {
      var k = d.recentKills[i];
      var relBadge = k.relation === 'kill'
        ? '<span class="players-badge-join">击杀</span>'
        : '<span class="players-badge-leave">被杀</span>';
      html += '<tr class="' + (i % 2 === 1 ? 'players-row-alt' : '') + '">' +
        '<td>' + esc(k.serverName || '-') + '</td>' +
        '<td>' + relBadge + '</td>' +
        '<td>' + esc(k.killer || '-') + '</td>' +
        '<td>' + esc(k.victim || '-') + '</td>' +
        '<td>' + esc(k.weapon || '-') + '</td>' +
        '<td>' + fmtTime(k.timestamp) + '</td>' +
      '</tr>';
    }
    html += '</tbody></table></div>';
  }

  el.innerHTML = html;
}

export function playersCloseDetail() {
  var modal = document.getElementById('modal-container');
  if (modal) modal.innerHTML = '';
}

// ── Action 处理 ──
export function playersChangeServer(val, el) {
  if (val instanceof HTMLElement) { el = val; val = undefined; }
  if (el && el.value !== undefined) val = el.value;
  pServerId = val;
  pPage = 1;
  loadPlayers();
}

export function playersChangeSort(val, el) {
  if (val instanceof HTMLElement) { el = val; val = undefined; }
  if (el && el.value !== undefined) val = el.value;
  pSort = val;
  pPage = 1;
  loadPlayers();
}

export function playersToggleOrder() {
  pOrder = pOrder === 'desc' ? 'asc' : 'desc';
  pPage = 1;
  loadPlayers();
}

export function playersRefresh() {
  pPage = 1;
  loadPlayers();
}

export function playersGoPage(p) {
  pPage = parseInt(p) || 1;
  loadPlayers();
}

window._playersSearch = function () {
  var kwEl = document.getElementById('players-kw');
  pKeyword = kwEl ? kwEl.value.trim() : '';
  pPage = 1;
  loadPlayers();
};

// ── 工具函数 ──
function fmtTime(ts) {
  if (!ts) return '-';
  try {
    var d = new Date(ts + (ts.indexOf('Z') !== -1 ? '' : 'Z'));
    var pad = function (n) { return String(n).padStart(2, '0'); };
    return d.getFullYear() + '-' + pad(d.getMonth() + 1) + '-' + pad(d.getDate()) + ' ' + pad(d.getHours()) + ':' + pad(d.getMinutes());
  } catch (e) { return ts; }
}

function fmtNum(n) {
  if (n >= 1000000) return (n / 1000000).toFixed(1) + 'M';
  if (n >= 1000) return (n / 1000).toFixed(1) + 'K';
  return String(n);
}

function esc(s) {
  if (!s) return '';
  return String(s).replace(/&/g, '&amp;').replace(/</g, '&lt;').replace(/>/g, '&gt;').replace(/"/g, '&quot;');
}

function escAttr(s) {
  if (!s) return '';
  return String(s).replace(/&/g, '&amp;').replace(/"/g, '&quot;').replace(/'/g, '&#39;').replace(/</g, '&lt;');
}

// ── Module Contract ──
export const manifest = { id: 'players', label: '玩家库', icon: '🎮', section: 'data', order: 0 };
export const pages = { 'players': renderPlayers };
export const actions = { playersChangeServer, playersChangeSort, playersToggleOrder, playersRefresh, playersGoPage, playersDetail, playersCloseDetail };
