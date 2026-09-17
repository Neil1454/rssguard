# Torrent integration changelog

## Build 66

- Redesigned the setup wizard as a visually welcoming, question-led walkthrough rather than a dense settings form.
- Added clear live safety banners for Dry run and cleanup, plain-English recommendations and strategy-specific explanations.
- Added conditional controls so follow-up values are enabled only when their parent choice applies.
- Added an **Explain this page** action with detailed descriptions of every setting group.
- Added per-column plain-language help to the client-limits table while preserving every advanced option.
- Expanded the welcome, rule, retry, maintenance, cleanup, protection and final-review guidance with practical examples.

## Build 65

- Added a complete step-by-step Torrent automation setup wizard.
- Covers safety and dry-run mode, routing, size estimates, every per-client limit, RSS rules, retries, maintenance, scheduling, cleanup thresholds and cleanup protections.
- Adds plain-language explanations, practical examples, a final safety summary and explicit Apply/OK guidance.
- Wizard cancellation restores the original RSS rules and applies no other choices.

## Build 64

- Reworked dry-run notifications into short, strongly labelled outcome cards with separate torrent, destination, reason and live-behaviour sections.
- Added clear **TEST ONLY** footers and matching send, cleanup, wait, blocked and duplicate wording.

## Build 63

- Added a dedicated **Simple** dry-run results tab with bold, colour-coded outcome rows.
- Separates would-send, would-delete, would-keep, wait/retry, blocked and skipped results from diagnostic activity.
- Deletion results identify the torrent, client, estimated recovered size and the cleanup-rule reason.
- Preserved the complete underlying explanation in row tooltips and the Activity audit trail.
- Dry tests now fall back to a clearly labelled sample of previously received RSS articles when no newly fetched batch is available, including already processed items for read-only simulation.

## Torrent automation development build

- Build 62 adds a persistent MiB/GiB/TiB selector to **Clients and limits**. Changing the unit converts the displayed minimum-free-space and capacity values without changing their stored byte values.
- Storage cells accept explicit `MiB`, `GiB` or `TiB` suffixes (including decimal values such as `1.8 TiB`) regardless of the selected display unit. Maximum download-rate cells also accept `MiB/s`, `GiB/s` and `TiB/s`, and invalid entries are blocked with a clear example instead of silently becoming zero.

- Build 61 fixes the malformed **Clients and limits** table. The duplicated custom cell overlay was removed, client name/type now use one DPI-aware two-line item, colour swatches render once, row height follows the active Windows font, and limit columns size to their contents with smooth horizontal scrolling when required.
- Corrected the visible fork build definition so Help > About reports Build 61 consistently with the package tag and documentation.

- Build 60 replaces the short dry-run result with a complete, read-only decision trace. It reports the matched rule, estimated size, routing strategy, every participating client's traffic-light assessment and exact restriction or selection reason.
- Dry-run cleanup now names every torrent that would be removed to reach the configured target, whether downloaded data would be deleted, its size, estimated recovered space, and every enabled cleanup condition it satisfied. Torrents retained by active-upload, unknown-speed, recent-upload, age, ratio, inactivity, tag/tracker, copy-count, incomplete-download or grace-period protection are named with the exact reason.
- Forced dry tests now include already-processed recent items and explicitly report unmatched rules and RSS items with no supported torrent link. They simulate routing windows, retry/backoff, duplicate handling, cleanup grace and multi-item batch load without writing queue, cleanup-candidate, processed-item, reconciliation or client-health state.
- Activity displays the configured retained history instead of silently truncating it to 100 entries, and long decision explanations wrap in the settings page.

- Build 59 adds a readiness audit that separates blocking configuration problems, warnings and passed checks before dry-run mode is disabled.
- Completed RSS rule size handling: minimum/maximum sizes are editable and enforced per extracted torrent, including multiple torrent links within one article. Rules can now be duplicated and moved up/down to control first-match ordering.
- Added retry-all and cancel-all controls for the persistent queue. Retried work still passes through normal client limits and safety checks; cancellation does not change existing torrents.
- Added credential-free JSON activity export and safe history clearing. Clearing history preserves processed-item protection, managed allocations and queued work.

- Build 58 adds periodic and per-routing reconciliation of RSS Guard's managed ledger against reachable client torrent lists. Actual sizes and remaining-download reservations are refreshed, externally removed entries are discarded, and matching owned hashes are rediscovered conservatively.
- Added explicit storage-source reporting in **Clients and limits**: live free space, reconciled capacity estimate, managed-ledger estimate, or unknown. Unfinished managed downloads can reserve their remaining bytes to prevent over-allocation.
- Added cross-client magnet info-hash duplicate prevention for unattended automation. Direct named-client actions remain available when a deliberate additional copy is wanted.
- Added a persistent queue viewer with the hold/retry reason and local next-attempt time, plus **Retry selected now**, **Choose client and send**, and **Cancel selected** controls.
- Added a configurable client circuit breaker. Repeated failures temporarily sideline one client without blocking the others; consecutive successful recovery checks are required before automatic reuse.
- Added optional local-time windows for unattended routing and cleanup. Deferred items remain in the persistent queue across restart.
- Added smart cleanup scoring using age, size and ratio; recent-upload protection; tag/label and tracker protection; optional minimum completed-copy protection across configured clients; and two-stage mark/wait/recheck cleanup with a configurable grace period.
- Added a rule dry-test shortcut that explains which named rule would route each recent RSS item.
- Added credential-free JSON export/import for torrent client layout and automation configuration. Passwords and tokens are never exported; matching locally stored credentials are retained on import.
- Build 57 remains the immediate rollback candidate, and Build 19 remains the pre-automation rollback point.

- Build 57 fixes cleanup bookkeeping so a successfully removed torrent is removed from both the live snapshot and the managed-capacity ledger before another cleanup decision. Failed removals are bounded and cannot repeatedly select the same client in one attempt.
- Client status checks now honour global/per-client retry counts, request timeouts and backoff independently, so one busy client does not prevent other clients from being assessed.
- Capability tests only mark transfer speeds when the server actually returns aggregate or per-torrent rate values. Removal is now labelled as adapter/API support rather than a guarantee of server-side permission.
- Added non-destructive correction prompts for common Transmission Web UI, ruTorrent homepage and qBittorrent API-suffix mistakes, while preserving custom endpoints when no confident correction is available.
- Added URL-correction regression tests and a visible torrent-fork build number under Help > About RSS Guard.

- Build 55 adds approval-based **Process automatically** actions to new-article notifications and the main article context menu. It assesses every participating client before sending and retains the existing direct client buttons.
- Added a traffic-light destination assessment: blue is the recommended destination, green is suitable, amber can be manually overridden, and red is unavailable. Every colour is accompanied by text and a reason.
- Balanced routing now considers priority, active and queued downloads, aggregate download rate, and free-space ratio. Per-client maximum download rate, request timeout, retry count, minimum free space and target-free-space percentage are configurable.
- Added persistent bounded retries with optional exponential backoff, immediate failover after definite transient failures, and configurable global/per-client request timeouts.
- Added duplicate-safe handling for ambiguous timeouts: magnet info hashes are checked on the original client before failover. Ambiguous direct `.torrent` URL submissions are held for manual checking because blindly retrying could create a duplicate.
- Cleanup now deletes eligible completed automation-managed torrents oldest-first, can protect torrents uploading above a configurable rate, can conservatively protect unknown upload speeds, and can round recovery to a configurable capacity percentage.
- Capability tests now record transfer-rate monitoring as a separate detected capability. Live aggregate and per-torrent transfer rates are collected where supported by qBittorrent, Transmission, Flood, rTorrent/ruTorrent, Deluge, rQBit and Porla.
- Direct named-client sends now use the same resilient asynchronous send pipeline and retry history, while remaining real manual sends when automation dry-run mode is enabled.
- Build 53 synchronizes persistent green per-client send ticks between article notifications, the main article context menu and automatic sends.
- Capability tests retry one transient status failure, explain API limitations precisely, and fill **Capacity GB** only when a server reports an exact total capacity.
- Client rows now show the configured name plus the underlying client type in secondary text.
- Added independent enable switches for the cleanup age, ratio, inactivity, per-run limit and target-free-space controls. Disabled removal limits retain an internal hard cap of 25 per run.
- Added **Run dry test now** to simulate routing and cleanup against the most recently fetched eligible items without sending or deleting anything.
- Added warnings when users enable cleanup, downloaded-data deletion, unattended removal, high removal counts, or disable cleanup safeguards. Downloaded-data deletion and cleanup with no eligibility filters now always require confirmation.
- Added a configurable 10 GB default reservation for torrents whose links do not declare their size; magnet `xl` values are used when present.
- Added **Test proxy connection** under Network & web, reporting success/failure, elapsed time and the public IP reached through the selected proxy.
- Added a top-right one-click switch between RSS Guard's bundled minimal-light and minimal-dark skins.
- Added the torrent-integration contributor attribution and lawful-use notice to Help > About RSS Guard.
- Added raised, hover and visibly pressed notification-button states, followed by a persistent **Sending…** state while the request is active.
- Successfully sent notification articles are now marked with a green completed band; selection advances to the next unprocessed article.
- Added optional per-client colour swatches to the main article **Send to Torrent Client** context menu and its default-client shortcut.
- Split client-colour placement into independent notification-button, context-menu and settings/automation-list choices.
- Added comprehensive mouse-over explanations across General, Clients and limits, RSS rules, Safe cleanup and Activity.
- Replaced manual internal feed-ID entry with selection from feeds already configured in RSS Guard, plus an explicit all-feeds choice and rule validation.
- Added persistent, non-destructive per-client capability detection for connection, workload, disk space, torrent listing and safe removal, with selected/all-client testing.
- Expanded automation monitoring to Flood, rTorrent/ruTorrent and rQBit. Flood now supplies workload, torrent listing, removal and live disk information when its activity API exposes it; rTorrent supplies workload/listing and safe torrent removal; rQBit supplies workload/listing and removal. Transmission now retries disk checks against its reported download directory.
- Notification torrent-client actions now remain independently usable. Each successful destination receives its own remembered green tick, so the same RSS item can be sent to additional clients without losing the selection.
- Changed the new-install retry default from 15 minutes to 1 minute.
- Replaced the unclear automation weight field with automation priority (1 is highest) and updated priority, priority-biased and balanced routing accordingly.
- Added independent choices for applying each client's colour to notification buttons and to settings/automation lists.
- Added a separate **Torrent automation** settings section with General, Clients and limits, RSS rules, Safe cleanup and Activity pages.
- Added optional event-driven processing of newly fetched torrent RSS entries; the existing manual send controls remain available.
- Added priority, least-busy, most-free-space, round-robin, weighted and balanced routing strategies.
- Added per-client participation, weight, active-download limit, managed-torrent limit, minimum free space, estimated capacity and cleanup permission.
- Added persistent duplicate protection, allocation records, decision history, bounded retry and automation notifications.
- Added live status adapters for qBittorrent, Transmission, Deluge and Porla. Other adapters use connection health and optional estimated capacity.
- Added the `rssguard-auto` ownership marker for supported clients.
- Added opt-in guarded cleanup for marked qBittorrent and Transmission torrents, with completion, ratio, age, inactivity, confirmation, data-deletion and per-run limits.
- Automation and cleanup are disabled by default; dry-run mode is enabled by default.

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
- Changed the dedicated Windows portable workflow to run automatically when its build trigger is updated on `feature/torrent-automation`, as well as manually.
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
