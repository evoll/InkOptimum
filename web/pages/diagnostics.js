(function () {
  'use strict';
  var $ = function (id) { return document.getElementById(id); };
  function upt(s) { var d = Math.floor(s / 86400), h = Math.floor(s % 86400 / 3600), m = Math.floor(s % 3600 / 60); return (d ? d + 'd ' : '') + h + 'h ' + m + 'm'; }

  function loadStatus() {
    fetch('/api/status').then(function (r) { return r.json(); }).then(function (s) {
      var box = $('dgStatus'); box.textContent = '';
      [['diag.version', s.version], ['diag.device', s.device], ['diag.ip', s.ip + ' (' + s.mode + ')'],
        ['diag.rssi', s.mode === 'AP' ? '–' : s.rssi + ' dBm'], ['diag.heap', Math.round(s.freeHeap / 1024) + ' KB'], ['diag.uptime', upt(s.uptime)]]
        .forEach(function (p) {
          var d = document.createElement('div'), l = document.createElement('span'), v = document.createElement('b');
          l.setAttribute('data-i18n', p[0]); l.textContent = t(p[0]); v.textContent = p[1];
          d.appendChild(l); d.appendChild(v); box.appendChild(d);
        });
    }).catch(function () { $('dgStatus').textContent = t('diag.error'); });
  }
  function loadReport() {
    $('dgReport').textContent = t('diag.loading');
    fetch('/api/diagnostics').then(function (r) { return r.text(); }).then(function (x) { $('dgReport').textContent = x; })
      .catch(function () { $('dgReport').textContent = t('diag.error'); });
  }
  document.addEventListener('i18n:ready', function () {
    $('dgReload').onclick = function () { loadStatus(); loadReport(); };
    $('dgCopy').onclick = function () { if (navigator.clipboard) navigator.clipboard.writeText($('dgReport').textContent); };
    loadStatus(); loadReport();
  });
})();
