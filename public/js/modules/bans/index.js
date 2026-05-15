// ─── bans.js — 封禁列表 + 预留位管理 ───

let ctx;
export function init(_ctx) { ctx = _ctx; }

export async function renderBans(el) {
  var subTab = ctx.bansSubTab || 'bans';
  var html = '<div id="bans-subnav" style="display:flex;gap:4px;margin-bottom:16px">';
  html += '<span class="nav-item' + (subTab === 'bans' ? ' active' : '') + '" data-action="switchBansTab" data-subnav="bans" style="padding:6px 14px;border-radius:6px;cursor:pointer;font-size:13px">🚫 封禁列表</span>';
  if (ctx.currentUser && ctx.hasPerm('reserved')) {
    html += '<span class="nav-item' + (subTab === 'reserved' ? ' active' : '') + '" data-action="switchBansTab" data-subnav="reserved" style="padding:6px 14px;border-radius:6px;cursor:pointer;font-size:13px">🎟️ 预留位管理</span>';
  }
  html += '</div>';

  if (subTab === 'bans') {
    html += await _renderBanList();
  } else {
    html += await _renderReservedSlots();
  }

  el.innerHTML = html;
}

async function _renderBanList() {
  var d = await ctx.api("/bans");
  var b = d.bans || [];
  var html = '<div style="display:flex;justify-content:space-between;align-items:center;margin-bottom:12px">';
  html += '<h3 style="margin:0;font-size:15px">封禁记录</h3>';
  html += '<button class="btn btn-primary btn-sm" data-action="showAddBanModal">＋ 添加封禁</button>';
  html += '</div>';

  if (b.length) {
    html += '<div class="card" style="padding:0;overflow:hidden"><table><tr><th>玩家</th><th>Steam ID</th><th>原因</th><th>封禁人</th><th>时间</th><th>操作</th></tr>';
    b.forEach(function(x) {
      html += '<tr>';
      html += '<td style="font-weight:600;color:var(--text)">' + ctx.esc(x.playerName || '-') + '</td>';
      html += '<td class="font-mono">' + ctx.esc(x.steamId) + '</td>';
      html += '<td>' + ctx.esc(x.reason || '-') + '</td>';
      html += '<td>' + ctx.esc(x.bannedBy || '-') + '</td>';
      html += '<td style="color:var(--text3)">' + x.createdAt + '</td>';
      html += '<td><button class="btn btn-sm" data-action="serverUnban" data-param="' + x.id + '">解封</button> <button class="btn btn-sm btn-danger" data-action="deleteBan" data-param="' + x.id + '">删除</button></td>';
      html += '</tr>';
    });
    html += '</table></div>';
  } else {
    html += '<div class="empty"><div class="title">暂无封禁记录</div></div>';
  }
  return html;
}

async function _renderReservedSlots() {
  var rs = await ctx.api('/reserved-slots');
  var slots = rs.slots || [];
  var html = '<div class="card"><h3>添加预留位</h3>';
  html += '<div style="display:flex;gap:12px;align-items:center">';
  html += '<input id="rs-steam" placeholder="Steam ID" class="inline-input" style="flex:1">';
  html += '<input id="rs-name" placeholder="玩家名 (可选)" class="inline-input" style="width:160px">';
  html += '<button class="btn btn-primary" data-action="addReservedSlot">添加</button>';
  html += '</div></div>';

  if (slots.length) {
    html += '<div class="card" style="padding:0;overflow:hidden;margin-top:12px">';
    html += '<table><tr><th>玩家名</th><th>Steam ID</th><th>添加人</th><th>添加时间</th><th>有效期至</th><th>操作</th></tr>';
    slots.forEach(function(s) {
      var expireInfo = s.expiresAt ? '<span style="color:var(--warn)">' + s.expiresAt + '</span>' : '<span style="color:var(--text3)">永久</span>';
      html += '<tr>';
      html += '<td style="font-weight:600;color:var(--text)">' + ctx.esc(s.playerName || '-') + '</td>';
      html += '<td class="font-mono">' + ctx.esc(s.steamId) + '</td>';
      html += '<td>' + ctx.esc(s.addedBy || '-') + '</td>';
      html += '<td style="color:var(--text3)">' + s.createdAt + '</td>';
      html += '<td>' + expireInfo + '</td>';
      html += '<td><button class="btn btn-sm btn-danger" data-action="deleteReservedSlot" data-param="' + s.id + '">移除</button></td>';
      html += '</tr>';
    });
    html += '</table></div>';
  } else {
    html += '<div class="empty"><div class="title">暂无预留位</div><div class="desc">添加需要预留服务器位置的玩家 Steam ID</div></div>';
  }
  return html;
}

// ─── Tab switching ───
export function switchBansTab(tab) {
  if (tab instanceof HTMLElement) {
    tab = tab.getAttribute('data-subnav') || tab.dataset.subnav;
  }
  ctx.setBansSubTab(tab);
  import('../../app.js?v=1775700005').then(function(m) { m.render(); });
}

// ─── Ban actions ───
export function showAddBanModal() {
  document.getElementById("modal-container").innerHTML =
    "<div class=\"modal-overlay\" data-action=\"closeModal\"><div class=\"modal\" onclick=\"event.stopPropagation()\"><h3>手动添加封禁</h3>" +
    "<input id=\"ban-steamid\" placeholder=\"Steam ID\" style=\"width:100%;margin:8px 0;padding:8px\">" +
    "<input id=\"ban-name\" placeholder=\"玩家名\" style=\"width:100%;margin:8px 0;padding:8px\">" +
    "<input id=\"ban-reason\" placeholder=\"原因\" style=\"width:100%;margin:8px 0;padding:8px\">" +
    "<div style=\"display:flex;gap:8px;margin-top:12px\"><button class=\"btn btn-primary\" data-action=\"addBan\">添加</button><button class=\"btn\" data-action=\"closeModal\">取消</button></div></div></div>";
}

export async function addBan() {
  var steamId = document.getElementById("ban-steamid").value.trim();
  var playerName = document.getElementById("ban-name").value.trim();
  var reason = document.getElementById("ban-reason").value.trim();
  if (!steamId) return ctx.toast("Steam ID 必填", "error");
  var d = await ctx.api("/bans", { method: "POST", body: JSON.stringify({ steamId: steamId, playerName: playerName, reason: reason, serverId: ctx.selectedServerId || ctx.servers[0]?.id }) });
  if (d.error) return ctx.toast(d.error, "error");
  ctx.toast("封禁添加成功");
  ctx.closeModal();
  import('../../app.js?v=1775700005').then(function(m) { m.render(); });
}

export async function deleteBan(id) {
  if (!confirm("确定删除此封禁记录？")) return;
  var d = await ctx.api("/bans/" + id, { method: "DELETE" });
  if (d.error) return ctx.toast(d.error, "error");
  ctx.toast("已删除");
  import('../../app.js?v=1775700005').then(function(m) { m.render(); });
}

export async function serverUnban(banId) {
  var d = await ctx.api("/bans/" + banId, { method: "DELETE" });
  if (d.error) return ctx.toast(d.error, "error");
  if (d.unbanResult && d.unbanResult.error) {
    ctx.toast("面板记录已删除，但 relay 解封失败: " + d.unbanResult.error, "warning");
  } else if (d.unbanResult && d.unbanResult.ok) {
    ctx.toast("已解封并从游戏服务器 BanList.cfg 移除");
  } else {
    ctx.toast("面板记录已删除（无 relay 连接，请手动编辑服务端 BanList.cfg）");
  }
  import('../../app.js?v=1775700005').then(function(m) { m.render(); });
}

// ─── Reserved slot actions ───
export async function addReservedSlot() {
  var sid = window._selectedServerId;
  var steam = document.getElementById('rs-steam').value;
  var name = document.getElementById('rs-name').value;
  if (!sid || !steam) return ctx.toast('请选择服务器并填写 Steam ID', 'error');
  var r = await ctx.api('/reserved-slots', { method: 'POST', body: { serverId: parseInt(sid), steamId: steam, playerName: name || null } });
  if (r.error) return ctx.toast(r.error || '操作失败', 'error');
  ctx.toast('预留位已添加');
  import('../../app.js?v=1775700005').then(function(m) { m.render(); });
}

export async function deleteReservedSlot(id) {
  if (!confirm('确定移除该预留位？')) return;
  await ctx.api('/reserved-slots/' + id, { method: 'DELETE' });
  ctx.toast('已移除');
  import('../../app.js?v=1775700005').then(function(m) { m.render(); });
}

export const manifest = { id: "bans", label: "封禁管理", icon: "🚫", section: "data", order: 2, permissions: ["ban"], subnav: [
  { id: 'bans', label: '封禁列表', permissions: ['ban'] },
  { id: 'reserved', label: '预留位管理', permissions: ['reserved'] },
] };
export const pages = { "bans": renderBans };
export const actions = { switchBansTab, showAddBanModal, addBan, deleteBan, serverUnban, addReservedSlot, deleteReservedSlot };
