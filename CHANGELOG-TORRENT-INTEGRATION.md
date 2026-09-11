# Torrent integration changelog

## 2026-09-09 — Notifications and compatibility corrections

- Added rQBit HTTP API support with optional Basic authentication and output-folder selection.
- Added Porla bearer-JWT JSON-RPC support for magnets and downloaded `.torrent` files, including save paths and presets.
- Corrected ruTorrent guidance to use `plugins/httprpc/action.php`, with `/RPC2` documented as an alternative.
- Added per-client enable/disable and numbered button priority; disabled clients stay configured but disappear from send actions.
- Added **Test all enabled** with a combined pass/fail result for every active client.
- Replaced the generic notification test with the real new-article layout and an option to include the live torrent-button arrangement.
- Moved disabled clients to the bottom of settings, removed their priority number, and displayed them with disabled text colouring.
- Added a common-colour selector per client and applied the chosen colour with readable contrasting text to notification buttons and previews.

- Replaced the cramped single-row notification client buttons with a larger two-column grid so configured names remain readable and easier to click.
- Fixed the successful-send confirmation checkbox so changing it marks Settings dirty and enables Apply.
- Made the client editor type-aware with per-client URL examples, field help, and disabled unsupported options.
- Added Deluge Web support with password/session authentication, daemon connection and version detection, URL/magnet sending, and remote download paths.
- Added automatic Transmission RPC detection and omitted torrent-add labels on Transmission 3.00, where that option is unsupported.
- Added named torrent-client buttons to new-article notifications, automatic first-row selection, and Ctrl/Shift multi-selection for sending one or several notification articles.
- Added challenge-based HTTP authentication for hosted Transmission/reverse-proxy installations while retaining pre-emptive Basic authentication and the Transmission session-ID retry.
- Corrected qBittorrent false failures when a hosted instance returns HTTP 202 after successfully queuing a torrent; all successful HTTP 2xx add responses are now accepted.
- Added explicit ruTorrent support through its XML-RPC endpoint, Basic/Digest authentication challenges, clearer client naming, and guidance when the web-interface homepage is entered instead of the RPC endpoint.
- Added an option to keep new-article notifications open until explicitly dismissed, disabling their timeout and accidental right-click dismissal while retaining the close button.
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
