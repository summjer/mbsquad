// ─── app.js v4.0 — 动态侧边栏 + 模块热重载 ───

import { loadAllModules, renderPage, hasPage, getLoadedModules } from './core/loader.js?v=1775700005';
import {
  token, currentUser, setCurrentUser, servers, setServers, setServersSubTab,
  currentPage, setCurrentPage, serversSubTab,
  ROLE_LABELS, allLayers, setAllLayers,
  selectedServerId, setSelectedServerId, setCurrentPlayerServerId,
  setPermissions
} from './state.js?v=1775700005';
import {
  api, esc, canAccess, toast, hasPerm,
  stopAllAutoRefresh, startAutoRefresh, getRefreshInterval, doPageRefresh
} from './utils.js?v=1775700005';
import {
  showLogin, switchTab, doLogin, doRegister, doLogout
} from './auth.js?v=1775700005';

// ─── Dynamic Sidebar Builder ───

// --- Server list cache (v14.1) ---
var _serversCache = null;
var _serversCacheTime = 0;
var SERVERS_CACHE_TTL = 30000;

async function fetchServersCached(force) {
  var now = Date.now();
  if (!force && _serversCache && (now - _serversCacheTime) < SERVERS_CACHE_TTL) {
    setServers(_serversCache);
    return _serversCache;
  }
  var result = await api("/servers");
  if (!result.error) {
    _serversCache = result.servers || [];
    _serversCacheTime = now;
    setServers(_serversCache);
  }
  return _serversCache;
}

var SECTION_DEFS = {
  manage: { label: '管理', order: 1 },
  data:   { label: '数据', order: 2 },
  system: { label: '系统', order: 3 },
};

function buildSidebar(modules, role) { console.log("[Sidebar] modules:", JSON.stringify(modules.map(function(m){return {id:m.id,section:m.section,order:m.order}})));
  // Group modules by section
  var sections = {};
  modules.forEach(function(m) {
    if (m.section) {
      // permissions 字段检查：用户需要至少拥有其中一个权限
      if (m.permissions && m.permissions.length > 0) {
        if (!m.permissions.some(function(p) { return hasPerm(p); })) return;
      }
      // 旧的 access 字段保持兼容
      if (m.access && !m.access.includes(role)) return;
      if (!sections[m.section]) sections[m.section] = [];
      sections[m.section].push(m);
    }
  });
  // Sort modules within each section
  Object.keys(sections).forEach(function(k) {
    sections[k].sort(function(a, b) { return (a.order || 99) - (b.order || 99); });
  });

  var html = '';
  var sectionKeys = Object.keys(SECTION_DEFS).sort(function(a, b) {
    return SECTION_DEFS[a].order - SECTION_DEFS[b].order;
  });

  sectionKeys.forEach(function(sk) {
    var mods = sections[sk];
    if (!mods || !mods.length) return;
    var def = SECTION_DEFS[sk];
    html += '<div class="nav-section"><div class="nav-section-label">' + def.label + '</div>';
    mods.forEach(function(m) {
      var iconSpan = m.icon ? '<span class="nav-icon">' + m.icon + '</span>' : '';
      html += '<a class="nav-item" data-nav="' + m.id + '" data-action="navigate" data-param="' + m.id + '">' +
        iconSpan + '<span>' + esc(m.label) + '</span></a>';
      // Subnav (e.g. servers module has list/players/reserved/settings)
      if (m.id === 'servers' && m.subnav) {
        html += '<div id="servers-subnav" style="display:none">';
        m.subnav.forEach(function(sub) {
          if (sub.permissions && sub.permissions.length > 0) {
            if (!sub.permissions.some(function(p) { return hasPerm(p); })) return;
          }
          if (sub.access && !sub.access.includes(role)) return;
          html += '<a class="nav-item nav-sub-item" data-subnav="' + sub.id + '" data-action="switchServersTab" data-param="' + sub.id + '"><span>' + sub.label + '</span></a>';
        });
        html += '</div>';
      }
    });
    html += '</div>';
  });
  return html;
}

// ─── Navigation ───
export function navigate(page) {
  if (!canAccess(page)) { toast('无权限访问此页面', 'error'); page = 'servers'; }
  stopAllAutoRefresh();
  setCurrentPage(page);
  document.querySelectorAll('.nav-item').forEach(function(el) { el.classList.remove('active'); });
  var sel = document.querySelector('a[data-nav="' + page + '"]');
  if (sel) sel.classList.add('active');
  var subnav = document.getElementById('servers-subnav');
  if (page === 'servers') {
    if (subnav) subnav.style.display = '';
    document.querySelectorAll('#servers-subnav .nav-item').forEach(function(el) { el.classList.remove('active'); });
    var subSel = document.querySelector('#servers-subnav a[data-subnav="' + serversSubTab + '"]');
    if (subSel) subSel.classList.add('active');
  } else {
    if (subnav) subnav.style.display = 'none';
  }
  render();
}

// ─── Global Server Selector ───
function updateGlobalServerSelector() {
  var nameEl = document.getElementById('topbar-server-name');
  var listEl = document.getElementById('topbar-server-list');
  var wrap = document.getElementById('topbar-server-wrap');
  if (!nameEl || !listEl || !wrap) return;
  if (!servers.length) { wrap.style.display = 'none'; return; }
  wrap.style.display = '';
  
  if (!selectedServerId && servers.length) { setSelectedServerId(servers[0].id); setCurrentPlayerServerId(servers[0].id); }
  var cur = servers.find(function(s) { return s.id === selectedServerId; }) || servers[0];
  nameEl.textContent = cur.name || cur.host;
  listEl.innerHTML = servers.map(function(s) {
    var active = s.id === selectedServerId;
    var itemCls = 'tsd-item' + (active ? ' tsd-item-active' : '');
    var check = active ? '<span class="tsd-check">&#10003;</span>' : '';
    var relayBtns = s.connectionMode === 'relay' ? '<button class="tsd-act tsd-act-start" data-action="relayControl" data-params="[' + s.id + ',\"start\"]">\u25b6 \u542f\u52a8</button><button class="tsd-act tsd-act-stop" data-action="relayControl" data-params="[' + s.id + ',\"stop\"]">\u25a0 \u505c\u6b62</button>' : '';
    return '<div class="' + itemCls + '">' +
      '<div class="tsd-item-header" data-action="topbarSelectServer" data-param="' + s.id + '">' +
        '<div class="tsd-item-info"><span class="tsd-item-name">' + esc(s.name) + '</span><span class="tsd-item-addr">' + esc(s.host) + ':' + s.rconPort + '</span></div>' + check +
      '</div>' +
      '<div class="tsd-item-body"><div class="tsd-item-notes"><input class="server-notes-input" data-server-id="' + s.id + '" value="' + esc(s.notes || '') + '" placeholder="备注..."></div>' +
      '<div class="tsd-item-actions">' +
        relayBtns +
        '<button class="tsd-act tsd-act-edit" data-action="showEditServerModal" data-param="' + s.id + '">编辑</button>' +
        '<button class="tsd-act tsd-act-del" data-action="deleteServer" data-param="' + s.id + '">删除</button>' +
      '</div></div></div>';
  }).join('');
  listEl.querySelectorAll('.server-notes-input').forEach(function(input) {
    input.addEventListener('blur', function() {
      var sid = this.getAttribute('data-server-id');
      import('./modules/servers/index.js?v=1775700005').then(function(m) { m.saveServerNotes(sid, input.value); });
    });
    input.addEventListener('keydown', function(e) { if (e.key === 'Enter') this.blur(); });
  });
}

// ─── Render Dispatcher ───
export async function render() {
  var el = document.getElementById('content');
  if (!el) return;
  await fetchServersCached();
  updateGlobalServerSelector();
  if (!renderPage(currentPage, el)) {
    el.innerHTML = '<div class="empty">页面 "' + esc(currentPage) + '" 未找到对应的模块</div>';
  }
  var refreshKey = currentPage === 'servers' ? serversSubTab : currentPage;
  var noRefreshPages = ['settings', 'points-award', 'points-redeem', 'plugins-new'];
  if (noRefreshPages.indexOf(refreshKey) === -1) {
    startAutoRefresh(refreshKey, function() { doPageRefresh(refreshKey); }, getRefreshInterval(refreshKey));
  }
}

// ─── Main App Shell ───
export async function showApp() {
  if (!currentUser) {
    var r = await api('/auth/me');
    if (r.error) { showLogin(); return; }
    setCurrentUser(r.user);
    if (r.permissions) setPermissions(r.permissions);
  }
  try { var lr = await fetch('/layers.json'); if (lr.ok) setAllLayers(await lr.json()); } catch(e) {}
  try { await loadAllModules({ api }); } catch(e) { console.error('[App] Module load error:', e); }

  var role = currentUser.role;
  var roleLabel = ROLE_LABELS[role] || role;
  var initial = (currentUser.username || '?').charAt(0).toUpperCase();
  var modules = getLoadedModules(); console.log("[App] loadedModules:", JSON.stringify(modules.map(function(m){return {id:m.id,section:m.section,permissions:m.permissions,access:m.access}})));
  var sidebarNav = buildSidebar(modules, role);

document.getElementById('app').innerHTML =
    '<div class="global-topbar">' +
      '<div class="global-topbar-left">' +
        '<div class="logo logo-compact"><div class="logo-icon">' + String.fromCodePoint(0x1F35F) + '</div><div class="logo-text">薯条面板</div></div>' +
      '</div>' +
      '<div class="global-topbar-center">' +
        '<div class="global-server-group">' +
        '<div id="topbar-server-wrap" style="display:none;position:relative">' +
          '<div id="topbar-server-btn" class="global-server-btn">' +
            '<span style="font-size:14px;line-height:1">' + String.fromCodePoint(0x1F4BB) + '</span>' +
            '<span id="topbar-server-name" style="font-size:13px;font-weight:600;color:var(--text);max-width:200px;overflow:hidden;text-overflow:ellipsis;white-space:nowrap">选择服务器</span>' +
            '<span style="font-size:10px;color:var(--text3);margin-left:2px">' + String.fromCodePoint(0x25BC) + '</span>' +
          '</div>' +
          '<div id="topbar-server-dropdown" class="tsd-dropdown">' +
            '<div class="tsd-dropdown-header">服务器管理</div>' +
            '<div id="topbar-server-list" class="tsd-dropdown-list"></div>' +
            '<div class="tsd-dropdown-footer"><button data-action="showAddServerModal" class="tsd-add-btn">+ 添加服务器</button></div>' +
          '</div>' +
        '</div>' +
        '<div id="topbar-server-stats" class="global-server-stats">' +
          '<span class="stat-item"><span class="stat-dot stat-dot-green"></span><span id="stat-players">-/-</span></span>' +
          '<span class="stat-divider">|</span>' +
          '<span class="stat-item"><span id="stat-map">-</span></span>' +
        '</div>' +
        '</div>' +
      '</div>' +
      '<div class="global-topbar-right">' +
        '<div class="global-user-info">' +
          '<div class="user-avatar-small">' + initial + '</div>' +
          '<span class="global-user-name">' + esc(currentUser.username) + '</span>' +
          '<button class="btn-icon" data-action="doLogout" title="退出登录">✕</button>' +
        '</div>' +
      '</div>' +
    '</div>' +
    '<div class="app-body">' +
      '<div class="sidebar">' +
        sidebarNav +
        '<div class="sidebar-footer">' +
          '<span style="font-size:11px;color:var(--text3)">' + roleLabel + '</span>' +
        '</div>' +
      '</div>' +
      '<div class="sidebar-backdrop" id="sidebar-backdrop"></div>' +
      '<div class="main">' +
        '<div class="topbar">' +
          '<div style="display:flex;align-items:center;gap:12px">' +
            '<button class="hamburger-btn" id="hamburger-btn"><span></span></button>' +
 '</div>' +
        '</div>' +
        '<div class="content" id="content"></div>' +
        '<div id="modal-container"></div>' +
      '</div>' +
    '</div>';
  var hash = location.hash.slice(2) || 'servers';
  if (hash === 'rcon') hash = 'servers/settings';
  if (hash === 'servers/players') { setServersSubTab('players'); hash = 'servers'; }
  if (hash === 'servers/reserved') { setServersSubTab('reserved'); hash = 'servers'; }
  if (hash === 'servers/settings') { setServersSubTab('settings'); hash = 'servers'; }
  if (!canAccess(hash.split('/')[0])) hash = 'servers';
  navigate(hash);
  startStatsRefresh();

  // Mobile sidebar
  var hamburgerBtn = document.getElementById('hamburger-btn');
  var sidebar = document.querySelector('.sidebar');
  var backdrop = document.getElementById('sidebar-backdrop');
  function closeSidebar() { if (sidebar) sidebar.classList.remove('open'); if (backdrop) backdrop.classList.remove('active'); }
  if (hamburgerBtn) hamburgerBtn.addEventListener('click', function(e) {
    e.stopPropagation();
    if (sidebar && sidebar.classList.contains('open')) closeSidebar();
    else { if (sidebar) sidebar.classList.add('open'); if (backdrop) backdrop.classList.add('active'); }
  });
  if (backdrop) backdrop.addEventListener('click', closeSidebar);
  document.querySelectorAll('.sidebar .nav-item').forEach(function(item) {
    item.addEventListener('click', function() { if (window.innerWidth <= 480) closeSidebar(); });
  });
  window.addEventListener('resize', function() { if (window.innerWidth > 480) closeSidebar(); });

  // Topbar dropdown
  var serverBtn = document.getElementById('topbar-server-btn');
  var serverDropdown = document.getElementById('topbar-server-dropdown');
  if (serverBtn && serverDropdown) {
    serverBtn.addEventListener('click', function(e) {
      e.stopPropagation();
      var isOpen = serverDropdown.style.display === 'block';
      serverDropdown.style.display = isOpen ? 'none' : 'block';
      if (!isOpen) updateGlobalServerSelector();
    });
    document.addEventListener('click', function(e) {
      if (serverDropdown.style.display === 'block' && !serverDropdown.contains(e.target) && !serverBtn.contains(e.target))
        serverDropdown.style.display = 'none';
    });
  }
}

// ─── Topbar Server Selection ───

// ─── Global Topbar Server Stats ───
var _statsTimer = null;
var _cachedMap = '-';
async function updateServerStats() {
  var wrap = document.getElementById("topbar-server-stats");
  var playersEl = document.getElementById("stat-players");
  var mapEl = document.getElementById("stat-map");
  if (!wrap) return;
  var _sid = selectedServerId;
  if (!_sid) { if (servers.length) _sid = servers[0].id; else return; }
  try {
    var result = await api("/rcon/server-status/" + _sid);
    if (result && !result.error) {
      var players = parseInt(result.players) || 0;
      var maxP = parseInt(result.maxPlayers) || 0;
      playersEl.textContent = players + "/" + maxP;
      if (result.map) { _cachedMap = result.map; mapEl.textContent = _cachedMap; }
    } else {
      var live = await api("/players/live?serverId=" + _sid);
      var players = (live && live.players) ? live.players.length : 0;
      playersEl.textContent = players + "/-";
    }
  } catch(e) {
    playersEl.textContent = "0/0";
    mapEl.textContent = _cachedMap;
  }
}


function startStatsRefresh() {
  if (_statsTimer) clearInterval(_statsTimer);
  updateServerStats();
  _statsTimer = setInterval(updateServerStats, 60000);
}
function topbarSelectServer(id) {
  id = parseInt(id);
  setSelectedServerId(id);
  setCurrentPlayerServerId(id);
  var dropdown = document.getElementById('topbar-server-dropdown');
  if (dropdown) dropdown.style.display = 'none';
  updateServerStats();
  render();
}

// ─── Module Hot Reload ───
function reloadModule(name) {
  var ts = Date.now();
  import('./modules/' + name + '/index.js?v=' + ts).then(function(mod) {
    if (typeof mod.init === 'function') {
      import('./core/loader.js?v=' + ts).then(function(loader) {
        // Re-inject ctx
        console.log('[HotReload] Reloading module:', name);
        toast('模块 ' + name + ' 已更新，刷新页面生效', 'info');
      });
    }
  }).catch(function(e) {
    console.error('[HotReload] Failed to reload ' + name + ':', e);
  });
}

// ─── WebSocket ───
var wsRetryCount = 0;
var wsMaxRetry = 5;
var wsConnected = false;
var wsPingTimer = null;
var wsPongReceived = true;

// ─── 事件合并队列：100ms 内合并 chat/kill/revive toast ───
var _eventQueue = [];
var _eventFlushTimer = null;
function _enqueueEvent(type, data) {
  _eventQueue.push({ type: type, data: data });
  if (!_eventFlushTimer) {
    _eventFlushTimer = setTimeout(function() {
      _eventFlushTimer = null;
      _flushEventQueue();
    }, 100);
  }
}
function _flushEventQueue() {
  var queue = _eventQueue;
  _eventQueue = [];
  var batched = {};
  queue.forEach(function(e) {
    if (!batched[e.type]) batched[e.type] = [];
    batched[e.type].push(e.data);
  });
  // Chat: 合并多条
  if (batched.chat && batched.chat.length > 1) {
    var first = batched.chat[0];
    if (typeof window._logAppendChat === 'function')
      batched.chat.forEach(function(ev) { window._logAppendChat(ev); });
    if (typeof window._liveChatAppend === 'function')
      batched.chat.forEach(function(ev) { window._liveChatAppend(ev); });
  } else if (batched.chat) {
    if (typeof window._logAppendChat === 'function') window._logAppendChat(batched.chat[0]);
    if (typeof window._liveChatAppend === 'function') window._liveChatAppend(batched.chat[0]);
  }
  // Kill: 合并 toast
  if (batched.kill) {
    batched.kill.forEach(function(ev) {
      var killer = ev.killerName || 'Unknown';
      var victim = ev.victimName || 'Unknown';
      var wpn = ev.weapon ? ' [' + ev.weapon + ']' : '';
      toast('\u2694\uFE0F ' + killer + ' \u27A1 ' + victim + wpn, 'danger');
      if (typeof window._logAppendKill === 'function') window._logAppendKill(ev);
    });
  }
  // Teamkill
  if (batched.teamkill) {
    batched.teamkill.forEach(function(ev) {
      toast('\uD83D\uDC80 TK: ' + (ev.killerName||'?') + ' \u27A1 ' + (ev.victimName||'?'), 'warning');
      if (typeof window._logAppendKill === 'function') window._logAppendKill(ev);
    });
  }
  // Revive
  if (batched.revive) {
    batched.revive.forEach(function(ev) {
      toast('\uD83D\uDC9A ' + (ev.reviverName||'?') + ' 救援了 ' + (ev.revivedName||'?'), 'success');
    });
  }
}

function _showWsReconnectBtn() {
  var existing = document.getElementById("ws-reconnect-btn");
  if (existing) return;
  var btn = document.createElement("button");
  btn.id = "ws-reconnect-btn";
  btn.textContent = "\u21bb \u91cd\u8fde";
  btn.style.cssText = "position:fixed;bottom:20px;right:20px;z-index:9999;padding:10px 18px;background:var(--accent);color:#fff;border:none;border-radius:var(--radius);cursor:pointer;font-size:13px;box-shadow:0 2px 8px rgba(0,0,0,.3)";
  btn.onclick = function() {
    wsRetryCount = 0;
    btn.textContent = "\u91cd\u8fde\u4e2d...";
    initWs();
    setTimeout(function() { btn.remove(); }, 5000);
  };
  document.body.appendChild(btn);
}

function initWs() {
  try {
    var proto = location.protocol === 'https:' ? 'wss:' : 'ws:';
    var ws = new WebSocket(proto + '//' + location.host + '/ws?channel=main&token=' + token);
    ws.onopen = function() {
      wsPongReceived = true;
      if (wsPingTimer) clearInterval(wsPingTimer);
      wsPingTimer = setInterval(function() {
        if (!wsPongReceived) { console.warn("[WS] No pong"); ws.close(); return; }
        if (ws.readyState === WebSocket.OPEN) {
          wsPongReceived = false;
          try { ws.send(JSON.stringify({type:"ping"})); } catch(e) {}
        }
      }, 30000);
      var wasReconnect = wsRetryCount > 0;
      wsRetryCount = 0;
      wsConnected = true;
      if (wasReconnect) { toast('WebSocket 已重连', 'success'); fetchCurrentPageData(); }
    };
    ws.onclose = function() {
      if (wsPingTimer) { clearInterval(wsPingTimer); wsPingTimer = null; }
      wsConnected = false;
      if (wsRetryCount === 0) toast('WebSocket 连接已断开，正在尝试重连...', 'warning');
      if (wsRetryCount < wsMaxRetry) {
        wsRetryCount++;
        setTimeout(initWs, Math.min(1000 * Math.pow(2, wsRetryCount), 30000));
      } else toast('WebSocket 连接失败', 'error');
    };
    ws.onmessage = function(e) {
      try {
        var d = JSON.parse(e.data);
        // Module hot reload
        if (d.type === "pong") { wsPongReceived = true; return; }
        if (d.type === 'module_changed') {
          toast('模块 ' + d.module + ' 已更新', 'info');
          return;
        }
        if (d.type === 'server_registered') {
          toast('新服务器已连接: ' + d.serverName, 'success');
          import('./modules/servers/index.js?v=1775700005').then(function(m) { m.checkNewServerAlert(d.serverId, d.serverName, d.host, d.rconPort); });
          if (currentPage === 'servers' && serversSubTab === 'list') { fetchServersCached(true).then(function() { render(); }); }
        } else if (d.type === 'players_updated' || d.type === 'log_join' || d.type === 'log_disconnect') {
          import('./state.js?v=1775700005').then(function(st) {
            if (st.currentPlayerServerId == 0 && d.serverId && d.players && d.players.length > 0) {
              st.setCurrentPlayerServerId(d.serverId);
              if (st.currentPage === 'servers' && st.serversSubTab === 'players')
                import('./modules/servers/index.js?v=1775700005').then(function(sv) { sv.refreshPlayersLive(); });
              return;
            }
            if (d.serverId == st.currentPlayerServerId) {
              if (d.players) {
                st.setCurrentPlayers(d.players);
                st.setCurrentTeamNames(d.teamNames || {});
                if (st.currentPage === 'servers' && st.serversSubTab === 'players')
                  import('./modules/servers/index.js?v=1775700005').then(function(sv) { sv.refreshPlayersLive(); });
              }
            }
          });
        } else if (d.type === 'player_diff') {
          // ─── 增量更新：只处理加入/离开/变更的玩家 ───
          import('./state.js?v=1775700005').then(function(st) {
            if (d.serverId != st.currentPlayerServerId) return;
            import('./modules/servers/index.js?v=1775700005').then(function(sv) {
              sv.applyPlayerDiff(d);
            });
          });
        } else if (d.type === 'playerlist') {
          import('./state.js?v=1775700005').then(function(st) {
            if (st.currentPlayerServerId == 0 && d.serverId && d.players && d.players.length > 0) {
              st.setCurrentPlayerServerId(d.serverId);
              if (st.currentPage === 'servers' && st.serversSubTab === 'players')
                import('./modules/servers/index.js?v=1775700005').then(function(sv) { sv.refreshPlayersLive(); });
              return;
            }
            if (d.serverId == st.currentPlayerServerId) {
              st.setCurrentPlayers(d.players || []);
              if (d.teamNames) st.setCurrentTeamNames(d.teamNames);
              if (st.currentPage === 'servers' && st.serversSubTab === 'players')
                import('./modules/servers/index.js?v=1775700005').then(function(sv) { sv.refreshPlayersLive(); });
            }
          });
        } else if (d.type === 'log_wound') {
          var evW = d.data || d;
          toast('\uD83D\uDD3A 击倒: ' + (evW.attackerName || '?') + ' \u27A1 ' + (evW.victimName || '?') + (evW.weapon ? ' [' + evW.weapon + ']' : '') + (evW.damage ? ' -' + Math.round(evW.damage) + ' dmg' : ''), 'urgent');
        } else if (d.type === 'log_kill' || d.type === 'log_teamkill' || d.type === 'kill' || d.type === 'teamkill') {
          _enqueueEvent((d.type === 'log_teamkill' || d.type === 'teamkill') ? 'teamkill' : 'kill', d.data || d);
        } else if (d.type === 'log_revive' || d.type === 'revive') {
          _enqueueEvent('revive', d.data || d);
        } else if (d.type === 'log_chat' || d.type === 'chat') {
          _enqueueEvent('chat', d.data || d);
        } else if (d.type === 'rcon_status') {
          toast('RCON ' + (d.status === 'connected' ? '已连接' : '已断开'), d.status === 'connected' ? 'success' : 'warning');
        } else if (d.type === 'relay_status' || d.type === 'relay_control') {
          if (window.relayStatusCache && d.serverId)
            window.relayStatusCache[d.serverId] = { relayOk: d.status !== 'offline', gameServer: { status: d.status || 'unknown', uptime: d.uptime || 0 } };
          if (currentPage === 'servers' && serversSubTab === 'list') { fetchServersCached(true).then(function() { render(); }); }
        }
      } catch {}
    };
  } catch {}
}

function fetchCurrentPageData() {
  if (currentPage === 'servers') {
    if (serversSubTab === 'players') import('./modules/servers/index.js?v=1775700005').then(function(sv) { sv.refreshPlayers(); });
    else render();
  } else if (currentPage === 'logs') import('./modules/logs/index.js?v=1775700005').then(function(m) { m.logRefresh(); });
}

// ─── Action Registry (core only) ───
import { registerMany } from './actions.js?v=1775700005';
registerMany({ navigate, render, showApp, topbarSelectServer, showLogin, switchTab, doLogin, doRegister, doLogout, toast });

// ─── Init ───
if (token) { initWs(); showApp(); } else { showLogin(); }
