# <img width="22" src="resources/graphics/rssguard.png" alt="RSS Guard icon"> RSS Guard — Windows Torrent Automation Fork

[![Windows portable build](https://github.com/Neil1454/rssguard/actions/workflows/torrent-windows-portable.yml/badge.svg?branch=feature%2Ftorrent-automation)](https://github.com/Neil1454/rssguard/actions/workflows/torrent-windows-portable.yml)
[![Current test build](https://img.shields.io/badge/current%20test%20build-55-blue)](CHANGELOG-TORRENT-INTEGRATION.md)
[![Platform](https://img.shields.io/badge/platform-Windows%2010%2F11%20x64-0078D4)](BUILD-WINDOWS.md)
[![License](https://img.shields.io/badge/license-GPLv3-green)](LICENSE.md)

This is Neil1454's Windows-focused fork of [Martin Rotter's RSS Guard](https://github.com/martinrotter/rssguard). It retains RSS Guard's full feed-reader functionality and adds native manual and automated routing of recognised torrent RSS entries to multiple remote torrent clients or seedboxes.

The current downloadable test version is **Build 55**, based on RSS Guard **5.2.6 development source**. Build 55 passed the Windows x64 Qt 6 WebEngine compile, packaging and artifact-upload workflow. It is a portable test build, not a separately installed service, and it runs only while RSS Guard and Windows are running.

The current application source is on **[`feature/torrent-automation`](https://github.com/Neil1454/rssguard/tree/feature/torrent-automation)**. The repository keeps `master` as its GitHub default branch for upstream history, but `master` does not contain the current Build 55 application code. Clone or download the feature branch when building this fork from source.

> Use torrents only for material you are legally permitted to download and share. The integration is intended for lawful use. Automatic cleanup can remove torrent jobs and, when explicitly enabled, downloaded data. Start with dry-run mode and keep a backup of your RSS Guard profile.

## Download and install

1. Open the [Windows portable workflow](https://github.com/Neil1454/rssguard/actions/workflows/torrent-windows-portable.yml).
2. Open the newest successful run on `feature/torrent-automation`.
3. Download the **RSSGuard-Torrent-Test** artifact.
4. Extract the downloaded ZIP, then extract the `.7z` archive inside it into a new folder.
5. Run `rssguard.exe`.

The package is an unsigned **Windows 10/11 x64, Qt 6, WebEngine portable build**. Windows SmartScreen may warn because it is not code-signed. Do not extract it over an existing RSS Guard installation. This fork currently produces no custom installer, Linux package or macOS package.

To retain an existing portable profile, close both copies of RSS Guard and copy the existing `data5` folder beside the new `rssguard.exe`. Check **Help > About RSS Guard > Resources** after launch to confirm which data folder is active. See [Windows build and profile migration](BUILD-WINDOWS.md) for details.

## What the fork adds

### Manual torrent sending

- Right-click one or several selected articles and choose **Send to torrent client**.
- Send directly to a named client or choose **Process automatically** for an assessed destination.
- New-article notifications provide the same automatic action and named client buttons.
- Recognises magnet links, BitTorrent enclosures and identifiable HTTP(S) `.torrent` URLs.
- Deduplicates extracted links across multi-selected articles.
- Saves successful destinations per article and client. Synchronized green ticks appear in both notifications and the main article menu.
- Marks a successfully processed notification row with a green completion band and advances to the next item.
- Allows successful-send confirmations to be suppressed while keeping failure messages visible.

### Torrent clients

| Client | Authentication | Important URL guidance |
|---|---|---|
| qBittorrent | Web UI username/password; no API key | Web UI root, such as `http://host:8080` |
| Transmission 3.x/4.x | RPC or reverse-proxy username/password | Usually `https://host/transmission/rpc` |
| Flood / RFlood | Username/password or Flood JWT | Flood web root; do not add `/api` |
| rTorrent / ruTorrent | XML-RPC gateway authentication | ruTorrent commonly uses `/plugins/httprpc/action.php`; some hosts provide `/RPC2` |
| Deluge | Deluge Web password | Deluge Web root, commonly port `8112` |
| rQBit | Optional HTTP Basic authentication | HTTP API root; do not add `/api` |
| Porla | Porla bearer JWT | JSON-RPC server root; do not add `/api` |

Multiple instances of the same client type are supported. Each instance can have its own name, endpoint, credentials, proxy choice, save path, supported category/labels/tags, enabled state, button order and colour.

The client editor changes its examples, tooltips and enabled fields for the selected client type. **Test connection** checks one client and **Test all enabled** checks all active configurations. See the [complete client setup and compatibility guide](docs/source/features/torrent-clients.md).

### Notifications and appearance

- Configurable two-column client-button layout with full instance names.
- Raised, hover, pressed and in-progress button feedback.
- Per-client colours can be applied independently to notification buttons, article context menus and settings/automation lists.
- Clients can be disabled without deleting their configuration; disabled clients are greyed, moved below enabled clients and removed from send actions.
- Optional **Keep new-article notifications open until dismissed** behaviour.
- Notification settings can preview the actual article notification, optionally including the live torrent-button layout.
- One-click main-window switch between the bundled minimal-light and minimal-dark skins.

### Automatic RSS routing

Open **Tools > Settings > Torrent automation**. Automation is **disabled by default** and starts in **dry-run mode**.

- Processes newly fetched torrent entries without requiring a direct client-button click.
- Ordered rules can match feeds already configured in RSS Guard, required/excluded text, title regular expressions, size limits and allowed client pools.
- Routing strategies include priority order, least busy, most free space, round robin, priority-biased distribution and balanced routing.
- Balanced routing considers priority, free-space ratio, active downloads, queued downloads and aggregate download speed.
- Per-client controls include maximum active downloads, maximum managed torrents, minimum free space, target free-space percentage, fallback capacity, maximum aggregate download speed, request timeout, retry count and cleanup permission.
- Disabled, disallowed, physically full or unreachable clients are never selected automatically.

### Approval and traffic-light assessment

The **Process automatically** action performs the same live assessment without removing manual control:

| Colour | Meaning | Manual action |
|---|---|---|
| Blue | Best currently recommended destination | Send normally |
| Green | Suitable alternative | Send normally |
| Amber | Available but outside a configured load or reserve target | Explicit override permitted |
| Red | Unavailable, disallowed or physically lacks space | Sending is blocked |

Every colour is accompanied by a written status and reason, so the decision does not depend on colour alone. Direct named-client buttons remain available and deliberately bypass soft amber automation limits.

### Retries, failover and duplicate protection

- Configurable request timeout, retry count, initial delay, maximum delay and exponential backoff.
- Per-client timeout and retry overrides.
- Definite temporary network failures can immediately fail over to the next suitable client.
- Work waiting for a healthy destination is retained in a persistent retry queue across application restarts.
- Authentication and invalid-configuration errors are not repeatedly retried.
- After an ambiguous magnet timeout, RSS Guard checks the magnet info hash on the original client before any failover, preventing an unnecessary duplicate send.
- An ambiguous direct `.torrent` URL cannot always be verified reliably. RSS Guard reports that it may already have succeeded instead of blindly resending it.

### Capability detection

Non-destructive selected/all-client tests record whether the authenticated API can provide:

- connection and authentication;
- live workload;
- disk free space and total capacity;
- torrent listing;
- aggregate and per-torrent transfer rates;
- safe torrent removal.

An exact server-reported capacity is copied into the automation capacity field. Where an API does not expose filesystem totals, a manually configured capacity can be used as an estimate. The capability test never adds or removes a torrent.

### Guarded cleanup

Cleanup is **off by default**. It only considers completed torrents that RSS Guard automation marked with `rssguard-auto`; unrelated and manually added torrents are excluded.

- Oldest eligible completed torrents are considered first.
- Minimum seeding age, ratio and inactivity requirements can be enabled independently.
- Cleanup can target a fixed free-space value and/or a percentage of client capacity.
- Space recovery can be rounded into configurable capacity-percentage batches.
- Torrents uploading at or above a configurable speed can be protected until a later cleanup pass.
- Unknown upload speeds can be protected conservatively.
- A configurable removal limit applies per run; disabling that setting still leaves an internal emergency maximum of 25 removals.
- Removing downloaded data is a separate destructive option and requires confirmation.
- Cleanup with all eligibility filters disabled also requires confirmation.

Use **Run dry test now** to assess recent eligible RSS entries against live client status and record what would be sent or removed. A dry run makes no add, remove or delete request.

## Network and privacy behaviour

Torrent links are sent directly from RSS Guard to the configured torrent-client API. A browser login session or browser cookies are not used, except for API session cookies obtained by RSS Guard itself when a client protocol requires them.

Each client can use or bypass RSS Guard's configured proxy. **Tools > Settings > Network & web > Network proxy > Test proxy connection** tests the displayed proxy configuration and reports connectivity, elapsed time and the public IP reached without displaying the proxy password. HTTPS certificate verification remains enabled.

Client passwords, Flood tokens and Porla JWTs use RSS Guard's encrypted-settings convention. Users should still protect and back up their profile appropriately.

## Build history

The full record is maintained in [Torrent integration changelog](CHANGELOG-TORRENT-INTEGRATION.md).

| Milestone | Main changes |
|---|---|
| **Build 55 — current** | Approval-based automatic processing; blue/green/amber/red client assessment; transfer-speed-aware balanced routing; percentage free-space targets; persistent configurable retries and failover; duplicate-safe magnet timeout verification; oldest-first upload-aware cleanup; expanded documentation. |
| Build 54 | Intermediate CI run containing the Build 55 feature set; failed Windows compilation and was replaced. It should not be used or distributed. |
| Build 53 | Synchronized send ticks; capability testing and capacity autofill; configurable cleanup safeguards and forced dry-run testing; proxy test; light/dark switch; clearer tooltips, feed selection and client identity; pressed buttons and processed notification bands. |
| 09/09/2026 compatibility milestone | Added rQBit, Porla and Deluge; corrected ruTorrent endpoints; improved qBittorrent 5.2, Transmission 3.00/4.x and Flood responses; enabling/disabling, priority, colours, test-all and notification persistence/layout improvements. |
| 08/09/2026 initial integration | Added the native client configuration, extraction engine, manual context-menu sending, qBittorrent/Transmission/Flood/rTorrent adapters, proxy integration, tests and dedicated Windows portable workflow. |

Build numbers identify this fork's incremental Windows test packages; they do not replace the underlying upstream RSS Guard version number.

## Documentation

- [Complete feature and architecture overview](README-TORRENT-INTEGRATION.md)
- [Torrent-client configuration and troubleshooting](docs/source/features/torrent-clients.md)
- [Torrent automation, routing and cleanup](docs/source/features/torrent-automation.md)
- [Notifications](docs/source/features/notifications.md)
- [Windows portable build and profile migration](BUILD-WINDOWS.md)
- [Testing checklist and live-client matrix](TESTING-TORRENT-INTEGRATION.md)
- [Incremental torrent-integration changelog](CHANGELOG-TORRENT-INTEGRATION.md)
- [Contributing](CONTRIBUTING.md)
- [Security policy](SECURITY.md)
- [GPLv3 licence](LICENSE.md)

## Testing status and reporting problems

Build 55 passed the automated Windows compile and packaging workflow. Real torrent-client behaviour still depends on server versions, reverse proxies, authentication policies and API permissions. Before replacing an existing copy, test the portable build separately using the [testing checklist](TESTING-TORRENT-INTEGRATION.md).

When reporting a problem, include:

- the fork build number and commit;
- the torrent-client type and version;
- whether the client is local, remote or behind a reverse proxy;
- the configured URL with private host details removed;
- whether **Use RSS Guard proxy** is enabled;
- the exact error text and whether the torrent nevertheless appeared in the client.

Never include passwords, tokens, cookies or private tracker URLs in a report.

## Upstream RSS Guard

RSS Guard is a fast, lightweight and customisable feed reader supporting Windows, Linux, BSD, OS/2 and macOS. It supports RSS, Atom, JSON Feed, iCalendar and Sitemap feeds; Feedly, Gmail, Google Reader-compatible services, Nextcloud News, Tiny Tiny RSS and XMPP; podcasts and mpv/FFmpeg media playback; filtering, labels, article extraction and extensive interface customisation.

This fork remains based on the upstream RSS Guard codebase. General RSS Guard issues and contributions belong with the [upstream project](https://github.com/martinrotter/rssguard); fork-specific torrent integration reports should clearly identify the Neil1454 build first.

## Credits and lawful use

RSS Guard copyright © 2011–2026 Martin Rotter and contributors.

Torrent-related integration added by **Neil Sampson, aka MonsterDK**.

Any torrent integrations contributed through this fork must be used only for lawful purposes. Illegal activity is strictly prohibited.

RSS Guard and this fork are open-source software licensed under the [GNU GPLv3](LICENSE.md).
