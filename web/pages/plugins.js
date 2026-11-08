// PluginHost: runs each installed plugin.js in this page and gives it the `api` object.
// The same page, opened as /plugins-run, is the headless runner that executes queued jobs.
(function () {
  const headless = location.pathname === '/plugins-run';
  const listEl = document.getElementById('plList');
  const hosts = [];

  function b64ToBytes(b64) {
    const bin = atob(b64);
    const out = new Uint8Array(bin.length);
    for (let i = 0; i < bin.length; i++) out[i] = bin.charCodeAt(i);
    return out;
  }
  async function jsonOrThrow(res) {
    let data = null;
    try { data = await res.json(); } catch (e) { /* not json */ }
    if (!res.ok) throw new Error((data && data.error) || ('HTTP ' + res.status));
    return data;
  }

  function makeApi(plugin, root, log) {
    const actions = {};
    const api = {
      name: plugin.name,
      dir: plugin.dir,
      root: root,
      version: plugin.version || '',
      log: log,
      registerAction(name, fn) { actions[name] = fn; },
      // api.relay(method, url, headers, body)  or  api.relay({method, url, headers, body})
      async relay(method, url, headers, body) {
        const r = typeof method === 'object' ? method : { method, url, headers, body };
        const res = await fetch('/api/relay', {
          method: 'POST',
          headers: { 'Content-Type': 'application/json' },
          body: JSON.stringify({ plugin: plugin.name, method: r.method || 'GET', url: r.url, headers: r.headers || {}, body: r.body || '' })
        });
        if (!res.ok) await jsonOrThrow(res);
        let buf;
        try {
          buf = new Uint8Array(await res.arrayBuffer());
        } catch (e) {
          // The device streams the body and closes early when it is cut short or over the cap.
          const err = new Error('/api/relay 502: relay failed; large bodies need /api/fetch');
          err.status = 502;
          throw err;
        }
        let hdrs = [];
        try { hdrs = JSON.parse(res.headers.get('X-Relay-Headers') || '[]'); } catch (e) { /* ignore */ }
        return {
          status: parseInt(res.headers.get('X-Relay-Status') || '0', 10),
          headers: hdrs,
          bytes: buf,
          body: new TextDecoder().decode(buf)
        };
      },
      // Downloads in segments of up to 1 MB so no browser request stays open long; resumes after a drop.
      async fetchToSd(url, dest, headers) {
        let offset = 0, last = null, failures = 0;
        for (;;) {
          const res = await fetch('/api/fetch', {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify({ plugin: plugin.name, url, dest, headers: headers || {}, offset, maxBytes: 1048576 })
          });
          let data = null;
          try { data = await res.json(); } catch (e) { /* not json */ }
          if (res.ok && data) {
            last = data;
            if (data.complete) return data;
            offset = data.bytes;
            failures = 0;
            continue;
          }
          // A cut-off body keeps its .part: continue from where the device says it stopped.
          if (data && data.error === 'download truncated' && ++failures < 4) { offset = data.bytes; continue; }
          if (data && data.error === 'offset mismatch' && ++failures < 4) { offset = data.bytes; continue; }
          throw new Error((data && data.error) || ('HTTP ' + res.status));
        }
      },
      // data: base64 string (default), Uint8Array, Blob or, with {text:true}, plain text.
      async writeFile(path, data, opts) {
        let blob;
        if (data instanceof Blob) blob = data;
        else if (data instanceof Uint8Array) blob = new Blob([data]);
        else if (opts && opts.text) blob = new Blob([data]);
        else blob = new Blob([b64ToBytes(data)]);
        const fd = new FormData();
        fd.append('file', blob, 'data');
        const res = await fetch('/api/plugin-fs?plugin=' + encodeURIComponent(plugin.name) + '&path=' + encodeURIComponent(path), { method: 'POST', body: fd });
        return jsonOrThrow(res);
      },
      // api.crypto(op, fields): stateless device-side primitives (random, sha1, sha256, aesenc, aesdec, keygen,
      // pubencrypt, sign, rsadec, pkcs12). Binary fields are base64 in both directions; rejects on {error}.
      async crypto(op, fields) {
        const res = await fetch('/api/crypto', {
          method: 'POST',
          headers: { 'Content-Type': 'application/json' },
          body: JSON.stringify(Object.assign({ op: op }, fields || {}))
        });
        const data = await jsonOrThrow(res);
        if (data && data.error) throw new Error(data.error);
        return data;
      },
      // api.bookKey(path, keyBase64, expiresEpoch?): store a protected book's content key wrapped to this device.
      async bookKey(path, key, expires) {
        const res = await fetch('/api/book-key', {
          method: 'POST',
          headers: { 'Content-Type': 'application/json' },
          body: JSON.stringify({ path: path, key: key, expires: expires || 0 })
        });
        return jsonOrThrow(res);
      },
      // api.lcpUnlock(path, {passphrase, userKey}): unlock a Readium LCP book from its license.lcpl and store the key.
      // Rejects with {error: 'need_passphrase'|'wrong_passphrase'|'unsupported_profile', hint}; production-profile
      // licenses need userKey (base64, 32 bytes) computed by the plugin.
      async lcpUnlock(path, opts) {
        const res = await fetch('/api/lcp-unlock', {
          method: 'POST',
          headers: { 'Content-Type': 'application/json' },
          body: JSON.stringify(Object.assign({ path: path }, opts || {}))
        });
        return res.json();
      },
      async mkdir(path) {
        const i = path.lastIndexOf('/');
        const body = new URLSearchParams({ name: path.slice(i + 1), path: i <= 0 ? '/' : path.slice(0, i) });
        return fetch('/mkdir', { method: 'POST', body });
      }
    };
    return { api, actions };
  }

  async function loadPlugin(plugin) {
    const card = document.createElement('div');
    card.className = 'card';
    const title = document.createElement('h2');
    title.textContent = plugin.title;
    card.appendChild(title);
    const meta = document.createElement('div');
    meta.className = 'pl-meta';
    meta.textContent = plugin.name + (plugin.version ? ' · v' + plugin.version : '') + (plugin.description ? ' — ' + plugin.description : '');
    card.appendChild(meta);
    const root = document.createElement('div');
    root.className = 'pl-root';
    card.appendChild(root);
    const logEl = document.createElement('div');
    logEl.className = 'pl-log';
    logEl.style.display = 'none';
    card.appendChild(logEl);
    const log = function () {
      logEl.style.display = '';
      logEl.textContent += Array.prototype.slice.call(arguments).join(' ') + '\n';
    };
    if (!headless) listEl.appendChild(card);

    const { api, actions } = makeApi(plugin, root, log);
    try {
      const res = await fetch('/plugin?name=' + encodeURIComponent(plugin.name) + '&file=plugin.js');
      if (!res.ok) throw new Error('HTTP ' + res.status);
      const code = await res.text();
      // The plugin is code the user put on their own card: it runs with the page's privileges.
      new Function('api', 'JSZip', code + '\n//# sourceURL=plugin-' + plugin.name + '.js')(api, window.JSZip);
      // Re-register actions as buttons so they can be run by hand.
      const names = Object.keys(actions);
      if (names.length && !headless) {
        const bar = document.createElement('div');
        bar.className = 'pl-actions';
        names.forEach(function (n) {
          const b = document.createElement('button');
          b.className = 'btn';
          b.textContent = n;
          b.onclick = async function () {
            try { log(n, '→', JSON.stringify(await actions[n]({}))); } catch (e) { log('error:', e.message); }
          };
          bar.appendChild(b);
        });
        card.insertBefore(bar, root);
      }
    } catch (e) {
      log('load failed:', e.message);
      logEl.classList.add('pl-err');
    }
    hosts.push({ plugin, actions });
  }

  async function pollJobs() {
    for (const h of hosts) {
      if (!Object.keys(h.actions).length) continue;
      try {
        const job = await (await fetch('/api/plugin-jobs/claim?plugin=' + encodeURIComponent(h.plugin.name))).json();
        if (!job || !job.id) continue;
        let ok = true, result = {};
        try {
          if (!h.actions[job.action]) throw new Error('no such action');
          result = (await h.actions[job.action](job.args || {})) || {};
        } catch (e) { ok = false; result = { error: String(e.message || e).slice(0, 120) }; }
        await fetch('/api/plugin-jobs/complete', {
          method: 'POST',
          headers: { 'Content-Type': 'application/json' },
          body: JSON.stringify({ id: job.id, claim: job.claim, ok, result })
        });
      } catch (e) { /* device busy: try again next round */ }
    }
  }

  async function start() {
    if (headless) {
      document.getElementById('plRunner').style.display = '';
      document.getElementById('plHead').style.display = 'none';
    }
    let plugins;
    try {
      const res = await fetch('/api/plugins');
      if (res.status === 403) { document.getElementById('plOff').style.display = ''; return; }
      plugins = await res.json();
    } catch (e) { plugins = null; }
    // A busy device can drop the request or answer 503 {"error":"out of memory"}: say so instead of showing nothing.
    if (!Array.isArray(plugins)) {
      listEl.innerHTML = '<div class="card"><p class="hint">Could not load plugins. Reload the page.</p></div>';
      return;
    }
    const withJs = plugins.filter(function (p) { return p.hasPluginJs; });
    if (!withJs.length && !headless) {
      listEl.innerHTML = '<div class="card"><p class="hint">No plugin.js plugins installed. Put a folder with plugin.js into /.inkmod/plugins/ on the card.</p></div>';
    }
    for (const p of withJs) await loadPlugin(p);
    if (headless) {
      setInterval(pollJobs, 2500);
      pollJobs();
    }
  }
  start();
})();
