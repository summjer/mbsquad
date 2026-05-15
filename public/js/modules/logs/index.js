/**
 * log-manager.js — 日志管理（聊天/建队/救援/进出/换图 五 Tab）
 * v2.0 - 现代化重设计
 */

let ctx;
export function init(_ctx) { ctx = _ctx; }

let logTab = 'chat';
let logServerId = 'all';
let logPage = 1;
let logData = { data: [], total: 0, total_pages: 1 };
let logKeyword = '';
let logType = '';
let logCounts = {}; // 各 Tab 的记录总数缓存

// Tab 配置
const TAB_CONFIG = {
  'chat': { icon: '💬', label: '聊天记录', endpoint: '/api/chat' },
  'claims': { icon: '👥', label: '建队记录', endpoint: '/api/squad-events' },
  'revives': { icon: '🚑', label: '救援记录', endpoint: '/api/revives' },
  'player-events': { icon: '🚪', label: '进出记录', endpoint: '/api/player-events' },
  'game-rounds': { icon: '🗺', label: '换图记录', endpoint: '/api/game-rounds' }
};

export function renderLogManager(container) {
  var tabHtml = buildTabs();
  var searchHtml = buildSearchControls();
  var statsHtml = buildStatsBar();

  container.innerHTML =
    '<div class="log-manager">' +
      '<div class="log-header">' +
        '<h2>日志管理</h2>' +
      '</div>' +
      '<div class="log-tabs-wrapper">' +
        '<div class="log-tabs" id="log-tabs">' + tabHtml + '</div>' +
      '</div>' +
      '<div class="log-controls">' +
        '<div class="log-filters">' +
          '<select id="log-server-filter" class="log-select" data-action="logChangeServer">' +
            '<option value="all">全部服务器</option>' +
            (ctx.servers || []).map(function(s) {
              return '<option value="' + s.id + '"' + (String(s.id) === String(logServerId) ? ' selected' : '') + '>' + (s.name || s.host) + '</option>';
            }).join('') +
          '</select>' +
          searchHtml +
        '</div>' +
        '<button class="btn btn-sm log-refresh-btn" data-action="logRefresh" title="刷新">' +
          '<span class="refresh-icon">↻</span>' +
        '</button>' +
      '</div>' +
      '<div class="log-stats-bar" id="log-stats-bar">' + statsHtml + '</div>' +
      '<div class="log-table-wrapper" id="log-table-container">' +
        '<div class="log-loading">' +
          '<div class="log-loading-icon">⏳</div>' +
          '<div class="log-loading-text">加载中...</div>' +
        '</div>' +
      '</div>' +
      '<div class="log-pagination" id="log-pagination"></div>' +
    '</div>';

  loadLogData();
  loadTabCounts();
}

function buildTabs() {
  return Object.keys(TAB_CONFIG).map(function(key) {
    var cfg = TAB_CONFIG[key];
    var count = logCounts[key];
    var badge = count ? '<span class="log-tab-badge">' + formatCount(count) + '</span>' : '';
    return '<button class="log-tab-btn' + (logTab === key ? ' active' : '') + '" data-action="logSwitchTab" data-param="' + key + '">' +
      '<span class="log-tab-icon">' + cfg.icon + '</span>' +
      '<span class="log-tab-label">' + cfg.label + '</span>' +
      badge +
    '</button>';
  }).join('');
}

function buildSearchControls() {
  var placeholder = '';
  var typeOptions = '';

  switch (logTab) {
    case 'chat':
      placeholder = '搜索玩家/消息...';
      typeOptions =
        '<select id="log-type-filter" class="log-select log-select-type" onchange="window._logChangeType()">' +
          '<option value="">全部类型</option>' +
          '<option value="chatall"' + (logType === 'chatall' ? ' selected' : '') + '>全服</option>' +
          '<option value="chatteam"' + (logType === 'chatteam' ? ' selected' : '') + '>队伍</option>' +
          '<option value="chatsquad"' + (logType === 'chatsquad' ? ' selected' : '') + '>小队</option>' +
          '<option value="chatadmin"' + (logType === 'chatadmin' ? ' selected' : '') + '>管理员</option>' +
        '</select>';
      break;
    case 'claims':
      placeholder = '搜索队长/队名...';
      break;
    case 'revives':
      placeholder = '搜索医疗兵/被救者...';
      break;
    case 'player-events':
      placeholder = '搜索玩家...';
      typeOptions =
        '<select id="log-type-filter" class="log-select log-select-type" onchange="window._logChangeType()">' +
          '<option value="">全部类型</option>' +
          '<option value="join"' + (logType === 'join' ? ' selected' : '') + '>加入</option>' +
          '<option value="leave"' + (logType === 'leave' ? ' selected' : '') + '>离开</option>' +
        '</select>';
      break;
    case 'game-rounds':
      placeholder = '搜索地图名...';
      break;
  }

  return '<div class="log-search-box">' +
      '<input type="text" id="log-keyword" class="log-search-input" placeholder="' + placeholder + '" value="' + escAttr(logKeyword) + '" onkeydown="if(event.key===\'Enter\')window._logSearch()">' +
      (logKeyword ? '<button class="log-search-clear" onclick="window._logClearSearch()">×</button>' : '') +
    '</div>' +
    typeOptions;
}

function buildStatsBar() {
  var total = logData.total || 0;
  var now = new Date();
  var todayStart = new Date(now.getFullYear(), now.getMonth(), now.getDate()).getTime();
  var hourStart = now.getTime() - 3600000;

  var todayCount = 0;
  var hourCount = 0;

  if (logData.data && logData.data.length > 0) {
    for (var i = 0; i < logData.data.length; i++) {
      var row = logData.data[i];
      var ts = row.timestamp || row.createdAt;
      if (ts) {
        var t = new Date(ts + (ts.indexOf('Z') !== -1 ? '' : 'Z')).getTime();
        if (t >= todayStart) todayCount++;
        if (t >= hourStart) hourCount++;
      }
    }
  }

  if (total === 0) return '';

  return '<span class="log-stat-item"><strong>' + formatCount(total) + '</strong> 条记录</span>' +
    '<span class="log-stat-divider">|</span>' +
    '<span class="log-stat-item">本页今日 <strong>' + todayCount + '</strong> 条</span>' +
    '<span class="log-stat-divider">|</span>' +
    '<span class="log-stat-item">本小时 <strong>' + hourCount + '</strong> 条</span>';
}

function formatCount(n) {
  if (n >= 1000000) return (n / 1000000).toFixed(1) + 'M';
  if (n >= 1000) return (n / 1000).toFixed(1) + 'K';
  return String(n);
}

export function logSwitchTab(tab) {
  logTab = tab;
  logPage = 1;
  logKeyword = '';
  logType = '';
  renderLogManager(document.getElementById('content'));
}

export function logChangeServer(val, el) {
  // change 事件委托只传 el（DOM 元素），兼容两种调用方式
  if (!el && val && val.value !== undefined) { el = val; val = el.value; }
  else if (el && el.value !== undefined) val = el.value;
  logServerId = val;
  logPage = 1;
  loadLogData();
  loadTabCounts();
}

export function logRefresh() {
  logPage = 1;
  loadLogData();
  loadTabCounts();
}

window._logSearch = function() {
  var kwEl = document.getElementById('log-keyword');
  var typeEl = document.getElementById('log-type-filter');
  logKeyword = kwEl ? kwEl.value.trim() : '';
  logType = typeEl ? typeEl.value : '';
  logPage = 1;
  loadLogData();
};

window._logClearSearch = function() {
  logKeyword = '';
  logPage = 1;
  loadLogData();
  renderLogManager(document.getElementById('content'));
};

export function logGoPage(p) {
  logPage = p;
  loadLogData();
}

window._logChangeType = function() {
  var typeEl = document.getElementById('log-type-filter');
  var kwEl = document.getElementById('log-keyword');
  logType = typeEl ? typeEl.value : '';
  logKeyword = kwEl ? kwEl.value.trim() : '';
  logPage = 1;
  loadLogData();
};

// 操作按钮
export async function logWarnPlayer(steamId, message) {
  var serverId = logServerId === 'all' ? prompt('请输入服务器 ID：') : logServerId;
  if (!serverId) return;
  try {
    var resp = await fetch('/api/rcon/send', {
      method: 'POST',
      headers: { 'Content-Type': 'application/json', 'Authorization': 'Bearer ' + ctx.token },
      body: JSON.stringify({ serverId: parseInt(serverId), command: 'AdminWarn "' + steamId + '" ' + message })
    });
    var data = await resp.json();
    ctx.toast(data.error ? ('发送失败: ' + data.error) : '警告已发送', data.error ? 'error' : 'success');
  } catch (e) { ctx.toast('发送失败: ' + e.message, 'error'); }
}

export async function logKickPlayer(serverId, steamId) {
  var reason = prompt('踢出原因：');
  if (!reason) return;
  try {
    var resp = await fetch('/api/players/kick', {
      method: 'POST',
      headers: { 'Content-Type': 'application/json', 'Authorization': 'Bearer ' + ctx.token },
      body: JSON.stringify({ serverId: parseInt(serverId), steamId: steamId, reason: reason })
    });
    var data = await resp.json();
    ctx.toast(data.error ? ('失败: ' + data.error) : '已踢出', data.error ? 'error' : 'success');
  } catch (e) { ctx.toast('失败: ' + e.message, 'error'); }
}

export async function logBanPlayer(serverId, steamId, playerName) {
  var reason = prompt('封禁原因：');
  if (!reason) return;
  try {
    var resp = await fetch('/api/bans', {
      method: 'POST',
      headers: { 'Content-Type': 'application/json', 'Authorization': 'Bearer ' + ctx.token },
      body: JSON.stringify({ serverId: parseInt(serverId), steamId: steamId, playerName: playerName, reason: reason, duration: 0 })
    });
    var data = await resp.json();
    ctx.toast(data.error ? ('失败: ' + data.error) : '已封禁', data.error ? 'error' : 'success');
  } catch (e) { ctx.toast('失败: ' + e.message, 'error'); }
}

export async function logSwitchTeam(serverId, steamId) {
  try {
    var resp = await fetch('/api/players/switch-team', {
      method: 'POST',
      headers: { 'Content-Type': 'application/json', 'Authorization': 'Bearer ' + ctx.token },
      body: JSON.stringify({ serverId: parseInt(serverId), steamId: steamId })
    });
    var data = await resp.json();
    ctx.toast(data.error ? ('失败: ' + data.error) : '已跳边', data.error ? 'error' : 'success');
  } catch (e) { ctx.toast('失败: ' + e.message, 'error'); }
}

// ─── 内部函数 ───

async function loadTabCounts() {
  // 并行加载所有 Tab 的总数
  var keys = Object.keys(TAB_CONFIG);
  for (var i = 0; i < keys.length; i++) {
    loadSingleCount(keys[i]);
  }
}

async function loadSingleCount(tabKey) {
  var cfg = TAB_CONFIG[tabKey];
  var params = { page: 1, page_size: 1 };
  if (logServerId && logServerId !== 'all') params.serverId = logServerId;
  var qs = Object.keys(params).map(function(k) { return k + '=' + encodeURIComponent(params[k]); }).join('&');
  try {
    var resp = await fetch(cfg.endpoint + '?' + qs, { headers: { 'Authorization': 'Bearer ' + ctx.token } });
    var data = await resp.json();
    if (data.total !== undefined) {
      logCounts[tabKey] = data.total;
      updateTabBadge(tabKey, data.total);
    }
  } catch (e) {}
}

function updateTabBadge(tabKey, count) {
  var btn = document.querySelector('[data-param="' + tabKey + '"]');
  if (!btn) return;
  var badge = btn.querySelector('.log-tab-badge');
  if (badge) {
    badge.textContent = formatCount(count);
  } else {
    badge = document.createElement('span');
    badge.className = 'log-tab-badge';
    badge.textContent = formatCount(count);
    btn.appendChild(badge);
  }
}

async function loadLogData() {
  var container = document.getElementById('log-table-container');
  if (!container) return;
  container.innerHTML = '<div class="log-loading"><div class="log-loading-icon">⏳</div><div class="log-loading-text">加载中...</div></div>';

  try {
    var cfg = TAB_CONFIG[logTab];
    var endpoint = cfg ? cfg.endpoint : '/api/chat';
    var params = { page: logPage, page_size: 50 };
    if (logServerId && logServerId !== 'all') params.serverId = logServerId;
    if (logKeyword) params.keyword = logKeyword;
    if (logType) params.type = logType;

    var qs = Object.keys(params).map(function(k) { return k + '=' + encodeURIComponent(params[k]); }).join('&');
    var resp = await fetch(endpoint + '?' + qs, { headers: { 'Authorization': 'Bearer ' + ctx.token } });
    var data = await resp.json();

    if (!resp.ok) {
      container.innerHTML = '<div class="log-error"><div class="log-error-icon">⚠️</div><div class="log-error-text">加载失败: ' + esc(data.error || '') + '</div></div>';
      return;
    }

    logData = data;
    renderLogTable();
    renderLogPagination();
    updateStatsBar();
  } catch (e) {
    container.innerHTML = '<div class="log-error"><div class="log-error-icon">⚠️</div><div class="log-error-text">加载失败: ' + esc(e.message) + '</div></div>';
  }
}

function updateStatsBar() {
  var el = document.getElementById('log-stats-bar');
  if (!el) return;
  el.innerHTML = buildStatsBar();
}

function renderLogTable() {
  var container = document.getElementById('log-table-container');
  if (!container) return;
  var rows = logData.data || [];

  if (rows.length === 0) {
    container.innerHTML = buildEmptyState();
    return;
  }

  var html = '<div class="log-table-scroll"><table class="log-table">';

  switch (logTab) {
    case 'chat':
      html += renderChatTable(rows);
      break;
    case 'revives':
      html += renderRevivesTable(rows);
      break;
    case 'player-events':
      html += renderPlayerEventsTable(rows);
      break;
    case 'game-rounds':
      html += renderGameRoundsTable(rows);
      break;
    case 'claims':
      html += renderClaimsTable(rows);
      break;
    default:
      html += renderChatTable(rows);
  }

  html += '</table></div>';
  container.innerHTML = html;
}

function buildEmptyState() {
  var icons = {
    'chat': '💬',
    'claims': '👥',
    'revives': '🚑',
    'player-events': '🚪',
    'game-rounds': '🗺'
  };
  var messages = {
    'chat': '暂无聊天记录',
    'claims': '暂无建队记录',
    'revives': '暂无救援记录',
    'player-events': '暂无进出记录',
    'game-rounds': '暂无换图记录'
  };
  var icon = icons[logTab] || '📋';
  var msg = messages[logTab] || '暂无记录';

  return '<div class="log-empty">' +
    '<div class="log-empty-icon">' + icon + '</div>' +
    '<div class="log-empty-title">' + msg + '</div>' +
    '<div class="log-empty-desc">尝试切换服务器或清除筛选条件</div>' +
  '</div>';
}

// 聊天记录表格
function renderChatTable(rows) {
  var html = '<thead><tr>' +
    '<th class="th-sm">ID</th>' +
    '<th class="th-sm">服务器</th>' +
    '<th>玩家</th>' +
    '<th class="th-mono">SteamID</th>' +
    '<th class="th-msg">消息</th>' +
    '<th class="th-type">类型</th>' +
    '<th class="th-time">时间</th>' +
    '<th class="th-action">操作</th>' +
  '</tr></thead><tbody>';

  for (var i = 0; i < rows.length; i++) {
    var r = rows[i];
    var typeClass = getTypeClass(r.type);
    var typeLabel = getTypeLabel(r.type);
    var isAdmin = r.type === 'chatadmin';
    var rowClass = isAdmin ? 'log-row-admin' : (i % 2 === 1 ? 'log-row-alt' : '');

    html += '<tr class="' + rowClass + '">' +
      '<td class="td-sm">' + r.id + '</td>' +
      '<td class="td-sm">' + (r.serverId || '-') + '</td>' +
      '<td class="td-player">' + esc(r.playerName) + '</td>' +
      '<td class="td-mono">' + esc(r.steamId || '-') + '</td>' +
      '<td class="td-msg">' + esc(r.message) + '</td>' +
      '<td class="td-type"><span class="log-type-badge ' + typeClass + '">' + typeLabel + '</span></td>' +
      '<td class="td-time">' + formatTime(r.timestamp) + '</td>' +
      '<td class="td-action">' +
        '<button class="log-action-btn" title="跳边" data-action="logSwitchTeam" data-params=\'["' + r.serverId + '","' + escAttr(r.steamId) + '"]\'>跳边</button>' +
        '<button class="log-action-btn" title="踢出" data-action="logKickPlayer" data-params=\'["' + r.serverId + '","' + escAttr(r.steamId) + '"]\'>踢出</button>' +
        '<button class="log-action-btn log-action-danger" title="封禁" data-action="logBanPlayer" data-params=\'["' + r.serverId + '","' + escAttr(r.steamId) + '","' + escAttr(r.playerName) + '"]\'>封禁</button>' +
      '</td>' +
    '</tr>';
  }

  html += '</tbody>';
  return html;
}

function getTypeClass(type) {
  switch (type) {
    case 'chatall': return 'log-type-all';
    case 'chatteam': return 'log-type-team';
    case 'chatsquad': return 'log-type-squad';
    case 'chatadmin': return 'log-type-admin';
    default: return '';
  }
}

function getTypeLabel(type) {
  switch (type) {
    case 'chatall': return '全服';
    case 'chatteam': return '队伍';
    case 'chatsquad': return '小队';
    case 'chatadmin': return '管理员';
    default: return type || '-';
  }
}

// 救援记录表格
function renderRevivesTable(rows) {
  var html = '<thead><tr>' +
    '<th class="th-sm">ID</th>' +
    '<th class="th-sm">服务器</th>' +
    '<th>救援者</th>' +
    '<th>被救者</th>' +
    '<th class="th-time">时间</th>' +
  '</tr></thead><tbody>';

  for (var i = 0; i < rows.length; i++) {
    var r = rows[i];
    var rowClass = i % 2 === 1 ? 'log-row-alt' : '';

    html += '<tr class="' + rowClass + '">' +
      '<td class="td-sm">' + r.id + '</td>' +
      '<td class="td-sm">' + (r.serverId || '-') + '</td>' +
      '<td class="td-player">' +
        '<div class="log-player-name">' + esc(r.reviverName || '-') + '</div>' +
        '<div class="log-player-id">' + esc(r.reviverSteamId || '-') + '</div>' +
      '</td>' +
      '<td class="td-player">' +
        '<div class="log-player-name">' + esc(r.revivedName || '-') + '</div>' +
        '<div class="log-player-id">' + esc(r.revivedSteamId || '-') + '</div>' +
      '</td>' +
      '<td class="td-time">' + formatTime(r.timestamp) + '</td>' +
    '</tr>';
  }

  html += '</tbody>';
  return html;
}

// 进出记录表格
function renderPlayerEventsTable(rows) {
  var html = '<thead><tr>' +
    '<th class="th-sm">ID</th>' +
    '<th class="th-sm">服务器</th>' +
    '<th>玩家</th>' +
    '<th class="th-event">事件</th>' +
    '<th class="th-time">时间</th>' +
    '<th class="th-action">操作</th>' +
  '</tr></thead><tbody>';

  for (var i = 0; i < rows.length; i++) {
    var r = rows[i];
    var isJoin = r.eventType === 'join';
    var eventClass = isJoin ? 'log-event-join' : 'log-event-leave';
    var eventLabel = isJoin ? '加入' : '离开';
    var eventIcon = isJoin ? '↓' : '↑';
    var rowClass = i % 2 === 1 ? 'log-row-alt' : '';

    html += '<tr class="' + rowClass + '">' +
      '<td class="td-sm">' + r.id + '</td>' +
      '<td class="td-sm">' + (r.serverId || '-') + '</td>' +
      '<td class="td-player">' +
        '<div class="log-player-name">' + esc(r.playerName || '-') + '</div>' +
        '<div class="log-player-id">' + esc(r.steamId || '-') + '</div>' +
      '</td>' +
      '<td class="td-event"><span class="log-event-badge ' + eventClass + '">' + eventIcon + ' ' + eventLabel + '</span></td>' +
      '<td class="td-time">' + formatTime(r.timestamp) + '</td>' +
      '<td class="td-action">' +
        '<button class="log-action-btn" title="跳边" data-action="logSwitchTeam" data-params=\'["' + r.serverId + '","' + escAttr(r.steamId) + '"]\'>跳边</button>' +
        '<button class="log-action-btn" title="踢出" data-action="logKickPlayer" data-params=\'["' + r.serverId + '","' + escAttr(r.steamId) + '"]\'>踢出</button>' +
        '<button class="log-action-btn log-action-danger" title="封禁" data-action="logBanPlayer" data-params=\'["' + r.serverId + '","' + escAttr(r.steamId) + '","' + escAttr(r.playerName) + '"]\'>封禁</button>' +
      '</td>' +
    '</tr>';
  }

  html += '</tbody>';
  return html;
}

// 换图记录表格
function renderGameRoundsTable(rows) {
  var html = '<thead><tr>' +
    '<th class="th-sm">ID</th>' +
    '<th class="th-sm">服务器</th>' +
    '<th class="th-map">地图</th>' +
    '<th class="th-time">开始时间</th>' +
  '</tr></thead><tbody>';

  for (var i = 0; i < rows.length; i++) {
    var r = rows[i];
    var rowClass = i % 2 === 1 ? 'log-row-alt' : '';

    html += '<tr class="' + rowClass + '">' +
      '<td class="td-sm">' + r.id + '</td>' +
      '<td class="td-sm">' + (r.serverId || '-') + '</td>' +
      '<td class="td-map"><span class="log-map-name">' + esc(r.map) + '</span></td>' +
      '<td class="td-time">' + formatTime(r.timestamp) + '</td>' +
    '</tr>';
  }

  html += '</tbody>';
  return html;
}

// 建队记录表格
function renderClaimsTable(rows) {
  var html = '<thead><tr>' +
    '<th class="th-sm">ID</th>' +
    '<th class="th-sm">服务器</th>' +
    '<th class="th-sm">队伍</th>' +
    '<th class="th-sm">小队ID</th>' +
    '<th>小队名</th>' +
    '<th>队长</th>' +
    '<th class="th-time">创建时间</th>' +
  '</tr></thead><tbody>';

  for (var i = 0; i < rows.length; i++) {
    var r = rows[i];
    var teamLabel = r.teamId != null ? (parseInt(r.teamId) === 1 ? 'A队' : 'B队') : '-';
    var rowClass = i % 2 === 1 ? 'log-row-alt' : '';

    html += '<tr class="' + rowClass + '">' +
      '<td class="td-sm">' + r.id + '</td>' +
      '<td class="td-sm">' + (r.serverId || '-') + '</td>' +
      '<td class="td-sm">' + teamLabel + '</td>' +
      '<td class="td-sm">' + (r.squadId || '-') + '</td>' +
      '<td>' + esc(r.squadName || '-') + '</td>' +
      '<td class="td-player">' +
        '<div class="log-player-name">' + esc(r.creatorName || '-') + '</div>' +
        '<div class="log-player-id">' + esc(r.creatorSteamId || '-') + '</div>' +
      '</td>' +
      '<td class="td-time">' + formatTime(r.createdAt) + '</td>' +
    '</tr>';
  }

  html += '</tbody>';
  return html;
}

function renderLogPagination() {
  var el = document.getElementById('log-pagination');
  if (!el) return;
  var page = logData.page || 1;
  var tp = logData.total_pages || 1;
  var total = logData.total || 0;

  if (tp <= 1) {
    el.innerHTML = '<div class="log-pagination-info">共 ' + formatCount(total) + ' 条记录</div>';
    return;
  }

  el.innerHTML =
    '<div class="log-pagination-controls">' +
      '<button class="log-pagination-btn" ' + (page <= 1 ? 'disabled' : '') + ' data-action="logGoPage" data-param="' + (page - 1) + '">' +
        '<span class="log-pagination-arrow">◀</span> 上一页' +
      '</button>' +
      '<div class="log-pagination-info">第 <strong>' + page + '</strong> / ' + tp + ' 页（共 ' + formatCount(total) + ' 条）</div>' +
      '<button class="log-pagination-btn" ' + (page >= tp ? 'disabled' : '') + ' data-action="logGoPage" data-param="' + (page + 1) + '">' +
        '下一页 <span class="log-pagination-arrow">▶</span>' +
      '</button>' +
    '</div>';
}

function formatTime(ts) {
  if (!ts) return '-';
  try {
    var d = new Date(ts + (ts.indexOf('Z') !== -1 ? '' : 'Z'));
    var pad = function(n) { return String(n).padStart(2, '0'); };
    return d.getFullYear() + '-' + pad(d.getMonth()+1) + '-' + pad(d.getDate()) + ' ' + pad(d.getHours()) + ':' + pad(d.getMinutes());
  } catch (e) { return ts; }
}

function esc(s) {
  if (!s) return '';
  return String(s).replace(/&/g,'&amp;').replace(/</g,'&lt;').replace(/>/g,'&gt;').replace(/"/g,'&quot;');
}

function escAttr(s) {
  if (!s) return '';
  return String(s).replace(/&/g,'&amp;').replace(/"/g,'&quot;').replace(/'/g,'&#39;').replace(/</g,'&lt;');
}

// ─── 实时推送 ───

window._logAppendChat = function(d) {
  if (logTab !== 'chat') return;
  var tbody = document.querySelector('.log-table tbody');
  if (!tbody) return;
  var serverFilter = logServerId === 'all' ? true : String(logServerId) === String(d.serverId);
  if (!serverFilter) return;

  var typeClass = getTypeClass(d.type || d.channel);
  var typeLabel = getTypeLabel(d.type || d.channel);
  var isAdmin = (d.type || d.channel) === 'chatadmin';

  var tr = document.createElement('tr');
  tr.className = 'log-row-new' + (isAdmin ? ' log-row-admin' : '');
  tr.innerHTML =
    '<td class="td-sm">(new)</td>' +
    '<td class="td-sm">' + (d.serverId || '-') + '</td>' +
    '<td class="td-player">' + esc(d.playerName) + '</td>' +
    '<td class="td-mono">' + esc(d.steamId || '-') + '</td>' +
    '<td class="td-msg">' + esc(d.message) + '</td>' +
    '<td class="td-type"><span class="log-type-badge ' + typeClass + '">' + typeLabel + '</span></td>' +
    '<td class="td-time">' + formatTime(d.timestamp) + '</td>' +
    '<td class="td-action">' +
      '<button class="log-action-btn" title="跳边" data-action="logSwitchTeam" data-params=\'["' + d.serverId + '","' + escAttr(d.steamId || '') + '"]\'>跳边</button>' +
      '<button class="log-action-btn" title="踢出" data-action="logKickPlayer" data-params=\'["' + d.serverId + '","' + escAttr(d.steamId || '') + '"]\'>踢出</button>' +
      '<button class="log-action-btn log-action-danger" title="封禁" data-action="logBanPlayer" data-params=\'["' + d.serverId + '","' + escAttr(d.steamId || '') + '","' + escAttr(d.playerName) + '"]\'>封禁</button>' +
    '</td>';

  if (tbody.firstChild) tbody.insertBefore(tr, tbody.firstChild);
  else tbody.appendChild(tr);
};

// ─── Module Contract ───
export const manifest = { id: 'logs', label: '日志管理', icon: '📋', section: 'data', order: 4 };
export const pages = { 'logs': renderLogManager };
export const actions = { logSwitchTab, logChangeServer, logRefresh, logGoPage, logWarnPlayer, logKickPlayer, logBanPlayer, logSwitchTeam };
