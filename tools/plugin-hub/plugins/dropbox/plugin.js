// Dropbox browser for the Plugins page: sign in once (PKCE, no secret), browse folders, download books to the SD.
// `api` is provided by the page. All Dropbox traffic goes through api.relay (no CORS, TLS terminated on the book);
// files are fetched by the book itself from a short-lived link (api.fetchToSd), never through the browser.
const BOOK_EXT = ['epub', 'fb2', 'fb3', 'mobi', 'azw3', 'txt', 'md', 'rtf', 'docx', 'html', 'htm', 'xtc', 'xtch', 'pdf', 'cbz', 'zip'];
const STORE = 'inkmod.dropbox.';
const store = {
  get(k) { try { return localStorage.getItem(STORE + k) || ''; } catch (e) { return ''; } },
  set(k, v) { try { v ? localStorage.setItem(STORE + k, v) : localStorage.removeItem(STORE + k); } catch (e) { /* private mode */ } }
};

const root = api.root;
const el = (tag, props, ...kids) => {
  const n = Object.assign(document.createElement(tag), props || {});
  n.append(...kids);
  return n;
};
const b64url = (b64) => b64.replace(/\+/g, '-').replace(/\//g, '_').replace(/=+$/, '');
const form = (o) => Object.entries(o).map(([k, v]) => encodeURIComponent(k) + '=' + encodeURIComponent(v)).join('&');

let access = '', accessUntil = 0;

async function post(url, headers, body) {
  const r = await api.relay('POST', url, headers, body);
  let data = null;
  try { data = JSON.parse(r.body); } catch (e) { /* not json */ }
  if (r.status < 200 || r.status >= 300) {
    const msg = (data && (data.error_summary || data.error_description || data.error)) || ('HTTP ' + r.status);
    throw new Error(String(msg));
  }
  return data;
}

async function token() {
  if (access && Date.now() < accessUntil - 60000) return access;
  const key = store.get('key'), refresh = store.get('refresh');
  if (!key || !refresh) throw new Error('not signed in');
  const d = await post('https://api.dropboxapi.com/oauth2/token', { 'Content-Type': 'application/x-www-form-urlencoded' },
    form({ grant_type: 'refresh_token', refresh_token: refresh, client_id: key }));
  access = d.access_token;
  accessUntil = Date.now() + (d.expires_in || 14400) * 1000;
  return access;
}

async function rpc(path, args) {
  const t = await token();
  return post('https://api.dropboxapi.com/2/' + path, { Authorization: 'Bearer ' + t, 'Content-Type': 'application/json' }, JSON.stringify(args));
}

// ---- sign-in: the app key of your own Dropbox app (App console, "Scoped access", files.metadata.read +
// files.content.read), authorization code pasted back from the Dropbox page (no redirect address needed).
async function startAuth(key) {
  const rnd = await api.crypto('random', { len: 48 });
  const verifier = b64url(rnd.data);
  const h = await api.crypto('sha256', { data: btoa(verifier) });
  const challenge = b64url(h.data);
  store.set('key', key);
  store.set('verifier', verifier);
  return 'https://www.dropbox.com/oauth2/authorize?' +
    form({ client_id: key, response_type: 'code', code_challenge: challenge, code_challenge_method: 'S256', token_access_type: 'offline' });
}

async function finishAuth(code) {
  const d = await post('https://api.dropboxapi.com/oauth2/token', { 'Content-Type': 'application/x-www-form-urlencoded' },
    form({ code: code.trim(), grant_type: 'authorization_code', code_verifier: store.get('verifier'), client_id: store.get('key') }));
  if (!d.refresh_token) throw new Error('no refresh token: create the app with offline access');
  store.set('refresh', d.refresh_token);
  store.set('verifier', '');
  access = d.access_token;
  accessUntil = Date.now() + (d.expires_in || 14400) * 1000;
}

// ---- listing and download
async function list(path) {
  const entries = [];
  let d = await rpc('files/list_folder', { path, limit: 100, include_deleted: false, include_mounted_folders: true });
  for (;;) {
    entries.push(...d.entries);
    if (!d.has_more) break;
    d = await rpc('files/list_folder/continue', { cursor: d.cursor });
  }
  entries.sort((a, b) => (a['.tag'] === b['.tag'] ? a.name.localeCompare(b.name, undefined, { sensitivity: 'base' }) : a['.tag'] === 'folder' ? -1 : 1));
  return entries;
}

const destRoot = () => (store.get('dest') || '/Dropbox').replace(/\/+$/, '');
async function download(entry, log) {
  const rel = entry.path_display;
  const dest = destRoot() + rel;
  const link = await rpc('files/get_temporary_link', { path: entry.path_lower });
  // Make the folders on the way, then let the book fetch the file itself.
  let dir = '';
  for (const part of dest.split('/').slice(1, -1)) { dir += '/' + part; await api.mkdir(dir); }
  log('→ ' + dest);
  const r = await api.fetchToSd(link.link, dest);
  log('✓ ' + dest + ' (' + (r.bytes || entry.size || 0) + ' B)');
  return dest;
}

// ---- page
const status = el('div', { className: 'muted' });
const body = el('div');
const logBox = el('pre');
const log = (s) => { logBox.textContent += s + '\n'; api.log(s); };
root.append(status, body, logBox);

function setStatus(s, bad) { status.textContent = s || ''; status.style.color = bad ? '#b00020' : ''; }

function renderLogin() {
  body.replaceChildren();
  const key = el('input', { placeholder: 'App key', value: store.get('key'), style: 'width:16em' });
  const link = el('a', { target: '_blank', rel: 'noopener', textContent: '' });
  const code = el('input', { placeholder: 'Code from Dropbox', style: 'width:16em' });
  const open = el('button', { className: 'btn', textContent: '1. Open Dropbox' });
  const done = el('button', { className: 'btn btn-primary', textContent: '2. Finish sign-in' });
  open.onclick = async () => {
    try {
      if (!key.value.trim()) return setStatus('Enter the app key of your Dropbox app', true);
      link.href = await startAuth(key.value.trim());
      link.textContent = 'Authorize in Dropbox, then copy the code';
      window.open(link.href, '_blank', 'noopener');
    } catch (e) { setStatus(e.message, true); }
  };
  done.onclick = async () => {
    try { await finishAuth(code.value); setStatus('Signed in'); renderBrowser('', ''); } catch (e) { setStatus(e.message, true); }
  };
  body.append(el('p', {}, 'Create an app at dropbox.com/developers/apps (Scoped access, permissions files.metadata.read and files.content.read), then paste its App key.'),
    key, ' ', open, el('br'), link, el('br'), code, ' ', done);
}

async function renderBrowser(path, pathNice) {
  body.replaceChildren(el('div', { textContent: 'Loading…' }));
  let entries;
  try { entries = await list(path); } catch (e) {
    if (/not signed in|invalid_grant|expired|invalid_access/.test(e.message)) { store.set('refresh', ''); setStatus(e.message, true); return renderLogin(); }
    body.replaceChildren(el('div', { textContent: e.message, style: 'color:#b00020' }));
    return;
  }
  const bar = el('div');
  const up = el('button', { className: 'btn', textContent: '↑ ..', disabled: !path });
  up.onclick = () => renderBrowser(path.slice(0, path.lastIndexOf('/')), '');
  const out = el('button', { className: 'btn', textContent: 'Sign out' });
  out.onclick = () => { store.set('refresh', ''); access = ''; renderLogin(); };
  const dest = el('input', { value: destRoot(), style: 'width:10em', title: 'SD folder for downloads' });
  dest.onchange = () => store.set('dest', dest.value.trim());
  bar.append(up, ' ', el('b', {}, path || '/'), ' ', el('span', { className: 'muted' }, 'SD: '), dest, ' ', out);
  const table = el('table');
  const books = [];
  for (const e of entries) {
    const tr = el('tr');
    if (e['.tag'] === 'folder') {
      const a = el('a', { href: '#', textContent: '📁 ' + e.name });
      a.onclick = (ev) => { ev.preventDefault(); renderBrowser(e.path_lower, e.path_display); };
      tr.append(el('td', {}, a), el('td'), el('td'));
    } else {
      const ext = (e.name.split('.').pop() || '').toLowerCase();
      if (!BOOK_EXT.includes(ext)) continue;
      books.push(e);
      const btn = el('button', { className: 'btn', textContent: 'Download' });
      btn.onclick = async () => {
        btn.disabled = true;
        try { await download(e, log); btn.textContent = 'Done'; } catch (er) { log('✗ ' + e.name + ': ' + er.message); btn.disabled = false; }
      };
      tr.append(el('td', {}, e.name), el('td', {}, e.size ? (e.size / 1048576).toFixed(1) + ' MB' : ''), el('td', {}, btn));
    }
    table.append(tr);
  }
  const all = el('button', { className: 'btn btn-primary', textContent: 'Download all books here (' + books.length + ')', disabled: !books.length });
  all.onclick = async () => {
    all.disabled = true;
    for (const e of books) { try { await download(e, log); } catch (er) { log('✗ ' + e.name + ': ' + er.message); } }
    all.disabled = false;
  };
  body.replaceChildren(bar, table, all);
}

if (store.get('refresh')) renderBrowser('', ''); else renderLogin();

// Queue action: {"plugin":"dropbox","action":"download","args":{"path":"/Books/x.epub"}} (signed in once in a browser).
api.registerAction('download', async (args) => {
  const meta = await rpc('files/get_metadata', { path: args.path });
  return { dest: await download(meta, () => {}) };
});
api.registerAction('list', async (args) => ({ entries: (await list((args && args.path) || '')).map((e) => ({ tag: e['.tag'], name: e.name, path: e.path_display, size: e.size || 0 })) }));
