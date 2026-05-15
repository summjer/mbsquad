let ctx;
export function init(_ctx) { ctx = _ctx; }

export async function renderKills(el) {
  var d = await ctx.api("/kills");
  var k = d.kills || [];
  el.innerHTML = ctx.renderRefreshBar("kills") +
    (k.length
      ? "<table><tr><th>击杀者</th><th>受害者</th><th>武器</th><th>时间</th></tr>" +
        k.map(function(x) {
          return "<tr><td>" + ctx.esc(x.killer) + "</td><td>" + ctx.esc(x.victim) + "</td><td>" + ctx.esc(x.weapon) + "</td><td>" + x.timestamp + "</td></tr>";
        }).join("") + "</table>"
      : "<div class=\"empty\">暂无击杀记录</div>");
}

export const manifest = { id: "kills", label: "击杀记录", icon: "⚔️", section: "data", order: 1 };
export const pages = { "kills": renderKills };
export const actions = {};
