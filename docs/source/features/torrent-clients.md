Torrent clients
===============

The Neil1454 Windows fork can send recognised torrent links from RSS articles to qBittorrent, Transmission, Flood/RFlood, rTorrent/ruTorrent, and Deluge. Sending is manual; fetching a feed does not automatically add every new item.

## Configure a client

Open **Tools > Settings > Torrent clients**, choose **Add**, enter a unique display name, select the client type, and use **Test connection**.

| Client | Server/base URL | Authentication |
|---|---|---|
| qBittorrent | Web UI root, for example `http://qbittorrent.example:8081` | Web UI username and password; no API key/token |
| Transmission | RPC endpoint, for example `https://host/transmission/rpc` | RPC username and password |
| Flood/RFlood | Flood web root, for example `http://flood.example:3000` | Flood username/password or optional Flood JWT token |
| rTorrent / ruTorrent | HTTP(S) XML-RPC gateway. For ruTorrent this is usually `https://host/plugins/rpc/rpc.php`, not the web-interface homepage | Gateway/web-interface username and password |
| Deluge | Deluge Web root, for example `http://deluge.example:8112` | Deluge Web password; a username is not used |

Do not add `/api` to qBittorrent or Flood URLs; RSS Guard appends their API routes. A Transmission browser URL ending in `/transmission/web` normally becomes `/transmission/rpc`. For ruTorrent, use its XML-RPC endpoint, usually the web address followed by `/plugins/rpc/rpc.php`. rTorrent's raw SCGI socket is not supported directly.

Transmission supports direct Basic credentials and Basic/Digest authentication challenges from a hosting reverse proxy. Transmission 3.00 uses the same JSON-RPC exchange and does not need an API key.

The editor changes its URL example and help text when the client type changes. It disables fields that the selected adapter cannot use, preventing an apparently valid setting from being silently ignored.

| Option | qBittorrent | Transmission | Flood | rTorrent/ruTorrent | Deluge |
|---|---:|---:|---:|---:|---:|
| Default save path | Yes | Yes | Yes | Yes | Yes |
| Category/label | Category | No | No | `d.custom1` label | No |
| Tags/labels | Tags | Labels on Transmission 4.0+ | Tags | No | No |
| Username | Yes | Yes | Yes, unless using token | Gateway dependent | No |
| Password | Yes | Yes | Yes, unless using token | Gateway dependent | Deluge Web password |

The save path is a path on the remote torrent server, not necessarily a folder on the RSS Guard computer. **Use RSS Guard proxy** inherits **Tools > Settings > Network & web > Network proxy**; disable it for a direct LAN connection.

## Version compatibility

RSS Guard detects the version or protocol level during **Test connection** wherever the upstream API exposes it. Compatibility is selected automatically; there is no manual version switch.

- qBittorrent supports both the traditional HTTP 200/`SID` login and the HTTP 204/prefixed-cookie login used by newer releases. Successful 2xx add responses, including hosted HTTP 202 responses, count as accepted.
- Transmission uses its legacy JSON RPC exchange because Transmission 3.00 supports it and current Transmission retains compatibility. RSS Guard reads `rpc-version` before adding. It sends labels only with RPC 17/Transmission 4.0 or newer; Transmission 3.00 sends the torrent without labels.
- Flood compatibility is response-driven: HTTP 200 and 202 are accepted and HTTP 207 is partial success.
- rTorrent reports its backend version through `system.client_version`; ruTorrent is only the web gateway. The XML-RPC endpoint, not the ruTorrent homepage, must be entered.
- Deluge Web reports the connected daemon version and available methods. RSS Guard logs in at `/json`, connects the configured daemon when needed, and supports URL and magnet adds.

## Send articles

- Select one or several article rows, right-click, and choose **Send to torrent client > _client name_**.
- Use the direct default-client action when a default client is configured.
- On a new-article notification, the first article is selected automatically. Click a named client button to send it, or use Ctrl/Shift to select several notification rows and send all of their torrent links together.
- To prevent a notification disappearing while choosing a torrent client, enable **Tools > Settings > Notifications > Keep new-article notifications open until dismissed**. Its timer and right-click dismissal are then disabled; close it with its close button.

RSS Guard recognises magnet links, BitTorrent enclosures, and recognisable HTTP(S) `.torrent` URLs. It deduplicates links across a multi-selection and reports articles with no usable torrent link.

## Confirmation messages

Successful-send confirmations can be disabled from a success dialog or with **Show confirmation after successful torrent sends** in Torrent clients settings. Error and failure messages are not suppressed.

Flood HTTP 202 means the request was queued successfully even when Flood returns an empty result array. An HTTP 500 response is ambiguous because some Flood installations submit the torrent before reporting the error; check Flood before retrying.

## Troubleshooting

- Verify the same address opens from the RSS Guard computer.
- Confirm the selected client type and credentials.
- For LAN clients, disable **Use RSS Guard proxy** unless the proxy can reach the LAN address.
- qBittorrent 5.2 uses an HTTP 204 login response and a newer session-cookie name; current fork builds support both this and older versions.
- qBittorrent add responses in the successful HTTP 2xx range are accepted; HTTP 202 is reported as queued rather than failed.
- A save path is interpreted by the remote torrent client, so it must exist and be allowed on that server, not merely on the RSS Guard computer.
- Download a fresh portable artifact after a code change; an older extracted executable is not updated automatically.

## Other clients considered

Deluge was added because it is a widely used, established torrent client with an official authenticated Web JSON-RPC API. aria2 and generic download managers were not added to this release because their broader download APIs do not provide the same torrent-client semantics. Additional adapters should be added separately, with an official API and testable success response, instead of presenting an unverified generic option.

## Protocol references

- [qBittorrent WebUI API](https://github.com/qbittorrent/qBittorrent/wiki/WebUI-API-%28qBittorrent-4.1%29)
- [Transmission RPC specification](https://github.com/transmission/transmission/blob/main/docs/rpc-spec.md)
- [Flood torrent API route](https://github.com/jesec/flood/blob/master/server/routes/api/torrents.ts)
- [Deluge Web JSON-RPC guide](https://deluge.readthedocs.io/en/latest/devguide/how-to/curl-jsonrpc.html)

Passwords and Flood tokens are stored through RSS Guard's encrypted-settings convention. TLS certificate verification remains enabled for HTTPS connections.
