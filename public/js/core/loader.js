/**
 * core/loader.js — 前端模块加载器 (v3, Object.assign ctx)
 *
 * 职责：
 *   1. 调用 GET /api/modules 获取模块列表
 *   2. 动态 import 每个模块，调用 init(ctx) 注入上下文
 *   3. 自动注册 pages（路由）和 actions（事件委托）
 *   4. 提供 renderPage / getLoadedModules 给 app.js 使用
 *
 * 模块合约：每个 modules/xxx/index.js 必须导出：
 *   export const manifest = { id, label, access? };
 *   export const pages = { pageId: renderFn };
 *   export const actions = { actionName: fn };
 *   export function init(ctx) { ... }  // 可选：注入上下文
 */

import { registerMany } from '../actions.js?v=1775700005';
import * as _state from '../state.js?v=1775700005';
import * as _utils from '../utils.js?v=1775700005';

// 页面渲染函数注册表
const pageRenderers = {};

// 已加载模块注册表
const loadedModules = {};

// 已注册的 action 名（用于去重，避免覆盖 app.js 的核心 actions）
const registeredActions = new Set();

/**
 * 构建模块上下文 — 直接合并 state + utils 的导出
 * 不用 Proxy，避免模块命名空间对象兼容性问题
 */
function buildModuleCtx() {
  const ctx = Object.assign({}, _utils);
  // Ensure apiDedup is available
  if (!ctx.apiDedup && _utils.apiDedup) ctx.apiDedup = _utils.apiDedup;
  // 不复制 _state 的值，全部用 getter 实现 live binding
  const stateKeys = [
    'token','currentUser','servers','currentPage',
    'serversSubTab','pluginSubTab','pointsSubTab','tkForgiveSubTab','bansSubTab',
    'currentPlayers','currentSquads','currentTeamNames',
    'currentPlayerServerId','selectedServerId',
    'allChats','chatFilter','allLayers',
    'autoRefreshTimers','refreshIntervals','defaultRefreshSeconds',
    'permissions',
    'setToken','setCurrentUser','setServers','setCurrentPage',
    'setServersSubTab','setPluginSubTab','setPointsSubTab','setTkForgiveSubTab','setBansSubTab',
    'setCurrentPlayers','setCurrentSquads','setCurrentTeamNames',
    'setCurrentPlayerServerId','setSelectedServerId',
    'setAllLayers','setPermissions','setAutoRefreshTimers','setRefreshIntervals',
    // 常量
    'API','MAP_CN','MODE_CN','ROLE_CN','FACTION_CN','FACTIONS','MAP_FACTIONS','ROLE_LABELS','allLayers',
  ];
  for (const key of stateKeys) {
    if (key in _state) {
      Object.defineProperty(ctx, key, {
        get() { return _state[key]; },
        configurable: true, enumerable: true,
      });
    }
  }
  return ctx;
}

/**
 * 把 state.js 和 utils.js 的导出挂到 window 上
 * 这样模块代码可以裸引用这些符号（兼容旧的非模块化写法）
 */
function exposeGlobals() {
  // state.js 所有导出
  for (const [key, val] of Object.entries(_state)) {
    if (!(key in window)) window[key] = val;
  }
  // utils.js 所有导出
  for (const [key, val] of Object.entries(_utils)) {
    if (!(key in window)) window[key] = val;
  }
  // state.js 的 mutable 变量需要保持 live binding
  // 用 defineProperty + getter 代理到 _state 上
  const liveStateVars = [
    'token','currentUser','servers','currentPage',
    'serversSubTab','pluginSubTab','pointsSubTab','tkForgiveSubTab','bansSubTab',
    'currentPlayers','currentSquads','currentTeamNames',
    'currentPlayerServerId','selectedServerId',
    'allChats','chatFilter','allLayers',
    'autoRefreshTimers','refreshIntervals','defaultRefreshSeconds',
    'permissions',
  ];
  const setterMap = {
    token: 'setToken', currentUser: 'setCurrentUser', servers: 'setServers',
    currentPage: 'setCurrentPage', serversSubTab: 'setServersSubTab',
    pluginSubTab: 'setPluginSubTab', pointsSubTab: 'setPointsSubTab',
    tkForgiveSubTab: 'setTkForgiveSubTab',
    bansSubTab: 'setBansSubTab',
    currentPlayers: 'setCurrentPlayers', currentSquads: 'setCurrentSquads',
    currentTeamNames: 'setCurrentTeamNames',
    currentPlayerServerId: 'setCurrentPlayerServerId',
    selectedServerId: 'setSelectedServerId',
    allLayers: 'setAllLayers',
    permissions: 'setPermissions',
    autoRefreshTimers: 'setAutoRefreshTimers', refreshIntervals: 'setRefreshIntervals',
  };
  for (const v of liveStateVars) {
    try {
      const setterName = setterMap[v];
      Object.defineProperty(window, v, {
        get() { return _state[v]; },
        set(val) {
          if (setterName && typeof _state[setterName] === 'function') {
            _state[setterName](val);
          }
        },
        configurable: true,
        enumerable: true,
      });
    } catch(e) {}
  }
}


/**
 * 加载所有模块
 * @param {Object} opts - { api } 基础选项
 */
export async function loadAllModules(opts) {
  exposeGlobals();
  const ctx = buildModuleCtx();
  let moduleNames;
  try {
    const res = await opts.api('/modules');
    moduleNames = res.modules || [];
  } catch (e) {
    console.error('[Loader] 无法获取模块列表:', e);
    return;
  }

  if (!moduleNames.length) {
    console.log('[Loader] 没有发现模块（modules/ 目录为空或不存在）');
    return;
  }

  // 带缓存破坏的动态 import
  const ts = 1775700005;  // must match cache buster in utils.js/app.js

  for (const name of moduleNames) {
    try {
      const mod = await import(`../modules/${name}/index.js?v=${ts}`);

      // 验证模块合约
      if (!mod.manifest || !mod.manifest.id) {
        console.warn(`[Loader] 模块 ${name} 缺少 manifest.id，跳过`);
        continue;
      }

      const m = mod.manifest;

      // 权限检查
      if (m.access && _state.currentUser) {
        const role = _state.currentUser.role;
        if (!m.access.includes(role)) {
          console.log(`[Loader] 跳过 ${name}（权限不足: ${role}）`);
          continue;
        }
      }
      // 新的 permissions 字段检查
      if (m.permissions && m.permissions.length > 0 && _state.currentUser) {
        const role = _state.currentUser.role;
        if (role !== 'server_owner') {
          const userPerms = _state.permissions || {};
          const hasAny = m.permissions.some(p => !!userPerms[p]);
          if (!hasAny) {
            console.log(`[Loader] 跳过 ${name}（缺少权限: ${m.permissions.join(',')}）`);
            continue;
          }
        }
      }

      // 注入 ctx（如果模块导出了 init 函数）
      if (typeof mod.init === 'function') {
        mod.init(ctx);
      }

      // 注册页面
      if (mod.pages) {
        for (const [pageId, renderFn] of Object.entries(mod.pages)) {
          if (typeof renderFn === 'function') {
            pageRenderers[pageId] = renderFn;
          }
        }
      }

      // 注册 actions（不覆盖已注册的）
      if (mod.actions) {
        const actionsToRegister = {};
        for (const [actionName, fn] of Object.entries(mod.actions)) {
          if (typeof fn === 'function' && !registeredActions.has(actionName)) {
            actionsToRegister[actionName] = fn;
            registeredActions.add(actionName);
          }
        }
        if (Object.keys(actionsToRegister).length > 0) {
          registerMany(actionsToRegister);
        }
      }

      loadedModules[name] = {
        manifest: m,
        pageIds: Object.keys(mod.pages || {}),
      };

      console.log(`[Loader] OK: ${m.label || m.id} (${name})`);

    } catch (e) {
      console.error('[Loader] 加载模块 ' + name + ' 失败:', e.message, e.stack); document.title = 'LOADER_FAIL:' + name + ':' + e.message;
    }
  }

  console.log('[Loader] 全部加载完成:', Object.keys(loadedModules));
}

/**
 * 渲染页面 — app.js 的 render() 调用这个
 */
export function renderPage(pageId, container) {
  const renderFn = pageRenderers[pageId];
  if (renderFn) {
    renderFn(container);
    return true;
  }
  return false;
}

export function hasPage(pageId) {
  return pageId in pageRenderers;
}

export function getLoadedModules() {
  return Object.values(loadedModules).map(m => ({
    id: m.manifest.id,
    label: m.manifest.label || m.manifest.id,
    access: m.manifest.access, icon: m.manifest.icon, section: m.manifest.section, order: m.manifest.order || 99, subnav: m.manifest.subnav, permissions: m.manifest.permissions,
  }));
}

export function markActionsRegistered(actionNames) {
  for (const name of actionNames) {
    registeredActions.add(name);
  }
}

