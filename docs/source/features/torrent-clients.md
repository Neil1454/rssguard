Torrent clients
===============

The Neil1454 Windows fork can send recognised torrent links from RSS articles to qBittorrent, Transmission, Flood/RFlood, or rTorrent. Sending is manual; fetching a feed does not automatically add every new item.

## Configure a client

Open **Tools > Settings > Torrent clients**, choose **Add**, enter a unique display name, select the client type, and use **Test connection**.

| Client | Server/base URL | Authentication |
|---|---|---|
| qBittorrent | Web UI root, for example `http://qbittorrent.example:8081` | Web UI username and password; no API key/token |
| Transmission | RPC endpoint, for example `https://host/transmission/rpc` | RPC username and password |
| Flood/RFlood | Flood web root, for example `http://flood.example:3000` | Flood username/password or optional Flood JWT token |
| rTorrent | HTTP(S) XML-RPC gateway URL | Gateway Basic-auth credentials when required |

Do not add `/api` to qBittorrent or Flood URLs; RSS Guard appends their API routes. A Transmission browser URL ending in `/transmission/web` normally becomes `/transmission/rpc`. rTorrent's raw SCGI socket is not supported directly.

Optional fields include default save path, category/label, tags, default-client selection, and **Use RSS Guard proxy**. The proxy option inherits **Tools > Settings > Network & web > Network proxy**; disable it for a direct LAN connection.

## Send articles

- Select one or several article rows, right-click, and choose **Send to torrent client > _client name_**.
- Use the direct default-client action when a default client is configured.
- On a new-article notification, select an article and click its named client button.

RSS Guard recognises magnet links, BitTorrent enclosures, and recognisable HTTP(S) `.torrent` URLs. It deduplicates links across a multi-selection and reports articles with no usable torrent link.

## Confirmation messages

Successful-send confirmations can be disabled from a success dialog or with **Show confirmation after successful torrent sends** in Torrent clients settings. Error and failure messages are not suppressed.

Flood HTTP 202 means the request was queued successfully even when Flood returns an empty result array. An HTTP 500 response is ambiguous because some Flood installations submit the torrent before reporting the error; check Flood before retrying.

## Troubleshooting

- Verify the same address opens from the RSS Guard computer.
- Confirm the selected client type and credentials.
- For LAN clients, disable **Use RSS Guard proxy** unless the proxy can reach the LAN address.
- qBittorrent 5.2 uses an HTTP 204 login response and a newer session-cookie name; current fork builds support both this and older versions.
- A save path is interpreted by the remote torrent client, so it must exist and be allowed on that server, not merely on the RSS Guard computer.
- Download a fresh portable artifact after a code change; an older extracted executable is not updated automatically.

Passwords and Flood tokens are stored through RSS Guard's encrypted-settings convention. TLS certificate verification remains enabled for HTTPS connections.
