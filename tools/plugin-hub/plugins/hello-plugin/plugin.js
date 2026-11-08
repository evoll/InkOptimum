// `api` and `JSZip` are provided by the Plugins page. api.root is this plugin's card.
const button = document.createElement('button');
button.className = 'btn btn-primary';
button.textContent = 'Device info';
const out = document.createElement('pre');
button.onclick = async () => {
  const status = await (await fetch('/api/status')).json();
  out.textContent = JSON.stringify(status, null, 2);
};
api.root.append(button, out);

// Runs when an external system queues {"plugin":"hello-plugin","action":"ping"} (see /plugins-run).
api.registerAction('ping', async () => ({ pong: Date.now() % 100000 }));
