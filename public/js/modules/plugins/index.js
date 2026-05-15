 

// ─── plugins.js — 插件管理（核心功能列表 + 配置 + 口令管理）───

import { PLUGIN_SCHEMAS } from './state.js?v=1775700005';
import { api, toast, esc, escAttr, renderRefreshBar, stopAutoRefresh, startAutoRefresh, getRefreshInterval, doPageRefresh } from '../../utils.js?v=1775700005';
let ctx;
export function init(_ctx) { ctx = _ctx; }

export const manifest = { id: 'plugins', label: '插件管理', icon: '🔌', section: 'system', order: 1, permissions: ['plugins'] };
export const pages = { 'plugins': renderPlugins };
export const actions = { switchPluginTab, showJsPluginConfig, saveJsPluginConfig, showChatCmdManager, addChatCmd, editChatCmd, saveChatCmd, deleteChatCmd, showCdkManager, showCdkLogs, batchCreateCdk, deleteCdk, showBroadcastManager, addBroadcastMsg, removeBroadcastMsgDirect, removeBroadcastMsg, saveBroadcastMsgs };


export async function renderPlugins(el) {
  var d = await ctx.api('/plugins/js');
  var plugins = d.plugins || [];
  var settings = d.settings || {};

  var html = ctx.renderRefreshBar('plugins', '');

  html += '<div class="card"><h3>核心功能</h3>';
  html += '<div style="color:var(--text2);font-size:13px;margin-bottom:16px">以下功能由面板自动管理，点击「配置」调整参数。</div>';

  html += '<table style="font-size:14px"><tr><th>功能</th><th>说明</th><th>监听事件</th><th>版本</th><th>状态</th><th>操作</th></tr>';

  plugins.forEach(function(p) {
    var eventBadges = (p.events || []).map(function(e) {
      var labels = { kill: '击杀', chat: '聊天', revive: '救援', playerlist: '玩家列表' };
      return '<span style="background:var(--bg3);padding:2px 8px;border-radius:4px;font-size:11px;margin:0 2px">' + (labels[e] || e) + '</span>';
    }).join('');

    var statusBadge = p.loaded
      ? '<span style="color:var(--green);font-size:13px">● 运行中</span>'
      : '<span style="color:var(--red);font-size:13px">● 未加载</span>';

    var hasConfig = (PLUGIN_SCHEMAS[p.name] || []).length > 0;
    var configBtn = hasConfig
      ? '<button class="btn btn-sm" data-action="showJsPluginConfig" data-param="' + ctx.escAttr(p.name) + '">配置</button>'
      : '<span style="color:var(--text2);font-size:12px">无需配置</span>';

    // ChatCommands gets an extra button
    if (p.name === 'ChatCommands') {
      configBtn = '<button class="btn btn-sm" data-action="showJsPluginConfig" data-param="' + ctx.escAttr(p.name) + '">配置</button> <button class="btn btn-sm btn-primary" data-action="showChatCmdManager">口令管理</button>';
    }
    // CDKRedeem gets CDK management button
    if (p.name === 'CDKRedeem') {
      configBtn = '<button class="btn btn-sm" data-action="showJsPluginConfig" data-param="' + ctx.escAttr(p.name) + '">配置</button> <button class="btn btn-sm btn-primary" data-action="showCdkManager">CDK管理</button>';
    }
    // TimedBroadcast gets message management button
    if (p.name === 'TimedBroadcast') {
      configBtn = '<button class="btn btn-sm" data-action="showJsPluginConfig" data-param="' + ctx.escAttr(p.name) + '">配置</button> <button class="btn btn-sm btn-primary" data-action="showBroadcastManager">广播消息</button>';
    }

    html += '<tr>';
    html += '<td><b>' + ctx.esc(p.label) + '</b><br><code style="font-size:11px;color:var(--text2)">' + ctx.esc(p.name) + '</code></td>';
    html += '<td style="font-size:13px;color:var(--text2);max-width:260px">' + ctx.esc(p.desc) + '</td>';
    html += '<td>' + eventBadges + '</td>';
    html += '<td style="font-size:13px">' + ctx.esc(p.version) + '</td>';
    html += '<td>' + statusBadge + '</td>';
    html += '<td>' + configBtn + '</td>';
    html += '</tr>';
  });

  html += '</table></div>';

  html += '<div style="margin-top:12px"><button class="btn btn-sm" data-action="reloadPlugins">重载所有插件</button> <input type="file" id="plugin-upload-input" accept=".cpp" style="display:none" onchange="uploadPluginFile(this)"><button class="btn btn-sm btn-primary" onclick="document.getElementById(\'plugin-upload-input\').click()">上传插件</button></div>';

  el.innerHTML = html;
}

export function switchPluginTab(tab) {
  import('../../app.js?v=1775700005').then(function(m) { m.render(); });
}

// ─── JS 插件配置弹窗 ───
export async function showJsPluginConfig(pluginName) {
  var d = await ctx.api('/plugins/js');
  var plugins = d.plugins || [];
  var settings = d.settings || {};
  var plugin = plugins.find(function(p) { return p.name === pluginName; });
  if (!plugin) return ctx.toast('插件未找到', 'error');

  var schema = PLUGIN_SCHEMAS[pluginName] || [];
  if (!schema.length) return ctx.toast('此插件无需配置', 'info');

  var html = '<div class="modal-overlay" data-action="closeModal">' +
    '<div class="modal" style="min-width:440px;max-width:560px">' +
    '<h3>' + ctx.esc(plugin.label) + ' 配置</h3>' +
    '<div style="font-size:12px;color:var(--text2);margin-bottom:16px">' + ctx.esc(plugin.desc) + '</div>';

  schema.forEach(function(field) {
    var val = settings[field.key];
    if (val === undefined || val === null) val = String(field.default !== undefined ? field.default : '');

    html += '<div class="form-group" style="margin-bottom:14px"><label style="font-weight:600">' + ctx.esc(field.label) + '</label>';
    if (field.type === 'bool') {
      var checked = (val === '1' || val === 'true' || val === true) ? ' checked' : '';
      html += '<label style="display:flex;align-items:center;gap:8px;cursor:pointer;margin-top:4px"><input type="checkbox" id="jpc-' + field.key + '"' + checked + '> 启用</label>';
    } else if (field.type === 'number') {
      html += '<input id="jpc-' + field.key + '" type="number" value="' + ctx.esc(String(val)) + '" style="width:100%;padding:8px 12px;border-radius:6px;border:1px solid var(--border);font-size:14px">';
    } else {
      html += '<input id="jpc-' + field.key + '" value="' + ctx.esc(String(val)) + '" style="width:100%;padding:8px 12px;border-radius:6px;border:1px solid var(--border);font-size:14px">';
    }
    html += '</div>';
  });

  html += '<div style="font-size:12px;color:var(--text2);margin-bottom:16px">修改后点击保存，立即生效（无需重启）。</div>';
  html += '<div class="modal-actions">' +
    '<button class="btn" data-action="closeModal">取消</button>' +
    '<button class="btn btn-primary" data-action="saveJsPluginConfig" data-param="' + ctx.escAttr(pluginName) + '">保存配置</button>' +
    '</div></div></div>';

  document.getElementById('modal-container').innerHTML = html;
}

export async function saveJsPluginConfig(pluginName) {
  var schema = PLUGIN_SCHEMAS[pluginName] || [];
  try {
    for (var i = 0; i < schema.length; i++) {
      var field = schema[i];
      var el = document.getElementById('jpc-' + field.key);
      if (!el) continue;
      var val;
      if (field.type === 'bool') { val = el.checked ? '1' : '0'; }
      else { val = el.value; }
      await ctx.api('/plugins/settings', { method: 'POST', body: { key: field.key, value: val } });
    }
    document.getElementById('modal-container').innerHTML = '';
    ctx.toast('配置已保存，立即生效');
  } catch(e) {
    ctx.toast('保存失败: ' + e.message, 'error');
  }
}

// ─── 口令管理 ───
export async function showChatCmdManager() {
  ctx.stopAutoRefresh('plugins-list');
  ctx.stopAutoRefresh('plugins-plugin');
  var d = await ctx.api('/chat-commands');
  var cmds = d.commands || [];

  var html = '<div class="modal-overlay" style="background:rgba(0,0,0,.6)">' +
    '<div class="modal" style="min-width:700px;max-width:900px;max-height:85vh;overflow-y:auto">' +
    '<h3>口令管理</h3>' +
    '<div style="color:var(--text2);font-size:13px;margin-bottom:16px">玩家在游戏聊天中输入口令，自动触发对应操作。</div>';

  html += '<table style="font-size:13px;margin-bottom:16px"><tr><th>名称</th><th>触发口令</th><th>操作</th><th>消耗</th><th>奖励</th><th>状态</th><th>操作</th></tr>';
  cmds.forEach(function(cmd, i) {
    var actionLabels = {
      sign_in: '签到得积分', lottery: '抽奖得积分', query_kd: '查询战绩', query_points: '查询积分',
      redeem: '兑换预留位', switch_team: '跳边'
    };
    html += '<tr>';
    html += '<td>' + ctx.esc(cmd.name) + '</td>';
    html += '<td><code style="background:var(--bg3);padding:2px 8px;border-radius:4px">' + ctx.esc(cmd.trigger) + '</code></td>';
    html += '<td style="font-size:12px;color:var(--text2)">' + (actionLabels[cmd.action] || cmd.action) + '</td>';
    html += '<td>' + (cmd.cost || 0) + '</td>';
    html += '<td>' + (cmd.reward || '-') + '</td>';
    html += '<td>' + (cmd.enabled ? '<span style="color:var(--green)">启用</span>' : '<span style="color:var(--red)">禁用</span>') + '</td>';
    html += '<td><button class="btn btn-sm" data-action="editChatCmd" data-param="' + i + '">编辑</button> <button class="btn btn-sm btn-danger" data-action="deleteChatCmd" data-param="' + i + '">删除</button></td>';
    html += '</tr>';
  });
  html += '</table>';

  html += '<button class="btn btn-primary" data-action="addChatCmd">+ 添加口令</button>';
  html += ' <button class="btn" data-action="closeModal">关闭</button>';
  html += '</div></div>';

  document.getElementById('modal-container').innerHTML = html;
}

export async function addChatCmd() {
  var d = await ctx.api('/chat-commands');
  var cmds = d.commands || [];
  cmds.push({ name: '新口令', trigger: '', action: 'sign_in', cost: 0, reward: 5, enabled: true });
  await ctx.api('/chat-commands', { method: 'POST', body: { commands: cmds } });
  ctx.toast('已添加，请编辑');
  showChatCmdManager();
}

export async function editChatCmd(index) {
  var d = await ctx.api('/chat-commands');
  var cmds = d.commands || [];
  var cmd = cmds[index];
  if (!cmd) return;

  var actionOpts = [
    ['sign_in', '签到得积分'], ['lottery', '抽奖得积分'], ['query_kd', '查询战绩'], ['query_points', '查询积分'],
    ['redeem', '兑换预留位'], ['switch_team', '跳边']
  ].map(function(a) {
    return '<option value="' + a[0] + '"' + (cmd.action === a[0] ? ' selected' : '') + '>' + a[1] + '</option>';
  }).join('');

  var html = '<div class="modal-overlay" data-action="closeModal">' +
    '<div class="modal" style="min-width:420px">' +
    '<h3>编辑口令</h3>' +
    '<div class="form-group"><label>名称</label><input id="ec-name" value="' + ctx.esc(cmd.name) + '"></div>' +
    '<div class="form-group"><label>触发口令（玩家在聊天输入的内容）</label><input id="ec-trigger" value="' + ctx.esc(cmd.trigger) + '" placeholder="如 qd"></div>' +
    '<div class="form-group"><label>操作类型</label><select id="ec-action" style="width:100%;padding:8px 12px;border-radius:6px;border:1px solid var(--border);font-size:14px">' + actionOpts + '</select></div>' +
    '<div class="form-group"><label>消耗积分</label><input id="ec-cost" type="number" value="' + (cmd.cost || 0) + '"></div>' +
    '<div class="form-group"><label>奖励积分（仅签到）</label><input id="ec-reward" type="number" value="' + (cmd.reward || 0) + '"></div>' +
    '<div class="form-group"><label style="display:flex;align-items:center;gap:8px;cursor:pointer"><input type="checkbox" id="ec-enabled"' + (cmd.enabled !== false ? ' checked' : '') + '> 启用</label></div>' +
    '<div class="modal-actions">' +
    '<button class="btn" data-action="closeModal">取消</button>' +
    '<button class="btn btn-primary" data-action="saveChatCmd" data-param="' + index + '">保存</button>' +
    '</div></div></div>';

  document.getElementById('modal-container').innerHTML = html;
}

export async function saveChatCmd(index) {
  var d = await ctx.api('/chat-commands');
  var cmds = d.commands || [];
  cmds[index] = {
    name: document.getElementById('ec-name').value.trim(),
    trigger: document.getElementById('ec-trigger').value.trim(),
    action: document.getElementById('ec-action').value,
    cost: parseInt(document.getElementById('ec-cost').value) || 0,
    reward: parseInt(document.getElementById('ec-reward').value) || 0,
    enabled: document.getElementById('ec-enabled').checked,
  };
  await ctx.api('/chat-commands', { method: 'POST', body: { commands: cmds } });
  document.getElementById('modal-container').innerHTML = '';
  ctx.toast('已保存');
  showChatCmdManager();
}

export async function deleteChatCmd(index) {
  if (!confirm('确定删除此口令？')) return;
  var d = await ctx.api('/chat-commands');
  var cmds = d.commands || [];
  cmds.splice(index, 1);
  await ctx.api('/chat-commands', { method: 'POST', body: { commands: cmds } });
  ctx.toast('已删除');
  showChatCmdManager();
}

// ─── CDK 管理 ───
export async function showCdkManager() {
  ctx.stopAutoRefresh('plugins-list');
  ctx.stopAutoRefresh('plugins-plugin');
  var d = await ctx.api('/cdk');
  var codes = d.codes || [];
  var html = '<div class="modal-overlay" style="background:rgba(0,0,0,.6)">' +
    '<div class="modal" style="min-width:780px;max-width:960px;max-height:85vh;overflow-y:auto">' +
    '<h3>CDK 激活码管理</h3>' +
    '<div style="color:var(--text2);font-size:13px;margin-bottom:16px">玩家在游戏聊天中输入「cdk CODE」兑换激活码，获得积分或预留位。</div>';

  // Batch create
  html += '<div style="display:flex;gap:8px;align-items:flex-end;margin-bottom:16px;flex-wrap:wrap">';
  html += '<div class="form-group" style="margin:0"><label>批量生成数量</label><input id="cdk-batch-count" type="number" value="5" min="1" max="100" style="width:80px"></div>';
  html += '<div class="form-group" style="margin:0"><label>前缀</label><input id="cdk-batch-prefix" value="CDK" style="width:80px"></div>';
  html += '<div class="form-group" style="margin:0"><label>奖励类型</label><select id="cdk-batch-type"><option value="points">积分</option><option value="reserved">预留位(天)</option></select></div>';
  html += '<div class="form-group" style="margin:0"><label>奖励值</label><input id="cdk-batch-value" type="number" value="10" style="width:80px"></div>';
  html += '<div class="form-group" style="margin:0"><label>最大使用次数</label><input id="cdk-batch-uses" type="number" value="1" min="1" style="width:80px"></div>';
  html += '<button class="btn btn-primary" data-action="batchCreateCdk">批量生成</button>';
  html += '</div>';

  // Existing codes table
  if (codes.length) {
    html += '<table style="font-size:13px"><tr><th>激活码</th><th>奖励</th><th>使用情况</th><th>过期时间</th><th>创建时间</th><th>操作</th></tr>';
    codes.forEach(function(c) {
      var reward = c.rewardType === 'points' ? '+' + c.rewardValue + '积分' : '预留位' + c.rewardValue + '天';
      var uses = c.usedCount + '/' + c.maxUses;
      var expire = c.expiresAt || '永久';
      html += '<tr>';
      html += '<td><code style="background:var(--bg3);padding:2px 6px;border-radius:4px;cursor:pointer" data-copy="' + ctx.escAttr(c.code) + '">' + ctx.esc(c.code) + '</code></td>';
      html += '<td>' + reward + '</td>';
      html += '<td>' + uses + '</td>';
      html += '<td style="font-size:12px;color:var(--text3)">' + ctx.esc(expire) + '</td>';
      html += '<td style="font-size:12px;color:var(--text3)">' + (c.createdAt || '-') + '</td>';
      html += '<td><button class="btn btn-sm btn-danger" data-action="deleteCdk" data-param="' + c.id + '">删除</button></td>';
      html += '</tr>';
    });
    html += '</table>';
  } else {
    html += '<div class="empty" style="padding:24px">暂无激活码</div>';
  }

  html += '<div style="margin-top:12px"><button class="btn" data-action="closeModal">关闭</button> <button class="btn" data-action="showCdkLogs">使用记录</button></div>';
  html += '</div></div>';
  document.getElementById('modal-container').innerHTML = html;
}

export async function showCdkLogs() {
  var d = await ctx.api('/cdk/logs');
  var logs = d.logs || [];
  var html = '<div class="modal-overlay" style="background:rgba(0,0,0,.6)">' +
    '<div class="modal" style="min-width:700px;max-width:900px;max-height:85vh;overflow-y:auto">' +
    '<h3>CDK 使用记录</h3>' +
    '<div style="color:var(--text2);font-size:13px;margin-bottom:16px">所有 CDK 兑换记录</div>';

  if (logs.length) {
    html += '<table style="font-size:13px"><tr><th>CDK码</th><th>玩家名</th><th>Steam ID</th><th>奖励类型</th><th>奖励数值</th><th>兑换时间</th></tr>';
    logs.forEach(function(log) {
      var rewardLabel = log.rewardType === 'points' ? '积分' : '预留位';
      html += '<tr>';
      html += '<td><code style="background:var(--bg3);padding:2px 6px;border-radius:4px">' + ctx.esc(log.code) + '</code></td>';
      html += '<td>' + ctx.esc(log.playerName || '-') + '</td>';
      html += '<td class="font-mono">' + ctx.esc(log.steamId) + '</td>';
      html += '<td>' + rewardLabel + '</td>';
      html += '<td>' + log.rewardValue + '</td>';
      html += '<td style="font-size:12px;color:var(--text3)">' + log.usedAt + '</td>';
      html += '</tr>';
    });
    html += '</table>';
  } else {
    html += '<div class="empty" style="padding:24px">暂无使用记录</div>';
  }

  html += '<div style="margin-top:12px"><button class="btn" data-action="showCdkManager">返回CDK管理</button> <button class="btn" data-action="closeModal">关闭</button></div>';
  html += '</div></div>';
  document.getElementById('modal-container').innerHTML = html;
}

export async function batchCreateCdk() {
  var count = parseInt(document.getElementById('cdk-batch-count').value) || 5;
  var prefix = document.getElementById('cdk-batch-prefix').value || 'CDK';
  var type = document.getElementById('cdk-batch-type').value;
  var value = parseInt(document.getElementById('cdk-batch-value').value) || 10;
  var uses = parseInt(document.getElementById('cdk-batch-uses').value) || 1;
  var r = await ctx.api('/cdk/batch', { method: 'POST', body: { count: count, prefix: prefix, rewardType: type, rewardValue: value, maxUses: uses } });
  if (r.error) return ctx.toast(r.error, 'error');
  ctx.toast(r.message);
  showCdkManager();
}

export async function deleteCdk(id) {
  if (!confirm('确定删除此激活码？')) return;
  await ctx.api('/cdk/' + id, { method: 'DELETE' });
  ctx.toast('已删除');
  showCdkManager();
}

// ─── 广播消息管理 ───
export async function showBroadcastManager() {
  ctx.stopAutoRefresh('plugins-list');
  ctx.stopAutoRefresh('plugins-plugin');
  var d = await ctx.api('/broadcast/messages');
  var msgs = d.messages || [];
  var html = '<div class="modal-overlay" style="background:rgba(0,0,0,.6)">' +
    '<div class="modal" style="min-width:600px;max-width:800px;max-height:85vh;overflow-y:auto">' +
    '<h3>定时广播消息管理</h3>' +
    '<div style="color:var(--text2);font-size:13px;margin-bottom:16px">按配置的间隔循环发送以下消息（每条消息发送后轮到下一条）。</div>';

  html += '<div id="broadcast-msg-list">';
  msgs.forEach(function(msg, i) {
    html += '<div style="display:flex;gap:8px;margin-bottom:8px;align-items:center">';
    html += '<input class="broadcast-msg-input" data-index="' + i + '" value="' + ctx.esc(msg) + '" style="flex:1;padding:8px 12px;border-radius:6px;border:1px solid var(--border);font-size:14px">';
    html += '<button class="btn btn-sm btn-danger" data-action="removeBroadcastMsg" data-param="' + i + '">删除</button>';
    html += '</div>';
  });
  html += '</div>';

  html += '<button class="btn btn-sm" data-action="addBroadcastMsg" style="margin-bottom:16px">+ 添加消息</button>';
  html += '<div class="modal-actions"><button class="btn" data-action="closeModal">取消</button><button class="btn btn-primary" data-action="saveBroadcastMsgs">保存</button></div>';
  html += '</div></div>';
  document.getElementById('modal-container').innerHTML = html;
}

export function addBroadcastMsg() {
  var list = document.getElementById('broadcast-msg-list');
  var count = list.querySelectorAll('.broadcast-msg-input').length;
  var div = document.createElement('div');
  div.style.cssText = 'display:flex;gap:8px;margin-bottom:8px;align-items:center';
  div.innerHTML = '<input class="broadcast-msg-input" data-index="' + count + '" value="" placeholder="输入广播消息" style="flex:1;padding:8px 12px;border-radius:6px;border:1px solid var(--border);font-size:14px">' +
    '<button class="btn btn-sm btn-danger" data-action="removeBroadcastMsgDirect">删除</button>';
  list.appendChild(div);
}

export function removeBroadcastMsgDirect(arg1, arg2) {
  var el = arg2 || arg1;
  if (el && el.parentElement) el.parentElement.remove();
}

export function removeBroadcastMsg(arg1, arg2) {
  var index = (typeof arg1 === 'number' || (typeof arg1 === 'string' && arg1 !== '' && !isNaN(arg1))) ? parseInt(arg1) : -1;
  var inputs = document.querySelectorAll('.broadcast-msg-input');
  if (index >= 0 && inputs[index]) inputs[index].parentElement.remove();
}

export async function saveBroadcastMsgs() {
  var inputs = document.querySelectorAll('.broadcast-msg-input');
  var msgs = [];
  inputs.forEach(function(el) { var v = el.value.trim(); if (v) msgs.push(v); });
  await ctx.api('/broadcast/messages', { method: 'POST', body: { messages: msgs } });
  document.getElementById('modal-container').innerHTML = '';
  ctx.toast('广播消息已保存');
  ctx.startAutoRefresh('plugins-list', function() { ctx.doPageRefresh('plugins-list'); }, ctx.getRefreshInterval('plugins-list'));
}

// ─── 兼容旧函数 ───
export async function installPlugin(n) {}
export async function uninstallPlugin(id) {}
export async function togglePlugin(id, enabled) {}
export async function showPluginConfig(id) {}
export async function savePluginConfig(id) {}
export function handleFileSelect(input) {}
export async function checkCompile() {}
export function showCheckError(msg) {}
export async function uploadAndCompile() {}
export async function downloadBuild(id, filename) {}
export async function deleteBuild(id) {}
export async function installFromBuild(buildId) {}

// ─── Plugin Upload ───
window.uploadPluginFile = async function(input) {
  var file = input.files[0];
  if (!file) return;
  if (!file.name.endsWith(".cpp")) { alert("Only .cpp files allowed"); return; }
  var reader = new FileReader();
  reader.onload = async function(e) {
    try {
      var token = localStorage.getItem("token");
      var resp = await fetch("/api/plugins/upload?file=" + encodeURIComponent(file.name), {
        method: "POST",
        headers: { "Authorization": "Bearer " + token, "Content-Type": "text/plain" },
        body: e.target.result
      });
      var data = await resp.json();
      if (data.error) { alert("Upload failed: " + data.error); }
      else { alert("Plugin uploaded and compiled! Service restarting..."); location.reload(); }
    } catch(err) { alert("Upload error: " + err.message); }
  };
  reader.readAsText(file);
  input.value = "";
};
