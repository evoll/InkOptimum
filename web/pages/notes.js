(function () {
  'use strict';
  var $ = function (id) { return document.getElementById(id); };
  var books = [];
  var cache = {};  // key -> {bookmarks, clippings}

  function el(tag, cls, text) { var e = document.createElement(tag); if (cls) e.className = cls; if (text != null) e.textContent = text; return e; }
  function key(b) { return b.type + '|' + b.path; }
  function qs(b) { return 'path=' + encodeURIComponent(b.path) + '&type=' + encodeURIComponent(b.type) + '&title=' + encodeURIComponent(b.title || '') + '&author=' + encodeURIComponent(b.author || ''); }
  function when(ts) { return ts ? new Date(ts * 1000).toLocaleString(window.getLang ? window.getLang() : undefined) : ''; }
  function post(path, body) {
    return fetch(path, { method: 'POST', headers: { 'Content-Type': 'application/json' }, body: JSON.stringify(body) })
      .then(function (r) { if (!r.ok) throw new Error(path); });
  }
  function ref(b, extra) { var o = { path: b.path, type: b.type, title: b.title || '', author: b.author || '' }; for (var k in extra) o[k] = extra[k]; return o; }

  function loadBook(b) {
    if (cache[key(b)]) return Promise.resolve(cache[key(b)]);
    return fetch('/api/notes/book?' + qs(b)).then(function (r) { return r.json(); }).then(function (d) { cache[key(b)] = d; return d; });
  }

  // ---- export -------------------------------------------------------------
  function bookMarkdown(b, d) {
    var s = '## ' + (b.title || b.path) + (b.author ? ' — ' + b.author : '') + '\n\n';
    if (d.bookmarks.length) {
      s += '### ' + t('notes.bookmarks') + '\n\n';
      d.bookmarks.forEach(function (m) {
        s += '- ' + (m.name ? '**' + m.name + '** · ' : '') + (m.chapter || '') + ' (' + m.progress + '%)' + (m.snippet ? ' — ' + m.snippet : '') + '\n';
      });
      s += '\n';
    }
    if (d.clippings.length) {
      s += '### ' + t('notes.clippings') + '\n\n';
      d.clippings.forEach(function (c) {
        s += '> ' + c.text.replace(/\n+/g, '\n> ') + '\n>\n> — ' + (c.chapter || '') + ', ' + t('notes.page') + ' ' + c.page + '\n\n';
      });
    }
    return s;
  }
  function download(name, mime, text) {
    var a = document.createElement('a');
    a.href = URL.createObjectURL(new Blob([text], { type: mime + ';charset=utf-8' }));
    a.download = name; document.body.appendChild(a); a.click();
    setTimeout(function () { URL.revokeObjectURL(a.href); a.remove(); }, 1000);
  }
  function safeName(s) { return (s || 'notes').replace(/[^\wЀ-ӿ.-]+/g, '_').slice(0, 60); }

  function exportBooks(list, fmt, fileName) {
    $('ntStatus').textContent = t('notes.exporting');
    var out = [];
    list.reduce(function (p, b) {
      return p.then(function () { return loadBook(b).then(function (d) { out.push({ book: b, data: d }); }); });
    }, Promise.resolve()).then(function () {
      $('ntStatus').textContent = '';
      if (fmt === 'json') {
        download(fileName + '.json', 'application/json', JSON.stringify(out.map(function (o) {
          return { title: o.book.title, author: o.book.author, path: o.book.path, bookmarks: o.data.bookmarks, clippings: o.data.clippings };
        }), null, 2));
      } else {
        download(fileName + '.md', 'text/markdown', '# ' + t('notes.title') + '\n\n' + out.map(function (o) { return bookMarkdown(o.book, o.data); }).join('\n'));
      }
    }).catch(function () { $('ntStatus').textContent = t('notes.error'); });
  }

  // ---- rendering ----------------------------------------------------------
  function renderBody(b, body) {
    body.textContent = t('notes.loading');
    loadBook(b).then(function (d) {
      body.textContent = '';
      if (d.bookmarks.length) body.appendChild(el('div', 'nt-sub', t('notes.bookmarks')));
      d.bookmarks.forEach(function (m) {
        var it = el('div', 'nt-item');
        if (m.name) it.appendChild(el('div', 'nt-name', m.name));
        it.appendChild(el('div', 'nt-meta', (m.chapter || '') + ' · ' + m.progress + '% · ' + when(m.time)));
        if (m.snippet) it.appendChild(el('div', 'nt-text', m.snippet));
        var act = el('div', 'nt-actions');
        var ren = el('button', 'btn', t('notes.rename'));
        ren.onclick = function () {
          var n = prompt(t('notes.rename_prompt'), m.name || '');
          if (n === null) return;
          post('/api/notes/bookmark/rename', ref(b, { index: m.i, name: n })).then(function () { delete cache[key(b)]; renderBody(b, body); }).catch(fail);
        };
        var del = el('button', 'btn btn-danger', t('notes.delete'));
        del.onclick = function () {
          if (!confirm(t('notes.confirm_delete'))) return;
          post('/api/notes/bookmark/delete', ref(b, { index: m.i })).then(function () { delete cache[key(b)]; refreshList(); }).catch(fail);
        };
        act.appendChild(ren); act.appendChild(del); it.appendChild(act); body.appendChild(it);
      });
      if (d.clippings.length) body.appendChild(el('div', 'nt-sub', t('notes.clippings')));
      d.clippings.forEach(function (c) {
        var it = el('div', 'nt-item clip');
        it.appendChild(el('div', 'nt-meta', (c.chapter || '') + ' · ' + t('notes.page') + ' ' + c.page + ' · ' + when(c.time)));
        it.appendChild(el('div', 'nt-text', c.text));
        var act = el('div', 'nt-actions');
        var cp = el('button', 'btn', t('notes.copy'));
        cp.onclick = function () { if (navigator.clipboard) navigator.clipboard.writeText(c.text); };
        var del = el('button', 'btn btn-danger', t('notes.delete'));
        del.onclick = function () {
          if (!confirm(t('notes.confirm_delete'))) return;
          post('/api/notes/clipping/delete', ref(b, { index: c.i })).then(function () { delete cache[key(b)]; refreshList(); }).catch(fail);
        };
        act.appendChild(cp); act.appendChild(del); it.appendChild(act); body.appendChild(it);
      });
      var ex = el('div', 'nt-actions');
      var md = el('button', 'btn btn-primary', 'Markdown'); md.onclick = function () { exportBooks([b], 'md', safeName(b.title)); };
      var js = el('button', 'btn', 'JSON'); js.onclick = function () { exportBooks([b], 'json', safeName(b.title)); };
      ex.appendChild(md); ex.appendChild(js); body.appendChild(ex);
    }).catch(fail);
  }
  function fail() { $('ntStatus').textContent = t('notes.error'); }

  function renderList() {
    var box = $('ntBooks'), f = $('ntFilter').value.trim().toLowerCase(); box.textContent = '';
    var shown = books.filter(function (b) { return !f || ((b.title || '') + ' ' + (b.author || '') + ' ' + b.path).toLowerCase().indexOf(f) >= 0; });
    if (!shown.length) { box.appendChild(el('p', 'hint', t('notes.empty'))); return; }
    shown.forEach(function (b) {
      var wrap = el('div', 'nt-book'), head = el('div', 'nt-head'), left = el('div');
      left.appendChild(el('h3', null, b.title || b.path.split('/').pop()));
      if (b.author) left.appendChild(el('small', null, b.author));
      head.appendChild(left);
      head.appendChild(el('small', null, '🔖 ' + b.bookmarks + '  ✂ ' + b.clippings));
      var body = el('div', 'nt-body'); body.hidden = true;
      head.onclick = function () { body.hidden = !body.hidden; if (!body.hidden && !body.firstChild) renderBody(b, body); };
      wrap.appendChild(head); wrap.appendChild(body); box.appendChild(wrap);
    });
  }

  function refreshList() {
    fetch('/api/notes').then(function (r) { return r.json(); }).then(function (d) { books = d.books || []; renderList(); }).catch(fail);
  }

  document.addEventListener('i18n:ready', function () {
    $('ntFilter').addEventListener('input', renderList);
    $('ntExportAll').onclick = function () { exportBooks(books, 'md', 'inkmod-notes'); };
    $('ntExportAllJson').onclick = function () { exportBooks(books, 'json', 'inkmod-notes'); };
    refreshList();
  });
  document.addEventListener('i18n:changed', renderList);
})();
