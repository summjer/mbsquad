// ─── users.js — 人员管理（用户 + 团队 + 操作日志） ───


let ctx;
export function init(_ctx) { ctx = _ctx; }

var _appMod = null;
function getApp() { if (_appMod) return Promise.resolve(_appMod); return import('../../app.js?v=1775700005').then(function(m) { _appMod = m; return m; }); }
function rerender() { getApp().then(function(m) { m.render(); }); }

var userSearchQuery = '';
var usersActiveTab = 'list';
var opLogActionFilter = 'all';
var teamTab = 'my'; // my | browse

var ROLE_LABELS = { op: 'OP', server_owner: '服主', member: '成员' };
var roleColors = { server_owner: 'var(--accent)', op: '#f59e0b' };

var OP_ACTIONS = {
  ban: { label: '封禁', icon: '🚫', color: 'var(--red)', bg: 'var(--red-subtle)' },
  unban: { label: '解封', icon: '✅', color: 'var(--green)', bg: 'var(--green-subtle)' },
  kick: { label: '踢出', icon: '👢', color: '#ea580c', bg: 'rgba(234,88,12,0.1)' },
  warn: { label: '警告', icon: '⚠️', color: 'var(--yellow)', bg: 'var(--yellow-subtle)' },
  fly: { label: '飞天', icon: '🦅', color: '#7c3aed', bg: 'rgba(124,58,237,0.1)' },
  cam: { label: '观战', icon: '👁️', color: '#0891b2', bg: 'rgba(8,145,178,0.1)' },
  cheat: { label: '作弊', icon: '💀', color: 'var(--red)', bg: 'var(--red-subtle)' },
  tk: { label: '恶意TK', icon: '🔪', color: '#dc2626', bg: 'rgba(220,38,38,0.1)' },
  other: { label: '其他', icon: '📝', color: 'var(--text3)', bg: 'var(--bg3)' }
};

// ─── 主入口 ───

export async function renderUsers(el) {
  var onlinePromise = ctx.api('/admin/online').catch(function() { return {}; });
  var usersPromise = ctx.api('/admin/users');
  var results = await Promise.all([onlinePromise, usersPromise]);
  var onlineData = results[0];
  var d = results[1];

  var tabHtml = '<div style="display:flex;gap:4px;margin-bottom:20px">' +
    '<button class="btn btn-sm' + (usersActiveTab === 'list' ? ' btn-primary' : '') + '" data-action="switchTab" data-param="list" style="border-radius:6px">👥 用户列表</button>' +
    '<button class="btn btn-sm' + (usersActiveTab === 'team' ? ' btn-primary' : '') + '" data-action="switchTab" data-param="team" style="border-radius:6px">🏆 已加入的团队</button>' +
    '<button class="btn btn-sm' + (usersActiveTab === 'logs' ? ' btn-primary' : '') + '" data-action="switchTab" data-param="logs" style="border-radius:6px">📋 操作日志</button>' +
  '</div>';

  if (usersActiveTab === 'team') { renderTeamTab(el, tabHtml); return; }
  if (usersActiveTab === 'logs') { renderOpLogs(el, tabHtml); return; }

  // === Tab: 用户列表 ===
  if (d.error) { el.innerHTML = '<div class="empty">无权限</div>'; return; }
  var users = d.users || [];
  var pending = users.filter(function(u) { return u.status === 'pending'; });
  var activeUsers = users.filter(function(u) { return u.status !== 'pending'; });

  if (userSearchQuery) {
    var q = userSearchQuery.toLowerCase();
    activeUsers = activeUsers.filter(function(u) {
      return (u.username || '').toLowerCase().indexOf(q) !== -1 ||
             (u.steamId || '').toLowerCase().indexOf(q) !== -1;
    });
  }

  var onlineHtml = renderOnlineOverview(onlineData);

  var html = onlineHtml + '<div class="section-card" style="margin-bottom:20px">' +
    '<div style="display:flex;align-items:center;justify-content:space-between;gap:12px;flex-wrap:wrap;margin-bottom:20px">' +
      '<h2 style="margin:0;font-size:18px">人员管理</h2>' +
      '<div style="display:flex;gap:8px;align-items:center">' +
        '<input type="text" id="user-search" placeholder="搜索用户名/SteamID..." value="' + ctx.esc(userSearchQuery) + '" style="padding:8px 12px;border-radius:8px;border:1px solid var(--border);width:200px;font-size:13px" onkeydown="if(event.key===\'Enter\')A(\'userSearch\')">' +
        '<button class="btn btn-sm" data-action="userSearch">搜索</button>' +
        '<button class="btn btn-primary btn-sm" data-action="showAddUserModal">+ 添加用户</button>' +
      '</div>' +
    '</div>' +
    tabHtml;

  if (pending.length) {
    html += '<div class="pending-section" style="background:var(--yellow-subtle);border:1px solid rgba(217,119,6,0.2);border-radius:12px;padding:16px;margin-bottom:20px">' +
      '<h3 style="margin:0 0 12px;color:var(--yellow);font-size:14px">待审核 (' + pending.length + ')</h3>' +
      '<div class="table-wrapper"><table class="data-table" style="margin:0"><thead><tr>' +
        '<th>用户名</th><th>SteamID</th><th>注册时间</th><th>操作</th>' +
      '</tr></thead><tbody>';
    pending.forEach(function(u) {
      var steamCell = u.steamId ? '<code style="font-size:11px;color:var(--text3)">' + ctx.esc(u.steamId) + '</code>' : '<span style="color:var(--text3)">-</span>';
      html += '<tr>' +
        '<td><strong>' + ctx.esc(u.username) + '</strong></td>' +
        '<td>' + steamCell + '</td>' +
        '<td style="color:var(--text3);font-size:12px">' + formatTime(u.createdAt) + '</td>' +
        '<td class="action-cell">' +
          '<button class="btn btn-xs btn-success" data-action="approveUser" data-param="' + u.id + '">批准</button> ' +
          '<button class="btn btn-xs btn-danger" data-action="showRejectModal" data-param="' + u.id + '">拒绝</button>' +
        '</td></tr>';
    });
    html += '</tbody></table></div></div>';
  }

  html += '<div style="margin-bottom:12px;display:flex;align-items:center;justify-content:space-between">' +
    '<span style="color:var(--text2);font-size:13px">共 ' + activeUsers.length + ' 个用户</span>' +
    '<button class="btn btn-sm" data-action="refreshUsers">刷新</button>' +
  '</div>';

  if (activeUsers.length === 0) {
    html += '<div class="empty-text" style="padding:40px;color:var(--text3)">' +
      '<div style="font-size:32px;margin-bottom:8px;opacity:0.3">👥</div>' +
      '<div>' + (userSearchQuery ? '未找到匹配的用户' : '暂无用户') + '</div></div>';
  } else {
    html += '<div class="table-wrapper"><table class="data-table user-table"><thead><tr>' +
      '<th>用户名</th><th>角色</th><th>SteamID</th><th>状态</th><th>最后登录</th><th>注册时间</th><th>操作</th>' +
    '</tr></thead><tbody>';
    activeUsers.forEach(function(u, idx) {
      var rowClass = idx % 2 === 1 ? ' class="log-row-alt"' : '';
      var statusBadge = u.status === 'active' ? '<span class="badge badge-green">正常</span>' :
                        u.status === 'pending' ? '<span class="badge badge-yellow">待审</span>' :
                        '<span class="badge badge-red">已拒绝</span>';
      var roleLabel = ROLE_LABELS[u.role] || u.role;
      var roleBadge = '<span style="color:' + (roleColors[u.role] || 'var(--text2)') + ';font-weight:600">' + roleLabel + '</span>';
      var steamCell = u.steamId ? '<code style="font-size:11px;color:var(--text3)">' + ctx.esc(u.steamId) + '</code>' : '<span style="color:var(--text3)">-</span>';
      var lastLogin = u.lastLogin ? formatTime(u.lastLogin) : '<span style="color:var(--text3)">-</span>';
      var rejectTip = '';
      if (u.status === 'rejected' && u.rejectReason) {
        rejectTip = ' <span style="cursor:help;color:var(--red);font-size:11px" title="拒绝理由: ' + ctx.esc(u.rejectReason) + '">[查看理由]</span>';
      }
      html += '<tr' + rowClass + '>' +
        '<td><strong>' + ctx.esc(u.username) + '</strong></td>' +
        '<td>' + roleBadge + '</td>' +
        '<td>' + steamCell + '</td>' +
        '<td>' + statusBadge + rejectTip + '</td>' +
        '<td style="color:var(--text3);font-size:12px">' + lastLogin + '</td>' +
        '<td style="color:var(--text3);font-size:12px">' + formatTime(u.createdAt) + '</td>' +
        '<td class="action-cell">';
      if (ctx.currentUser && u.id !== ctx.currentUser.id) {
        var roleLabel = ROLE_LABELS[u.role] || u.role;
        html += '<button class="btn btn-xs" data-action="showRoleModal" data-params=\'[' + u.id + ',"' + u.role + '"]\' style="min-width:40px">' + roleLabel + ' ▾</button> ';
        if (u.role !== 'server_owner') html += '<button class="btn btn-xs" data-action="showPermModal" data-param="' + u.id + '">权限</button> ';
        html += '<button class="btn btn-xs btn-danger" data-action="deleteUser" data-param="' + u.id + '">删除</button>';
      } else {
        html += '<span style="color:var(--text3);font-size:11px">当前用户</span>';
      }
      html += '</td></tr>';
    });
    html += '</tbody></table></div>';
  }
  html += '</div>';
  html += '<style>.user-table tbody tr:hover td { background: var(--accent-subtle) !important; }' +
    '.role-select { padding:4px 8px;border-radius:6px;border:1px solid var(--border);font-size:11px;background:var(--bg2);color:var(--text);cursor:pointer; }' +
    '.role-select:hover { border-color:var(--accent); }' +
    '.pending-section table tbody tr:hover td { background: rgba(217,119,6,0.08) !important; }</style>';
  el.innerHTML = html;
}

// ─── 团队 Tab ───

async function renderTeamTab(el, tabHtml) {
  var d = await ctx.api('/teams');
  if (d.error) { el.innerHTML = '<div class="section-card">' + tabHtml + '<div class="empty" style="padding:40px;color:var(--text3)">' + ctx.esc(d.error) + '</div></div>'; return; }

  var html = '<div class="section-card">' +
    '<div style="display:flex;align-items:center;justify-content:space-between;margin-bottom:20px">' +
      '<h2 style="margin:0;font-size:18px">人员管理</h2>' +
      '<button class="btn btn-sm" data-action="refreshUsers">↻ 刷新</button>' +
    '</div>' + tabHtml;

  if (!d.team) {
    html += '<div style="text-align:center;padding:48px 20px;color:var(--text3)">' +
      '<div style="font-size:48px;margin-bottom:12px;opacity:0.3">🏆</div>' +
      '<div style="font-size:14px;margin-bottom:20px">暂未加入任何团队</div>' +
      '<div style="display:flex;gap:12px;justify-content:center">' +
        '<button class="btn btn-primary" data-action="showCreateTeamModal">创建团队</button>' +
        '<button class="btn" data-action="showJoinByCodeModal">邀请码加入</button>' +
      '</div></div>';
  } else {
    var team = d.team;
    var members = team.members || [];
    var codes = team.inviteCodes || [];
    var isOwner = team.isOwner;
    var ownerName = '-';
    for (var i = 0; i < members.length; i++) { if (members[i].id === team.ownerId) { ownerName = members[i].username; break; } }

    // 团队信息卡
    html += '<div style="display:flex;align-items:center;gap:12px;margin-bottom:20px;padding:16px;background:var(--bg2);border-radius:10px">' +
      '<div style="font-size:28px">🏆</div>' +
      '<div style="flex:1">' +
        '<div style="font-size:18px;font-weight:700">' + ctx.esc(team.name) + '</div>' +
        '<div style="color:var(--text2);font-size:12px">' + members.length + ' 名成员 · 服主: ' + ctx.esc(ownerName) + '</div>' +
      '</div>' +
      (isOwner
        ? '<button class="btn btn-sm btn-primary" data-action="showGenCodeModal">生成邀请码</button>'
        : '<button class="btn btn-sm btn-danger" data-action="leaveTeam">退出团队</button>') +
    '</div>';

    // 成员列表
    html += '<div class="table-wrapper"><table class="data-table"><thead><tr><th>用户名</th><th>角色</th><th>SteamID</th>' + (isOwner ? '<th>操作</th>' : '') + '</tr></thead><tbody>';
    for (var i = 0; i < members.length; i++) {
      var m = members[i];
      var mIsOwner = m.id === team.ownerId;
      var isMe = m.id === ctx.currentUser.id;
      var roleLabel = mIsOwner ? '<span style="color:var(--accent);font-weight:600">服主</span>' : '<span style="color:var(--text2)">成员</span>';
      html += '<tr>' +
        '<td><strong>' + ctx.esc(m.username) + (isMe ? ' <span style="color:var(--text3);font-size:11px">(你)</span>' : '') + '</strong></td>' +
        '<td>' + roleLabel + '</td>' +
        '<td><code style="font-size:11px;color:var(--text3)">' + ctx.esc(m.steamId || '-') + '</code></td>' +
        (isOwner ? '<td>' + (!mIsOwner && !isMe ? '<button class="btn btn-xs btn-danger" data-action="removeMember" data-param="' + m.id + '" data-confirm="确定移除该成员？">移除</button>' : '') + '</td>' : '') +
      '</tr>';
    }
    html += '</tbody></table></div>';

    // 邀请码列表（仅服主可见）
    if (isOwner && codes.length > 0) {
      html += '<h3 style="margin:20px 0 12px;font-size:14px">🔑 有效邀请码 (' + codes.length + ')</h3>';
      html += '<div class="table-wrapper"><table class="data-table"><thead><tr><th>邀请码</th><th>过期时间</th><th>操作</th></tr></thead><tbody>';
      for (var i = 0; i < codes.length; i++) {
        var c = codes[i];
        html += '<tr>' +
          '<td><code style="font-size:14px;font-weight:700;letter-spacing:1px;color:var(--accent)">' + ctx.esc(c.code) + '</code></td>' +
          '<td style="color:var(--text3);font-size:12px">' + formatTime(c.expiresAt) + '</td>' +
          '<td><button class="btn btn-xs" data-action="copyCode" data-param="' + ctx.esc(c.code) + '">复制</button> '+
            '<button class="btn btn-xs btn-danger" data-action="revokeCode" data-param="' + c.id + '">撤销</button></td></tr>';
      }
      html += '</tbody></table></div>';
    }
  }
  html += '</div>';
  el.innerHTML = html;
}

// ─── 操作日志 Tab ───

function renderOpLogs(el, tabHtml) {
  var filterBtns = '<button class="log-filter-btn' + (opLogActionFilter === 'all' ? ' active' : '') + '" data-action="logFilter" data-param="all">全部</button>';
  Object.keys(OP_ACTIONS).forEach(function(key) {
    var a = OP_ACTIONS[key];
    filterBtns += '<button class="log-filter-btn' + (opLogActionFilter === key ? ' active' : '') + '" data-action="logFilter" data-param="' + key + '" style="--action-color:' + a.color + '">' + a.icon + ' ' + a.label + '</button>';
  });
  var html = '<div class="section-card">' +
    '<div style="display:flex;align-items:center;justify-content:space-between;margin-bottom:20px">' +
      '<h2 style="margin:0;font-size:18px">人员管理</h2></div>' +
    tabHtml +
    '<div style="display:flex;gap:6px;align-items:center;margin-bottom:16px;flex-wrap:wrap">' +
      filterBtns + '<div style="flex:1"></div>' +
      '<button class="btn btn-sm btn-primary" data-action="logAdd">手动记录</button>' +
      '<button class="btn btn-sm" data-action="logRefresh">刷新</button>' +
    '</div>' +
    '<div id="op-log-container"><p class="loading-text">加载中...</p></div></div>' +
  '<style>' +
    '.log-filter-btn { padding:6px 12px;border-radius:6px;font-size:12px;border:1px solid var(--border);background:var(--bg2);color:var(--text2);cursor:pointer;transition:all 0.15s; }' +
    '.log-filter-btn:hover { border-color:var(--action-color,var(--accent));color:var(--action-color,var(--accent)); }' +
    '.log-filter-btn.active { background:var(--action-color,var(--accent));color:#fff;border-color:transparent; }</style>';
  el.innerHTML = html;
  loadOpLogs();
}

async function loadOpLogs() {
  var container = document.getElementById('op-log-container');
  if (!container) return;
  container.innerHTML = '<p class="loading-text">加载中...</p>';
  try {
    var url = '/api/op/logs?limit=200';
    if (opLogActionFilter !== 'all') url += '&action=' + opLogActionFilter;
    var resp = await fetch(url, { headers: { 'Authorization': 'Bearer ' + ctx.token } });
    var data = await resp.json();
    if (!resp.ok) { container.innerHTML = '<p class="error-text">加载失败: ' + (data.error || '') + '</p>'; return; }
    var rows = data.data || [];
    if (!rows.length) {
      container.innerHTML = '<div class="empty-text" style="padding:40px;text-align:center;color:var(--text3)"><div style="font-size:32px;margin-bottom:8px;opacity:0.3">📋</div>暂无操作日志</div>';
      return;
    }
    var html = '<div class="table-wrapper"><table class="data-table log-table"><thead><tr><th>时间</th><th>操作人</th><th>类型</th><th>目标</th><th>详情</th></tr></thead><tbody>';
    for (var i = 0; i < rows.length; i++) {
      var r = rows[i];
      var rowAlt = i % 2 === 1 ? ' class="log-row-alt"' : '';
      var actionInfo = OP_ACTIONS[r.action] || { label: r.action, icon: '', color: 'var(--text3)', bg: 'var(--bg3)' };
      var actionBadge = '<span style="display:inline-flex;align-items:center;gap:4px;padding:3px 10px;border-radius:100px;font-size:11px;font-weight:600;background:' + actionInfo.bg + ';color:' + actionInfo.color + '">' + actionInfo.icon + ' ' + actionInfo.label + '</span>';
      html += '<tr' + rowAlt + '>' +
        '<td style="white-space:nowrap;color:var(--text3);font-size:12px">' + formatTime(r.createdAt) + '</td>' +
        '<td><strong>' + ctx.esc(r.operator) + '</strong></td>' +
        '<td>' + actionBadge + '</td>' +
        '<td>' + ctx.esc(r.target || '-') + '</td>' +
        '<td style="color:var(--text2);max-width:300px;word-break:break-word">' + ctx.esc(r.details || '-') + '</td></tr>';
    }
    html += '</tbody></table></div><style>.log-table tbody tr:hover td { background: var(--accent-subtle) !important; }</style>';
    container.innerHTML = html;
  } catch (e) { container.innerHTML = '<p class="error-text">加载失败: ' + e.message + '</p>'; }
}

// ─── 通用 Actions ───

export function switchTab(tab) { usersActiveTab = tab; teamTab = 'my'; rerender(); }
export function refreshUsers() { rerender(); }
export function refreshOnline() { rerender(); }
export function userSearch() { userSearchQuery = (document.getElementById('user-search') || {}).value || ''; rerender(); }

// ─── 用户管理 Actions ───

export function showAddUserModal() {
  document.getElementById('modal-container').innerHTML =
    '<div class="modal-overlay" data-action="closeModal"><div class="modal"><h3>添加用户</h3>' +
    '<div class="form-group"><label>用户名 (3-32字符)</label><input id="add-username" placeholder="用户名"></div>' +
    '<div class="form-group"><label>密码 (至少6位)</label><input id="add-password" type="password" placeholder="密码"></div>' +
    '<div class="form-group"><label>账户级别</label><select id="add-role" style="width:100%;padding:8px 12px;border-radius:6px;border:1px solid var(--border);font-size:14px">' +
      '<option value="op">OP — 无服务器设置/用户管理权限</option><option value="server_owner">服主 — 全部权限</option></select></div>' +
    '<div class="form-group"><label>Steam 64位ID <span style="color:var(--red)">*</span></label><input id="add-steamid" placeholder="76561198xxxxxxxxx"></div>' +
    '<div style="font-size:12px;color:var(--text2);margin-bottom:12px">所有账户需绑定 Steam 64位ID，用于游戏内在线判定。</div>' +
    '<div class="modal-actions"><button class="btn" data-action="closeModal">取消</button><button class="btn btn-primary" data-action="addUser">创建</button></div></div></div>';
  setTimeout(function() { document.getElementById('add-username').focus(); }, 100);
}

export async function addUser() {
  var username = document.getElementById('add-username').value;
  var password = document.getElementById('add-password').value;
  var role = document.getElementById('add-role').value;
  var steamId = (document.getElementById('add-steamid') || {}).value || '';
  if (!username || !password) return ctx.toast('请填写完整信息', 'error');
  if (!steamId.trim()) return ctx.toast('必须填写 Steam 64位ID', 'error');
  if (username.length < 3) return ctx.toast('用户名至少3个字符', 'error');
  if (password.length < 6) return ctx.toast('密码至少6位', 'error');
  var r = await ctx.api('/admin/users/add', { method: 'POST', body: { username: username, password: password, role: role, steamId: steamId.trim() } });
  if (r.error) return ctx.toast(r.error, 'error');
  document.getElementById('modal-container').innerHTML = '';
  ctx.toast('已创建 ' + (ROLE_LABELS[role] || role) + ' 账户: ' + username);
  rerender();
}

export function showRoleModal(userId, currentRole) {
  var options = '<option value="op"' + (currentRole === 'op' ? ' selected' : '') + '>OP</option>' +
    '<option value="server_owner"' + (currentRole === 'server_owner' ? ' selected' : '') + '>服主</option>';
  document.getElementById('modal-container').innerHTML =
    '<div class="modal-overlay" data-action="closeModal"><div class="modal" style="max-width:360px"><h3>切换用户角色</h3>' +
    '<div class="form-group"><label>选择新角色</label><select id="role-select" style="width:100%;padding:10px 12px;border-radius:8px;border:1px solid var(--border);font-size:14px">' + options + '</select></div>' +
    '<div class="modal-actions"><button class="btn" data-action="closeModal">取消</button><button class="btn btn-primary" data-action="confirmRoleChange" data-param="' + userId + '">确认切换</button></div></div></div>';
}

export async function confirmRoleChange(userId) {
  var btn = document.querySelector('[data-action="confirmRoleChange"]');
  if (btn) { btn.disabled = true; btn.textContent = '切换中...'; }
  var newRole = document.getElementById('role-select').value;
  var r = await ctx.api('/admin/users/' + userId + '/role', { method: 'PUT', body: { role: newRole } });
  document.getElementById('modal-container').innerHTML = '';
  if (r.error) { ctx.toast(r.error, 'error'); } else { ctx.toast('角色已切换为 ' + (ROLE_LABELS[newRole] || newRole)); }
  rerender();
}

export async function approveUser(id) { await ctx.api('/admin/users/' + id + '/approve', { method: 'POST' }); ctx.toast('已批准'); rerender(); }

export function showRejectModal(id) {
  document.getElementById('modal-container').innerHTML =
    '<div class="modal-overlay" data-action="closeModal"><div class="modal" style="max-width:400px"><h3>拒绝注册申请</h3>' +
    '<div class="form-group"><label>拒绝理由（可选）</label><textarea id="reject-reason" placeholder="填写拒绝理由..." style="width:100%;height:80px;padding:8px 12px;border-radius:6px;border:1px solid var(--border);font-size:13px;resize:vertical"></textarea></div>' +
    '<div class="modal-actions"><button class="btn" data-action="closeModal">取消</button><button class="btn btn-danger" data-action="rejectUser" data-param="' + id + '">确认拒绝</button></div></div></div>';
  setTimeout(function() { document.getElementById('reject-reason').focus(); }, 100);
}

export async function rejectUser(id) {
  var reason = (document.getElementById('reject-reason') || {}).value || '';
  await ctx.api('/admin/users/' + id + '/reject', { method: 'POST', body: { reason: reason.trim() } });
  document.getElementById('modal-container').innerHTML = '';
  ctx.toast('已拒绝'); rerender();
}

export async function deleteUser(id) { if (!confirm('确定删除该用户？')) return; await ctx.api('/admin/users/' + id, { method: 'DELETE' }); ctx.toast('已删除'); rerender(); }

var ALL_PERMS = {
  players: '玩家管理', kick: '踢人', ban: '封禁管理', reserved: '预留位管理', tk: 'TK 管理',
  points: '积分管理', quick_commands: '快捷命令', rcon: 'RCON 终端', settings: '服务器设置', user_admin: '用户管理', plugins: '插件管理',
};

export async function showPermModal(userId) {
  var r = await ctx.api('/admin/users/' + userId + '/permissions');
  if (r.error) return ctx.toast(r.error, 'error');
  var perms = r.permissions || {};
  var rows = Object.keys(ALL_PERMS).map(function(key) {
    var checked = perms[key] ? ' checked' : '';
    return '<tr><td style="padding:8px 0;font-weight:500">' + ALL_PERMS[key] + '</td>' +
      '<td style="text-align:center"><code style="font-size:11px;color:var(--text3)">' + key + '</code></td>' +
      '<td style="text-align:center"><input type="checkbox" id="perm-' + key + '"' + checked + ' style="transform:scale(1.2)"></td></tr>';
  }).join('');
  document.getElementById('modal-container').innerHTML =
    '<div class="modal-overlay" data-action="closeModal"><div class="modal" style="min-width:480px;max-height:80vh;overflow-y:auto"><h3>编辑用户权限</h3>' +
    '<div style="color:var(--text2);font-size:13px;margin-bottom:16px">勾选该用户可使用的功能模块。</div>' +
    '<table style="width:100%;font-size:13px;border-collapse:collapse"><tr style="color:var(--text3);border-bottom:1px solid var(--border)"><th style="text-align:left;padding:6px 0">功能</th><th style="text-align:center">Key</th><th style="text-align:center">启用</th></tr>' + rows + '</table>' +
    '<div class="modal-actions" style="margin-top:16px"><button class="btn" data-action="closeModal">取消</button><button class="btn btn-primary" data-action="saveUserPermissions" data-param="' + userId + '">保存</button></div></div></div>';
}

export async function saveUserPermissions(userId) {
  var perms = {};
  Object.keys(ALL_PERMS).forEach(function(key) { var el = document.getElementById('perm-' + key); perms[key] = el ? el.checked : false; });
  var r = await ctx.api('/admin/users/' + userId + '/permissions', { method: 'PUT', body: { permissions: perms } });
  if (r.error) return ctx.toast(r.error, 'error');
  document.getElementById('modal-container').innerHTML = '';
  ctx.toast('权限已更新');
}

// ─── 操作日志 Actions ───

export function logFilter(filter) { opLogActionFilter = filter; loadOpLogs(); }
export function logRefresh() { loadOpLogs(); }

export function logAdd() {
  var options = '';
  Object.keys(OP_ACTIONS).forEach(function(key) {
    if (key !== 'ban') { var a = OP_ACTIONS[key]; options += '<option value="' + key + '">' + a.icon + ' ' + a.label + '</option>'; }
  });
  document.getElementById('modal-container').innerHTML =
    '<div class="modal-overlay" data-action="closeModal"><div class="modal" style="max-width:420px"><h3>手动记录操作日志</h3>' +
    '<div class="form-group"><label>操作类型</label><select id="ola-action" style="width:100%;padding:8px 12px;border-radius:6px;border:1px solid var(--border);font-size:13px">' + options + '</select></div>' +
    '<div class="form-group"><label>目标（玩家名/SteamID）</label><input id="ola-target" placeholder="选填" style="width:100%"></div>' +
    '<div class="form-group"><label>详情说明</label><textarea id="ola-details" placeholder="选填" style="width:100%;height:60px;resize:vertical;padding:8px 12px;border-radius:6px;border:1px solid var(--border);font-size:13px"></textarea></div>' +
    '<div class="modal-actions"><button class="btn" data-action="closeModal">取消</button><button class="btn btn-primary" data-action="logSubmit">提交</button></div></div></div>';
}

export async function logSubmit() {
  var action = document.getElementById('ola-action').value;
  var target = document.getElementById('ola-target').value.trim();
  var details = document.getElementById('ola-details').value.trim();
  try {
    var resp = await fetch('/api/op/logs', { method: 'POST', headers: { 'Content-Type': 'application/json', 'Authorization': 'Bearer ' + ctx.token }, body: JSON.stringify({ action: action, target: target, details: details }) });
    var data = await resp.json();
    if (resp.ok) { document.getElementById('modal-container').innerHTML = ''; ctx.toast('已记录'); loadOpLogs(); }
    else { ctx.toast(data.error || '记录失败', 'error'); }
  } catch (e) { ctx.toast('记录失败: ' + e.message, 'error'); }
}

// ─── 团队 Actions ───

export function switchTeamTab(tab) { teamTab = tab; rerender(); }

export function showCreateTeamModal() {
  document.getElementById('modal-container').innerHTML =
    '<div class="modal-overlay" data-action="closeModal"><div class="modal" style="max-width:400px"><h3>创建团队</h3>' +
    '<div class="form-group"><label>团队名称</label><input id="team-name" placeholder="输入团队名称（最多50字符）" maxlength="50"></div>' +
    '<div class="modal-actions"><button class="btn" data-action="closeModal">取消</button><button class="btn btn-primary" data-action="createTeam">创建</button></div></div></div>';
  setTimeout(function() { document.getElementById('team-name').focus(); }, 100);
}

export async function createTeam() {
  var name = (document.getElementById('team-name') || {}).value || '';
  if (!name.trim()) return ctx.toast('请输入团队名称', 'error');
  var d = await ctx.api('/teams', { method: 'POST', body: { name: name.trim() } });
  if (d.error) return ctx.toast(d.error, 'error');
  document.getElementById('modal-container').innerHTML = '';
  ctx.toast('团队已创建'); rerender();
}

export function showJoinByCodeModal() {
  document.getElementById('modal-container').innerHTML =
    '<div class="modal-overlay" data-action="closeModal"><div class="modal" style="max-width:400px"><h3>通过邀请码加入团队</h3>' +
    '<div class="form-group"><label>邀请码</label><input id="join-code" placeholder="输入8位邀请码" maxlength="8" style="text-transform:uppercase;letter-spacing:2px;font-weight:700;font-size:16px;text-align:center"></div>' +
    '<div class="modal-actions"><button class="btn" data-action="closeModal">取消</button><button class="btn btn-primary" data-action="joinByCode">加入</button></div></div></div>';
  setTimeout(function() { document.getElementById('join-code').focus(); }, 100);
}

export async function joinByCode() {
  var code = ((document.getElementById('join-code') || {}).value || '').trim().toUpperCase();
  if (!code) return ctx.toast('请输入邀请码', 'error');
  var d = await ctx.api('/teams/join-by-code', { method: 'POST', body: { code: code } });
  if (d.error) return ctx.toast(d.error, 'error');
  document.getElementById('modal-container').innerHTML = '';
  ctx.toast('已加入团队: ' + (d.teamName || '')); rerender();
}

export function showJoinRequestModal(teamId, teamName) {
  document.getElementById('modal-container').innerHTML =
    '<div class="modal-overlay" data-action="closeModal"><div class="modal" style="max-width:400px"><h3>申请加入团队</h3>' +
    '<div style="margin-bottom:16px;color:var(--text2)">确认申请加入 <strong>' + ctx.esc(teamName) + '</strong>？服主审核通过后你将自动加入。</div>' +
    '<div class="modal-actions"><button class="btn" data-action="closeModal">取消</button><button class="btn btn-primary" data-action="submitJoinRequest" data-param="' + teamId + '">确认申请</button></div></div></div>';
}

export async function submitJoinRequest(teamId) {
  var d = await ctx.api('/teams/' + teamId + '/join-request', { method: 'POST' });
  if (d.error) return ctx.toast(d.error, 'error');
  document.getElementById('modal-container').innerHTML = '';
  ctx.toast('申请已提交，等待服主审核');
}

export function showGenCodeModal() {
  document.getElementById('modal-container').innerHTML =
    '<div class="modal-overlay" data-action="closeModal"><div class="modal" style="max-width:400px"><h3>生成邀请码</h3>' +
    '<div class="form-group"><label>有效期</label><select id="code-hours" style="width:100%;padding:8px 12px;border-radius:6px;border:1px solid var(--border);font-size:14px">' +
      '<option value="1">1 小时</option><option value="6">6 小时</option><option value="24" selected>24 小时</option><option value="72">3 天</option><option value="168">7 天</option></select></div>' +
    '<div class="modal-actions"><button class="btn" data-action="closeModal">取消</button><button class="btn btn-primary" data-action="genCode">生成</button></div></div></div>';
}

export async function genCode() {
  var hours = parseInt((document.getElementById('code-hours') || {}).value) || 24;
  var d = await ctx.api('/teams/invite-code', { method: 'POST', body: { hours: hours } });
  if (d.error) return ctx.toast(d.error, 'error');
  document.getElementById('modal-container').innerHTML =
    '<div class="modal-overlay" data-action="closeModal"><div class="modal" style="max-width:400px;text-align:center"><h3>邀请码已生成</h3>' +
    '<div style="margin:20px 0"><div style="font-size:28px;font-weight:800;letter-spacing:4px;color:var(--accent);font-family:monospace;background:var(--bg2);padding:16px;border-radius:10px">' + ctx.esc(d.code) + '</div>' +
    '<div style="color:var(--text2);font-size:12px;margin-top:8px">有效期: ' + (d.expiresIn || hours + 'h') + '</div></div>' +
    '<div class="modal-actions" style="justify-content:center"><button class="btn" data-action="closeModal">关闭</button><button class="btn btn-primary" data-action="copyCode" data-param="' + ctx.esc(d.code) + '">复制</button></div></div></div>';
}

export function copyCode(code) {
  if (navigator.clipboard) navigator.clipboard.writeText(code).then(function() { ctx.toast('已复制'); });
  else { var ta = document.createElement('textarea'); ta.value = code; document.body.appendChild(ta); ta.select(); document.execCommand('copy'); document.body.removeChild(ta); ctx.toast('已复制'); }
}

export async function revokeCode(id) { await ctx.api('/teams/invite-code/' + id, { method: 'DELETE' }); ctx.toast('已撤销'); rerender(); }
export async function approveJoinReq(id) { await ctx.api('/teams/requests/' + id + '/approve', { method: 'POST' }); ctx.toast('已批准'); rerender(); }
export async function rejectJoinReq(id) { await ctx.api('/teams/requests/' + id + '/reject', { method: 'POST' }); ctx.toast('已拒绝'); rerender(); }
export async function removeMember(userId) { await ctx.api('/teams/members/' + userId, { method: 'DELETE' }); ctx.toast('已移除'); rerender(); }

export async function leaveTeam() {
  if (!confirm('确定退出团队？')) return;
  var d = await ctx.api('/teams/members/' + ctx.currentUser.id, { method: 'DELETE' });
  if (d.error) return ctx.toast(d.error, 'error');
  ctx.toast('已退出团队'); rerender();
}

// ─── 工具函数 ───

function formatTime(ts) {
  if (!ts) return '-';
  try { var d = new Date(ts + (ts.indexOf('Z') !== -1 ? '' : 'Z')); var pad = function(n) { return String(n).padStart(2, '0'); }; return d.getFullYear() + '-' + pad(d.getMonth()+1) + '-' + pad(d.getDate()) + ' ' + pad(d.getHours()) + ':' + pad(d.getMinutes()); }
  catch (e) { return ts; }
}

function renderOnlineOverview(data) {
  var panelOnline = data.panelOnline || [];
  var gameOnline = data.gameOnline || [];
  var panelListHtml = '';
  if (!panelOnline.length) { panelListHtml = '<div style="color:var(--text3);font-size:12px;padding:8px 0">暂无</div>'; }
  else { for (var i = 0; i < panelOnline.length; i++) { var u = panelOnline[i]; panelListHtml += '<div style="display:flex;align-items:center;justify-content:space-between;padding:4px 0;font-size:12px"><span><strong>' + ctx.esc(u.username) + '</strong> <span style="color:' + (roleColors[u.role] || 'var(--text3)') + '">' + (ROLE_LABELS[u.role] || u.role) + '</span></span><span style="color:var(--text3)">' + timeAgo(u.lastActivity) + '</span></div>'; } }
  var gameListHtml = '';
  if (!gameOnline.length) { gameListHtml = '<div style="color:var(--text3);font-size:12px;padding:8px 0">暂无</div>'; }
  else { for (var i = 0; i < gameOnline.length; i++) { var g = gameOnline[i]; gameListHtml += '<div style="display:flex;align-items:center;justify-content:space-between;padding:4px 0;font-size:12px"><span><strong>' + ctx.esc(g.username) + '</strong> <span style="color:var(--text3)">(' + ctx.esc(g.playerName || g.steamId) + ')</span></span><span style="color:var(--text3)">' + ctx.esc(g.serverName || '?') + ' · ' + timeAgo(g.lastSeen) + '</span></div>'; } }
  return '<div class="section-card" style="margin-bottom:16px">' +
    '<div style="display:flex;align-items:center;justify-content:space-between;margin-bottom:12px"><span style="font-size:14px;font-weight:600">在线概览</span><button class="btn btn-sm" data-action="refreshOnline">↻ 刷新</button></div>' +
    '<div style="display:grid;grid-template-columns:1fr 1fr;gap:12px">' +
      '<div style="background:var(--bg2);border-radius:8px;padding:12px"><div style="display:flex;align-items:center;gap:6px;margin-bottom:8px"><span style="font-size:16px">🖥️</span><span style="font-weight:600;font-size:13px">面板在线</span><span style="background:var(--green);color:#fff;padding:1px 8px;border-radius:100px;font-size:11px;font-weight:600">' + panelOnline.length + '</span></div><div style="max-height:160px;overflow-y:auto">' + panelListHtml + '</div></div>' +
      '<div style="background:var(--bg2);border-radius:8px;padding:12px"><div style="display:flex;align-items:center;gap:6px;margin-bottom:8px"><span style="font-size:16px">🎮</span><span style="font-weight:600;font-size:13px">游戏在线</span><span style="background:var(--accent);color:#fff;padding:1px 8px;border-radius:100px;font-size:11px;font-weight:600">' + gameOnline.length + '</span></div><div style="max-height:160px;overflow-y:auto">' + gameListHtml + '</div></div>' +
    '</div></div>';
}

function timeAgo(ts) {
  if (!ts) return '-';
  try { var d = new Date(ts + (ts.indexOf('Z') !== -1 ? '' : 'Z')); var diff = Math.floor((Date.now() - d.getTime()) / 1000); if (diff < 60) return '刚刚'; if (diff < 3600) return Math.floor(diff / 60) + '分钟前'; if (diff < 86400) return Math.floor(diff / 3600) + '小时前'; return Math.floor(diff / 86400) + '天前'; }
  catch (e) { return ts; }
}

// ─── Module Contract ───
export const manifest = { id: 'users', label: '人员管理', icon: '👥', section: 'system', order: 3, access: ['server_owner'], permissions: ['user_admin'] };
export const pages = { 'users': renderUsers };
export const actions = {
  refreshOnline, refreshUsers, userSearch,
  showAddUserModal, addUser, showRoleModal, confirmRoleChange, approveUser, rejectUser, deleteUser,
  showRejectModal, showPermModal, saveUserPermissions,
  switchTab, logFilter, logRefresh, logAdd, logSubmit,
  // 团队
  switchTeamTab, showCreateTeamModal, createTeam, showJoinByCodeModal, joinByCode,
  showJoinRequestModal, submitJoinRequest, showGenCodeModal, genCode, copyCode, revokeCode,
  approveJoinReq, rejectJoinReq, removeMember, leaveTeam,
};

