// ─── servers-map.js — 地图选择器（从服务器实时拉取图层 + 自由选阵营）───

let ctx;
export function init(_ctx) { ctx = _ctx; }

function closeModal() { document.getElementById("modal-container").innerHTML = ""; }

function qcGetServer() {
  var sid = window._selectedServerId;
  if (!sid) { ctx.toast('请先选择服务器', 'error'); return null; }
  return sid;
}

// ═══ 数据 ═══
var _mapModalState = {};
var _layerData = null;
var _factionsData = null; // factions.json: [{code, name, roles: [{code, name}]}]
var _idx = {};

// 阵营中文名 fallback（factions.json 未加载时用）
var _FACTION_CN_FALLBACK = {
  USA:'美军',USMC:'美海军陆战队',BAF:'英陆军',CAF:'加军',ADF:'澳国防军',
  RGF:'俄地面部队',VDV:'俄空降兵',PLA:'解放军',PLANMC:'海军陆战队',
  INS:'叛军',MEA:'中东军',TLF:'土耳其',WPMC:'瓦格纳',GFI:'法军',
  MEI:'中东叛军',CRF:'加叛军',IMF:'非正规武装',MIL:'民兵',AFU:'乌克兰'
};

// 编制中文名 fallback
var _ROLE_CN_FALLBACK = {
  CombinedArms:'合成营',Armored:'装甲',Mechanized:'机械化',Motorized:'摩托化',
  LightInfantry:'轻步兵',AirAssault:'空降/空突',AmphibiousAssault:'两栖机械化',
  Support:'支援'
};

function factionCn(code) {
  if (typeof ctx.FACTION_CN !== 'undefined' && ctx.FACTION_CN[code]) return ctx.FACTION_CN[code];
  return _FACTION_CN_FALLBACK[code] || code;
}

function roleCn(code) {
  if (typeof ctx.ROLE_CN !== 'undefined' && ctx.ROLE_CN[code]) return ctx.ROLE_CN[code];
  return _ROLE_CN_FALLBACK[code] || code;
}

// 从 factions.json 查找阵营的全部编制
function getFactionRoles(factionCode) {
  if (!_factionsData) return [];
  var f = _factionsData.find(function(x) { return x.code === factionCode; });
  return f ? (f.roles || []) : [];
}

function buildLayerIndex(data) {
  _layerData = data;
  _idx = { mapToModes: {}, mapModeToVers: {}, mapModeVerToLayer: {}, allMaps: [] };
  var mapSet = {};
  data.forEach(function(l) {
    if (!mapSet[l.map]) { mapSet[l.map] = true; _idx.allMaps.push(l.map); }
    if (!_idx.mapToModes[l.map]) _idx.mapToModes[l.map] = [];
    if (_idx.mapToModes[l.map].indexOf(l.mode) === -1) _idx.mapToModes[l.map].push(l.mode);
    var mk = l.map + '|' + l.mode;
    if (!_idx.mapModeToVers[mk]) _idx.mapModeToVers[mk] = [];
    if (_idx.mapModeToVers[mk].indexOf(l.ver) === -1) _idx.mapModeToVers[mk].push(l.ver);
    var mvk = l.map + '|' + l.mode + '|' + l.ver;
    _idx.mapModeVerToLayer[mvk] = l;
  });
  _idx.allMaps.sort();
  for (var k in _idx.mapToModes) _idx.mapToModes[k].sort();
  for (var k2 in _idx.mapModeToVers) _idx.mapModeToVers[k2].sort();
}

function getFilterValues() {
  var mapEl = document.getElementById('qc-map-sel');
  var modeEl = document.getElementById('qc-mode-sel');
  var verEl = document.getElementById('qc-ver-sel');
  return { map: mapEl ? mapEl.value : '', mode: modeEl ? modeEl.value : '', ver: verEl ? verEl.value : '' };
}

function getRoleLabels(mode) {
  if (mode === 'Invasion') return { t1: '防守方', t2: '进攻方' };
  if (mode === 'Insurgency') return { t1: '进攻方', t2: '防守方' };
  return { t1: 'Team 1', t2: 'Team 2' };
}

// ═══ 渲染下拉框 ═══

function renderMapDropdown() {
  var maps = _idx.allMaps || [];
  var h = '<div style="margin-bottom:12px"><label style="font-size:12px;color:var(--text2);display:block;margin-bottom:4px">地图</label>';
  h += '<select id="qc-map-sel" style="width:100%;padding:6px 8px" onchange="window._onMapChange()">';
  h += '<option value="">请选择地图</option>';
  maps.forEach(function(m) { h += '<option value="' + ctx.esc(m) + '">' + ctx.esc(m) + '</option>'; });
  h += '</select></div>';
  return h;
}

function renderModeDropdown(mapName) {
  var modes = mapName ? (_idx.mapToModes[mapName] || []) : [];
  var h = '<div style="margin-bottom:12px"><label style="font-size:12px;color:var(--text2);display:block;margin-bottom:4px">模式</label>';
  h += '<select id="qc-mode-sel" style="width:100%;padding:6px 8px" onchange="window._onModeChange()">';
  h += '<option value="">请选择模式</option>';
  modes.forEach(function(m) {
    var cn = (typeof ctx.MODE_CN !== 'undefined' && ctx.MODE_CN[m]) ? ctx.MODE_CN[m] : m;
    h += '<option value="' + ctx.esc(m) + '">' + ctx.esc(cn) + ' (' + ctx.esc(m) + ')</option>';
  });
  h += '</select></div>';
  return h;
}

function renderVersionDropdown(mapName, mode) {
  var vers = (mapName && mode) ? (_idx.mapModeToVers[mapName + '|' + mode] || []) : [];
  var h = '<div style="margin-bottom:12px"><label style="font-size:12px;color:var(--text2);display:block;margin-bottom:4px">版本</label>';
  h += '<select id="qc-ver-sel" style="width:100%;padding:6px 8px" onchange="window._onVersionChange()">';
  h += '<option value="">请选择版本</option>';
  vers.forEach(function(v) { h += '<option value="' + ctx.esc(v) + '">' + ctx.esc(v) + '</option>'; });
  h += '</select></div>';
  return h;
}

// ═══ 阵营选择器（从 factions.json 读取全部阵营）═══

function renderFactionSelectors(layer) {
  if (!layer) return '<div style="color:var(--text3);padding:8px 0">请先选择版本</div>';
  var vals = getFilterValues();
  var roleLabels = getRoleLabels(vals.mode);
  // 从 factions.json 获取所有阵营
  var allFactions = _factionsData || [];

  function factionOpts(defaultCode) {
    var o = '';
    allFactions.forEach(function(f) {
      o += '<option value="' + ctx.esc(f.code) + '"' + (f.code === defaultCode ? ' selected' : '') + '>' + ctx.esc(f.name) + ' (' + ctx.esc(f.code) + ')</option>';
    });
    return o;
  }

  var h = '<div style="margin-bottom:12px">';
  h += '<div style="display:flex;gap:12px;align-items:flex-end">';
  h += '<div style="flex:1"><label style="font-size:12px;color:var(--text2);display:block;margin-bottom:4px">' + ctx.esc(roleLabels.t1) + ' 阵营</label>';
  h += '<select id="qc-faction1-sel" style="width:100%;padding:6px 8px" onchange="window._onFactionChange()">' + factionOpts(layer.t1) + '</select></div>';
  h += '<div style="display:flex;align-items:center;padding-bottom:8px;font-size:13px;color:var(--text2);font-weight:600">VS</div>';
  h += '<div style="flex:1"><label style="font-size:12px;color:var(--text2);display:block;margin-bottom:4px">' + ctx.esc(roleLabels.t2) + ' 阵营</label>';
  h += '<select id="qc-faction2-sel" style="width:100%;padding:6px 8px" onchange="window._onFactionChange()">' + factionOpts(layer.t2) + '</select></div>';
  h += '</div>';
  h += '<div id="qc-role-row">' + renderRoleSelectors(layer) + '</div>';
  h += '</div>';
  return h;
}

// ═══ 编制选择器（从 factions.json 检索该阵营所有编制）═══

function renderRoleSelectors(layer) {
  var f1El = document.getElementById('qc-faction1-sel');
  var f2El = document.getElementById('qc-faction2-sel');
  var f1 = f1El ? f1El.value : (layer ? layer.t1 : '');
  var f2 = f2El ? f2El.value : (layer ? layer.t2 : '');

  // 从 factions.json 查找每个阵营的全部编制
  var r1List = getFactionRoles(f1);
  var r2List = getFactionRoles(f2);

  if (!r1List.length && !r2List.length) return '';

  // 默认编制：图层自带的 > 阵营第一个编制
  var defaultR1 = (layer && layer.t1Role) ? layer.t1Role : (r1List.length ? r1List[0].code : '');
  var defaultR2 = (layer && layer.t2Role) ? layer.t2Role : (r2List.length ? r2List[0].code : '');

  var vals = getFilterValues();
  var roleLabels = getRoleLabels(vals.mode);

  function roleOpts(list, defaultCode) {
    var o = '';
    list.forEach(function(r) {
      o += '<option value="' + ctx.esc(r.code) + '"' + (r.code === defaultCode ? ' selected' : '') + '>' + ctx.esc(r.name || roleCn(r.code)) + '</option>';
    });
    return o;
  }

  var h = '<div style="display:flex;gap:12px;align-items:flex-end;margin-top:8px">';
  if (r1List.length) {
    h += '<div style="flex:1"><label style="font-size:12px;color:var(--text2);display:block;margin-bottom:4px">' + ctx.esc(roleLabels.t1) + ' 编制</label>';
    h += '<select id="qc-role1-sel" style="width:100%;padding:6px 8px">' + roleOpts(r1List, defaultR1) + '</select></div>';
  } else { h += '<div style="flex:1"></div>'; }
  h += '<div style="width:40px"></div>';
  if (r2List.length) {
    h += '<div style="flex:1"><label style="font-size:12px;color:var(--text2);display:block;margin-bottom:4px">' + ctx.esc(roleLabels.t2) + ' 编制</label>';
    h += '<select id="qc-role2-sel" style="width:100%;padding:6px 8px">' + roleOpts(r2List, defaultR2) + '</select></div>';
  } else { h += '<div style="flex:1"></div>'; }
  h += '</div>';
  return h;
}

// ═══ 命令预览 ═══

function buildCommandPreview() {
  var vals = getFilterValues();
  var layer = _idx.mapModeVerToLayer[vals.map + '|' + vals.mode + '|' + vals.ver];
  if (!layer) return '';
  var f1 = document.getElementById('qc-faction1-sel') ? document.getElementById('qc-faction1-sel').value : '';
  var f2 = document.getElementById('qc-faction2-sel') ? document.getElementById('qc-faction2-sel').value : '';
  var r1 = document.getElementById('qc-role1-sel') ? document.getElementById('qc-role1-sel').value : '';
  var r2 = document.getElementById('qc-role2-sel') ? document.getElementById('qc-role2-sel').value : '';

  // 如果没选编制，使用图层默认
  if (!r1 && f1 === layer.t1 && layer.t1Role) r1 = layer.t1Role;
  if (!r2 && f2 === layer.t2 && layer.t2Role) r2 = layer.t2Role;

  var cmdPrefix = (_mapModalState.actionName === 'qcDoChangeMap') ? 'AdminChangeLayer' : 'AdminSetNextLayer';
  var fullCmd = cmdPrefix + ' ' + layer.name;
  // 阵营和编制用空格分隔，不能拼进图层名
  if (f1 && f2) {
    fullCmd += ' ' + f1 + (r1 ? ' ' + r1 : '') + ' ' + f2 + (r2 ? ' ' + r2 : '');
  }

  // 显示中文信息
  var f1Name = factionCn(f1);
  var f2Name = factionCn(f2);
  var r1Name = r1 ? roleCn(r1) : '';
  var r2Name = r2 ? roleCn(r2) : '';

  var h = '<div style="border-top:1px solid var(--border);padding-top:12px;margin-top:4px">';
  h += '<div style="font-size:11px;color:var(--text3);margin-bottom:4px">命令预览</div>';
  h += '<code style="display:block;font-size:12px;padding:8px;background:var(--bg2);border-radius:4px;word-break:break-all;color:var(--accent);margin-bottom:8px">' + ctx.esc(fullCmd) + '</code>';
  if (f1 && f2) {
    h += '<div style="font-size:12px;color:var(--text2);margin-bottom:12px">';
    h += ctx.esc(f1Name) + (r1Name ? ' · ' + ctx.esc(r1Name) : '') + ' <b>VS</b> ' + ctx.esc(f2Name) + (r2Name ? ' · ' + ctx.esc(r2Name) : '');
    h += '</div>';
  }
  var btnText = (_mapModalState.actionName === 'qcDoChangeMap') ? '⚡ 立即切换' : '⏳ 预设下张';
  h += '<button class="btn btn-primary" style="width:100%" data-action="executeMapCommand" data-cmd="' + ctx.escAttr(fullCmd) + '" data-layer="' + ctx.escAttr(layer.name) + '">' + btnText + '</button>';
  h += '</div>';
  return h;
}

function renderCommandArea() {
  var vals = getFilterValues();
  if (!vals.map || !vals.mode || !vals.ver) {
    return '<div style="color:var(--text3);padding:24px;text-align:center">请完成上方选择</div>';
  }
  var preview = buildCommandPreview();
  if (!preview) return '<div style="color:var(--text3);padding:24px;text-align:center">未找到匹配的图层</div>';
  return preview;
}

// ═══ 级联事件 ═══

window._onMapChange = function() {
  var vals = getFilterValues();
  var mc = document.getElementById('qc-mode-container'); if (mc) mc.innerHTML = renderModeDropdown(vals.map);
  var vc = document.getElementById('qc-ver-container'); if (vc) vc.innerHTML = renderVersionDropdown(vals.map, null);
  var fc = document.getElementById('qc-faction-container'); if (fc) fc.innerHTML = renderFactionSelectors(null);
  var ca = document.getElementById('qc-layer-area'); if (ca) ca.innerHTML = renderCommandArea();
};

window._onModeChange = function() {
  var vals = getFilterValues();
  var vc = document.getElementById('qc-ver-container'); if (vc) vc.innerHTML = renderVersionDropdown(vals.map, vals.mode);
  var fc = document.getElementById('qc-faction-container'); if (fc) fc.innerHTML = renderFactionSelectors(null);
  var ca = document.getElementById('qc-layer-area'); if (ca) ca.innerHTML = renderCommandArea();
};

window._onVersionChange = function() {
  var vals = getFilterValues();
  var layer = _idx.mapModeVerToLayer[vals.map + '|' + vals.mode + '|' + vals.ver] || null;
  var fc = document.getElementById('qc-faction-container'); if (fc) fc.innerHTML = renderFactionSelectors(layer);
  var ca = document.getElementById('qc-layer-area'); if (ca) ca.innerHTML = renderCommandArea();
};

window._onFactionChange = function() {
  var vals = getFilterValues();
  var layer = _idx.mapModeVerToLayer[vals.map + '|' + vals.mode + '|' + vals.ver] || null;
  var rr = document.getElementById('qc-role-row'); if (rr) rr.innerHTML = renderRoleSelectors(layer);
  var ca = document.getElementById('qc-layer-area'); if (ca) ca.innerHTML = buildCommandPreview();
};

// ═══ 命令执行 ═══

export function executeMapCommand(el) {
  var cmd = el.getAttribute('data-cmd');
  var layerName = el.getAttribute('data-layer');
  if (!cmd || !layerName) return;
  if (_mapModalState.actionName === 'qcDoChangeMap') doChangeMapWithCmd(layerName, cmd);
  else doQuickMapWithCmd(layerName, cmd);
}

export function serverLayerPick(layerName, el) {
  var f1 = el ? el.getAttribute('data-f1') : '';
  var f2 = el ? el.getAttribute('data-f2') : '';
  var cmd = (_mapModalState.actionName === 'qcDoChangeMap' ? 'AdminChangeLayer ' : 'AdminSetNextLayer ') + layerName;
  if (f1 && f2) cmd += ' ' + f1 + ' ' + f2;
  if (_mapModalState.actionName === 'qcDoChangeMap') doChangeMapWithCmd(layerName, cmd);
  else doQuickMapWithCmd(layerName, cmd);
}

// ═══ 弹窗 ═══

async function openMapModal(title, desc, actionName) {
  var sid = qcGetServer(); if (!sid) return;
  _mapModalState.actionName = actionName;
  var container = document.getElementById('modal-container');
  if (!container) { container = document.createElement('div'); container.id = 'modal-container'; document.body.appendChild(container); }
  var html = '<div class="modal-overlay" data-action="closeModal">';
  html += '<div class="modal" style="min-width:640px;max-width:860px">';
  html += '<h3>' + title + '</h3>';
  html += '<p style="font-size:12px;color:var(--text2);margin:-4px 0 12px">' + desc + '</p>';
  html += '<div id="qc-loading" style="text-align:center;padding:40px;color:var(--text2)">正在加载数据...</div>';
  html += '<div id="qc-modal-body" style="display:none">';
  html += '<div id="qc-map-container"></div>';
  html += '<div id="qc-mode-container"></div>';
  html += '<div id="qc-ver-container"></div>';
  html += '<div id="qc-faction-container"></div>';
  html += '<div id="qc-layer-area"></div>';
  html += '</div>';
  html += '<div class="modal-actions"><button class="btn" data-action="closeModal">取消</button></div>';
  html += '</div></div>';
  container.innerHTML = html;
  try {
    // 并行加载 layers.json 和 factions.json
    var ts = Date.now();
    if (!_layerData || !_factionsData) {
      var results = await Promise.all([
        _layerData ? Promise.resolve(_layerData) : fetch('/layers.json?v=' + ts).then(function(r) { return r.json(); }),
        _factionsData ? Promise.resolve(_factionsData) : fetch('/factions.json?v=' + ts).then(function(r) { return r.json(); })
      ]);
      if (!_layerData) buildLayerIndex(results[0]);
      if (!_factionsData) _factionsData = results[1];
    }
    var loadingEl = document.getElementById('qc-loading'); if (loadingEl) loadingEl.style.display = 'none';
    var modalBody = document.getElementById('qc-modal-body'); if (modalBody) modalBody.style.display = 'block';
    var mapContainer = document.getElementById('qc-map-container'); if (mapContainer) mapContainer.innerHTML = renderMapDropdown();
  } catch(e) {
    var loadingEl2 = document.getElementById('qc-loading');
    if (loadingEl2) loadingEl2.textContent = '加载失败: ' + e.message;
  }
}

export function qcChangeMap() {
  openMapModal('更换地图', '选择地图 → 模式 → 版本 → 阵营 → 编制，点击按钮立即切换', 'qcDoChangeMap');
}

export function qcShowMapList() {
  openMapModal('预设地图', '选择地图 → 模式 → 版本 → 阵营 → 编制，点击按钮预设下张地图', 'qcQuickMap');
}

export function qcPickMap() { qcShowMapList(); }
export function qcDoChangeMap(layerName) { doChangeMapWithCmd(layerName, 'AdminChangeLayer ' + layerName); }
export function qcQuickMap(layerName) { doQuickMapWithCmd(layerName, 'AdminSetNextLayer ' + layerName); }

async function doChangeMapWithCmd(layerName, cmd) {
  var sid = qcGetServer(); if (!sid) return;
  document.getElementById("modal-container").innerHTML = '';
  try {
    ctx.toast('正在结束当前对局...');
    await ctx.api('/rcon/send', { method: 'POST', body: { serverId: sid, command: 'AdminEndMatch' } });
    await new Promise(function(resolve) { setTimeout(resolve, 1000); });
  } catch(e) { console.warn('AdminEndMatch failed:', e); }
  ctx.toast('正在切换地图...');
  try {
    var r = await ctx.api('/rcon/send', { method: 'POST', body: { serverId: sid, command: cmd } });
    var msg = r.result && r.result.trim() ? r.result.trim() : '';
    if (/error|not found|unable|failed/i.test(msg)) {
      ctx.toast('切换失败：' + msg.substring(0, 120), 'error');
      return;
    }
    await new Promise(function(resolve) { setTimeout(resolve, 1500); });
    try {
      var verify = await ctx.api('/rcon/send', { method: 'POST', body: { serverId: sid, command: 'ShowCurrentMap' } });
      var curLayer = (verify.result || '').trim();
      if (curLayer && curLayer.toLowerCase().indexOf(layerName.toLowerCase()) === -1) {
        ctx.toast('切换可能未生效（当前: ' + curLayer.substring(0, 60) + '）', 'error');
        return;
      }
    } catch(ve) {}
    ctx.toast('已切换到 ' + layerName);
  } catch(e) { ctx.toast('切换失败：' + (e.message || '服务器连接异常'), 'error'); }
}

async function doQuickMapWithCmd(layerName, cmd) {
  var sid = qcGetServer(); if (!sid) return;
  document.getElementById("modal-container").innerHTML = '';
  try {
    var r = await ctx.api('/rcon/send', { method: 'POST', body: { serverId: sid, command: cmd } });
    var msg = r.result && r.result.trim() ? r.result.trim() : '';
    if (/error|not found|unable|failed/i.test(msg)) {
      ctx.toast('预设失败：' + msg.substring(0, 120), 'error');
      return;
    }
    await new Promise(function(resolve) { setTimeout(resolve, 1000); });
    try {
      var verify = await ctx.api('/rcon/send', { method: 'POST', body: { serverId: sid, command: 'ShowNextMap' } });
      var nextLayer = (verify.result || '').trim();
      if (nextLayer && nextLayer.toLowerCase().indexOf(layerName.toLowerCase()) === -1) {
        ctx.toast('预设可能未生效（下张: ' + nextLayer.substring(0, 60) + '）', 'error');
        return;
      }
    } catch(ve) {}
    ctx.toast('已预设 ' + layerName + '（下把生效）');
  } catch(e) { ctx.toast('预设失败：' + (e.message || '服务器连接异常'), 'error'); }
}
