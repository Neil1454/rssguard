# Native torrent-client integration

The `master` branch of this fork adds native torrent sending to RSS Guard 5.2.6 development source. It is intentionally separated into `src/librssguard/torrent/` with narrow hooks in settings, article actions, and article notifications.

## User workflow

1. Open **Tools > Settings > Torrent clients**.
2. Add one or more named clients and use **Test connection**.
3. Select one or multiple articles.
4. Right-click and choose **Send to torrent client > _client name_**. If a default is configured, the direct default-client action is also shown.
5. RSS Guard reports accepted/failed totals, articles with no usable link, and duplicates skipped.

When a new-article notification contains a usable torrent link, the notification also shows one button per configured client. The first article is selected automatically; Ctrl/Shift can select several rows. Clicking a named button sends torrent links from all selected notification articles to that client. Buttons are disabled only when none of the selected articles contains a usable torrent link.

**Keep new-article notifications open until dismissed** in Notification settings disables both the article notification timer and right-click dismissal. The notification remains available for torrent selection until its close button is used.

Successful-send dialogs can be disabled either from the dialog itself or with **Show confirmation after successful torrent sends** in Torrent clients settings. Failures remain visible.

This feature is manual. It does not automatically send newly fetched feed entries.

## Architecture

- `torrentclientconfig.*`: typed client configuration, validation, persistence, and encrypted secret fields.
- `torrentextractor.*`: client-independent discovery and bulk deduplication.
- `torrentclient.*`: common asynchronous interface and separate qBittorrent, Transmission, Flood, and rTorrent adapters.
- `settingstorrentclients.*`: native settings panel and add/edit/remove/test UI.
- `MessagesView`: builds the dynamic client menu from saved configurations and passes selected messages to the extractor.
- `ArticleListNotification`: builds per-client notification buttons and sends the selected notification article.

Adding a new client requires a new `TorrentClient` subclass, one enum/type label, and a factory case. Extraction and UI selection do not need to be rewritten.

## Torrent extraction

The extractor examines, in priority-neutral order:

- enclosures whose MIME type contains `bittorrent`;
- enclosure URLs, article URLs, or metadata URLs ending in `.torrent`;
- `magnet:` links in raw/processed message content and custom metadata;
- HTTP(S) links ending in `.torrent`, including query strings;
- query values which themselves end in `.torrent`.

The normal article URL is not accepted unless it is recognisably a torrent URL. Links are deduplicated within and across selected articles. Magnet comparisons are case-insensitive.

## Client protocols

### qBittorrent

- Cookie login: `POST /api/v2/auth/login`.
- Connection test: `GET /api/v2/app/version`.
- Bulk add: multipart `POST /api/v2/torrents/add`, with newline-delimited URLs.
- Optional save path, category, and tags are sent when configured.
- Any successful HTTP 2xx add response is treated as accepted; HTTP 202 is reported as queued because some hosted proxies return it after successfully submitting the torrent.

The adapter supplies matching `Origin` and `Referer` headers and safely form-encodes credentials. It supports both the traditional HTTP 200/`Ok.`/`SID` login and qBittorrent 5.2's HTTP 204/`QBT_SID_...` login. qBittorrent uses the Web UI username and password; it does not require an API key.

### Transmission

The adapter supports both pre-emptive HTTP Basic authentication and server/reverse-proxy authentication challenges (including Digest authentication), followed by Transmission's normal `X-Transmission-Session-Id` HTTP 409 retry. Transmission 3.00 uses the same JSON-RPC flow and does not require an API key.

- JSON RPC at the exact configured URL (normally `/transmission/rpc`).
- HTTP Basic authentication when credentials are supplied.
- Correctly retries once after HTTP 409 using `X-Transmission-Session-Id`.
- Connection test uses `session-get`; sending uses one asynchronous `torrent-add` call per URL.
- Optional download directory and labels are supported.

### Flood

- Login: `POST /api/auth/authenticate`; connection test: `GET /api/auth/verify`.
- Bulk add: `POST /api/torrents/add-urls` with a URL array.
- Supports username/password or an existing Flood JWT auth token. Tokens are sent as an HTTP-only-style `jwt` cookie, never in the URL.
- Optional destination and tags are supported; added torrents start immediately.
- HTTP 200 is treated as accepted, HTTP 202 as accepted/queued, and HTTP 207 as partial success. A Flood HTTP 500 warning states that the server may still have submitted the torrent and should be checked before retrying.

### rTorrent / ruTorrent

- XML-RPC over HTTP(S) at the exact configured gateway URL.
- HTTP Basic authentication when supplied.
- Connection test calls `system.client_version`.
- Sending calls `load.start` asynchronously for each URL, with the required empty target argument.
- Optional directory and category (`d.custom1`) commands are supported.

rTorrent itself normally exposes SCGI, not HTTP. The configured URL must therefore be an authenticated HTTP(S) XML-RPC gateway provided by the user's web server/reverse proxy. For ruTorrent installations this is normally the ruTorrent web address followed by `/plugins/rpc/rpc.php`, not the homepage. Direct and challenged Basic/Digest authentication are supported. RSS Guard does not expose raw SCGI to the internet.

## Proxy and TLS behavior

Each adapter owns an RSS Guard `BaseNetworkAccessManager`:

- **Use RSS Guard proxy** enabled (default): the manager uses the application-wide Network/Web proxy, including its authentication.
- Disabled: the manager is explicitly assigned `QNetworkProxy::NoProxy`, useful for LAN clients.

TLS verification remains enabled. SSL errors are reported and are not ignored. Client passwords/tokens are never included in log statements or URLs.

## Configuration storage

Non-secret client fields are stored as compact JSON under `TorrentClients/clients` in RSS Guard's existing settings file. Passwords and Flood tokens are stored separately under `TorrentClientSecrets/<uuid>/...` using RSS Guard's existing `Settings::setPassword()` encryption convention. Removing/saving the list removes orphaned secrets.

## New files

- `.github/workflows/torrent-windows-portable.yml`
- `src/librssguard/torrent/torrentclient.{h,cpp}`
- `src/librssguard/torrent/torrentclientconfig.{h,cpp}`
- `src/librssguard/torrent/torrentextractor.{h,cpp}`
- `src/librssguard/gui/settings/settingstorrentclients.{h,cpp}`
- `docs/source/features/torrent-clients.md`
- `tests/torrent/test_torrentextractor.cpp`
- `README-TORRENT-INTEGRATION.md`
- `BUILD-WINDOWS.md`
- `CHANGELOG-TORRENT-INTEGRATION.md`
- `TESTING-TORRENT-INTEGRATION.md`

## Modified upstream files

- `src/librssguard/CMakeLists.txt`
- `src/librssguard/gui/dialogs/formsettings.cpp`
- `src/librssguard/gui/messagesview.{h,cpp}`
- `src/librssguard/gui/notifications/articlelistnotification.{h,cpp}`
- `src/librssguard/network-web/basenetworkaccessmanager.cpp` (preserves explicit API cookies)
- `tests/CMakeLists.txt`

## Known limitations

- The first version sends magnet links and remotely accessible torrent URLs. It does not download a `.torrent` file into RSS Guard and re-upload its binary body.
- Generic download endpoints with no torrent MIME enclosure and no recognisable `.torrent`/magnet marker cannot be safely distinguished from article links.
- rTorrent requires an HTTP(S) XML-RPC gateway; direct SCGI sockets are not supported.
- Live behavior still depends on each server/reverse proxy and should be tested before relying on it unattended.
- Strings are ready for Qt translation extraction but translations have not been supplied.

## Upstream maintenance

The maintained fork branch is `master`. For a future upstream refresh:

```bash
git fetch upstream
git rebase upstream/master master
```

Resolve conflicts primarily in the narrow UI hooks named above. Run the extraction test, all RSS Guard tests, and the Windows portable workflow. GitHub `master` is the canonical project state.
