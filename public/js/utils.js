 
// ─── utils.js — 工具函数 ───

import {
  token, setToken, currentUser, setCurrentUser,
  autoRefreshTimers, setAutoRefreshTimers,
  refreshIntervals, setRefreshIntervals,
  defaultRefreshSeconds,
  API, FACTIONS, ROLE_LABELS,
  permissions
} from './state.js?v=1775700005';

// ─── API Helper ───
export async function api(path, opts) {
  opts = opts || {};
  var headers = { 'Content-Type': 'application/json' };
  if (token) headers['Authorization'] = 'Bearer ' + token;
  if (opts.headers) Object.assign(headers, opts.headers);
  try {
    var res = await fetch(API + path, {
      headers: headers,
      method: opts.method,
      body: opts.body ? JSON.stringify(opts.body) : undefined
    });
    var text = await res.text();
    var data;
    try { data = JSON.parse(text); } catch(e) {
      if (res.status >= 400) data = { error: 'HTTP ' + res.status + ': ' + (text || 'empty response') };
      else data = { error: 'Invalid JSON response' };
    }
    if (res.status === 401 && path !== '/auth/login') {
      setToken(null);
      localStorage.removeItem('squad_token');
      import('./auth.js?v=1775700005').then(function(m) { m.showLogin(); });
    }
    return data;
  } catch (e) {
    return { error: 'Network error: ' + e.message };
  }
}

// ─── Toast ───
export function toast(msg, type) {
  type = type || 'success';
  var t = document.createElement('div');
  t.className = 'toast toast-' + type;
  t.textContent = msg;
  document.body.appendChild(t);
  setTimeout(function() { t.remove(); }, 3000);
}

// ─── HTML Escape ───
export function esc(s) {
  var d = document.createElement('div');
  d.textContent = s;
  return d.innerHTML;
}

// ─── Attribute Escape (for data-param, etc.) ───
export function escAttr(s) {
  return String(s).replace(/\\/g, '\\\\').replace(/'/g, "\\'").replace(/"/g, '\\"').replace(/`/g, '\\`');
}

// ─── Auto-Refresh ───
export function getRefreshInterval(page) {
  return refreshIntervals[page] || defaultRefreshSeconds;
}

export function startAutoRefresh(page, callback, seconds) {
  stopAutoRefresh(page);
  if (!seconds || seconds <= 0) return;
  var intervals = { ...refreshIntervals };
  intervals[page] = seconds;
  setRefreshIntervals(intervals);
  var timers = { ...autoRefreshTimers };
  timers[page] = setTimeout(function() {
    timers[page] = setInterval(callback, seconds * 1000);
    setAutoRefreshTimers(timers);
  }, 100);
  setAutoRefreshTimers(timers);
  // Start countdown for server list page
  if (page === 'list') {
    updateRefreshCountdown(page, intervals[page]);
  }
}

export function stopAutoRefresh(page) {
  if (autoRefreshTimers[page]) {
    clearInterval(autoRefreshTimers[page]);
    var timers = { ...autoRefreshTimers };
    delete timers[page];
    setAutoRefreshTimers(timers);
  }
}

export function stopAllAutoRefresh() {
  Object.keys(autoRefreshTimers).forEach(function(p) {
    clearInterval(autoRefreshTimers[p]);
  });
  setAutoRefreshTimers({});
}

// renderRefreshBar: returns only extraHtml (no UI shown on non-server-list pages)
export function renderRefreshBar(page, extraHtml) {
  return extraHtml ? '<div class="refresh-bar">' + extraHtml + '</div>' : '';
}

// renderAutoRefreshControl: dropdown-based interval selector (only for server list page)
var countdownTimers = {};

export function renderAutoRefreshControl(page) {
  var sec = getRefreshInterval(page);
  var options = [
    { value: 0, label: '关闭' },
    { value: 30, label: '30秒' },
    { value: 60, label: '1分钟' },
    { value: 120, label: '2分钟' },
    { value: 300, label: '5分钟' }
  ];
  var html = '<div class="refresh-bar" style="gap:8px">' +
    '<button class="btn" data-action="doPageRefresh" data-param="' + page + '">刷新</button>' +
    '<span style="font-size:13px;color:var(--text2)">自动刷新:</span>' +
    '<select id="refresh-interval-' + page + '" data-action="setRefreshInterval" data-page="' + page + '" style="padding:6px 10px;border-radius:var(--radius);border:1px solid var(--border);background:var(--bg2);color:var(--text);font-size:13px;outline:none;cursor:pointer">';
  options.forEach(function(opt) {
    var selected = sec === opt.value ? ' selected' : '';
    html += '<option value="' + opt.value + '"' + selected + '>' + opt.label + '</option>';
  });
  html += '</select>' +
    '<span id="refresh-countdown-' + page + '" style="font-size:12px;color:var(--text3)"></span>' +
    '</div>';
  return html;
}

// updateRefreshCountdown: show countdown timer
function updateRefreshCountdown(page, totalSec) {
  if (countdownTimers[page]) clearInterval(countdownTimers[page]);
  if (totalSec <= 0) return;

  var remaining = totalSec;
  var countdownEl = document.getElementById('refresh-countdown-' + page);

  function tick() {
    if (!countdownEl) return;
    if (remaining <= 0) {
      remaining = totalSec;
    }
    countdownEl.textContent = '下次刷新: ' + remaining + '秒';
    remaining--;
  }
  tick();
  countdownTimers[page] = setInterval(tick, 1000);
}

// setRefreshInterval: handle interval select changes
export function setRefreshInterval(el) {
  // el is the select element
  var page = el.getAttribute('data-page');
  var sec = parseInt(el.value) || 0;
  var intervals = { ...refreshIntervals };
  intervals[page] = sec;
  setRefreshIntervals(intervals);
  if (sec > 0) {
    startAutoRefresh(page, function() { doPageRefresh(page); }, sec);
    updateRefreshCountdown(page, sec);
  } else {
    stopAutoRefresh(page);
    var countdownEl = document.getElementById('refresh-countdown-' + page);
    if (countdownEl) countdownEl.textContent = '';
    if (countdownTimers[page]) {
      clearInterval(countdownTimers[page]);
      delete countdownTimers[page];
    }
  }
}

export function updateAutoInterval(page) {
  var secInput = document.getElementById('auto-sec-' + page);
  if (!secInput) return;
  var sec = parseInt(secInput.value) || defaultRefreshSeconds;
  if (sec < 3) sec = 3;
  startAutoRefresh(page, function() { doPageRefresh(page); }, sec);
}

var _playerRefreshPending = false;
export function doPageRefresh(page) {
  if (page === 'players') {
    // 玩家列表：使用防抖避免频繁刷新
    if (currentPage === 'servers') {
      if (_playerRefreshPending) return;
      _playerRefreshPending = true;
      import('./modules/servers/index.js?v=1775700005').then(function(m) { 
        m.refreshPlayersLive().finally(function() { _playerRefreshPending = false; });
      }).catch(function() { _playerRefreshPending = false; });
    }
    return;
  } else if (page === 'list') {
    // 服务器列表：使用超轻量状态更新（不重建DOM）
    import('./modules/servers/index.js?v=1775700005').then(function(m) { 
      if (m.updateStatusLightweight) {
        m.updateStatusLightweight();
      } else {
        m.refreshServerListLight(); 
      }
    });
  } else if (page === 'logs') {
    // chat auto-fetch removed (DumpLog not a Squad command)
  } else {
    // 其他页面：暂时保留完整渲染（未来可优化）
    import('./app.js?v=1775700005').then(function(m) { if (document.getElementById('page-title')) m.render(); });
  }
}

// ─── Permission ───
export function isOp() { return currentUser && currentUser.role === 'op'; }
export function isAdmin() { return currentUser && (currentUser.role === 'admin' || currentUser.role === 'server_owner'); }
export function hasPerm(perm) {
  if (!currentUser) return false;
  if (currentUser.role === 'server_owner') return true;
  return !!permissions[perm];
}
export function canAccess(page) {
  if (isAdmin() || (currentUser && currentUser.role === 'server_owner')) return true;
  if (isOp() && page === 'users') return false;
  return true;
}

// ─── Faction/Map helpers ───
export function factionOptsHtml() {
  return Object.keys(FACTIONS).map(function(k) {
    return '<option value="' + k + '">' + FACTIONS[k].cn + '</option>';
  }).join('');
}

export function updateFactionOptions(team) {
  var sel = document.getElementById('qc-t' + team + '-faction');
  var branchSel = document.getElementById('qc-t' + team + '-branch');
  if (!sel || !branchSel) return;
  var f = FACTIONS[sel.value];
  if (!f) return;
  branchSel.innerHTML = f.branches.map(function(b) {
    return '<option value="' + b.v + '">' + b.cn + '</option>';
  }).join('');
  updateUnitOptions(team);
}

export function updateUnitOptions(team) {
  var factionSel = document.getElementById('qc-t' + team + '-faction');
  var branchSel = document.getElementById('qc-t' + team + '-branch');
  var unitSel = document.getElementById('qc-t' + team + '-unit');
  if (!factionSel || !branchSel || !unitSel) return;
  var f = FACTIONS[factionSel.value];
  if (!f) return;
  var branch = f.branches.find(function(b) { return b.v === branchSel.value; });
  if (!branch || !branch.types) { unitSel.innerHTML = ''; return; }
  unitSel.innerHTML = branch.types.map(function(t) {
    return '<option value="' + t + '">' + t + '</option>';
  }).join('');
}

window.updateFactionOptions = updateFactionOptions;
window.updateUnitOptions = updateUnitOptions;
export function layerOptsHtml() {
  return ['AAS','RAAS','Insurgency','Invasion','Destruction','Skirmish','Seed','TC'].map(function(l) {
    return '<option value="' + l + '">' + l + '</option>';
  }).join('');
}

export function verOptsHtml() {
  return ['v1','v2','v3'].map(function(v) {
    return '<option value="' + v + '">' + v + '</option>';
  }).join('');
}


// --- Input validation (v14.1) ---
export function validateSteamId(val) {
  if (!val) return '';
  if (!/^7656119\d{10}$/.test(val) && !/^\d{17}$/.test(val)) return 'SteamID format error (17 digits)';
  return '';
}
export function validatePort(val) {
  var port = parseInt(val);
  if (isNaN(port) || port < 1 || port > 65535) return 'Port must be 1-65535';
  return '';
}
export function validateServerName(val) {
  if (!val || !val.trim()) return 'Server name required';
  if (val.length > 50) return 'Max 50 chars';
  return '';
}
export function confirmAction(message, onConfirm) {
  var overlay = document.createElement('div');
  overlay.className = 'modal-overlay';
  overlay.innerHTML = '<div class="modal-box" style="max-width:400px">' +
    '<div class="modal-header">Confirm</div>' +
    '<div class="modal-body"><p style="margin:0;color:var(--text2)">' + esc(message) + '</p></div>' +
    '<div class="modal-actions">' +
      '<button class="btn btn-secondary" data-action="cancel">Cancel</button>' +
      '<button class="btn btn-danger" data-action="confirm">Confirm</button>' +
    '</div></div>';
  overlay.querySelector('[data-action="cancel"]').onclick = function() { overlay.remove(); };
  overlay.querySelector('[data-action="confirm"]').onclick = function() { overlay.remove(); onConfirm(); };
  document.body.appendChild(overlay);
}
