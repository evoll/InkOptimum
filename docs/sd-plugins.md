# SD-card plugins (stage 1)

Ported from CrossPoint's SD plugin system (see its `docs/sd-plugins.md`). A plugin is a folder on the card:

```
<root>/<name>/
    manifest.json     title, description (optional)
    device.json       on-device catalog / events manifest (optional)
    plugin.js         browser-side plugin (optional)
    README.md         usage notes, shown on the device (optional)
```

Roots, earlier wins on a name clash: `/.inkmod/plugins`, `/.crosspoint/plugins`, `/plugins`, `/.plugins`.
A folder holding an `.installing` file is skipped until the installer removes it.

## Switch

**Settings > System > Plugins** (default off). A card that already holds plugins is switched on once at the
first boot of this version; after that the setting is yours. Turning it off hides the Home entry and the
discovery, and deletes nothing.

## What works

- **Home > Plugins** lists every plugin with its title and description (`manifest.json`, overridden by
  `device.json`); the folder is rescanned each time the list opens.
- Selecting a plugin shows its `README.md` (first 16 KB) as paged text. Up/Down turn pages.
- Policy code shared with the later stages: root priority, quarantine, version comparison, the picker rule
  (catalog or README) and the credential-store path guard (`network/ProtectedPaths.h`).

## Not yet

- On-device catalogs (`device.json` `browse`): selecting one shows a notice (its README opens if present).
- The web-server side: `/api/relay`, `/api/crypto`, `/api/fetch`, `/api/plugin-fs`, the job queue and
  hosting `plugin.js` in the web UI.
- Plugin events and the per-book metadata sidecar, Plugin Hub install.


## On-device catalogs and the web host

`device.json` catalogs open from Home > Plugins (`PluginCatalogActivity`), and `plugin.js` runs on the web page
`/plugins`. Full reference, the hosting server (`tools/plugin-hub`) and the TLS modes: see `docs/plugin-hub.md`.
