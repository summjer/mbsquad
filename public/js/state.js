// ─── state.js v2.0 — 全局状态集中管理 ───

export const API = window.location.origin + '/api';

// ─── 从 generated-constants.js 导入常量 ───
import { MAP_CN as _MAP_CN, MODE_CN as _MODE_CN, ROLE_CN as _ROLE_CN, FACTION_CN as _FACTION_CN, FACTIONS as _FACTIONS, MAP_FACTIONS as _MAP_FACTIONS } from './generated-constants.js?v=1775700005';
export const MAP_CN = _MAP_CN;
export const MODE_CN = _MODE_CN;
export const ROLE_CN = _ROLE_CN;
export const FACTION_CN = _FACTION_CN;
export const FACTIONS = _FACTIONS;
export const MAP_FACTIONS = _MAP_FACTIONS;

// Auth state
export let token = localStorage.getItem('squad_token');
export let currentUser = null;

// Server state
export let servers = [];

// Page routing
export let currentPage = 'servers';
export let serversSubTab = 'list';
export let pluginSubTab = 'installed';
export let pointsSubTab = 'ranking';
export let tkForgiveSubTab = 'active';
export let bansSubTab = 'bans';

// Player state
export let currentPlayers = [];
export let currentSquads = [];
export let currentTeamNames = {};
export let currentPlayerServerId = 0;
export let selectedServerId = 0;

// Chat state
export let allLayers = [];

// Permissions state
export let permissions = {};

// Auto-refresh
export let autoRefreshTimers = {};
export let defaultRefreshSeconds = 60;
export let refreshIntervals = { players: 5, list: 60 };  // players: 5s, server list: 60s

// ─── Setters ───
export function setToken(v) { token = v; }
export function setCurrentUser(v) { currentUser = v; }
export function setServers(v) { servers = v; }
export function setCurrentPage(v) { currentPage = v; }
export function setServersSubTab(v) { serversSubTab = v; }
export function setPluginSubTab(v) { pluginSubTab = v; }
export function setPointsSubTab(v) { pointsSubTab = v; }
export function setTkForgiveSubTab(v) { tkForgiveSubTab = v; }
export function setBansSubTab(v) { bansSubTab = v; }
export function setCurrentPlayers(v) { currentPlayers = v; }
export function setCurrentSquads(v) { currentSquads = v; }
export function setCurrentTeamNames(v) { currentTeamNames = v; }
export function setCurrentPlayerServerId(v) { currentPlayerServerId = v; }
export function setSelectedServerId(v) { selectedServerId = v; window._selectedServerId = v; }
export function setAllLayers(v) { allLayers = v; }
export function setPermissions(v) { permissions = v; }
export function setAutoRefreshTimers(v) { autoRefreshTimers = v; }
export function setRefreshIntervals(v) { refreshIntervals = v; }

// ─── Constants ───

export const ROLE_LABELS = { admin: '管理员', server_owner: '服主', op: 'OP' };


// 地图列表（从 layers.json 提取）
export const mapList = [
  'Al Basrah','Anvil','Belaya Pass','Black Coast','Chora','Fallujah',"Fool's Road",
  'Goose Bay','Gorodok','Harju','Kamdesh Highlands','Kohat Toi','Kokan',
  'Lashkar Valley','Logar Valley','Manicouagan','Mestia','Mutaha','Narva',
  'Pacific Proving Grounds','Sanxian Islands','Skorpo','Sumari Bala','Tallil Outskirts',
  'Yehorivka','Operation First Light'
];

// 阵营信息已移至 generated-constants.js

// 编制英文代码 → 用于 RCON 命令
export const ROLE_CMD = {
  'CombinedArms': 'CombinedArms',
  'AirAssault': 'AirAssault',
  'LightInfantry': 'LightInfantry',
  'Armored': 'Armored',
  'Mechanized': 'Mechanized',
  'Motorized': 'Motorized',
  'Support': 'Support',
  'AmphibiousAssault': 'AmphibiousAssault'
};

// 中文翻译常量已移至 generated-constants.js

export function tr(cnMap, key) { return cnMap[key] || key; }


export const roleColors = { admin: 'var(--red)', server_owner: 'var(--accent)', op: 'var(--green)' };
