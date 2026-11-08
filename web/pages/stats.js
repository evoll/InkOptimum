(function () {
  'use strict';
  var $ = function (id) { return document.getElementById(id); };
  function dur(sec) { var h = Math.floor(sec / 3600), m = Math.floor(sec % 3600 / 60); return h ? h + t('stats.h') + ' ' + m + t('stats.m') : m + t('stats.m'); }
  function cell(label, value) {
    var d = document.createElement('div'), l = document.createElement('span'), v = document.createElement('b');
    l.textContent = label; v.textContent = value; d.appendChild(l); d.appendChild(v); return d;
  }
  function bars(box, values, labels) {
    box.textContent = '';
    var max = Math.max.apply(null, values.concat([1]));
    values.forEach(function (v, i) {
      var d = document.createElement('div'), s = document.createElement('span'), l = document.createElement('em');
      s.style.height = Math.round(v / max * 70) + 'px'; s.title = dur(v);
      l.textContent = labels[i]; l.style.fontStyle = 'normal';
      d.appendChild(s); d.appendChild(l); box.appendChild(d);
    });
  }
  function render(s) {
    var sum = $('stSummary'); sum.textContent = '';
    $('stEmpty').style.display = s.totalSeconds ? 'none' : '';
    sum.appendChild(cell(t('stats.time'), dur(s.totalSeconds)));
    sum.appendChild(cell(t('stats.sessions'), String(s.sessions)));
    sum.appendChild(cell(t('stats.pages'), String(s.pages)));
    sum.appendChild(cell(t('stats.finished'), String(s.completed)));
    sum.appendChild(cell(t('stats.streak'), s.currentStreak + ' / ' + s.longestStreak));
    // history[i] === '1': day (anchor - i). Grid fills column-wise, 7 rows, Monday first.
    var heat = $('stHeat'); heat.textContent = '';
    var weeks = 26, total = weeks * 7;
    var anchor = new Date(Date.UTC(s.anchor.y, s.anchor.m - 1, s.anchor.d));
    var anchorDow = (anchor.getUTCDay() + 6) % 7;            // Monday = 0
    var lastCol = anchorDow;                                   // rows after anchor day in its week stay empty
    for (var idx = 0; idx < total; idx++) {
      var col = Math.floor(idx / 7), row = idx % 7;
      var daysBack = (weeks - 1 - col) * 7 + (anchorDow - row);
      var on = daysBack >= 0 && s.history.charAt(daysBack) === '1';
      var i = document.createElement('i');
      if (daysBack < 0) i.style.visibility = 'hidden';
      if (on) i.className = 'on';
      heat.appendChild(i);
    }
    var dn = t('stats.dow').split(',');
    bars($('stDow'), s.dayOfWeek, dn);
    bars($('stTod'), s.timeOfDay, t('stats.tod').split(','));
  }
  function load() {
    fetch('/api/stats').then(function (r) { return r.json(); }).then(render)
      .catch(function () { $('stEmpty').style.display = ''; $('stEmpty').textContent = t('stats.error'); });
  }
  document.addEventListener('i18n:ready', load);
  document.addEventListener('i18n:changed', load);
})();
