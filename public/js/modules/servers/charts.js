// ─── servers-charts.js — 在线人数趋势图 + Tick Rate 折线图 ───


let ctx; export function init(_ctx) { ctx = _ctx; }

function showEmptyChart(canvas, msg) {
  var wrap = canvas.parentElement;
  if (!wrap) return;
  var old = wrap.querySelector('.chart-empty-hint');
  if (old) old.remove();
  var div = document.createElement('div');
  div.className = 'chart-empty-hint';
  div.style.cssText = 'position:absolute;inset:0;display:flex;align-items:center;justify-content:center;color:#64748b;font-size:14px;pointer-events:none;z-index:2;';
  div.textContent = msg || '暂无数据';
  wrap.style.position = 'relative';
  wrap.appendChild(div);
}
function hideEmptyChart(canvas) {
  var wrap = canvas.parentElement;
  if (!wrap) return;
  var old = wrap.querySelector('.chart-empty-hint');
  if (old) old.remove();
}

var _onlineChart = null;
export var _onlineCanvas = null;

export var _chartRange = '1d';

var _onlineChartRange = null;

export async function chartRangeChange(val, el) {
  if (!el && val && val.value !== undefined) { el = val; }
  if (el && el.value !== undefined) { _chartRange = el.value; }
  refreshOnlineChart();
}

export async function refreshOnlineChart() {
  var canvas = document.getElementById('player-online-chart');
  if (!canvas) { _onlineChart = null; _onlineCanvas = null; return; }
  try {
    var data = await ctx.api('/players/online-chart?range=' + _chartRange);
    if (!data || !data.labels) return;

    if (data.labels.length === 0) {
      if (_onlineChart) { _onlineChart.destroy(); _onlineChart = null; }
      showEmptyChart(canvas, '暂无在线数据（游戏服务器未连接）');
      return;
    }
    hideEmptyChart(canvas);

    // range 未变且图表仍绑定当前 canvas → 原地更新数据，避免闪烁
    if (_onlineChart && _onlineChartRange === _chartRange && _onlineCanvas === canvas) {
      _onlineChart.data.labels = data.labels;
      _onlineChart.data.datasets[0].data = data.joins;
      _onlineChart.data.datasets[1].data = data.disconnects || [];
      _onlineChart.update('none');
      return;
    }

    // canvas 被替换(range 切换/页面重渲染) → 销毁旧实例重建
    if (_onlineChart) { _onlineChart.destroy(); _onlineChart = null; }
    _onlineChartRange = _chartRange;
    _onlineCanvas = canvas;
    _onlineChart = new Chart(canvas, {
      type: 'bar',
      data: {
        labels: data.labels,
        datasets: [{
          label: '进入人数',
          data: data.joins,
          backgroundColor: 'rgba(59,130,246,0.6)',
          borderColor: 'rgba(59,130,246,0.9)',
          borderWidth: 1,
          borderRadius: 4,
          maxBarThickness: 24
        }, {
          label: '退出人数',
          data: data.disconnects || [],
          backgroundColor: 'rgba(239,68,68,0.5)',
          borderColor: 'rgba(239,68,68,0.8)',
          borderWidth: 1,
          borderRadius: 4,
          maxBarThickness: 24
        }]
      },
      options: {
        responsive: true,
        maintainAspectRatio: false,
        plugins: {
          legend: { display: true, labels: { color: '#94a3b8', boxWidth: 12, padding: 12 } },
          tooltip: {
            backgroundColor: '#1e293b',
            titleColor: '#e2e8f0',
            bodyColor: '#e2e8f0',
            cornerRadius: 6,
            padding: 8
          }
        },
        scales: {
          x: {
            grid: { display: false },
            ticks: { color: '#64748b', font: { size: 11 }, maxRotation: 0, autoSkip: true, maxTicksLimit: 12 },
            border: { display: false }
          },
          y: {
            beginAtZero: true,
            grid: { color: 'rgba(255,255,255,0.06)' },
            ticks: { color: '#64748b', font: { size: 11 }, stepSize: 1, precision: 0 },
            border: { display: false }
          }
        }
      }
    });
  } catch (e) {
    console.warn('[Chart] init error:', e);
  }
}


// ─── Tick Rate 折线图 ───
var _tickChart = null;
export var _tickCanvas = null;
export var _tickRange = '1h';

var _tickChartRange = null;

export async function tickRangeChange(val, el) {
  if (!el && val && val.value !== undefined) { el = val; }
  if (el && el.value !== undefined) { _tickRange = el.value; }
  refreshTickChart();
}

export async function refreshTickChart() {
  var canvas = document.getElementById('tick-rate-chart');
  if (!canvas) { _tickChart = null; _tickCanvas = null; return; }
  try {
    var data = await ctx.api('/players/tick-chart?range=' + _tickRange);
    if (!data || !data.labels) return;

    if (data.labels.length === 0) {
      if (_tickChart) { _tickChart.destroy(); _tickChart = null; }
      showEmptyChart(canvas, '暂无 Tick 数据（游戏服务器未连接）');
      return;
    }
    hideEmptyChart(canvas);

    // range 未变且图表仍绑定当前 canvas → 原地更新数据，避免闪烁
    if (_tickChart && _tickChartRange === _tickRange && _tickCanvas === canvas) {
      _tickChart.data.labels = data.labels;
      _tickChart.data.datasets[0].data = data.avg;
      _tickChart.data.datasets[1].data = data.max;
      _tickChart.data.datasets[2].data = data.min;
      _tickChart.update('none');
      return;
    }

    // canvas 被替换(range 切换/页面重渲染) → 销毁旧实例重建
    if (_tickChart) { _tickChart.destroy(); _tickChart = null; }
    _tickChartRange = _tickRange;
    _tickCanvas = canvas;
    _tickChart = new Chart(canvas, {
      type: 'line',
      data: {
        labels: data.labels,
        datasets: [
          {
            label: '平均',
            data: data.avg,
            borderColor: '#22c55e',
            backgroundColor: 'rgba(34,197,94,0.08)',
            borderWidth: 2,
            fill: true,
            tension: 0.3,
            pointRadius: 0,
            pointHoverRadius: 4
          },
          {
            label: '最高',
            data: data.max,
            borderColor: 'rgba(239,68,68,0.4)',
            borderWidth: 1,
            borderDash: [4, 4],
            fill: false,
            tension: 0.3,
            pointRadius: 0,
            pointHoverRadius: 3
          },
          {
            label: '最低',
            data: data.min,
            borderColor: 'rgba(234,179,8,0.4)',
            borderWidth: 1,
            borderDash: [4, 4],
            fill: false,
            tension: 0.3,
            pointRadius: 0,
            pointHoverRadius: 3
          }
        ]
      },
      options: {
        responsive: true,
        maintainAspectRatio: false,
        interaction: { mode: 'index', intersect: false },
        plugins: {
          legend: {
            display: true,
            position: 'top',
            align: 'end',
            labels: { color: '#94a3b8', boxWidth: 12, padding: 10, font: { size: 11 } }
          },
          tooltip: {
            backgroundColor: '#1e293b',
            titleColor: '#e2e8f0',
            bodyColor: '#e2e8f0',
            cornerRadius: 6,
            padding: 8,
            callbacks: {
              label: function(ctx) { return ctx.dataset.label + ': ' + ctx.parsed.y; }
            }
          }
        },
        scales: {
          x: {
            grid: { display: false },
            ticks: { color: '#64748b', font: { size: 11 }, maxRotation: 0, autoSkip: true, maxTicksLimit: 12 },
            border: { display: false }
          },
          y: {
            grid: { color: 'rgba(255,255,255,0.06)' },
            ticks: { color: '#64748b', font: { size: 11 } },
            border: { display: false },
            suggestedMin: 30,
            suggestedMax: 50
          }
        }
      }
    });
  } catch (e) {
    console.warn('[TickChart] init error:', e);
  }
}

// ─── Chart lifecycle (save before innerHTML, restore after) ───
var _savedTickWrap = null;
var _savedOnlineWrap = null;

export function saveChartContainers() {
  _savedTickWrap = document.getElementById('tick-chart-wrap');
  _savedOnlineWrap = document.getElementById('online-chart-wrap');
}

export function restoreChartContainers(subTab) {
  var newTickWrap = document.getElementById('tick-chart-wrap');
  var newOnlineWrap = document.getElementById('online-chart-wrap');

  if (newTickWrap && _savedTickWrap) {
    newTickWrap.replaceWith(_savedTickWrap);
  } else if (_savedTickWrap && !newTickWrap) {
    if (_tickChart) { _tickChart.destroy(); _tickChart = null; }
    _tickCanvas = null;
  }

  if (newOnlineWrap && _savedOnlineWrap) {
    newOnlineWrap.replaceWith(_savedOnlineWrap);
  } else if (_savedOnlineWrap && !newOnlineWrap) {
    if (_onlineChart) { _onlineChart.destroy(); _onlineChart = null; }
    _onlineCanvas = null;
  }

  if (subTab === 'players' && document.getElementById('player-online-chart') && !_onlineCanvas) {
    refreshOnlineChart();
  }
  if (subTab === 'list' && document.getElementById('tick-rate-chart') && !_tickCanvas) {
    refreshTickChart();
  }
}
