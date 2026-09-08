# Torrent integration changelog

## 2026-09-09 — Notifications and compatibility corrections

- Added named torrent-client buttons to new-article notifications when the selected notification article contains a usable torrent link.
- Added a setting to show or suppress successful-send confirmations, plus a one-click opt-out in the success dialog. Failure messages remain enabled.
- Removed forced alternating row colours from the client list to prevent dark navy/purple bands with affected Windows palettes.
- Added qBittorrent 5.2 authentication compatibility: HTTP 204 login responses and `QBT_SID_...` session cookies, while retaining older HTTP 200/`Ok.`/`SID` support.
- Added matching qBittorrent Origin and Referer headers and robust credential form encoding.
- Corrected Flood results: HTTP 202 empty responses mean queued/accepted, not zero accepted; HTTP 207 remains partial success; ambiguous HTTP 500 responses now advise checking Flood before retrying.
- Changed the dedicated Windows portable workflow to run automatically on pushes to `master` as well as manually.
- Limited the custom distribution workflow to the required Windows 10/11 x64 Qt 6 WebEngine portable package.

## 2026-09-08 — Initial implementation

- Based on upstream RSS Guard 5.2.6 development commit `3342c7e3e9aefe637282378bc317e0d39d5ef95c` (`devbuild5`).
- Added named, multi-instance qBittorrent, Transmission, Flood, and rTorrent configurations.
- Added optional default client, proxy toggle, connection test, save path, category/label, and tags.
- Added independent torrent extraction and cross-selection deduplication.
- Added native article context submenu and bulk sending.
- Added asynchronous client adapters with TLS verification and RSS Guard proxy integration.
- Added encrypted-at-rest credential fields using RSS Guard's current settings convention.
- Updated the base network manager to preserve explicitly supplied API cookies instead of replacing them with its placeholder cookie.
- Added extractor unit tests and a manually triggered Windows portable build workflow.
- Added architecture, testing, maintenance, build, and profile-migration documentation.
