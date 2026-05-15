// ─── servers.js v2.0 — 服务器管理+快捷命令(分组)+玩家操作+预留位+服务器设置 ───



let _renderEl;
let ctx;
export function init(_ctx) { ctx = _ctx; chartsInit(_ctx); mapInit(_ctx); }

// --- Server List Cache ---
var _serverListCache = null;
var _serverListCacheTime = 0;
var SERVER_CACHE_TTL = 5000; // 5 seconds

async function getCachedServers() {
  var now = Date.now();
  if (_serverListCache && (now - _serverListCacheTime) < SERVER_CACHE_TTL) {
    return _serverListCache;
  }
  var data = await getCachedServers();
  if (data && !data.error) {
    _serverListCache = data;
    _serverListCacheTime = now;
  }
  return data;
}
// --- Action Modal (v14.2) ---
function _showActionModal(title, label, onSubmit) {
  var overlay = document.createElement("div");
  overlay.className = "modal-overlay";
  overlay.innerHTML = '<div class="modal-box" style="max-width:420px">' +
    '<div class="modal-header">' + ctx.esc(title) + '</div>' +
    '<div class="modal-body">' +
      '<label style="display:block;margin-bottom:8px;color:var(--text2);font-size:13px">' + ctx.esc(label) + '</label>' +
      '<input id="action-modal-input" type="text" style="width:100%;padding:10px 12px;border:1px solid var(--border);border-radius:var(--radius);background:var(--bg2);color:var(--text);font-size:14px;outline:none" autofocus />' +
    '</div>' +
    '<div class="modal-actions">' +
      '<button class="btn btn-secondary" data-action="cancel">取消</button>' +
      '<button class="btn btn-danger" data-action="submit">确认</button>' +
    '</div></div>';
  var input = overlay.querySelector("#action-modal-input");
  overlay.querySelector("[data-action=cancel]").onclick = function() { overlay.remove(); };
  overlay.querySelector("[data-action=submit]").onclick = function() { overlay.remove(); onSubmit(input.value); };
  input.addEventListener("keydown", function(e) { if (e.key === "Enter") { overlay.remove(); onSubmit(input.value); } });
  document.body.appendChild(overlay);
  setTimeout(function() { input.focus(); }, 50);
}


function invalidateServerCache() {
  _serverListCache = null;
  _serverListCacheTime = 0;
}


import {
  currentPage, serversSubTab, currentPlayerServerId,
  currentPlayers, currentTeamNames, servers,
  setCurrentPlayers, setCurrentTeamNames, setServers,
} from '../../state.js?v=1775700005';
import { api, toast, esc, hasPerm } from '../../utils.js?v=1775700005';
import { init as chartsInit, chartRangeChange, tickRangeChange, _chartRange, _tickRange, refreshOnlineChart, refreshTickChart, _onlineCanvas, _tickCanvas, saveChartContainers, restoreChartContainers } from './charts.js?v=1775700005';
import { init as mapInit, executeMapCommand, serverLayerPick, qcChangeMap, qcShowMapList, qcPickMap, qcDoChangeMap, qcQuickMap } from './map.js?v=1775700005';

// ─── Quick Command Groups ───
var QC_GROUPS = {
  map: {
    label: '地图',
    buttons: [
      { label: '查询当前地图', cmd: 'ShowCurrentMap' },
      { label: '查询下张地图', cmd: 'ShowNextMap' },
      { label: '更换地图', action: 'qcChangeMap' },
      { label: '预设地图', action: 'qcShowMapList' },
      { label: '重启本局', cmd: 'AdminRestartMatch', cls: 'warn' },
      { label: '结束对局', cmd: 'AdminEndMatch', cls: 'danger' },
    ]
  },
  server: {
    label: '服务器',
    buttons: [
      { label: '取消载具认领', on: 'AdminDisableVehicleClaiming 1', off: 'AdminDisableVehicleClaiming 0' },
      { label: '满载具刷新', on: 'AdminForceAllVehicleAvailability 1', off: 'AdminForceAllVehicleAvailability 0' },
      { label: '取消部署限制', on: 'AdminForceAllDeployableAvailability 1', off: 'AdminForceAllDeployableAvailability 0' },
      { label: '取消兵种限制', on: 'AdminForceAllRoleAvailability 1', off: 'AdminForceAllRoleAvailability 0' },
      { label: '可用敌方载具', on: 'AdminDisableVehicleTeamRequirement 1', off: 'AdminDisableVehicleTeamRequirement 0' },
      { label: '取消载具装要求', on: 'AdminDisableVehicleKitRequirement 1', off: 'AdminDisableVehicleKitRequirement 0' },
      { label: '取消复活时间', on: 'AdminNoRespawnTimer 1', off: 'AdminNoRespawnTimer 0' },
      { label: '服务器倍速', action: 'qcSpeedToggle', cls: 'warn' },
      { label: '重载插件', cmd: 'ReloadServerConfig', cls: 'warn' },
    ]
  },
  player: {
    label: '玩家',
    buttons: [
      { label: '下把打乱', action: 'qcScrambleTeams', cls: 'warn' },
      { label: '清理挂机', action: 'qcKickUnsquadded', cls: 'danger' },
      { label: '卸任指挥官', action: 'qcInputModal', params: '["AdminDemoteCommander","卸任指挥官",{"label":"Steam ID","placeholder":"输入SteamID"}]' },
      { label: '广播消息', action: 'qcInputModal', params: '["AdminBroadcast","广播消息",{"label":"消息内容","placeholder":"输入广播内容"}]' },
    ]
  }
};

// 作弊开关状态（按服务器ID隔离）
var cheatStates = {}; // { [serverId]: { [onCmd]: true/false } }

// 操作锁（防止并发）
var qcToggleLocked = false;
var activatePresetLocked = false;

// Relay 状态缓存 { [serverId]: { status: 'running'|'stopped'|'offline', gameServer: {...}, relay: {...} } }
// 已移除：面板直连 RCON，relay 只负责日志转发，不再需要状态显示

// 导出空函数保持兼容
window.setupRelayStatusListener = function() {};
window.fetchRelayStatus = function() {};
window.relayStatusCache = {};

// ─── Render Servers Page ───
export async function renderServers(el) {
  // Render throttle: skip if rendered less than 500ms ago
  var _now = Date.now();
  if (typeof window._lastServerRender !== 'undefined' && _now - window._lastServerRender < 500) {
    return;
  }
  window._lastServerRender = _now;
  var html = '';
  // Sync from window to handle ES module live-binding edge cases
  var _st = window.serversSubTab || serversSubTab;

  // Mobile tab bar (visible on small screens)
  html += '<div class="mobile-tab-bar">';
  var tabs = [['list','服务器列表'],['players','玩家管理'],['settings','服务器设置']];
  tabs.forEach(function(t) {
    var active = _st === t[0] ? ' active' : '';
    html += '<a class="mobile-tab' + active + '" data-action="switchServersTab" data-param="' + t[0] + '">' + t[1] + '</a>';
  });
  html += '</div>';

  if (_st === 'list') {
    // ── Server Status Bar ──
    html += '<div id="server-status-bar" class="server-status-bar">';
    html += '<span id="server-status-indicator">🟡 检测中...</span>';
    html += '</div>';

    // ── Quick Commands (sections) ──
    if (hasPerm('quick_commands')) {
    html += '<div class="card">';
    html += '<h3>快捷命令</h3>';

    var toggleSid = window._selectedServerId || null;

    // Render each group as a section
    Object.keys(QC_GROUPS).forEach(function(groupKey) {
      var group = QC_GROUPS[groupKey];
      html += '<div class="qc-section" style="margin-bottom:20px">';
      html += '<div class="qc-section-title" style="font-size:13px;color:var(--text2);font-weight:600;margin-bottom:10px;padding-bottom:6px;border-bottom:1px solid var(--border)">── ' + group.label + ' ──</div>';
      html += '<div class="qc-grid">';
      group.buttons.forEach(function(btn) {
        if (btn.on && btn.off) {
          // Toggle button
          var isOn = toggleSid && cheatStates[toggleSid] && cheatStates[toggleSid][btn.on];
          var cls = 'qc-btn qc-toggle ' + (isOn ? 'qc-toggle-on' : 'qc-toggle-off');
          html += '<button class="' + cls + '" data-action="qcToggle" data-params=\'' + JSON.stringify([btn.on, btn.off, btn.label]).replace(/'/g, "&#39;") + '\'>' + btn.label + '</button>';
        } else if (btn.cmd) {
          var cls = 'qc-btn' + (btn.cls ? ' ' + btn.cls : '');
          html += '<button class="' + cls + '" data-action="qcSend" data-param="' + btn.cmd + '">' + btn.label + '</button>';
        } else if (btn.action) {
          var cls = 'qc-btn' + (btn.cls ? ' ' + btn.cls : '');
          var dataParams = btn.params ? " data-params='" + btn.params + "'" : '';
          html += '<button class="' + cls + '" data-action="' + btn.action + '"' + dataParams + '>' + btn.label + '</button>';
        }
      });
      html += '</div></div>';
    });

    // ── Presets Section ──
    html += '<div class="qc-section" style="margin-bottom:20px">';
    html += '<div class="qc-section-title" style="font-size:13px;color:var(--text2);font-weight:600;margin-bottom:10px;padding-bottom:6px;border-bottom:1px solid var(--border)">── 预设 ──</div>';
    html += '<div id="presets-container" style="display:flex;flex-wrap:wrap;gap:8px;align-items:center">';
    html += '<span style="color:var(--text3);font-size:13px">加载中...</span>';
    html += '</div></div>';

    html += '</div>'; // end card
    } // end quick_commands permission check

    // Tick Rate 折线图
    html += '<div id="tick-chart-wrap" class="card" style="margin-bottom:16px;padding:16px">';
    html += '<div style="display:flex;align-items:center;justify-content:space-between;margin-bottom:12px;flex-wrap:wrap;gap:8px">';
    html += '<h3 style="margin:0;font-size:14px;color:var(--text2)">服务器 Tick Rate</h3>';
    html += '<div style="display:flex;gap:6px;align-items:center">';
    html += '<select id="tick-range" class="log-select" data-action="tickRangeChange" style="padding:4px 8px;font-size:12px">';
    var tickRanges = [['1h','1 小时'],['6h','6 小时'],['24h','24 小时'],['3d','3 天'],['7d','7 天'],['30d','30 天']];
    tickRanges.forEach(function(r) {
    html += '<option value="' + r[0] + '"' + (r[0] === _tickRange ? ' selected' : '') + '>' + r[1] + '</option>';
    });
    html += '</select>';
    html += '<button class="btn btn-sm" data-action="refreshTickChart">刷新</button>';
    html += '</div></div>';
    html += '<div style="position:relative;height:200px">';
    html += '<canvas id="tick-rate-chart"></canvas>';
    html += '</div></div>';

    // ── Server List with Auto-Refresh Control ──
    html += ctx.renderAutoRefreshControl('list');

    if (ctx.servers.length) {
      html += '<div class="card"><h3>服务器列表</h3>';
      html += '<table><tr><th>名称</th><th>地址</th><th>连接模式</th><th>备注</th><th>添加时间</th><th>操作</th></tr>';
      ctx.servers.forEach(function(s) {
        var modeLabel = { relay: 'Relay', local: '本地日志', remote_api: '远程API', external_api: '外部API' }[s.connectionMode] || 'Relay';
        var modeColor = { relay: 'var(--accent)', local: '#22c55e', remote_api: '#f59e0b', external_api: '#f59e0b' }[s.connectionMode] || 'var(--accent)';
        html += '<tr>';
        html += '<td style="font-weight:600;color:var(--text)">' + ctx.esc(s.name) + '</td>';
        html += '<td><span class="font-mono">' + ctx.esc(s.host) + '</span></td>';
        html += '<td><span style="color:' + modeColor + ';font-size:12px;font-weight:600">' + modeLabel + '</span>' + (s.connectionMode === 'relay' ? ' <span style="color:var(--text3);font-size:11px">:' + (s.rconPort || 27015) + '</span>' : '') + '</td>';
        html += '<td style="max-width:200px"><input class="server-notes-input" data-server-id="' + s.id + '" value="' + ctx.esc(s.notes || '') + '" placeholder="添加备注..." style="width:100%;padding:4px 8px;border-radius:4px;border:1px solid var(--border);font-size:12px;background:var(--bg2);color:var(--text)"></td>';
        html += '<td style="color:var(--text3)">' + s.createdAt + '</td>';
        html += '<td class="action-cell">';
        html += '<button class="btn btn-sm btn-success" data-action="relayControl" data-params=\'[' + s.id + ',"start"]\'>启动</button> ';
        html += '<button class="btn btn-sm btn-warn" data-action="relayControl" data-params=\'[' + s.id + ',"stop"]\'>停止</button> ';
        html += '<button class="btn btn-sm" data-action="showEditServerModal" data-param="' + s.id + '">编辑</button> ';
        html += '<button class="btn btn-sm" data-action="testServer" data-param="' + s.id + '">测试连接</button> ';
        html += '<button class="btn btn-sm btn-danger" data-action="deleteServer" data-param="' + s.id + '">删除</button>';
        html += '</td></tr>';
      });
      html += '</table></div>';
    } else {
      html += '<div class="empty"><div class="title">还没有添加服务器</div><div class="desc">点击上方「+ 添加服务器」开始使用</div></div>';
    }

  } else if (_st === 'players') {
    var playerRefreshExtra = '';
    if (ctx.servers.length) {
      playerRefreshExtra += '<button class="btn btn-primary" data-action="refreshPlayers">拉取玩家</button>';
      playerRefreshExtra += '<button class="btn btn-warn" data-action="qcScrambleTeams" data-param="' + (ctx.currentPlayerServerId || ctx.servers[0].id) + '">智能打乱</button>';
    }
    html += ctx.renderRefreshBar('players', playerRefreshExtra);

    // 在线人数趋势图
    html += '<div id="online-chart-wrap" class="card" style="margin-bottom:16px;padding:16px">';
    html += '<div style="display:flex;align-items:center;justify-content:space-between;margin-bottom:12px;flex-wrap:wrap;gap:8px">';
    html += '<h3 style="margin:0;font-size:14px;color:var(--text2)">玩家进入趋势</h3>';
    html += '<div style="display:flex;gap:6px;align-items:center">';
    html += '<select id="chart-range" class="log-select" data-action="chartRangeChange" style="padding:4px 8px;font-size:12px">';
    var rangeOptions = [['1d','1 天'],['3d','3 天'],['7d','7 天'],['15d','15 天'],['30d','1 个月'],['6m','6 个月'],['1y','1 年']];
    rangeOptions.forEach(function(r) {
      html += '<option value="' + r[0] + '"' + (r[0] === _chartRange ? ' selected' : '') + '>' + r[1] + '</option>';
    });
    html += '</select>';
    html += '<button class="btn btn-sm" data-action="refreshOnlineChart">刷新</button>';
    html += '</div></div>';
    html += '<div style="position:relative;height:200px">';
    html += '<canvas id="player-online-chart"></canvas>';
    html += '</div>';
    html += '</div>';

    if (ctx.currentPlayers.length) {
      var sid = ctx.currentPlayerServerId || 0;
      var tn1 = ctx.currentTeamNames['1'] || '阵营一';
      var tn2 = ctx.currentTeamNames['2'] || '阵营二';

      function renderTeamCard(teamId, teamName, teamColor, otherTeamId, otherTeamName) {
        var teamPlayers = ctx.currentPlayers.filter(function(x) { return x.teamId === teamId; });
        var teamSquads = ctx.currentSquads.filter(function(x) { return x.teamId === teamId; });
        var h = '<div class="card" style="padding:0;overflow:hidden">';
        h += '<div class="team-header">';
        h += '<div><span id="team-' + teamId + '-name" class="team-name" style="color:' + teamColor + '">' + ctx.esc(teamName) + '</span> <span id="team-' + teamId + '-count" class="team-count">' + teamPlayers.length + ' 人</span></div>';
        h += '</div>';
        h += '<div id="team-' + teamId + '-container">';
        if (!teamPlayers.length) {
          h += '<div class="empty" style="padding:32px">暂无玩家</div>';
        } else {
          var groups = {};
        teamPlayers.forEach(function(p) {
          var key = (p.squadId && p.squadId !== 'N/A' && p.squadId !== '-1') ? p.squadId : '_nosquad';
          if (!groups[key]) groups[key] = [];
          groups[key].push(p);
        });
        var squadNameMap = {};
        teamSquads.forEach(function(s) { squadNameMap[s.squadId] = s.squadName; });
        var squadKeys = Object.keys(groups).sort(function(a, b) {
          if (a === '_nosquad') return 1; if (b === '_nosquad') return -1;
          return parseInt(a) - parseInt(b);
        });

        var playerActions = function(x) {
          var h = '<button class="btn btn-xs" data-action="switchTeam" data-params=\'[' + sid + ',\"' + ctx.escAttr(x.steamId) + '\"]\' title="跳边">跳边</button> ';
          if (hasPerm('tk')) h += '<button class="btn btn-xs" data-action="showTkModal" data-params=\'[' + sid + ',"' + ctx.escAttr(x.steamId) + '","' + ctx.escAttr(x.name) + '"]\' title="TK">TK</button> ';
          h += '<button class="btn btn-xs" data-action="warnPlayerFrontend" data-params=\'[' + sid + ',"' + ctx.escAttr(x.steamId) + '","' + ctx.escAttr(x.name) + '"]\' title="私信">私信</button> ';
          if (hasPerm('kick')) h += '<button class="btn btn-xs btn-danger" data-action="kickPlayer" data-params=\'[' + sid + ',"' + ctx.escAttr(x.steamId) + '"]\' title="踢出">踢</button> <span style="display:inline-block;width:6px"></span> ';
          if (hasPerm('ban')) h += '<button class="btn btn-xs btn-danger" data-action="banPlayer" data-params=\'[' + sid + ',"' + ctx.escAttr(x.steamId) + '","' + ctx.escAttr(x.name) + '"]\' title="封禁">封</button>';
          return h;
        };

        squadKeys.forEach(function(key) {
          var players = groups[key];
          var squadIdAttr = ' data-squad-id="' + key + '" data-team-id="' + teamId + '"';
          if (key === '_nosquad') {
            h += '<div class="squad-group"' + squadIdAttr + '><div class="squad-label">未分配 <span class="squad-count" style="color:var(--text3)">(' + players.length + ')</span></div>';
          } else {
            var sName = squadNameMap[key] || ('Squad ' + key);
            var leader = players.filter(function(p) { return p.isLeader; })[0];
            h += '<div class="squad-group"' + squadIdAttr + '><div class="squad-label">' + ctx.esc(sName) + ' <span style="color:var(--text3);font-weight:400">#' + key + ' / <span class="squad-count">' + players.length + '</span>人' + (leader ? ' / ' + ctx.esc(leader.name) : '') + '</span></div>';
          }
          players.forEach(function(x) {
            var initial = (x.name || '?').charAt(0).toUpperCase();
            var leaderBadge = x.isLeader ? ' <span class="badge badge-blue" style="margin-left:6px">队长</span>' : '';
            h += '<div class="player-card" data-steam-id="' + ctx.escAttr(x.steamId) + '">';
            h += '<div class="player-info">';
            h += '<div class="player-avatar">' + initial + '</div>';
            h += '<div><div class="player-name">' + ctx.esc(x.name) + leaderBadge + '</div>';
            h += '<div class="player-meta">' + ctx.esc(x.steamId) + (x.playtime ? ' / ' + x.playtime : '') + '</div>';
            h += '</div></div>';
            h += '<div class="player-actions">' + playerActions(x) + '</div>';
            h += '</div>';
          });
          h += '</div>'; // end squad-group
        });
        }
        h += '</div>'; // end team container
        h += '</div>'; // end card
        return h;
      }

      html += '<div style="display:grid;grid-template-columns:1fr 1fr;gap:16px">';
      html += renderTeamCard('1', tn1, 'var(--accent)', '2', tn2);
      html += renderTeamCard('2', tn2, 'var(--red)', '1', tn1);
      html += '</div>';
    } else {
      html += '<div class="empty"><div class="title">暂无在线玩家</div></div>';
    }

  } else if (_st === 'settings') {
    // Status card
    html += '<div id="ss-status-card" class="card" style="margin-bottom:16px">';
    html += '<h3>服务器状态</h3>';
    html += '<div id="ss-status-content" style="color:var(--text3);font-size:13px">' + (window._ssStatusHtml || '点击「刷新状态」加载服务器信息') + '</div>';
    html += '</div>';

    // Danger zone
    html += '<div class="settings-section danger-zone">';
    html += '<h3 style="color:var(--red)">危险操作</h3>';
    html += '<div class="desc">关闭服务器将断开所有玩家连接，请谨慎操作。</div>';
    html += '<button class="btn btn-danger" data-action="ssCmd" data-param="Shutdown" data-confirm="确定关闭服务器？此操作不可撤销。">关闭服务器</button>';
    html += '</div>';

    // Name & Description
    html += '<div class="settings-section">';
    html += '<h3>服务器名称与介绍</h3>';
    html += '<div class="desc">修改游戏服务器列表中显示的名称和介绍。</div>';
    // Pre-fill name from DB (A2S_INFO auto-detected)
    var ssCur = ctx.servers.find(function(s) { return s.id === window._selectedServerId; });
    var curName = ssCur ? ssCur.name : '';
    html += '<div class="form-group"><label>服务器名称（游戏内显示）</label><input id="ss-name" value="' + ctx.esc(curName) + '" placeholder="输入新的服务器名称"></div>';
    html += '<div class="form-group"><label>服务器介绍（游戏内显示）</label><textarea id="ss-desc" rows="3" placeholder="输入新的服务器介绍" style="resize:vertical"></textarea></div>';
    html += '<div style="display:flex;gap:8px;margin-top:4px">';
    html += '<button class="btn btn-primary" data-action="ssSetName">修改名称</button>';
    html += '<button class="btn btn-primary" data-action="ssSetDesc">修改介绍</button>';
    html += '</div></div>';

    // Relay registration code (auto-load existing + status)
    var _regCode = '';
    var _regUsed = true;
    try { var _rs = await ctx.api('/relay/register-status'); _regCode = _rs.code || ''; _regUsed = _rs.used; } catch(e) {}
    html += '<div class="settings-section">';
    html += '<h3>中继注册码</h3>';
    html += '<div class="desc">relay.js 首次部署时需要此注册码来绑定服务器。生成后填入 relay.js 配置面板的「注册码」栏。</div>';
    html += '<div style="display:flex;gap:8px;align-items:center">';
    html += '<a class="btn btn-primary" href="/api/relay/download" download="squad-relay.exe" style="text-decoration:none">下载 squad-relay.exe</a>';
    html += '<span id="reg-code-display" class="font-mono" style="color:var(--accent)">' + ctx.esc(_regCode) + '</span>';
    html += '<span id="reg-code-status" style="font-size:12px;color:var(--text3);margin-left:8px">';
    if (_regCode && !_regUsed) {
      html += '<span style="color:#22c55e">已生成</span> · 未使用';
    } else if (_regUsed) {
      html += '<span style="color:var(--text3)">已使用</span>';
    }
    html += '</span>';
    html += '</div></div>';
  }

  _renderEl = el;

  // 保存图表容器（innerHTML 会销毁 DOM）
  saveChartContainers();

  el.innerHTML = html;

  // 恢复或重建图表
  restoreChartContainers(serversSubTab);

  // 空状态时自动选中第一个服务器并拉取玩家
  if (serversSubTab === 'players' && ctx.servers.length > 0 && currentPlayerServerId == 0) {
    var firstServerId = ctx.servers[0].id;
    ctx.setCurrentPlayerServerId(firstServerId);
    ctx.setSelectedServerId(firstServerId);
    refreshPlayersLive();
  }

  // 为备注输入框添加自动保存事件
  if (_st === 'list') {
    document.querySelectorAll('.server-notes-input').forEach(function(input) {
      input.addEventListener('blur', function() {
        var sid = this.getAttribute('data-server-id');
        var notes = this.value;
        saveServerNotes(sid, notes);
      });
      input.addEventListener('keydown', function(e) {
        if (e.key === 'Enter') {
          this.blur();
        }
      });
    });

    // 服务器选择器 change 事件由 topbar 全局处理

    // 加载预设
    initPresets();

    // 刷新服务器状态栏
    refreshServerStatus();
  }
}

// ─── Tab Switching ───
export function switchServersTab(tab) {
  window.serversSubTab = tab;
  if (tab === 'settings' && !hasPerm('settings')) { ctx.toast('无权限访问服务器设置', 'error'); return; }
  if (tab === 'tk-forgive' && !hasPerm('tk')) { ctx.toast('无权限访问TK管理', 'error'); return; }
  ctx.setServersSubTab(tab);
  document.querySelectorAll('#servers-subnav .nav-item').forEach(function(el) { el.classList.remove('active'); });
  var subSel = document.querySelector('#servers-subnav a[data-subnav="' + tab + '"]');
  if (subSel) subSel.classList.add('active');
  var parent = document.querySelector('a[data-nav="servers"]');
  if (parent) parent.classList.add('active');
  import('../../app.js?v=1775700005').then(function(m) { m.render(); });
}

// ─── Quick Commands (logic unchanged) ───
export function closeModal() { document.getElementById("modal-container").innerHTML = ""; }

function qcGetServer() {
  var sid = window._selectedServerId;
  if (!sid) { ctx.toast('请先选择服务器', 'error'); return null; }
  return sid;
}

export async function qcSend(cmd) {
  var sid = qcGetServer(); if (!sid) return;
  try {
    var r = await ctx.api('/rcon/send', { method: 'POST', body: { serverId: sid, command: cmd } });
    var msg = r.result && r.result.trim() ? r.result.trim() : '命令已执行（无返回值）';
    ctx.toast(cmd + ' ' + msg.substring(0, 100));
  } catch(e) { ctx.toast(cmd + ' 执行失败，请检查服务器连接', 'error'); }
}

// 开关按钮：点击切换 ON/OFF
export async function qcToggle(onCmd, offCmd, label) {
  // 防抖：检查锁状态
  if (qcToggleLocked) {
    ctx.toast('操作进行中，请稍候...', 'warning');
    return;
  }

  var sid = qcGetServer(); if (!sid) return;
  if (!cheatStates[sid]) cheatStates[sid] = {};
  var isOn = cheatStates[sid][onCmd];
  var cmd = isOn ? offCmd : onCmd;

  // 设置锁
  qcToggleLocked = true;

  try {
    var r = await ctx.api('/rcon/send', { method: 'POST', body: { serverId: sid, command: cmd } });
    var msg = r.result && r.result.trim() ? r.result.trim() : '命令已执行（无返回值）';
    // RCON 成功才更新状态
    cheatStates[sid][onCmd] = !isOn;
    ctx.toast(label + ' ' + (!isOn ? '已开启' : '已关闭') + ' — ' + msg.substring(0, 80));
    // 重新渲染当前页面以更新按钮颜色
    var el = document.getElementById('content');
    if (el) renderServers(el);
  } catch(e) {
    // RCON 失败，状态保持不变，提示用户
    ctx.toast(label + ' 操作失败: ' + (e.message || '请检查服务器连接'), 'error');
  } finally {
    qcToggleLocked = false;
  }
}

// ─── Preset Management ───
var presetsCache = []; // 全局预设列表

// 获取可配置的 toggle 列表（从 QC_GROUPS.server 提取）
function getToggleList() {
  var toggles = [];
  var serverGroup = QC_GROUPS.server;
  if (serverGroup && serverGroup.buttons) {
    serverGroup.buttons.forEach(function(btn) {
      if (btn.on && btn.off) {
        toggles.push({
          key: btn.on, // 使用 on 命令作为 key
          label: btn.label,
          onCmd: btn.on,
          offCmd: btn.off
        });
      }
    });
  }
  return toggles;
}

// 加载当前用户的预设列表
async function loadPresets() {
  try {
    var r = await ctx.api('/presets');
    presetsCache = r.presets || [];
    return r.presets || [];
  } catch (e) {
    console.error('加载预设失败:', e);
    return [];
  }
}

// 渲染预设按钮区域
function renderPresetsContainer() {
  var container = document.getElementById('presets-container');
  if (!container) return;

  var html = '';

  // 显示所有预设按钮
  if (presetsCache.length) {
    presetsCache.forEach(function(p) {
      html += '<button class="qc-btn" data-action="activatePreset" data-params=\'' + JSON.stringify([p.id, p.name, p.config]).replace(/'/g, "&#39;") + '\' style="background:var(--accent-bg);color:var(--accent)">' + ctx.esc(p.name) + '</button>';
    });
  }

  html += '<button class="qc-btn" data-action="showCreatePresetModal" style="background:var(--bg3);color:var(--text2)">+ 新建</button>';
  container.innerHTML = html;
}

// 加载并渲染预设
export async function initPresets() {
  await loadPresets();
  renderPresetsContainer();
}

// 激活预设
export async function activatePreset(presetId, presetName, config) {
  // 防抖：检查锁状态
  if (activatePresetLocked) {
    ctx.toast('操作进行中，请稍候...', 'warning');
    return;
  }

  var sid = qcGetServer();
  if (!sid) {
    ctx.toast('请先选择要应用预设的服务器', 'error');
    return;
  }

  // 设置锁
  activatePresetLocked = true;

  var toggles = getToggleList();
  var successCount = 0;
  var failCount = 0;
  var failedToggles = []; // 记录失败的 toggle 名称

  ctx.toast('正在激活预设: ' + presetName);

  // 初始化 cheatStates
  if (!cheatStates[sid]) cheatStates[sid] = {};

  for (var i = 0; i < toggles.length; i++) {
    var toggle = toggles[i];
    var value = config[toggle.key];

    // 只处理在 config 中明确设置的 toggle
    if (value === 'on' || value === 'off') {
      var cmd = value === 'on' ? toggle.onCmd : toggle.offCmd;
      try {
        await ctx.api('/rcon/send', { method: 'POST', body: { serverId: sid, command: cmd } });
        // 只有成功时才更新状态
        cheatStates[sid][toggle.key] = (value === 'on');
        successCount++;
      } catch (e) {
        failCount++;
        failedToggles.push(toggle.label);
        console.error('执行命令失败:', cmd, e);
      }
      // 避免发送太快
      await new Promise(function(r) { setTimeout(r, 100); });
    }
  }

  // 给用户详细反馈
  if (failCount > 0) {
    ctx.toast('预设「' + presetName + '」部分激活：' + successCount + ' 项成功，' + failCount + ' 项失败（' + failedToggles.slice(0, 3).join(', ') + (failedToggles.length > 3 ? '...' : '') + '）', 'warning');
  } else {
    ctx.toast('预设「' + presetName + '」已激活：全部 ' + successCount + ' 项成功');
  }

  // 重新渲染按钮状态
  var el = document.getElementById('content');
  if (el) renderServers(el);

  // 释放锁
  activatePresetLocked = false;
}

// 新建预设弹窗
export function showCreatePresetModal() {
  var toggles = getToggleList();
  var toggleHtml = toggles.map(function(t) {
    return '<tr>' +
      '<td style="padding:8px 0">' + ctx.esc(t.label) + '</td>' +
      '<td style="text-align:center"><label style="cursor:pointer"><input type="radio" name="preset-' + t.key + '" value="" checked> 不变</label></td>' +
      '<td style="text-align:center"><label style="cursor:pointer"><input type="radio" name="preset-' + t.key + '" value="on"> 开启</label></td>' +
      '<td style="text-align:center"><label style="cursor:pointer"><input type="radio" name="preset-' + t.key + '" value="off"> 关闭</label></td>' +
    '</tr>';
  }).join('');

  var html = '<div class="modal-overlay" data-action="closeModal">' +
    '<div class="modal" style="max-width:600px;max-height:80vh;overflow-y:auto">' +
    '<h3>新建预设</h3>' +
    '<div class="form-group"><label>预设名称</label><input id="preset-name" placeholder="例如：训练模式"></div>' +
    '<div style="margin-bottom:16px">' +
      '<table style="width:100%;font-size:13px">' +
      '<tr style="color:var(--text3)"><th style="text-align:left;padding:4px 0">功能</th><th style="text-align:center">不变</th><th style="text-align:center">开启</th><th style="text-align:center">关闭</th></tr>' +
      toggleHtml +
      '</table>' +
    '</div>' +
    '<div class="modal-actions">' +
      '<button class="btn" data-action="closeModal">取消</button>' +
      '<button class="btn btn-primary" data-action="createPreset">创建</button>' +
    '</div></div></div>';

  document.getElementById('modal-container').innerHTML = html;
}

// 创建预设
export async function createPreset() {
  var name = document.getElementById('preset-name').value.trim();
  if (!name) {
    ctx.toast('请输入预设名称', 'error');
    return;
  }

  var config = {};
  var toggles = getToggleList();
  toggles.forEach(function(t) {
    var radios = document.querySelectorAll('input[name="preset-' + t.key + '"]');
    radios.forEach(function(r) {
      if (r.checked && r.value) {
        config[t.key] = r.value;
      }
    });
  });

  try {
    var r = await ctx.api('/presets', {
      method: 'POST',
      body: { name: name, config: config }
    });

    if (r.error) {
      ctx.toast(r.error, 'error');
      return;
    }

    ctx.toast('预设创建成功');
    closeModal();
    await loadPresets();
    renderPresetsContainer();
  } catch (e) {
    ctx.toast('创建预设失败: ' + e.message, 'error');
  }
}

// 编辑预设弹窗
export function showEditPresetModal(presetId) {
  var preset = presetsCache.find(function(p) { return p.id === presetId; });
  if (!preset) return ctx.toast('预设不存在', 'error');

  var toggles = getToggleList();
  var toggleHtml = toggles.map(function(t) {
    var currentValue = preset.config[t.key] || '';
    return '<tr>' +
      '<td style="padding:8px 0">' + ctx.esc(t.label) + '</td>' +
      '<td style="text-align:center"><label style="cursor:pointer"><input type="radio" name="preset-' + t.key + '" value="" ' + (!currentValue ? 'checked' : '') + '> 不变</label></td>' +
      '<td style="text-align:center"><label style="cursor:pointer"><input type="radio" name="preset-' + t.key + '" value="on" ' + (currentValue === 'on' ? 'checked' : '') + '> 开启</label></td>' +
      '<td style="text-align:center"><label style="cursor:pointer"><input type="radio" name="preset-' + t.key + '" value="off" ' + (currentValue === 'off' ? 'checked' : '') + '> 关闭</label></td>' +
    '</tr>';
  }).join('');

  var html = '<div class="modal-overlay" data-action="closeModal">' +
    '<div class="modal" style="max-width:600px;max-height:80vh;overflow-y:auto">' +
    '<h3>编辑预设</h3>' +
    '<div class="form-group"><label>预设名称</label><input id="preset-name" value="' + ctx.esc(preset.name) + '"></div>' +
    '<div style="margin-bottom:16px">' +
      '<table style="width:100%;font-size:13px">' +
      '<tr style="color:var(--text3)"><th style="text-align:left;padding:4px 0">功能</th><th style="text-align:center">不变</th><th style="text-align:center">开启</th><th style="text-align:center">关闭</th></tr>' +
      toggleHtml +
      '</table>' +
    '</div>' +
    '<div class="modal-actions">' +
      '<button class="btn btn-danger" style="margin-right:auto" data-action="deletePreset" data-param="' + preset.id + '">删除</button>' +
      '<button class="btn" data-action="closeModal">取消</button>' +
      '<button class="btn btn-primary" data-action="updatePreset" data-param="' + preset.id + '">保存</button>' +
    '</div></div></div>';

  document.getElementById('modal-container').innerHTML = html;
}

// 更新预设
export async function updatePreset(presetId) {
  var name = document.getElementById('preset-name').value.trim();
  if (!name) {
    ctx.toast('请输入预设名称', 'error');
    return;
  }

  var config = {};
  var toggles = getToggleList();
  toggles.forEach(function(t) {
    var radios = document.querySelectorAll('input[name="preset-' + t.key + '"]');
    radios.forEach(function(r) {
      if (r.checked && r.value) {
        config[t.key] = r.value;
      }
    });
  });

  try {
    var r = await ctx.api('/presets/' + presetId, {
      method: 'PUT',
      body: { name: name, config: config }
    });

    if (r.error) {
      ctx.toast(r.error, 'error');
      return;
    }

    ctx.toast('预设已更新');
    closeModal();
    await loadPresets();
    renderPresetsContainer();
  } catch (e) {
    ctx.toast('更新预设失败: ' + e.message, 'error');
  }
}

// 删除预设
export async function deletePreset(presetId) {
  if (!confirm('确定删除该预设？')) return;

  try {
    var r = await ctx.api('/presets/' + presetId, { method: 'DELETE' });

    if (r.error) {
      ctx.toast(r.error, 'error');
      return;
    }

    ctx.toast('预设已删除');
    closeModal();
    await loadPresets();
    renderPresetsContainer();
  } catch (e) {
    ctx.toast('删除预设失败: ' + e.message, 'error');
  }
}

export async function qcInput(prefix, title, label) {
  var sid = qcGetServer(); if (!sid) return;
  var val = prompt(title + '\n请输入 ' + label + '：');
  if (val === null) return;
  if (prefix === 'AdminBroadcast') { val = '"' + val + '"'; }
  var cmd = prefix + ' ' + val;
  try {
    var r = await ctx.api('/rcon/send', { method: 'POST', body: { serverId: sid, command: cmd } });
    var msg = r.result && r.result.trim() ? r.result.trim() : '命令已执行（无返回值）';
    ctx.toast(cmd + ' ' + msg.substring(0, 100));
  } catch(e) { ctx.toast(cmd + ' 执行失败，请检查服务器连接', 'error'); }
}

export function qcInputModal(prefix, title, fields) {
  if (typeof fields === 'string') fields = [{ label: fields, placeholder: '' }];
  if (!Array.isArray(fields)) fields = [fields];
  var sid = qcGetServer(); if (!sid) return;
  var fieldsHtml = fields.map(function(f, i) {
    return '<div class="form-group"><label>' + ctx.esc(f.label) + '</label><input id="qim-field-' + i + '" placeholder="' + ctx.esc(f.placeholder || '') + '"></div>';
  }).join('');
  var defaultVals = fields.map(function(f) { return f.defaultValue || ''; });
  var h = '<div class="modal-overlay" data-action="closeModal">' +
    '<div class="modal" style="max-width:400px">' +
    '<h3>' + ctx.esc(title) + '</h3>' +
    fieldsHtml +
    '<div class="modal-actions">' +
    '<button class="btn" data-action="closeModal">取消</button>' +
    '<button class="btn btn-primary" data-action="qcInputModalSubmit" data-params=\'' + JSON.stringify([prefix, defaultVals]).replace(/'/g, "&#39;") + '\'>执行</button>' +
    '</div></div></div>';
  document.getElementById('modal-container').innerHTML = h;
  setTimeout(function() { var el = document.getElementById('qim-field-0'); if (el) el.focus(); }, 50);
}

export async function qcInputModalSubmit(prefix, defaults) {
  var parts = [];
  for (var i = 0; ; i++) {
    var el = document.getElementById('qim-field-' + i);
    if (!el) break;
    var val = el.value.trim();
    if (!val && defaults && defaults[i]) val = defaults[i];
    if (val.indexOf(' ') !== -1) val = '"' + val + '"';
    parts.push(val);
  }
  document.getElementById("modal-container").innerHTML = "";
  var cmd = prefix + ' ' + parts.join(' ');
  try {
    var sid = qcGetServer(); if (!sid) return;
    var r = await ctx.api('/rcon/send', { method: 'POST', body: { serverId: sid, command: cmd } });
    var msg = r.result && r.result.trim() ? r.result.trim() : '命令已执行（无返回值）';
    ctx.toast(msg.substring(0, 100));
  } catch(e) { ctx.toast('操作失败，请检查服务器连接', 'error'); }
}

export async function qcScrambleTeams(sid) {
  // 如果没有传入 sid，尝试从快捷命令区域获取
  if (!sid) {
    sid = qcGetServer();
    if (!sid) return;
  }
  try {
    ctx.toast('正在分析玩家数据...');
    var data = await ctx.api('/servers/' + sid + '/scramble/preview', { method: 'POST' });

    // 构建队伍列表 HTML
    function teamHtml(players, teamLabel) {
      var h = '<div style="flex:1"><h4 style="margin:0 0 8px">' + teamLabel + ' (' + players.length + '人)</h4>';
      h += '<table style="width:100%;font-size:13px;border-collapse:collapse">';
      h += '<tr style="border-bottom:1px solid #ddd"><th style="text-align:left;padding:4px">玩家</th><th style="text-align:right;padding:4px">时长</th><th style="text-align:right;padding:4px">K/D</th><th style="text-align:right;padding:4px">评分</th></tr>';
      for (var i = 0; i < players.length; i++) {
        var p = players[i];
        var hours = Math.floor(p.playtime / 3600);
        var mins = Math.floor((p.playtime % 3600) / 60);
        var timeStr = hours > 0 ? hours + 'h' + mins + 'm' : mins + 'm';
        h += '<tr style="border-bottom:1px solid #f0f0f0">';
        h += '<td style="padding:4px">' + ctx.esc(p.name) + '</td>';
        h += '<td style="text-align:right;padding:4px">' + timeStr + '</td>';
        h += '<td style="text-align:right;padding:4px">' + p.kd + ' (' + p.kills + '/' + p.deaths + ')</td>';
        h += '<td style="text-align:right;padding:4px;font-weight:bold">' + p.score + '</td>';
        h += '</tr>';
      }
      h += '</table></div>';
      return h;
    }

    var s = data.stats;
    var html = '<div class="modal-overlay" data-action="closeModal">'
      + '<div class="modal" style="min-width:700px;max-width:900px">'
      + '<h3>智能打乱预览</h3>'
      + '<p style="color:#666;font-size:13px;margin:0 0 12px">根据游戏时长(50%) + K/D(50%) 综合评分，蛇形分配均衡两队实力</p>'
      + '<div style="display:flex;gap:20px;margin-bottom:16px">'
      + '<div style="flex:1;background:#f0f7ff;padding:10px;border-radius:6px;text-align:center">'
      + '<div style="font-size:12px;color:#666">队伍1 总评分</div>'
      + '<div style="font-size:20px;font-weight:bold">' + s.team1.totalScore + '</div>'
      + '<div style="font-size:11px;color:#999">均时长 ' + Math.floor(s.team1.avgPlaytime / 3600) + 'h | 均K/D ' + s.team1.avgKD + '</div>'
      + '</div>'
      + '<div style="flex:1;background:#fff7f0;padding:10px;border-radius:6px;text-align:center">'
      + '<div style="font-size:12px;color:#666">队伍2 总评分</div>'
      + '<div style="font-size:20px;font-weight:bold">' + s.team2.totalScore + '</div>'
      + '<div style="font-size:11px;color:#999">均时长 ' + Math.floor(s.team2.avgPlaytime / 3600) + 'h | 均K/D ' + s.team2.avgKD + '</div>'
      + '</div>'
      + '</div>'
      + '<div style="display:flex;gap:20px;margin-bottom:16px">'
      + teamHtml(data.team1, '队伍1')
      + teamHtml(data.team2, '队伍2')
      + '</div>'
      + '<p style="color:#999;font-size:12px;margin:0 0 12px">共 ' + data.total + ' 名玩家，需跳边 ' + data.needSwitch + ' 人</p>'
      + '<div class="modal-actions">'
      + '<button class="btn" data-action="closeModal">取消</button>'
      + '<button class="btn btn-warn" data-action="qcScrambleExecute" data-param="' + sid + '">确认打乱</button>'
      + '</div>'
      + '</div></div>';

    document.getElementById('modal-container').innerHTML = html;
  } catch(e) {
    ctx.toast('打乱预览失败: ' + e.message, 'error');
  }
}

export async function qcScrambleExecute(sid) {
  closeModal();
  try {
    ctx.toast('正在执行智能打乱...');
    var r = await ctx.api('/servers/' + sid + '/scramble/execute', { method: 'POST' });
    if (r.switched === 0) {
      ctx.toast(r.message || '当前队伍已最优，无需调整', 'info');
    } else {
      ctx.toast('已打乱完成：' + r.switched + ' 人跳边（共 ' + r.total + ' 人）');
    }
  } catch(e) {
    ctx.toast('打乱执行失败: ' + e.message, 'error');
  }
}

export async function qcKickUnsquadded() {
  var sid = qcGetServer(); if (!sid) return;
  try {
    var r1 = await ctx.api('/rcon/send', { method: 'POST', body: { serverId: sid, command: 'ListPlayers' } });
    var playersRaw = r1.result || '';
    var unsquadded = [];
    playersRaw.split('\n').forEach(function(line) {
      var idMatch = line.match(/ID:\s*(\d+)/);
      var steamMatch = line.match(/SteamID:\s*(\d+)/i) || line.match(/steam:\s*(\d{17})/i);
      var nameMatch = line.match(/Name:\s*(.+?)(?:\s*$|\s*Team)/);
      var squadMatch = line.match(/Squad ID:\s*(\d+|-1|N\/A)/);
      if (idMatch && steamMatch) {
        var sq = squadMatch ? squadMatch[1] : '-1';
        if (sq === '-1' || sq === 'N/A' || sq === '0') {
          unsquadded.push({ rconId: parseInt(idMatch[1]), steamId: steamMatch[1], name: nameMatch ? nameMatch[1].trim() : '?' });
        }
      }
    });
    if (!unsquadded.length) { ctx.toast('没有未加入小队的玩家'); return; }
    if (!confirm('确定踢出 ' + unsquadded.length + ' 名未加入小队的玩家？\n\n' + unsquadded.map(function(p) { return '• ' + p.name + ' (' + p.steamId + ')'; }).join('\n'))) return;
    var kicked = 0, failed = 0;
    for (var i = 0; i < unsquadded.length; i++) {
      var p = unsquadded[i];
      try {
        await ctx.api('/rcon/send', { method: 'POST', body: { serverId: sid, command: 'AdminKick "' + p.steamId + '" 未加入小队，自动清理' } });
        kicked++;
      } catch(e) { failed++; }
      await new Promise(function(res) { setTimeout(res, 200); });
    }
    ctx.toast('清理完成: ' + kicked + ' 人踢出' + (failed > 0 ? ', ' + failed + ' 人失败' : ''));
  } catch(e) { ctx.toast('操作失败: ' + e.message, 'error'); }
}

// ─── Server CRUD ───
// ─── Connection Mode Form Helper ───
function connectionModeHtml(prefix, server) {
  var s = server || {};
  var mode = s.connectionMode || 'relay';
  var h = '';
  h += '<div class="form-group"><label>连接模式</label>';
  h += '<select id="' + prefix + '-connMode" data-action="connModeChange" data-prefix="' + prefix + '">';
  h += '<option value="relay"' + (mode === 'relay' ? ' selected' : '') + '>Relay 转发器（默认）</option>';
  h += '<option value="local"' + (mode === 'local' ? ' selected' : '') + '>本地日志直读</option>';
  h += '<option value="remote_api"' + (mode === 'remote_api' ? ' selected' : '') + '>远程 API</option>';
  h += '<option value="external_api">外部 API（只读）</option>';
  h += '</select></div>';

  // Relay fields (default visible)
  h += '<div id="' + prefix + '-relay-fields">';
  h += '<div class="form-row"><div class="form-group"><label>RCON端口</label><input id="' + prefix + '-port" type="number" value="' + (s.rconPort || 27015) + '"></div>';
  h += '<div class="form-group"><label>RCON密码' + (server ? '（留空不修改）' : '') + '</label><input id="' + prefix + '-pass" type="password" placeholder="' + (server ? '不修改则留空' : 'RCON密码') + '"></div></div>';
  h += '</div>';

  // Local fields
  h += '<div id="' + prefix + '-local-fields" style="display:' + (mode === 'local' ? 'block' : 'none') + '">';
  h += '<div class="form-group"><label>日志文件路径</label><input id="' + prefix + '-logPath" value="' + ctx.esc(s.logPath || '') + '" placeholder="/home/squad/SquadGame/Saved/Logs/SquadGame.log"></div>';
  h += '</div>';

  // Remote API fields
  h += '<div id="' + prefix + '-remote-fields" style="display:' + (mode === 'remote_api' ? 'block' : 'none') + '">';
  h += '<div class="form-group"><label>API 地址</label><input id="' + prefix + '-apiUrl" value="' + ctx.esc(s.remoteApiUrl || '') + '" placeholder="https://panel.example.com:8443"></div>';
  h += '<div class="form-group"><label>API Token</label><input id="' + prefix + '-apiToken" type="password" value="" placeholder="' + (server ? '留空不修改' : 'Bearer Token 或 API Key') + '"></div>';
  h += '<div class="form-group"><label>API 类型</label>';
  h += '<select id="' + prefix + '-apiType">';
  var apiType = s.apiType || 'self';
  h += '<option value="self"' + (apiType === 'self' ? ' selected' : '') + '>薯条面板（另一个实例）</option>';
  h += '<option value="generic"' + (apiType === 'generic' ? ' selected' : '') + '>通用 REST API</option>';
  h += '<option value="plugin_squad"' + (apiType === 'plugin_squad' ? ' selected' : '') + '>Plugin.squad.cyou</option>';
  h += '</select></div>';
  // apiConfig (JSON) - only for generic type
  var apiConfigStr = '';
  try { apiConfigStr = typeof s.apiConfig === 'string' ? s.apiConfig : JSON.stringify(s.apiConfig || {}); } catch(e) { apiConfigStr = '{}'; }
  if (apiConfigStr === '{}') apiConfigStr = '';
  h += '<div id="' + prefix + '-apiConfig-wrap" style="display:' + (apiType === 'generic' ? 'block' : 'none') + '">';
  h += '<div class="form-group"><label>API 配置 (JSON)</label><textarea id="' + prefix + '-apiConfig" rows="6" style="resize:vertical;font-family:monospace;font-size:12px" placeholder=\'{"authHeader":"X-API-KEY","authPrefix":"","players":{"endpoint":"/api/players","arrayField":"players","steamIdField":"steamId","nameField":"name"}}\'>' + ctx.esc(apiConfigStr) + '</textarea></div>';
  h += '</div>';
  // Plugin.squad.cyou fields
  var psApiKey = '';
  var psServerId = '';
  if (apiType === 'plugin_squad' && s.apiConfig) {
    try { var cfg = typeof s.apiConfig === 'string' ? JSON.parse(s.apiConfig) : s.apiConfig; psServerId = cfg.server_id || ''; } catch(e) {}
  }
  h += '<div id="' + prefix + '-plugin-squad-wrap" style="display:' + (apiType === 'plugin_squad' ? 'block' : 'none') + '">';
  h += '<div class="form-group"><label>API Key</label><input id="' + prefix + '-psApiKey" type="password" value="" placeholder="' + (server ? '留空不修改' : 'rcon_xxxxxxxx') + '"></div>';
  h += '<div class="form-group"><label>服务器 ID (Server ID)</label><input id="' + prefix + '-psServerId" value="' + ctx.esc(psServerId) + '" placeholder="0ba8446d-5bd5-4221-b64e-140e3b1d1c44"></div>';
  h += '</div>';
  h += '</div>';

  return h;
}

// Dynamic show/hide for connection mode fields
window.connModeChange = function(prefix) {
  var mode = document.getElementById(prefix + '-connMode').value;
  var relayEl = document.getElementById(prefix + '-relay-fields');
  var localEl = document.getElementById(prefix + '-local-fields');
  var remoteEl = document.getElementById(prefix + '-remote-fields');
  if (relayEl) relayEl.style.display = mode === 'relay' ? 'block' : 'none';
  if (localEl) localEl.style.display = mode === 'local' ? 'block' : 'none';
  if (remoteEl) remoteEl.style.display = mode === 'remote_api' ? 'block' : 'none';
};
// Also register as action for data-action dispatch
export function connModeChange(el) {
  var prefix = el.getAttribute('data-prefix');
  if (prefix) window.connModeChange(prefix);
}

// apiType change handler
window.apiTypeChange = function(prefix) {
  var apiType = document.getElementById(prefix + '-apiType').value;
  var configWrap = document.getElementById(prefix + '-apiConfig-wrap');
  if (configWrap) configWrap.style.display = apiType === 'generic' ? 'block' : 'none';
  var psWrap = document.getElementById(prefix + '-plugin-squad-wrap');
  if (psWrap) psWrap.style.display = apiType === 'plugin_squad' ? 'block' : 'none';
};

export function showAddServerModal() {
  var h = '<div class="modal-overlay" data-action="closeModal"><div class="modal" style="max-width:560px"><h3>添加服务器</h3>';
  h += '<div class="form-group"><label>服务器名称</label><input id="m-name" placeholder="我的Squad服"></div>';
  h += '<div class="form-group"><label>服务器地址</label><input id="m-host" placeholder="1.2.3.4"></div>';
  h += connectionModeHtml('m', null);
  h += '<div class="modal-actions"><button class="btn" data-action="closeModal">取消</button><button class="btn btn-primary" data-action="addServer">添加</button></div>';
  h += '</div></div>';
  document.getElementById("modal-container").innerHTML = h;
  // Bind apiType change
  var apiTypeEl = document.getElementById('m-apiType');
  if (apiTypeEl) apiTypeEl.addEventListener('change', function() { window.apiTypeChange('m'); });
}

export async function addServer() {
  var name = document.getElementById('m-name').value, host = document.getElementById('m-host').value;
  if (!name || !host) return ctx.toast('请填写名称和地址', 'error');
  var connMode = document.getElementById('m-connMode').value;
  var body = { name: name, host: host, connectionMode: connMode };
  if (connMode === 'relay') {
    body.rconPort = parseInt(document.getElementById('m-port').value) || 27015;
    body.rconPassword = document.getElementById('m-pass').value;
    if (!body.rconPassword) return ctx.toast('Relay 模式需要 RCON 密码', 'error');
  } else if (connMode === 'local') {
    body.logPath = document.getElementById('m-logPath').value;
    if (!body.logPath) return ctx.toast('请输入日志文件路径', 'error');
    body.rconPort = 0;
    body.rconPassword = '';
  } else if (connMode === 'remote_api') {
    body.remoteApiUrl = document.getElementById('m-apiUrl').value;
    body.remoteApiToken = document.getElementById('m-apiToken').value;
    body.apiType = document.getElementById('m-apiType').value;
    if (body.apiType === 'plugin_squad') {
      body.remoteApiUrl = 'https://plugin.squad.cyou';
      body.remoteApiToken = document.getElementById('m-psApiKey').value;
      var psSid = document.getElementById('m-psServerId').value.trim();
      if (!body.remoteApiToken) return ctx.toast('请输入 API Key', 'error');
      if (!psSid) return ctx.toast('请输入服务器 ID', 'error');
      body.apiConfig = JSON.stringify({server_id: psSid});
    } else {
      if (!body.remoteApiUrl) return ctx.toast('请输入 API 地址', 'error');
      if (body.apiType === 'generic') {
        var cfgStr = document.getElementById('m-apiConfig').value.trim();
        if (cfgStr) {
          try { JSON.parse(cfgStr); } catch(e) { return ctx.toast('API 配置 JSON 格式错误', 'error'); }
        }
        body.apiConfig = cfgStr || '{}';
      }
    }
    body.rconPort = 0;
    body.rconPassword = '';
  }
  var r = await ctx.api('/servers', { method: 'POST', body: body });
  document.getElementById("modal-container").innerHTML = '';
  if (r.apiKey) {
    document.getElementById("modal-container").innerHTML = '<div class="modal-overlay" data-action="closeModal"><div class="modal"><h3>服务器添加成功</h3><p style="margin:12px 0;color:var(--text2)">请复制下面的 API Key，填入游戏服务器的 relay.js 配置中：</p><div style="background:var(--gray-950);padding:12px;border-radius:8px;font-family:monospace;font-size:13px;word-break:break-all;user-select:all;margin-bottom:12px">' + ctx.esc(r.apiKey) + '</div><button class="btn btn-primary" data-copy="' + ctx.escAttr(r.apiKey) + '">复制并关闭</button></div></div>';
  } else {
    ctx.toast('服务器添加成功');
  }
  import('../../app.js?v=1775700005').then(function(m) { m.render(); });
}

export function showEditServerModal(id) {
  id = parseInt(id);
  var s = ctx.servers.find(function(x) { return x.id === id; });
  if (!s) return;
  var h = '<div class="modal-overlay" data-action="closeModal"><div class="modal" style="max-width:560px"><h3>编辑服务器</h3>';
  h += '<div class="form-group"><label>服务器名称</label><input id="me-name" value="' + ctx.esc(s.name) + '"></div>';
  h += '<div class="form-group"><label>服务器地址</label><input id="me-host" value="' + ctx.esc(s.host) + '"></div>';
  h += connectionModeHtml('me', s);
  h += '<div class="form-group"><label>备注/介绍</label><textarea id="me-notes" rows="3" style="resize:vertical">' + (s.notes || '') + '</textarea></div>';
  h += '<div class="modal-actions"><button class="btn" data-action="closeModal">取消</button><button class="btn btn-primary" data-action="updateServer" data-param="' + id + '">保存</button></div>';
  h += '</div></div>';
  document.getElementById('modal-container').innerHTML = h;
  // Bind apiType change
  var apiTypeEl = document.getElementById('me-apiType');
  if (apiTypeEl) apiTypeEl.addEventListener('change', function() { window.apiTypeChange('me'); });
  // Pre-fill plugin_squad API key hint
  if (s.apiType === 'plugin_squad' && s.remoteApiUrl) {
    var psKeyEl = document.getElementById('me-psApiKey');
    if (psKeyEl) psKeyEl.placeholder = '留空不修改';
  }
}

export async function updateServer(id) {
  var name = document.getElementById('me-name').value;
  var host = document.getElementById('me-host').value;
  var notes = document.getElementById('me-notes').value;
  if (!name || !host) return ctx.toast('请填写名称和地址', 'error');
  var connMode = document.getElementById('me-connMode').value;
  var body = { name: name, host: host, connectionMode: connMode };
  if (connMode === 'relay') {
    var port = parseInt(document.getElementById('me-port').value);
    if (port) body.rconPort = port;
    var pass = document.getElementById('me-pass').value;
    if (pass) body.rconPassword = pass;
  } else if (connMode === 'local') {
    body.logPath = document.getElementById('me-logPath').value;
    if (!body.logPath) return ctx.toast('请输入日志文件路径', 'error');
  } else if (connMode === 'remote_api') {
    body.remoteApiUrl = document.getElementById('me-apiUrl').value;
    var apiToken = document.getElementById('me-apiToken').value;
    if (apiToken) body.remoteApiToken = apiToken;
    body.apiType = document.getElementById('me-apiType').value;
    if (body.apiType === 'plugin_squad') {
      body.remoteApiUrl = 'https://plugin.squad.cyou';
      var psKey = document.getElementById('me-psApiKey').value;
      if (psKey) body.remoteApiToken = psKey;
      var psSid = document.getElementById('me-psServerId').value.trim();
      if (psSid) body.apiConfig = JSON.stringify({server_id: psSid});
    } else {
      if (body.apiType === 'generic') {
        var cfgStr = document.getElementById('me-apiConfig').value.trim();
        if (cfgStr) {
          try { JSON.parse(cfgStr); } catch(e) { return ctx.toast('API 配置 JSON 格式错误', 'error'); }
        }
        body.apiConfig = cfgStr || '{}';
      }
    }
  }
  await ctx.api('/servers/' + id, { method: 'PUT', body: body });
  if (notes !== undefined) await ctx.api('/servers/' + id + '/notes', { method: 'PUT', body: { notes: notes } });
  document.getElementById("modal-container").innerHTML = '';
  ctx.toast('服务器已更新');
  import('../../app.js?v=1775700005').then(function(m) { m.render(); });
}

export async function deleteServer(id) {
  if (!confirm('⚠️ 确定删除该服务器吗？ \( 将同时删除所有关联数据，不可撤销！)')) return;
  await ctx.api('/servers/' + id, { method: 'DELETE' });
  ctx.toast('已删除');
  import('../../app.js?v=1775700005').then(function(m) { m.render(); });
}

export async function testServer(id) {
  ctx.toast('正在测试连接...');
  var r = await ctx.api('/rcon/test', { method: 'POST', body: { serverId: id } });
  ctx.toast(r.success ? '连接成功！' : '连接失败: ' + r.error, r.success ? 'success' : 'error');
}

// ─── Relay 游戏服务器控制（已移除，面板直连 RCON）───
export async function relayControl(serverId, actionType) {
  var actionText = actionType === 'start' ? '启动' : '停止';
  if (!confirm('确定要' + actionText + '该游戏服务器吗？')) return;

  try {
    var r = await ctx.api('/relay/' + serverId + '/' + actionType, { method: 'POST' });
    if (r.error) {
      ctx.toast(r.error, 'error');
    } else {
      ctx.toast('服务器' + actionText + '命令已发送');
    }
  } catch (e) {
    ctx.toast('请求失败: ' + e.message, 'error');
  }
}

// ─── Player Operations ───
export async function switchTeam(serverId, steamId) {
  serverId = (serverId && serverId !== 0) ? serverId : ctx.currentPlayerServerId;
  if (!serverId) return ctx.toast('请先选择服务器', 'error');
  if (!steamId) {
    return ctx.toast('缺少玩家 SteamID', 'error');
  }
  var r = await ctx.api('/players/switch-team', { method: 'POST', body: { serverId: parseInt(serverId), steamId: steamId } });
  if (r.error) return ctx.toast(r.error || '跳边失败', 'error');
  ctx.toast('已执行跳边');
  refreshPlayersLive();
}

export async function kickPlayer(serverId, steamId) {
  serverId = (serverId && serverId !== 0) ? serverId : ctx.currentPlayerServerId;
  if (!serverId) return ctx.toast('请先选择服务器', 'error');
  _showActionModal('踢出玩家', '踢出原因（可留空）:', async function(reason) {
    var r = await ctx.api('/players/kick', { method: 'POST', body: { serverId: parseInt(serverId), steamId: steamId, reason: reason || 'Kicked by admin' } });
    if (r.error) return ctx.toast(r.error || '踢出失败', 'error');
    ctx.toast('已踢出玩家');
    refreshPlayersLive();
  });
}

export async function warnPlayerFrontend(serverId, steamId, playerName) {
  serverId = (serverId && serverId !== 0) ? serverId : ctx.currentPlayerServerId;
  if (!serverId) return ctx.toast('请先选择服务器', 'error');
  _showActionModal('私信 ' + (playerName || steamId), '消息内容:', async function(msg) {
    if (!msg) return ctx.toast('消息不能为空', 'error');
    var r = await ctx.api('/rcon/send', { method: 'POST', body: { serverId: parseInt(serverId), command: 'AdminWarn "' + steamId + '" ' + msg } });
    if (r.error) return ctx.toast(r.error || '私信发送失败', 'error');
    ctx.toast('已私信 ' + (playerName || steamId));
  });
}

export async function banPlayer(serverId, steamId, playerName) {
  serverId = (serverId && serverId !== 0) ? serverId : ctx.currentPlayerServerId;
  if (!serverId) return ctx.toast('请先选择服务器', 'error');
  _showActionModal('封禁 ' + (playerName || steamId), '封禁原因（可留空）:', async function(reason) {
    var r = await ctx.api('/bans', { method: 'POST', body: { serverId: parseInt(serverId), steamId: steamId, playerName: playerName, reason: reason || 'Banned by admin' } });
    if (r.error) return ctx.toast(r.error || '封禁失败', 'error');
    ctx.toast('已封禁并踢出玩家');
    refreshPlayersLive();
  });
}

export async function refreshPlayers() {
  var sid = window._selectedServerId;
  if (!sid) return ctx.toast('请选择服务器', 'error');
  ctx.setCurrentPlayerServerId(parseInt(sid));
  ctx.toast('正在从服务器拉取玩家数据...');
  var r = await ctx.api('/players/refresh', { method: 'POST', body: { serverId: parseInt(sid) } });
  if (r.error) return ctx.toast(r.error || '操作失败', 'error');
  ctx.setCurrentPlayers(r.players || []);
  ctx.setCurrentSquads(r.squads || []);
  ctx.setCurrentTeamNames(r.teamNames || {});
  ctx.toast('已刷新，当前 ' + (r.players || []).length + ' 名在线玩家');
  ctx.setServersSubTab('players');
  // 只更新玩家列表区域，不调用 render() 避免阵营名称闪烁
  if (currentPage === 'servers' && serversSubTab === 'players') {
    updatePlayersDisplay(r.players || [], r.teamNames || {});
  }
}

// 从 relay 推送的内存数据刷新（自动刷新用，不走 RCON）
export async function refreshPlayersLive() {
  var sid = currentPlayerServerId;
  try {
    var r = await api('/players/live?serverId=' + sid);
    setCurrentPlayers(r && r.players ? r.players : []);
    // squads 通过 ctx 或直接设置（currentSquads 不在 state.js，用 ctx）
    if (ctx && ctx.setCurrentSquads) ctx.setCurrentSquads(r && r.squads ? r.squads : []);
    setCurrentTeamNames(r && r.teamNames ? r.teamNames : {});
    // 只在玩家管理 Tab 下增量更新，避免 DOM 重建导致阵营名称闪烁
    if (currentPage === 'servers' && serversSubTab === 'players') {
      updatePlayersDisplay(r && r.players ? r.players : [], r && r.teamNames ? r.teamNames : {});
    }
  } catch(e) {
  }
}

// ─── TK Forgive ───
export async function showTkModal(serverId, steamId, name) {
  serverId = (serverId && serverId !== 0) ? serverId : ctx.currentPlayerServerId;
  if (!serverId) return ctx.toast('请先选择服务器', 'error');
  var activePlayers = ctx.currentPlayers.filter(function(p) { return p.steamId !== steamId; });
  var victimOpts = activePlayers.map(function(p) { return '<option value="' + ctx.esc(p.steamId) + '" data-name="' + ctx.esc(p.name) + '">' + ctx.esc(p.name) + ' (' + ctx.esc(p.steamId) + ')</option>'; }).join('');
  document.getElementById('modal-container').innerHTML =
    '<div class="modal-overlay" data-action="closeModal">' +
    '<div class="modal" style="max-width:420px">' +
    '<h3>TK 惩罚</h3>' +
    '<p style="color:var(--text2);font-size:13px;margin-bottom:16px">玩家 <b style="color:var(--text)">' + ctx.esc(name) + '</b> 击杀了队友，启动道歉倒计时。</p>' +
    '<div class="form-group"><label>受害者（可选）</label><select id="tk-victim-steam"><option value="">-- 未知 --</option>' + victimOpts + '</select></div>' +
    '<div style="background:var(--bg3);padding:14px;border-radius:var(--radius);font-size:13px;color:var(--text2);margin-bottom:16px;line-height:1.6">倒计时期间，击杀者在公屏输入 <b style="color:var(--accent)">sor</b>、<b style="color:var(--accent)">sorry</b> 或 <b style="color:var(--accent)">soy</b> 即可免除惩罚。超时自动踢出。</div>' +
    '<div class="modal-actions">' +
    '<button class="btn btn-danger" data-action="createTkForgive" data-params=\'[' + serverId + ',"' + ctx.escAttr(steamId) + '","' + ctx.escAttr(name) + '"]\'>启动惩罚</button>' +
    '</div></div></div>';
}

export async function createTkForgive(serverId, killerSteamId, killerName) {
  var victimEl = document.getElementById('tk-victim-steam');
  var victimSteamId = victimEl ? victimEl.value : '';
  var victimName = '';
  if (victimSteamId && victimEl.selectedIndex > 0) {
    victimName = victimEl.options[victimEl.selectedIndex].getAttribute('data-name') || '';
  }
  document.getElementById("modal-container").innerHTML = '';
  try {
    var r = await ctx.api('/tk-forgive', { method: 'POST', body: { serverId: parseInt(serverId), killerSteamId: killerSteamId, killerName: killerName, victimSteamId: victimSteamId || null, victimName: victimName || null } });
    ctx.toast('TK惩罚已启动，倒计时 ' + r.seconds + ' 秒', 'warn');
  } catch(e) { ctx.toast('TK 惩罚启动失败，请检查服务器连接', 'error'); }
}

export function switchTkTab(tab) {
  setTkForgiveSubTab(tab);
  import('../../app.js?v=1775700005').then(function(m) { m.render(); });
}

export async function forgiveTk(id) {
  if (!confirm('确定原谅该玩家？')) return;
  await ctx.api('/tk-forgive/' + id + '/forgive', { method: 'POST' });
  ctx.toast('已原谅');
  import('../../app.js?v=1775700005').then(function(m) { m.render(); });
}

export async function kickTk(id) {
  if (!confirm('确定立即踢出？')) return;
  var r = await ctx.api('/tk-forgive/' + id + '/kick', { method: 'POST' });
  ctx.toast(r.rcon ? '已踢出' : '踢出执行中');
  import('../../app.js?v=1775700005').then(function(m) { m.render(); });
}

export async function deleteTk(id) {
  if (!confirm('确定删除该记录？')) return;
  await ctx.api('/tk-forgive/' + id, { method: 'DELETE' });
  ctx.toast('已删除');
  import('../../app.js?v=1775700005').then(function(m) { m.render(); });
}

export async function saveTkConfig() {
  var secs = document.getElementById('tk-cfg-seconds').value;
  var keywords = document.getElementById('tk-cfg-keywords').value;
  var enabled = document.getElementById('tk-cfg-enabled').checked;
  await ctx.api('/config', { method: 'POST', body: { tk_forgive_seconds: parseInt(secs) || 180, tk_forgive_keywords: keywords || 'sor,sorry,soy', tk_forgive_enabled: enabled ? 1 : 0 } });
  ctx.toast('TK 设置已保存');
}

// ─── AFK Settings ───
export async function qcAfkSettings() {
  var sid = qcGetServer(); if (!sid) return;
  try {
    var afkCfg = (await ctx.api('/config')).config || {};
    var secs = afkCfg.afk_kick_seconds || '300';
    var enabled = afkCfg.afk_kick_enabled !== '0';
    var mins = Math.floor(parseInt(secs) / 60);
    var h = '<div class="modal-overlay" data-action="closeModal">' +
      '<div class="modal" style="max-width:400px">' +
      '<h3>挂机自动踢出</h3>' +
      '<div class="form-group"><label>超时时长（秒）</label><input id="afk-modal-seconds" type="number" value="' + secs + '"></div>' +
      '<div style="margin:8px 0;color:var(--text3);font-size:13px">当前: ' + mins + ' 分钟</div>' +
      '<div style="margin-bottom:16px"><label style="display:flex;align-items:center;gap:8px;cursor:pointer"><input type="checkbox" id="afk-modal-enabled"' + (enabled ? ' checked' : '') + '> 启用</label></div>' +
      '<div class="modal-actions">' +
      '<button class="btn" data-action="qcAfkSave">保存设置</button>' +
      '<button class="btn" data-action="qcAfkCleanNow">立即清理</button>' +
      '</div></div></div>';
    document.getElementById("modal-container").innerHTML = h;
  } catch(e) { ctx.toast("加载失败: " + e.message, "error"); }
}

export async function qcAfkSave() {
  var secs = parseInt(document.getElementById("afk-modal-seconds").value) || 300;
  var enabled = document.getElementById("afk-modal-enabled").checked;
  await ctx.api("/config", { method: "POST", body: { afk_kick_seconds: secs, afk_kick_enabled: enabled ? 1 : 0 } });
  document.getElementById("modal-container").innerHTML = "";
  ctx.toast("挂机踢出设置已保存");
}

export async function qcAfkCleanNow() {
  var sid = qcGetServer(); if (!sid) return;
  if (!confirm("确定立即清理挂机玩家？")) return;
  document.getElementById("modal-container").innerHTML = "";
  ctx.toast("正在扫描...");
  try {
    var r = await ctx.api("/players/afk-kick", { method: "POST", body: { serverId: parseInt(sid) } });
    if (r.message === "功能未启用") return ctx.toast("挂机踢出功能未启用", "error");
    ctx.toast("清理完成: " + r.kicked + " 人踢出 (共扫描 " + r.total + " 名超时玩家)");
  } catch(e) { ctx.toast("清理失败: " + e.message, "error"); }
}

// ─── Server Settings Commands ───
export async function ssCmd(cmd) {
  var sid = window._selectedServerId;
  if (!sid) return ctx.toast('请选择服务器', 'error');
  try {
    var r = await ctx.api('/rcon/send', { method: 'POST', body: { serverId: sid, command: cmd } });
    var msg = r.result && r.result.trim() ? r.result.trim() : '命令已执行（无返回值）';
    ctx.toast(cmd + ' ' + msg.substring(0, 100));
  } catch(e) { ctx.toast(cmd + ' 执行失败', 'error'); }
}

export async function ssRefreshStatus() {
  var sid = window._selectedServerId;
  if (!sid) return ctx.toast('请选择服务器', 'error');
  ctx.toast('正在刷新...');
  try {
    var r = await ctx.api('/rcon/send', { method: 'POST', body: { serverId: sid, command: 'ListPlayers' } });
    var content = r.result && r.result.trim() ? '<pre style="white-space:pre-wrap;font-family:monospace;font-size:12px;color:var(--text2)">' + ctx.esc(r.result.trim()) + '</pre>' : '<div style="color:var(--text3)">无响应，服务器可能未连接</div>';
    window._ssStatusHtml = content;
    var card = document.getElementById('ss-status-content');
    if (card) card.innerHTML = content;
  } catch(e) { ctx.toast('刷新失败', 'error'); }
}

export async function ssSetName() {
  var sid = window._selectedServerId;
  if (!sid) return ctx.toast('请选择服务器', 'error');
  var name = document.getElementById('ss-name').value;
  if (!name) return ctx.toast('请输入名称', 'error');
  try {
    await ctx.api('/rcon/send', { method: 'POST', body: { serverId: sid, command: 'SetServerName "' + name + '"' } });
    ctx.toast('名称已修改');
  } catch(e) { ctx.toast('修改失败', 'error'); }
}

export async function ssSetDesc() {
  var sid = window._selectedServerId;
  if (!sid) return ctx.toast('请选择服务器', 'error');
  var desc = document.getElementById('ss-desc').value;
  if (!desc) return ctx.toast('请输入介绍', 'error');
  try {
    await ctx.api('/rcon/send', { method: 'POST', body: { serverId: sid, command: 'SetServerDescription "' + desc + '"' } });
    ctx.toast('介绍已修改');
  } catch(e) { ctx.toast('修改失败', 'error'); }
}

export async function qcSpeedToggle() {
  var sid = qcGetServer(); if (!sid) return;
  if (window._speedActive) {
    // 倍速进行中 — 关闭
    if (window._speedTimer) { clearTimeout(window._speedTimer); window._speedTimer = null; }
    try {
      await ctx.api('/rcon/send', { method: 'POST', body: { serverId: sid, command: 'AdminSlomo 1.0' } });
      window._speedActive = null;
      ctx.toast('倍速已关闭');
      import('../../app.js?v=1775700005').then(function(m) { m.render(); });
    } catch(e) { ctx.toast('关闭倍速失败', 'error'); }
  } else {
    // 未开启 — 弹窗设置
    document.getElementById("modal-container").innerHTML =
      '<div class="modal-overlay" data-action="closeModal">' +
      '<div class="modal" style="max-width:400px">' +
      '<h3>设置倍速</h3>' +
      '<div class="form-group"><label>倍速值</label><input id="speed-val" type="number" value="2" min="1" max="10" step="0.5"></div>' +
      '<div class="form-group"><label>持续时长（秒，0 = 手动关闭）</label><input id="speed-duration" type="number" value="0" min="0" step="1" placeholder="0 = 不自动恢复"></div>' +
      '<div style="color:var(--text3);font-size:12px;margin-bottom:12px">设为 0 则一直保持倍速，直到手动关闭。填入秒数后到时间自动恢复正常倍速。</div>' +
      '<div class="modal-actions">' +
      '<button class="btn" data-action="closeModal">取消</button>' +
      '<button class="btn btn-primary" data-action="qcSpeedApply" data-param="' + sid + '">应用</button>' +
      '</div></div></div>';
  }
}

export async function qcSpeedApply(sid) {
  var val = parseFloat(document.getElementById('speed-val').value) || 2;
  var duration = parseInt(document.getElementById('speed-duration').value) || 0;
  document.getElementById("modal-container").innerHTML = '';
  try {
    await ctx.api('/rcon/send', { method: 'POST', body: { serverId: sid, command: 'AdminSlomo ' + val } });
    window._speedActive = val;
    var msg = '倍速已设置为 ' + val + 'x';
    if (duration > 0) {
      msg += '，' + duration + ' 秒后自动恢复';
      if (window._speedTimer) clearTimeout(window._speedTimer);
      window._speedTimer = setTimeout(async function() {
        try {
          await ctx.api('/rcon/send', { method: 'POST', body: { serverId: sid, command: 'AdminSlomo 1.0' } });
          window._speedActive = null;
          window._speedTimer = null;
          ctx.toast('倍速已自动恢复正常');
          import('../../app.js?v=1775700005').then(function(m) { m.render(); });
        } catch(e) { ctx.toast('自动恢复倍速失败', 'error'); }
      }, duration * 1000);
    }
    ctx.toast(msg);
    import('../../app.js?v=1775700005').then(function(m) { m.render(); });
  } catch(e) { ctx.toast('设置倍速失败', 'error'); }
}

export async function copyRelayToken() {
  var sid = window._selectedServerId;
  if (!sid) return ctx.toast('请选择服务器', 'error');
  try {
    var r = await ctx.api('/servers/' + sid);
    if (r.server && r.server.serverApiKey) {
      await navigator.clipboard.writeText(r.server.serverApiKey);
      ctx.toast('API Key 已复制到剪贴板');
    } else {
      ctx.toast('未找到 API Key', 'error');
    }
  } catch(e) { ctx.toast('复制失败', 'error'); }
}

export async function reloadPlugins() {
  ctx.toast('正在重载插件...');
  try {
    await ctx.api('/plugins/reload', { method: 'POST' });
    ctx.toast('插件已重载');
  } catch(e) { ctx.toast('重载失败', 'error'); }
}

export async function generateRegCode() {
  try {
    var r = await ctx.api('/relay/generate-code', { method: 'POST' });
    if (r.code) {
      var display = document.getElementById('reg-code-display');
      if (display) display.textContent = r.code;
      // 更新状态显示
      var statusEl = document.getElementById('reg-code-status');
      if (statusEl) statusEl.innerHTML = '<span style="color:#22c55e">已生成</span> · 未使用';
      ctx.toast('注册码已生成');
    } else {
      ctx.toast(r.error || '生成失败', 'error');
    }
  } catch(e) { ctx.toast('生成失败', 'error'); }
}

export async function checkNewServerAlert(serverId, serverName, host, rconPort) {
}

export async function saveAlertNotes() {}

export async function saveServerNotes(serverId, notes) {
  try {
    await ctx.api('/servers/' + serverId + '/notes', { method: 'PUT', body: { notes: notes } });
    ctx.toast('备注已保存');
  } catch(e) {
    ctx.toast('保存备注失败: ' + e.message, 'error');
  }
}

// ─── Player List Full Compare Update (Rebuild squad groups completely) ───

function getPlayerState(p) {
  return (p.teamId || '') + ':' + (p.squadId || '') + ':' + (p.name || '') + ':' + (p.isLeader ? '1' : '0');
}

function playerActionsHtml(x, sid) {
  var h = '<button class="btn btn-xs" data-action="switchTeam" data-params=\'[' + sid + ',"' + ctx.escAttr(x.steamId) + '"]\' title="跳边">跳边</button> ';
  if (hasPerm('tk')) h += '<button class="btn btn-xs" data-action="showTkModal" data-params=\'[' + sid + ',"' + ctx.escAttr(x.steamId) + '","' + ctx.escAttr(x.name) + '"]\' title="TK">TK</button> ';
  h += '<button class="btn btn-xs" data-action="warnPlayerFrontend" data-params=\'[' + sid + ',"' + ctx.escAttr(x.steamId) + '","' + ctx.escAttr(x.name) + '"]\' title="私信">私信</button> ';
  if (hasPerm('kick')) h += '<button class="btn btn-xs btn-danger" data-action="kickPlayer" data-params=\'[' + sid + ',"' + ctx.escAttr(x.steamId) + '"]\' title="踢出">踢</button> <span style="display:inline-block;width:6px"></span> ';
  if (hasPerm('ban')) h += '<button class="btn btn-xs btn-danger" data-action="banPlayer" data-params=\'[' + sid + ',"' + ctx.escAttr(x.steamId) + '","' + ctx.escAttr(x.name) + '"]\' title="封禁">封</button>';
  return h;
}

function createPlayerCard(p, sid) {
  var card = document.createElement('div');
  card.className = 'player-card';
  card.setAttribute('data-steam-id', p.steamId);

  var initial = (p.name || '?').charAt(0).toUpperCase();
  var leaderBadge = p.isLeader ? ' <span class="badge badge-blue" style="margin-left:6px">队长</span>' : '';

  card.innerHTML =
    '<div class="player-info">' +
      '<div class="player-avatar">' + initial + '</div>' +
      '<div>' +
        '<div class="player-name">' + ctx.esc(p.name) + leaderBadge + '</div>' +
        '<div class="player-meta">' + ctx.esc(p.steamId) + (p.playtime ? ' / ' + p.playtime : '') + '</div>' +
      '</div>' +
    '</div>' +
    '<div class="player-actions">' + playerActionsHtml(p, sid) + '</div>';
  card.setAttribute('data-state', getPlayerState(p));
  return card;
}

function createSquadGroupElement(squad) {
  var group = document.createElement('div');
  group.className = 'squad-group';
  group.setAttribute('data-squad-id', squad.squadId);
  group.setAttribute('data-team-id', squad.teamId);
  group.innerHTML = '<div class="squad-label"></div>';
  return group;
}

function updateSquadGroupTitle(group, squad, count, leaderName) {
  var label = group.querySelector('.squad-label');
  if (!label) return;
  if (squad.squadId === '_nosquad') {
    label.innerHTML = '未分配 <span class="squad-count" style="color:var(--text3)">(' + count + ')</span>';
  } else {
    var sName = squad.squadName || ('Squad ' + squad.squadId);
    var leaderHtml = leaderName ? ' / ' + ctx.esc(leaderName) : '';
    label.innerHTML = ctx.esc(sName) + ' <span style="color:var(--text3);font-weight:400">#' + squad.squadId + ' / <span class="squad-count">' + count + '</span>人' + leaderHtml + '</span>';
  }
}

function getSquadKey(squadId) {
  return (squadId && squadId !== 'N/A' && squadId !== '-1') ? String(squadId) : '_nosquad';
}

/**
 * 完全重建阵营内的 squad-group 和玩家卡片
 * 保留外层 card 和 team-header，只重建 team-container 内部结构
 */
export function updatePlayersDisplay(players, teamNames) {
  if (!players || !Array.isArray(players)) players = [];
  var _hasC = document.getElementById('team-1-container') && document.getElementById('team-2-container');

  // 容器不存在（初始渲染时 currentPlayers 为空，没创建团队卡片）→ 完整重渲染
  if (!_hasC) {
    if (_renderEl) renderServers(_renderEl);
    return;
  }

  var sid = ctx.currentPlayerServerId || 0;
  var squads = ctx.currentSquads || [];

  // 1. 更新队伍名和人数
  var sqTn1 = '', sqTn2 = '';
  squads.forEach(function(s) {
    if (s.teamId === '1' && s.teamName) sqTn1 = s.teamName;
    if (s.teamId === '2' && s.teamName) sqTn2 = s.teamName;
  });
  var tn1 = (teamNames && teamNames['1']) || sqTn1 || '阵营一';
  var tn2 = (teamNames && teamNames['2']) || sqTn2 || '阵营二';

  var team1Players = players.filter(function(p) { return p.teamId === '1'; });
  var team2Players = players.filter(function(p) { return p.teamId === '2'; });

  var team1NameEl = document.getElementById('team-1-name');
  var team2NameEl = document.getElementById('team-2-name');
  var team1CountEl = document.getElementById('team-1-count');
  var team2CountEl = document.getElementById('team-2-count');

  if (team1NameEl && team1NameEl.textContent !== tn1) team1NameEl.textContent = tn1;
  if (team2NameEl && team2NameEl.textContent !== tn2) team2NameEl.textContent = tn2;
  if (team1CountEl) team1CountEl.textContent = team1Players.length + ' 人';
  if (team2CountEl) team2CountEl.textContent = team2Players.length + ' 人';

  // 2. 构建新数据索引
  var newMap = {};
  players.forEach(function(p) { newMap[p.steamId] = p; });

  // 3. 对每个 team container 重建 squad 结构
  ['1', '2'].forEach(function(teamId) {
    var container = document.getElementById('team-' + teamId + '-container');
    if (!container) return;

    var teamPlayers = players.filter(function(p) { return p.teamId === teamId; });
    var teamSquads = squads.filter(function(s) { return s.teamId === teamId; }).map(function(s) {
      return Object.assign({}, s, { _key: getSquadKey(s.squadId) });
    });

    // 删除不再存在的 squad-group（排除 _nosquad）
    container.querySelectorAll('.squad-group').forEach(function(el) {
      var key = el.getAttribute('data-squad-id');
      if (!teamSquads.find(function(s) { return s._key === key; })) {
        el.remove();
      }
    });

    // 收集 squad 人数和队长
    var squadStats = {};
    teamSquads.forEach(function(s) { squadStats[s._key] = { count: 0, leader: null }; });
    squadStats._nosquad = { count: 0, leader: null };

    teamPlayers.forEach(function(p) {
      var key = getSquadKey(p.squadId);
      if (!squadStats[key]) squadStats[key] = { count: 0, leader: null };
      squadStats[key].count++;
      if (p.isLeader) squadStats[key].leader = p.name;
    });

    // 排序 squad：按 squadId 数字升序，_nosquad 在最后
    teamSquads.sort(function(a, b) {
      if (a._key === '_nosquad') return 1;
      if (b._key === '_nosquad') return -1;
      return parseInt(a.squadId) - parseInt(b.squadId);
    });

    // 更新/创建 squad-group
    teamSquads.forEach(function(squad) {
      var key = squad._key;
      var group = container.querySelector('.squad-group[data-squad-id="' + key + '"]');
      if (!group) {
        group = createSquadGroupElement({ squadId: key, teamId: teamId });
        container.appendChild(group);
      }
      updateSquadGroupTitle(group, squad, squadStats[key].count, squadStats[key].leader);
    });

    // 确保 _nosquad 存在（有未分配玩家时）
    var nosquadPlayers = teamPlayers.filter(function(p) { return getSquadKey(p.squadId) === '_nosquad'; });
    if (nosquadPlayers.length > 0 && !teamSquads.find(function(s) { return s._key === '_nosquad'; })) {
      var nosGroup = container.querySelector('.squad-group[data-squad-id="_nosquad"]');
      if (!nosGroup) {
        nosGroup = createSquadGroupElement({ squadId: '_nosquad', teamId: teamId });
        container.appendChild(nosGroup);
      }
      updateSquadGroupTitle(nosGroup, { squadId: '_nosquad' }, nosquadPlayers.length, null);
    }

    // 清理空的未分配组
    if (nosquadPlayers.length === 0) {
      var emptyNos = container.querySelector('.squad-group[data-squad-id="_nosquad"]');
      if (emptyNos) emptyNos.remove();
    }

    // 4. 更新/移动/创建玩家卡片
    teamPlayers.forEach(function(p) {
      var key = getSquadKey(p.squadId);
      var group = container.querySelector('.squad-group[data-squad-id="' + key + '"]');
      if (!group) {
        // fallback：如果 squad-group 意外缺失，附加到 container 末尾
        group = container;
      }
      var existingCard = document.querySelector('.player-card[data-steam-id="' + p.steamId + '"]');
      var newState = getPlayerState(p);

      if (existingCard) {
        if (existingCard.getAttribute('data-state') === newState) {
          // 状态没变，但可能 parent squad-group 变化了（比如跳边后换了队伍）
          if (existingCard.parentElement !== group) {
            group.appendChild(existingCard);
          }
          return;
        }
        existingCard.remove();
      }
      var card = createPlayerCard(p, sid);
      group.appendChild(card);
    });

    // 5. 删除离开的玩家的卡片
    container.querySelectorAll('.player-card').forEach(function(card) {
      var steamId = card.getAttribute('data-steam-id');
      if (!newMap[steamId] || newMap[steamId].teamId !== teamId) {
        card.remove();
      }
    });

    // 6. 清理空的 squad-group（除了 _nosquad，上面已处理）
    container.querySelectorAll('.squad-group').forEach(function(el) {
      var key = el.getAttribute('data-squad-id');
      if (key === '_nosquad') return;
      if (el.querySelectorAll('.player-card').length === 0) {
        el.remove();
      }
    });

    // 7. empty placeholder
    container.querySelectorAll('.empty').forEach(function(el) { el.remove(); });
    if (teamPlayers.length === 0 && container.querySelectorAll('.squad-group').length === 0) {
      var emptyDiv = document.createElement('div');
      emptyDiv.className = 'empty';
      emptyDiv.style.padding = '32px';
      emptyDiv.textContent = '暂无玩家';
      container.appendChild(emptyDiv);
    }

    // 8. 重新排序 squad-group 到正确顺序
    var allGroups = Array.from(container.querySelectorAll('.squad-group'));
    allGroups.sort(function(a, b) {
      var ak = a.getAttribute('data-squad-id');
      var bk = b.getAttribute('data-squad-id');
      if (ak === '_nosquad') return 1;
      if (bk === '_nosquad') return -1;
      return parseInt(ak) - parseInt(bk);
    });
    allGroups.forEach(function(g) { container.appendChild(g); });
  });
}

// ─── 增量更新：根据 player_diff 只操作变化的 DOM ───
var _diffDebounceTimer = null;
export function applyPlayerDiff(diff) {
  if (_diffDebounceTimer) return;
  _diffDebounceTimer = setTimeout(function() {
    _diffDebounceTimer = null;
    _doApplyDiff(diff);
  }, 50);
}

function _doApplyDiff(diff) {
  var _hasC = document.getElementById('team-1-container') && document.getElementById('team-2-container');
  if (!_hasC) {
    refreshPlayersLive();
    return;
  }

  var sid = ctx.currentPlayerServerId || 0;

  // 更新内存中的 player 列表
  var currentMap = {};
  ctx.currentPlayers.forEach(function(p) { currentMap[p.steamId] = p; });

  // 处理离开的玩家：删除 DOM + 内存
  (diff.left || []).forEach(function(p) {
    delete currentMap[p.steamId];
    var card = document.querySelector('.player-card[data-steam-id="' + p.steamId + '"]');
    if (card) card.remove();
  });

  // 处理加入和变更的玩家
  var toAdd = (diff.joined || []).concat(diff.changed || []);
  var needFullRefresh = false;

  toAdd.forEach(function(p) {
    currentMap[p.steamId] = p;
    var existingCard = document.querySelector('.player-card[data-steam-id="' + p.steamId + '"]');
    if (existingCard) existingCard.remove();

    var teamId = String(p.teamId);
    var key = getSquadKey(p.squadId);
    var container = document.getElementById('team-' + teamId + '-container');
    if (!container) { needFullRefresh = true; return; }
    var group = container.querySelector('.squad-group[data-squad-id="' + key + '"]');
    if (!group) { needFullRefresh = true; return; }
    var card = createPlayerCard(p, sid);
    group.appendChild(card);
  });

  if (needFullRefresh) { refreshPlayersLive(); return; }

  // 更新内存 state
  ctx.setCurrentPlayers(Object.values(currentMap));

  // 更新人数显示
  var t1Count = Object.values(currentMap).filter(function(p) { return p.teamId === '1'; }).length;
  var t2Count = Object.values(currentMap).filter(function(p) { return p.teamId === '2'; }).length;
  var t1c = document.getElementById('team-1-count');
  var t2c = document.getElementById('team-2-count');
  if (t1c) t1c.textContent = t1Count + ' 人';
  if (t2c) t2c.textContent = t2Count + ' 人';

  // 清理空的 squad-group
  ['1', '2'].forEach(function(teamId) {
    var container = document.getElementById('team-' + teamId + '-container');
    if (!container) return;
    container.querySelectorAll('.squad-group').forEach(function(el) {
      if (el.getAttribute('data-squad-id') === '_nosquad') return;
      if (el.querySelectorAll('.player-card').length === 0) el.remove();
    });
  });
}

// ─── Lightweight Auto-Refresh Functions (no full page re-render) ───

/**
 * 轻量级更新 toggle 按钮状态（不重建 DOM）
 * 只遍历现有按钮，根据 cheatStates 更新 CSS 类
 */
function updateToggleUI() {
  var sid = window._selectedServerId;
  if (!sid) return;

  var serverState = cheatStates[sid] || {};

  // 遍历所有 toggle 按钮
  document.querySelectorAll('.qc-toggle').forEach(function(btn) {
    try {
      var params = btn.getAttribute('data-params');
      if (!params) return;
      var parsed = JSON.parse(params);
      var onCmd = parsed[0];
      var isOn = serverState[onCmd];

      btn.classList.remove('qc-toggle-on', 'qc-toggle-off');
      btn.classList.add(isOn ? 'qc-toggle-on' : 'qc-toggle-off');
    } catch (e) {
      // ignore parse errors
    }
  });
}

/**
 * 轻量级刷新服务器列表（用于自动刷新）
 * 只更新数据展示层，不重建整个页面
 */
export async function refreshServerListLight() { return _doRefreshLight(); }
async function _doRefreshLight() {
  // 1. 获取最新服务器列表
  var r = await api('/servers');
  var newServers = r.servers || [];

  // 更新全局 state
  setServers(newServers);

  // 2. 轻量级更新 toggle 按钮状态
  updateToggleUI();

  // 3. 更新 topbar 服务器选择器选项（如果有新服务器添加）
  var topbarSelect = document.getElementById('topbar-server-select');
  if (topbarSelect) {
    var currentVal = window._selectedServerId;
    var optsHtml = newServers.map(function(s) {
      return '<option value="' + s.id + '"' + (String(s.id) === String(currentVal) ? ' selected' : '') + '>' + ctx.esc(s.name) + '</option>';
    }).join('');
    topbarSelect.innerHTML = optsHtml;
  }

  // 4. 刷新服务器状态栏
  refreshServerStatus();
}

/**
 * 刷新服务器运行状态栏
 * 显示当前选中服务器的进程状态（通过 relay 检测）
 */
export async function refreshServerStatus() {
  var indicator = document.getElementById('server-status-indicator');
  var qcServerSelect = document.getElementById('qc-server');

  if (!indicator) return;
  if (!window._selectedServerId) {
    indicator.innerHTML = '<span style="color:var(--text3)">请选择服务器</span>';
    return;
  }

  var serverId = window._selectedServerId;
  var server = (servers || []).find(function(s) { return String(s.id) === String(serverId); });

  // 简化判断：DB 有 relayUrl 就认为在线
  if (server && server.relayUrl) {
    indicator.innerHTML = '<span style="color:#22c55e">🟢 在线</span>';
  } else {
    indicator.innerHTML = '<span style="color:var(--warn)">⚠️ Relay 未连接</span>';
  }
}


// ─── Re-exports from sub-modules ───




// ─── Module Contract: manifest + pages + actions ───
export const manifest = {
  id: 'servers',
  label: '服务器管理',
  access: ['server_owner', 'op'],
  permissions: ['players'],
  icon: '🖥️',
  section: 'manage',
  order: 1,
  subnav: [
    { id: 'list', label: '服务器列表' },
    { id: 'players', label: '玩家管理', access: ['server_owner', 'op'], permissions: ['players'] },
    { id: 'settings', label: '服务器设置', access: ['server_owner'], permissions: ['settings'] },
  ],
};

export const pages = {
  'servers': renderServers,
};

export const actions = {
  switchServersTab,
  closeModal,
  qcSend, qcToggle, qcInput, qcInputModal, qcInputModalSubmit,
  qcScrambleTeams, qcScrambleExecute, qcKickUnsquadded,
  qcSpeedToggle, qcSpeedApply,
  qcAfkSettings, qcAfkSave, qcAfkCleanNow,
  showAddServerModal, addServer, showEditServerModal, updateServer, deleteServer, testServer,
  connModeChange,
  relayControl,
  // addReservedSlot, deleteReservedSlot moved to bans module
  switchTeam, kickPlayer, banPlayer, warnPlayerFrontend,
  refreshPlayers, refreshPlayersLive,
  ssCmd, ssRefreshStatus, ssSetName, ssSetDesc,
  showTkModal, createTkForgive, switchTkTab, forgiveTk, kickTk, deleteTk, saveTkConfig,
  copyRelayToken, reloadPlugins, generateRegCode,
  checkNewServerAlert, saveAlertNotes, saveServerNotes,
  updatePlayersDisplay, applyPlayerDiff, refreshServerListLight, refreshServerStatus,
  initPresets, showCreatePresetModal, createPreset, activatePreset,
  showEditPresetModal, updatePreset, deletePreset,
  // re-exported from map.js
  executeMapCommand, serverLayerPick,
  qcChangeMap, qcShowMapList, qcPickMap, qcDoChangeMap, qcQuickMap,
  // re-exported from charts.js
  chartRangeChange, tickRangeChange,
  refreshOnlineChart, refreshTickChart,
};

// --- Debounced Refresh ---
var _refreshDebounceTimer = null;
export function debouncedRefresh(delay) {
  delay = delay || 300;
  clearTimeout(_refreshDebounceTimer);
  _refreshDebounceTimer = setTimeout(function() {
    var el = document.getElementById('page-content');
    if (el) renderServers(el);
  }, delay);
}

// --- Lightweight Status Updater ---
// Updates only status indicators without rebuilding the entire DOM
export async function updateStatusLightweight() {
  try {
    var data = await getCachedServers();
    if (!data || data.error) return;
    var servers = data.servers || [];
    
    // Update server status indicators
    servers.forEach(function(s) {
      var card = document.querySelector('.server-card[data-sid="' + s.id + '"]');
      if (!card) return;
      
      // Update connection mode badge
      var modeBadge = card.querySelector('.server-mode-badge');
      if (modeBadge) {
        var mode = s.connectionMode || 'relay';
        var modeLabels = {relay: 'Relay', local: 'Local', remote_api: 'Remote', external_api: 'External'};
        modeBadge.textContent = modeLabels[mode] || mode;
      }
      
      // Update relay status indicator
      var relayDot = card.querySelector('.relay-status-dot');
      if (relayDot) {
        // Will be updated by relay status check
      }
    });
    
    // Update global server selector if needed
    var selector = document.getElementById('global-server-select');
    if (selector) {
      var currentVal = selector.value;
      var optsHtml = '<option value="">选择服务器</option>';
      servers.forEach(function(s) {
        optsHtml += '<option value="' + s.id + '">' + ctx.esc(s.name) + '</option>';
      });
      if (selector.innerHTML !== optsHtml) {
        selector.innerHTML = optsHtml;
        selector.value = currentVal;
      }
    }
  } catch (e) {
    console.warn('[StatusUpdate] Error:', e);
  }
}
