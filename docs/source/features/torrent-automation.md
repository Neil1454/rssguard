# Torrent automation

The Neil1454 Windows fork can automatically route torrent links from newly fetched RSS articles to configured torrent clients.

Open **Tools > Settings > Torrent automation**. Automation is disabled by default. Enable **Dry run** first to record and display routing decisions without sending torrents.

The separate **Exclusive mode** tab provides a send-only wake, collect and sleep workflow. It sleeps for the configured interval, refreshes all feeds, and accepts only torrent releases carrying a trustworthy publication time inside the configured freshness allowance. Older or undated entries are ignored completely. It then monitors at the configured refresh interval until either the target batch size or time limit is reached, distributes the fresh batch across enabled clients using their configured priorities, and sleeps again. While exclusive mode is enabled, all other unattended torrent routing, retries, reconciliation, retention, cleanup and deletion are frozen; manual sends still work. The tab shows the current state, collected count and next wake time.

For a guided first-time setup, select **Start guided setup wizard**. The step-by-step wizard covers every automation section in a safe order: basic safety, routing and size estimates, per-client limits, RSS matching rules, retries, storage and health maintenance, scheduling, storage-pressure cleanup, maximum retention, and cleanup protections. Each page explains the setting and gives examples where useful. Nothing is copied to the settings page until **Finish** is selected, and the main Settings window still requires **Apply** or **OK** before the choices are saved.

The wizard uses plain-language questions, live Dry-run and cleanup safety warnings, and conditional follow-up fields that are available only when their parent option applies. Select **Explain this page** for a fuller description of the current section. On **Clients and limits**, select any cell to see a clear explanation of that column below the table. Hovering an individual control provides its setting-specific tooltip.

**Quick Set** is available on the General tab and as an optional early wizard page. It provides four common starting points: Safety-first test only, Balanced protected automation, 30-day automatic rotation and Long-term seeding. Selecting a preset first shows exactly what it sets, what client/rule information it preserves and whether files could eventually be deleted. Presets never overwrite configured clients, per-client storage values, cleanup permission, protected names or RSS rules, and every preset turns Dry run on.

## Routing

Available strategies are priority order, least busy, most free space, even round-robin, priority-biased distribution and balanced. A client must be enabled both in **Torrent clients** and in the automation client table. Per-client controls cover active downloads, aggregate download rate, managed torrent count, minimum free space, target free-space percentage, request timeout and retry count. Automation priority 1 is highest; it is separate from the notification-button order. Balanced routing scores the free-space ratio, active and queued downloads, aggregate download rate and configured priority.

Use **Process automatically** on a new-article notification or in the main article context menu for approval-based routing. A destination assessment uses blue for the recommended client, green for another suitable client, amber for a soft limit that can be overridden manually, and red for an unavailable or disallowed client. The state and reason are always written beside the colour. Direct named-client actions remain available and bypass soft automation limits.

qBittorrent, Transmission, Flood, rTorrent/ruTorrent, Deluge, rQBit and Porla expose live workload and torrent-list information. qBittorrent, Transmission and Porla expose live disk information directly; Flood supplies it from its activity stream when the server makes mount information available. rTorrent and rQBit use the optional configured-capacity estimate because their portable APIs do not expose filesystem free space.

Removal API support is capability-reported rather than assumed. The test is deliberately non-destructive, so it cannot prove that a particular server account has permission to remove a torrent. rTorrent can remove a torrent entry through XML-RPC, but generic rTorrent XML-RPC cannot prove that downloaded files were deleted; therefore ``delete downloaded data`` is rejected for that adapter with an explanation. rQBit removal is unavailable when its server was started in read-only mode.

Rules are checked from top to bottom and can match selected feeds, required/excluded text and a title regular expression. The rule editor lists feeds already added to RSS Guard and stores their internal IDs automatically; users do not enter feed URLs or IDs. A rule can restrict the destination pool. With no rules, all new articles containing a recognised torrent link are eligible.

Rules can also set minimum and maximum torrent sizes, be duplicated, and be moved up or down. Size limits are evaluated separately for every extracted torrent link; links without a declared size use the configured assumed size. The first enabled matching rule wins.

**Test selected client** and **Test all clients** perform non-destructive capability discovery. The saved result shows connection/authentication, live workload, free-space, torrent-list and safe-removal availability. A test never adds or deletes a torrent, and running it again replaces the previous result.

Tests honour the configured global or per-client status retry allowance, delay, request timeout and optional backoff. When the API reports an exact total storage capacity, the test fills **Capacity GB** automatically. Existing manual capacity remains unchanged when the API reports only free space or no filesystem information. rTorrent and rQBit normally require manual capacity because their portable APIs do not expose filesystem totals. A removal-API tick means the adapter supports removal and its listing call succeeded; it is not proof of server-side removal permission.

The **Maintenance** tab can periodically reconcile RSS Guard's persistent managed ledger against current torrent lists. Reconciliation updates actual sizes and remaining bytes, removes records no longer present on a reachable client, and recognises server-side ownership markers or hashes already in the ledger. **Reserve space still needed** adds unfinished managed bytes to a live-space routing reserve. When live free space is unavailable, the configured capacity is treated as a torrent-storage budget and RSS Guard conservatively subtracts the full size of every torrent returned by the client—including torrents added outside RSS Guard—plus pending managed reservations. Deduct space used by unrelated files before entering the fallback capacity. The storage-source column states whether a client is using live data, an all-torrent estimate, a conservative ledger estimate or no usable storage figure.

Unattended magnet sends can be checked by info hash across every reachable configured client. If the torrent already exists, automation records the decision and does not create another copy. A direct named-client action is treated as an intentional override.

Repeatedly failing clients can be placed behind a circuit breaker. After the configured failure count, the client is skipped for a cooldown period while other clients remain usable. When the cooldown ends, the client must pass the configured number of consecutive recovery checks before it receives unattended work again.

Each client row displays the configured name and its underlying client type. **Run dry test now** uses live client status and the most recently fetched eligible RSS items to produce short **DRY RUN** routing and cleanup decisions in Activity. It never adds or removes a torrent. Refresh feeds first if no recent items are available.

Every control, table heading and capability indicator has mouse-over help. Client colours can be shown independently on notification buttons, article context menus and settings/automation lists. Notification actions have clear pressed/in-progress states, and successfully processed rows receive a green completion band.

Every successful client destination receives its own persistent green tick. The notification buttons and the main article **Send to torrent client** menu use the same saved history, including sends initiated by automation, so both locations remain synchronized.

The runtime ledger records processed links, routing activity, managed allocations and pending retries so restarting RSS Guard does not resend the same URL or lose queued work. Retry timing, attempt count, exponential backoff and request timeout are configurable globally, with per-client overrides. A definite transient failure can fail over to the next suitable client. After an ambiguous timeout, a magnet's info hash is checked on the original client before failover; an ambiguous direct `.torrent` URL is not automatically resent because its outcome cannot be verified safely.

The **Activity** tab displays queued items with the reason and next attempt in local time. A selected entry can be retried immediately, deliberately sent to a chosen enabled client, or cancelled without changing any torrent already on a server. Optional routing hours defer unattended work to the next opening of the configured local-time window.

All queued entries can be retried or cancelled together after confirmation. Activity can be exported as JSON for diagnosis and cleared separately. Exported reports can contain titles, torrent URLs and client identifiers but never stored passwords or API tokens. Clearing Activity does not erase duplicate protection, managed allocations or queued work.

**Check readiness for live automation** audits participating clients, saved capability tests and their age, storage fallbacks, RSS rules, queued work and cleanup safeguards. It reports blocking issues separately from warnings and passed checks; it does not silently change the configuration.

**Export torrent configuration** writes every portable torrent option to JSON: client layout, usernames, non-secret connection fields, destinations, colours, the success-notification choice, limits, rules, routing, retries, schedules, storage, cleanup, retention and protections. Passwords and API tokens are not written. Import replaces the torrent configuration after confirmation, retains credentials already stored locally for matching client IDs, and requires a fresh capability test when a client's type or endpoint changes.

For a complete transfer, **File > Export/backup settings** copies the whole application settings file. That backup includes every torrent section, encrypted passwords and tokens, send history and automation runtime state. Treat it as sensitive. **File > Import/restore settings** restores those sections together and applies them after restart.

## Cleanup safety

Cleanup is disabled by default and dry-run mode never removes anything. Automatic removal considers only completed torrents carrying the `rssguard-auto` marker. Existing and manually added torrents are not eligible.

The seeding-age, ratio, inactivity, per-run removal limit and target-free-space controls can each be enabled or disabled. Every enabled eligibility condition must pass; disabled conditions are ignored. Disabling the chosen per-run limit still leaves an internal hard maximum of 25 removals. **Delete downloaded data** is a separate destructive option.

Potentially dangerous changes display warnings when selected. Keep **Ask before every removal** and **Dry run** enabled while validating a configuration. Deleting downloaded data always requires confirmation, as does cleanup with every eligibility filter disabled. Unknown torrent sizes reserve 10 GB by default (configurable); an exact `xl` value in a magnet link takes precedence.

Eligible completed automation-managed torrents can be considered by a smart score combining age, size and ratio, or by strict oldest-first order. Optional upload protection skips a candidate whose current upload rate is at or above the configured threshold until a later cleanup pass; unknown upload speeds can also be protected conservatively. A recent-upload window retains torrents observed uploading even if the current sample later falls quiet.

Comma-separated protected tags/labels and tracker substrings exclude important torrents. Optional minimum-copy protection counts completed copies across all reachable configured clients and refuses to remove a copy when doing so would fall below the chosen minimum. If a client is unreachable, its unknown copy cannot weaken this safeguard.

Two-stage cleanup can first mark an eligible candidate, wait for a configurable grace period, then fetch current status and recheck every safeguard. A resumed download, renewed upload, protected tag/tracker, insufficient copy count or other failed eligibility condition clears the candidate. Cleanup can also be restricted to a separate local-time maintenance window. The free-space target can combine a fixed minimum, a percentage of detected/configured capacity, outstanding download reservations and a percentage-based cleanup batch.

The cleanup checkbox for a client remains disabled until a capability test confirms listing and safe-removal support. Actual cleanup still requires a completed torrent to carry RSS Guard's automation marker, so an adapter that cannot prove ownership remains effectively routing-only.

### Maximum retention

Maximum-retention cleanup is separate from storage-pressure cleanup. In live mode, RSS Guard checks approximately every five minutes while the application is running and considers a completed managed torrent due after the configured number of hours, even when the destination has plenty of free space. In Dry run, use **Run dry test now** to preview the same expiry decisions without repeated background reports. Age is measured from completion; if the client does not expose that value, its reported added time is used.

With **Treat the maximum time as a firm deadline** enabled, ratio, inactivity, current-upload and recent-upload protections no longer postpone an expired torrent. Protected tags, protected tracker terms and minimum-copy protection remain absolute exclusions. The cleanup maintenance window, maximum-removals limit and confirmation setting still apply. When **Delete downloaded data** is off, only the torrent job is removed and no recovered disk space is claimed; when it is on, downloaded files are permanently deleted too.

Automation runs only while RSS Guard is running and Windows is awake.

## Theme and proxy checks

RSS Guard already includes minimal-light and minimal-dark skins. The fork adds a button at the top-right of the main window to switch between them immediately. Full skin selection remains available under **Tools > Settings > User interface**.

Under **Tools > Settings > Network & web > Network proxy**, **Test proxy connection** uses the values currently displayed to make a timed public-IP request. The result identifies success, elapsed time and the public IP, or provides the connection error. Proxy passwords are never displayed.
