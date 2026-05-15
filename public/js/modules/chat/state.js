// modules/chat/state.js — 聊天模块私有状态

export let allChats = [];
export function setAllChats(v) { allChats = v; }

export let chatFilter = "";
export function setChatFilter(v) { chatFilter = v; }
