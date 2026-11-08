(function () {
  'use strict';
  const $ = id => document.getElementById(id);
  let dictionaries = [];
  const size = b => b >= 1048576 ? (b / 1048576).toFixed(1) + ' MB' : Math.max(1, Math.round(b / 1024)) + ' KB';

  function render() {
    const list = $('dcList'), select = $('dcSelect');
    list.textContent = ''; select.textContent = '';
    if (!dictionaries.length) { list.textContent = t('dict.empty'); return; }
    for (const d of dictionaries) {
      const row = document.createElement('div'); row.className = 'dc-item';
      const info = document.createElement('div');
      const name = document.createElement('b'); name.textContent = d.name;
      const meta = document.createElement('small'); meta.textContent = t('dict.meta', {entries: d.entries, size: size(d.bytes)}) + ' · ' + d.path;
      info.append(name, meta);
      const del = document.createElement('button'); del.className = 'btn btn-danger'; del.textContent = t('dict.delete');
      del.onclick = async () => {
        if (!confirm(t('dict.confirm_delete', {name: d.name}))) return;
        const r = await fetch('/api/dictionaries/delete', {method: 'POST', headers: {'Content-Type': 'application/json'}, body: JSON.stringify({path: d.path})});
        if (!r.ok) alert(t('dict.delete_failed'));
        load();
      };
      row.append(info, del); list.appendChild(row);
      const opt = document.createElement('option'); opt.value = d.index; opt.textContent = d.name; select.appendChild(opt);
    }
  }
  async function load() {
    try { dictionaries = (await (await fetch('/api/dictionaries')).json()).dictionaries || []; render(); }
    catch (e) { $('dcList').textContent = t('dict.error'); }
  }
  async function lookup() {
    const word = $('dcWord').value.trim(), out = $('dcResult');
    if (!word || !dictionaries.length) return;
    out.textContent = t('dict.loading');
    try {
      const r = await (await fetch('/api/dictionaries/lookup?index=' + encodeURIComponent($('dcSelect').value) + '&word=' + encodeURIComponent(word))).json();
      out.textContent = '';
      if (!r.found) { out.textContent = t('dict.not_found'); return; }
      const h = document.createElement('h3'); h.textContent = r.headword;
      out.append(h, document.createTextNode(r.article));
    } catch (e) { out.textContent = t('dict.error'); }
  }
  document.addEventListener('i18n:ready', () => {
    $('dcGo').onclick = lookup;
    $('dcWord').addEventListener('keydown', e => { if (e.key === 'Enter') lookup(); });
    load();
  });
})();
