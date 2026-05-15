// ─── chat.js — 聊天记录 ───
// 聊天数据由 relay → raw-events.js → chat_logs 表自动采集
// Squad 无 DumpLog RCON 命令，不支持从服务器拉取


let ctx;
export function init(_ctx) { ctx = _ctx; }

export async function renderLogs(el) {
  var chatRefreshExtra = '';
  var html = ctx.renderRefreshBar('logs', chatRefreshExtra);
  html += '<div style="margin-bottom:12px"><input id="chat-filter" placeholder="筛选玩家名、Steam ID 或消息内容..." style="width:100%;max-width:400px;padding:8px 12px;border-radius:6px;border:1px solid var(--border);font-size:14px;outline:none" oninput="applyChatFilter()" value="' + ctx.esc(ctx.chatFilter) + '"></div>';
  var d = await ctx.api('/chat');
  var chats = d.chats || [];
  ctx.setAllChats(chats);
  var sid = '';
  html += '<div class="card" style="padding:0;overflow:hidden"><table id="chat-table" style="margin:0"><tr><th style="width:140px">时间</th><th>玩家</th><th>消息</th><th style="width:200px">操作</th></tr><tbody>';
  if (chats.length) {
    chats.forEach(function(c) {
      if (!sid) {
        sid = window._selectedServerId || 0;
      }
      html += '<tr><td style="font-size:12px;color:var(--text2)">' + ctx.esc(c.timestamp || '-') + '</td><td>' + ctx.esc(c.playerName || '-') + '<br><span style="font-size:11px;color:var(--text2)">' + ctx.esc(c.steamId || '') + '</span></td><td>' + ctx.esc(c.message) + '</td><td style="white-space:nowrap">';
      if (c.steamId && sid) {
        html += '<button class="btn btn-sm" data-action="switchTeamChat" data-params=\'[' + sid + ',"' + ctx.escAttr(c.steamId) + '"]\'>跳边</button> ';
        html += '<button class="btn btn-sm" data-action="kickPlayer" data-params=\'[' + sid + ',"' + ctx.escAttr(c.steamId) + '"]\'>踢出</button> ';
        html += '<button class="btn btn-sm btn-danger" data-action="banPlayer" data-params=\'[' + sid + ',"' + ctx.escAttr(c.steamId) + '","' + ctx.escAttr(c.playerName || '') + '"]\'>封禁</button>';
      }
      html += '</td></tr>';
    });
  } else {
    html += '<tr><td colspan="4" class="empty">暂无聊天记录（relay 自动采集，需先连接游戏服务器）</td></tr>';
  }
  html += '</tbody></table></div>';
  el.innerHTML = html;
  if (ctx.chatFilter) applyChatFilter();
}

export function applyChatFilter() {
  var q = (document.getElementById('chat-filter') ? document.getElementById('chat-filter').value : '').toLowerCase().trim();
  ctx.setChatFilter(q);
  var tbody = document.querySelector('#chat-table tbody');
  if (!tbody) return;
  var sid = document.getElementById('chat-server') ? parseInt(document.getElementById('chat-server').value) || 0 : 0;
  var html = '';
  var filtered = q ? ctx.allChats.filter(function(c) {
    return (c.playerName || '').toLowerCase().includes(q) ||
           (c.steamId || '').toLowerCase().includes(q) ||
           (c.message || '').toLowerCase().includes(q);
  }) : ctx.allChats;
  if (filtered.length) {
    filtered.forEach(function(c) {
      html += '<tr><td style="font-size:12px;color:var(--text2)">' + ctx.esc(c.timestamp || '-') + '</td><td>' + ctx.esc(c.playerName || '-') + '<br><span style="font-size:11px;color:var(--text2)">' + ctx.esc(c.steamId || '') + '</span></td><td>' + ctx.esc(c.message) + '</td><td style="white-space:nowrap">';
      if (c.steamId && sid) {
        html += '<button class="btn btn-sm" data-action="switchTeamChat" data-params=\'[' + sid + ',"' + ctx.escAttr(c.steamId) + '"]\'>跳边</button> ';
        html += '<button class="btn btn-sm" data-action="kickPlayer" data-params=\'[' + sid + ',"' + ctx.escAttr(c.steamId) + '"]\'>踢出</button> ';
        html += '<button class="btn btn-sm btn-danger" data-action="banPlayer" data-params=\'[' + sid + ',"' + ctx.escAttr(c.steamId) + '","' + ctx.escAttr(c.playerName || '') + '"]\'>封禁</button>';
      }
      html += '</td></tr>';
    });
  } else {
    html += '<tr><td colspan="4" class="empty">' + (q ? '没有匹配 "' + ctx.esc(q) + '" 的聊天记录' : '暂无聊天记录') + '</td></tr>';
  }
  tbody.innerHTML = html;
}

export async function switchTeamChat(serverId, steamId) {
  if (!serverId) return ctx.toast('请先选择服务器', 'error');
  var state = await import('./state.js?v=1775700005');
  var player = state.currentPlayers.find(function(p) { return p.steamId === steamId; });
  if (!player) return ctx.toast('请先在「玩家管理」刷新玩家列表', 'error');
  var servers = await import('../servers/index.js?v=1775700005');
  await ctx.servers.switchTeam(serverId, player.rconId);
}

// ─── Module Contract ───
export const manifest = { id: 'chat', label: '聊天记录' };
export const pages = {};
export const actions = { applyChatFilter, switchTeamChat };
