// ─── auth.js — 登录/注册/退出 ───

import {
  token, setToken, setCurrentUser, currentUser, setPermissions
} from './state.js?v=1775700005';
import { api, toast, esc } from './utils.js?v=1775700005';

export function showLogin() {
  document.getElementById('app').innerHTML = '<div class="login-page"><div class="login-box">' +
    '<h2>薯条面板</h2><div class="subtitle">Squad 服务器控制面板</div>' +
    '<div class="tabs"><div class="tab active" onclick="A(\'switchTab\',\'login\')">登录</div><div class="tab" onclick="A(\'switchTab\',\'register\')">注册</div></div>' +
    '<div class="login-error" id="login-error"></div>' +
    '<div id="login-form">' +
      '<div class="form-group"><label>用户名</label><input id="l-username" placeholder="用户名" onkeydown="if(event.key===\'Enter\')A(\'doLogin\')"></div>' +
      '<div class="form-group"><label>密码</label><input id="l-password" type="password" placeholder="密码" onkeydown="if(event.key===\'Enter\')A(\'doLogin\')"></div>' +
      '<button class="btn btn-primary" style="width:100%;padding:10px;margin-top:4px" onclick="A(\'doLogin\')">登 录</button>' +
    '</div>' +
    '<div id="register-form" style="display:none">' +
      '<div class="form-group"><label>用户名 (3-32字符)</label><input id="r-username" placeholder="用户名"></div>' +
      '<div class="form-group"><label>密码 (至少6位)</label><input id="r-password" type="password" placeholder="密码"></div>' +
      '<div class="form-group"><label>确认密码</label><input id="r-password2" type="password" placeholder="再次输入密码"></div>' +
      '<div class="form-group"><label>Steam 64位ID <span style="color:var(--red);font-weight:400">*</span></label>' +
        '<input id="r-steamid" placeholder="17位数字，如 76561198xxxxxxxxx">' +
        '<div style="font-size:11px;color:var(--text3);margin-top:4px">必填，17位数字，用于游戏内权限绑定</div>' +
      '</div>' +
      '<button class="btn btn-primary" style="width:100%;padding:10px;margin-top:4px" onclick="A(\'doRegister\')">注 册</button>' +
      '' +
    '</div>' +
  '</div></div>';
  setTimeout(function() {
    var el = document.getElementById('l-username');
    if (el) el.focus();
  }, 100);
}

export function switchTab(tab) {
  document.querySelectorAll('.login-box .tab').forEach(function(t, i) {
    t.className = 'tab' + (i === (tab === 'login' ? 0 : 1) ? ' active' : '');
  });
  document.getElementById('login-form').style.display = tab === 'login' ? '' : 'none';
  document.getElementById('register-form').style.display = tab === 'register' ? '' : 'none';
  document.getElementById('login-error').style.display = 'none';
  document.getElementById('pending-msg').style.display = 'none';
}

function showError(msg) {
  var el = document.getElementById('login-error');
  el.textContent = msg;
  el.style.display = '';
}

export async function doLogin() {
  var username = document.getElementById('l-username').value;
  var password = document.getElementById('l-password').value;
  if (!username || !password) return showError('请输入用户名和密码');
  var r = await api('/auth/login', { method: 'POST', body: { username: username, password: password } });
  if (r.error) return showError(r.error);
  setToken(r.token);
  setCurrentUser(r.user);
  localStorage.setItem('squad_token', r.token);
  import('./app.js?v=1775700005').then(function(m) { m.showApp(); });
}

export async function doRegister() {
  var username = document.getElementById('r-username').value;
  var password = document.getElementById('r-password').value;
  var password2 = document.getElementById('r-password2').value;
  var steamId = document.getElementById('r-steamid') ? document.getElementById('r-steamid').value.trim() : '';
  if (!username || !password) return showError('请填写完整信息');
  if (password !== password2) return showError('两次密码不一致');
  // SteamID 格式校验（选填，但如果填写则必须正确）
  if (!steamId) return showError('请填写 Steam 64位ID（必填）');
  if (!/^76561198\d{9,}$/.test(steamId)) {
    return showError('SteamID 格式错误：需为 76561198 开头的数字');
  }
  var body = { username: username, password: password, steamId: steamId };
  var r = await api('/auth/register', { method: 'POST', body: body });
  if (r.error) return showError(r.error);
  if (r.token) {
    setToken(r.token);
    setCurrentUser(r.user);
    localStorage.setItem('squad_token', r.token);
    import('./app.js?v=1775700005').then(function(m) { m.showApp(); });
  } else {
    document.getElementById('pending-msg').style.display = '';
    document.getElementById('login-error').style.display = 'none';
  }
}

export async function doLogout() {
  await api('/auth/logout', { method: 'POST' });
  setToken(null);
  setCurrentUser(null);
  setPermissions({});
  localStorage.removeItem('squad_token');
  showLogin();
}
