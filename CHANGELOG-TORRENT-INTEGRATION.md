# Torrent integration changelog

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
