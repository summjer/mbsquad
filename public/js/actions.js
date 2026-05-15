/**
 * actions.js — 全局动作注册表 + 事件委托
 *
 * 解决 <script type="module"> 下 inline onclick 无法访问模块作用域的问题。
 * 所有可调用函数通过 register() 注册，HTML 中用 data-action 替代 onclick。
 *
 * 用法：
 *   HTML:  <button data-action="funcName">点击</button>
 *          <button data-action="funcName" data-param="hello">点击</button>
 *          <button data-action="funcName" data-params='["a","b"]'>点击</button>
 *          <button data-action="funcName" data-confirm="确定？">点击</button>
 *          <button data-copy="要复制的文本">复制</button>
 *   JS:    register("funcName", myFunc);
 *          registerMany({ funcA, funcB });
 */

var _actions = {};

export function register(name, fn) {
  _actions[name] = fn;
}

export function registerMany(map) {
  for (var k in map) {
    if (map.hasOwnProperty(k)) _actions[k] = map[k];
  }
}

// modal-overlay 用 mousedown 关闭（防止拖拽选文字时松开在 overlay 上误关弹窗）
document.addEventListener("mousedown", function(e) {
  var el = e.target.closest("[data-action]");
  if (!el || !el.classList.contains("modal-overlay")) return;
  if (e.target !== el) return;
  var action = el.getAttribute("data-action");
  var fn = _actions[action];
  if (fn) fn(el);
});

// 全局事件委托：一处监听，全局生效
document.addEventListener("click", function(e) {
  var el = e.target.closest("[data-action]");
  if (!el) return;

  // modal-overlay 的 closeModal 由 mousedown 处理，click 跳过
  if (el.classList.contains("modal-overlay")) return;

  // data-confirm：弹窗确认，取消则不执行
  var confirmMsg = el.getAttribute("data-confirm");
  if (confirmMsg !== null && !confirm(confirmMsg)) return;

  // data-copy：复制文本到剪贴板
  var copyText = el.getAttribute("data-copy");
  if (copyText !== null) {
    navigator.clipboard.writeText(copyText).then(function() {
      if (typeof window.toast === "function") window.toast("已复制");
    }).catch(function() {
      var ta = document.createElement("textarea");
      ta.value = copyText;
      document.body.appendChild(ta);
      ta.select();
      document.execCommand("copy");
      ta.remove();
      if (typeof window.toast === "function") window.toast("已复制");
    });
    return;
  }

  var action = el.getAttribute("data-action");
  var fn = _actions[action];
  if (!fn) {
    console.warn("[Actions] 未注册的动作: " + action);
    return;
  }

  // 优先 data-params（JSON 数组），其次 data-param（单字符串）
  var params = el.getAttribute("data-params");
  var param = el.getAttribute("data-param");

  if (params !== null) {
    try { params = JSON.parse(params); } catch(err) { params = [params]; }
    fn.apply(null, (Array.isArray(params) ? params : [params]).concat([el]));
  } else if (param !== null) {
    fn(param, el);
  } else {
    fn(el);
  }
});

// select 元素的 change 事件委托
document.addEventListener("change", function(e) {
  var el = e.target;
  if (el.tagName !== "SELECT" || !el.hasAttribute("data-action")) return;

  var action = el.getAttribute("data-action");
  var fn = _actions[action];
  if (!fn) {
    console.warn("[Actions] 未注册的动作: " + action);
    return;
  }

  // 优先 data-params（JSON 数组），其次 data-param（单字符串），最后只传 el
  var params = el.getAttribute("data-params");
  var param = el.getAttribute("data-param");

  if (params !== null) {
    try { params = JSON.parse(params); } catch(err) { params = [params]; }
    fn.apply(null, (Array.isArray(params) ? params : [params]).concat([el]));
  } else if (param !== null) {
    fn(param, el);
  } else {
    fn(el);
  }
});

// 调试用
export function listActions() {
  return Object.keys(_actions);
}

// 兼容旧调用：A() 函数保留
window.A = function(name, paramsStr) {
  var fn = _actions[name];
  if (!fn) { console.warn("[Actions] 未注册: " + name); return; }
  if (paramsStr === undefined || paramsStr === "") { fn(); return; }
  try {
    var params = JSON.parse(paramsStr);
    if (Array.isArray(params)) { fn.apply(null, params); }
    else { fn(params); }
  } catch(e) {
    fn(paramsStr);
  }
};

// 安全生成 onclick 字符串（兼容旧代码）
window.A.html = function(name /*, arg1, arg2, ... */) {
  var args = [];
  for (var i = 1; i < arguments.length; i++) args.push(arguments[i]);
  return "A('" + name + "'," + JSON.stringify(args) + ")";
};
