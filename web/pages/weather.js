(function () {
  'use strict';
  var $ = function (id) { return document.getElementById(id); };
  var state = null;      // last /api/weather payload
  var forecast = null;   // forecast being shown
  var savedTimer = null;

  var TEMP = ['°C', '°F'], WIND = ['km/h', 'm/s', 'mph', 'kn'], PRECIP = ['mm', 'in'];

  function codeInfo(code, day) {
    var g = [[0, '☀️', '🌙', 'clear'], [2, '⛅', '☁️', 'partly'], [3, '☁️', '☁️', 'overcast'], [48, '🌫️', '🌫️', 'fog'],
      [57, '🌦️', '🌦️', 'drizzle'], [67, '🌧️', '🌧️', 'rain'], [77, '❄️', '❄️', 'snow'], [82, '🌦️', '🌦️', 'showers'],
      [86, '🌨️', '🌨️', 'snowshowers'], [99, '⛈️', '⛈️', 'thunder']];
    for (var i = 0; i < g.length; i++) if (code <= g[i][0]) return { icon: day === 0 ? g[i][2] : g[i][1], key: 'wx.c.' + g[i][3] };
    return { icon: '❔', key: 'wx.c.unknown' };
  }
  function fmtTime(ts, off, withDate) {
    var d = new Date((ts + off) * 1000), p = function (n) { return (n < 10 ? '0' : '') + n; };
    var hm = p(d.getUTCHours()) + ':' + p(d.getUTCMinutes());
    return withDate ? p(d.getUTCDate()) + '.' + p(d.getUTCMonth() + 1) + ' ' + hm : hm;
  }
  function dayName(ts, off) {
    var d = new Date((ts + off) * 1000);
    return d.toLocaleDateString(window.getLang ? window.getLang() : undefined, { weekday: 'short', day: 'numeric', month: 'short', timeZone: 'UTC' });
  }
  function compass(deg) { return ['N', 'NE', 'E', 'SE', 'S', 'SW', 'W', 'NW'][Math.round(((deg % 360) + 360) % 360 / 45) % 8]; }
  function r(v) { return v == null ? '–' : Math.round(v); }
  function el(tag, cls, text) { var e = document.createElement(tag); if (cls) e.className = cls; if (text != null) e.textContent = text; return e; }

  function setStatus(text, err) { var s = $('wxStatus'); s.textContent = text || ''; s.className = 'wx-status' + (err ? ' err' : ''); }

  function render(f, fetchedAt) {
    var off = f.utc_offset_seconds || 0, u = state;
    var now = $('wxNow'); now.textContent = '';
    var c = f.current;
    if (!c) { return; }
    var info = codeInfo(c.weather_code, c.is_day ? 1 : 0);
    now.appendChild(el('div', 'wx-city', u.name || ''));
    now.appendChild(el('div', 'wx-icon', info.icon));
    now.appendChild(el('div', 'wx-big', r(c.temperature_2m) + TEMP[u.temp]));
    var facts = el('div', 'wx-facts');
    [[t(info.key), t('wx.condition')], [r(c.apparent_temperature) + TEMP[u.temp], t('wx.feels')],
      [r(c.wind_speed_10m) + ' ' + WIND[u.wind] + ' ' + compass(c.wind_direction_10m), t('wx.wind')],
      [r(c.relative_humidity_2m) + '%', t('wx.humidity')], [(c.precipitation || 0) + ' ' + PRECIP[u.precip], t('wx.precip')],
      [c.uv_index == null ? '–' : c.uv_index.toFixed(1), t('wx.uv')]].forEach(function (p) {
      var d = el('div'); d.appendChild(el('span', null, p[1])); d.appendChild(document.createTextNode(p[0])); facts.appendChild(d);
    });
    now.appendChild(facts);

    var h = f.hourly, n = h && h.time ? Math.min(48, h.time.length) : 0;
    $('wxHourlyCard').hidden = !n;
    if (n) { drawChart(h, n, off); drawHours(h, n, off); }

    var dl = f.daily, days = $('wxDays'); days.textContent = '';
    $('wxDailyCard').hidden = !(dl && dl.time && dl.time.length);
    if (dl && dl.time) dl.time.forEach(function (ts, i) {
      var di = codeInfo(dl.weather_code[i], 1), row = el('div', 'wx-day');
      row.appendChild(el('div', null, dayName(ts, off)));
      row.appendChild(el('div', null, di.icon));
      row.appendChild(el('div', null, r(dl.temperature_2m_max[i]) + '° / ' + r(dl.temperature_2m_min[i]) + '°'));
      row.appendChild(el('div', null, (dl.precipitation_sum[i] || 0) + ' ' + PRECIP[u.precip]));
      var sun = el('div'); sun.appendChild(document.createTextNode('↑' + fmtTime(dl.sunrise[i], off) + ' ↓' + fmtTime(dl.sunset[i], off)));
      row.appendChild(sun); days.appendChild(row);
    });
    if (fetchedAt) setStatus(t('wx.updated', { time: fmtTime(fetchedAt, new Date().getTimezoneOffset() * -60, true) }));
  }

  function drawChart(h, n, off) {
    var W = 640, H = 170, L = 30, B = 22, T = 10, temps = h.temperature_2m.slice(0, n).filter(function (v) { return v != null; });
    if (!temps.length) { $('wxChart').textContent = ''; return; }
    var lo = Math.floor(Math.min.apply(null, temps)), hi = Math.ceil(Math.max.apply(null, temps));
    if (hi === lo) hi++;
    var pw = (W - L - 6) / (n - 1), y = function (v) { return T + (H - T - B) * (1 - (v - lo) / (hi - lo)); };
    var s = '<svg viewBox="0 0 ' + W + ' ' + H + '" role="img">';
    s += '<line class="ax" x1="' + L + '" y1="' + (H - B) + '" x2="' + (W - 6) + '" y2="' + (H - B) + '"/>';
    s += '<text x="2" y="' + (T + 8) + '">' + hi + '</text><text x="2" y="' + (H - B) + '">' + lo + '</text>';
    var maxP = Math.max.apply(null, h.precipitation.slice(0, n).map(function (v) { return v || 0; }).concat([0.5]));
    var pts = [];
    for (var i = 0; i < n; i++) {
      var x = L + i * pw, p = h.precipitation[i] || 0;
      if (p > 0) { var bh = (H - T - B) * 0.45 * p / maxP; s += '<rect class="pr" x="' + (x - 3) + '" y="' + (H - B - bh) + '" width="6" height="' + bh + '"/>'; }
      if (h.temperature_2m[i] != null) pts.push(x.toFixed(1) + ',' + y(h.temperature_2m[i]).toFixed(1));
      if (i % 6 === 0) s += '<text x="' + (x - 12) + '" y="' + (H - 6) + '">' + fmtTime(h.time[i], off) + '</text>';
    }
    s += '<polyline class="ln" points="' + pts.join(' ') + '"/></svg>';
    $('wxChart').innerHTML = s;
  }

  function drawHours(h, n, off) {
    var box = $('wxHours'); box.textContent = '';
    for (var i = 0; i < n; i += 3) {
      var d = el('div', 'wx-hour'), info = codeInfo(h.weather_code[i], h.is_day ? h.is_day[i] : 1);
      d.appendChild(el('div', null, fmtTime(h.time[i], off)));
      d.appendChild(el('div', null, info.icon));
      d.appendChild(el('b', null, r(h.temperature_2m[i]) + '°'));
      d.appendChild(el('div', null, (h.precipitation_probability && h.precipitation_probability[i] != null ? h.precipitation_probability[i] : 0) + '%'));
      box.appendChild(d);
    }
  }

  function api(path, opts) {
    return fetch(path, opts).then(function (res) { if (!res.ok) throw new Error(path + ' ' + res.status); return res; });
  }

  function load() {
    api('/api/weather').then(function (res) { return res.json(); }).then(function (s) {
      state = s;
      $('wxTemp').value = s.temp; $('wxWind').value = s.wind; $('wxPrecip').value = s.precip;
      $('wxDaysSel').value = s.days; $('wxHome').checked = !!s.onHome;
      if (!s.hasLocation) { setStatus(t('wx.no_location')); $('wxNow').textContent = ''; return; }
      if (s.cache) { forecast = s.cache; render(s.cache, s.fetchedAt); }
      refresh();
    }).catch(function () { setStatus(t('wx.device_error'), true); });
  }

  // Browser downloads the forecast, then pushes the raw response to the device cache.
  function refresh() {
    if (!state || !state.hasLocation) return;
    setStatus(t('wx.loading'));
    fetch(state.forecastUrl).then(function (res) {
      if (!res.ok) throw new Error('open-meteo ' + res.status);
      return res.text();
    }).then(function (text) {
      var f = JSON.parse(text);
      forecast = f; render(f, Math.floor(Date.now() / 1000));
      return fetch('/api/weather/cache?t=' + Math.floor(Date.now() / 1000), { method: 'POST', headers: { 'Content-Type': 'text/plain' }, body: text })
        .then(function (res) { if (!res.ok) setStatus(t('wx.cache_failed'), true); });
    }).catch(function () { setStatus(forecast ? t('wx.offline_cached') : t('wx.fetch_failed'), true); });
  }

  function saveSettings(patch) {
    return api('/api/weather/settings', { method: 'POST', headers: { 'Content-Type': 'application/json' }, body: JSON.stringify(patch) })
      .then(function () {
        for (var k in patch) state[k] = patch[k];
        $('wxSaved').textContent = t('wx.saved');
        clearTimeout(savedTimer); savedTimer = setTimeout(function () { $('wxSaved').textContent = ''; }, 2500);
      }).catch(function () { $('wxSaved').textContent = t('wx.save_failed'); });
  }

  function onUnitChange() {
    saveSettings({ temp: +$('wxTemp').value, wind: +$('wxWind').value, precip: +$('wxPrecip').value, days: +$('wxDaysSel').value, onHome: $('wxHome').checked })
      .then(function () { return api('/api/weather'); }).then(function (r2) { return r2.json(); })
      .then(function (s) { state = s; refresh(); });
  }

  function search() {
    var q = $('wxQuery').value.trim(); if (!q) return;
    var box = $('wxResults'); box.textContent = t('wx.loading');
    var lang = state ? state.language : 'en';
    fetch('https://geocoding-api.open-meteo.com/v1/search?count=8&format=json&language=' + encodeURIComponent(lang) + '&name=' + encodeURIComponent(q))
      .then(function (res) { return res.json(); }).then(function (j) {
        box.textContent = '';
        if (!j.results || !j.results.length) { box.textContent = t('wx.no_results'); return; }
        j.results.forEach(function (c) {
          var row = el('div', 'wx-city-item'), sub = [c.admin1, c.country].filter(Boolean).join(', ');
          row.appendChild(el('span', null, c.name)); row.appendChild(el('small', null, sub));
          row.addEventListener('click', function () {
            var name = c.name + (c.country_code ? ', ' + c.country_code : '');
            saveSettings({ latitude: c.latitude, longitude: c.longitude, name: name }).then(function () { box.textContent = ''; load(); });
          });
          box.appendChild(row);
        });
      }).catch(function () { box.textContent = t('wx.fetch_failed'); });
  }

  document.addEventListener('i18n:ready', function () {
    ['wxTemp', 'wxWind', 'wxPrecip', 'wxDaysSel', 'wxHome'].forEach(function (id) { $(id).addEventListener('change', onUnitChange); });
    $('wxSearchBtn').addEventListener('click', search);
    $('wxQuery').addEventListener('keydown', function (e) { if (e.key === 'Enter') search(); });
    load();
  });
  document.addEventListener('i18n:changed', function () { if (forecast && state) render(forecast, state.fetchedAt); });
})();
