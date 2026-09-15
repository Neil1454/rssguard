# Torrent automation

The Neil1454 Windows fork can automatically route torrent links from newly fetched RSS articles to configured torrent clients.

Open **Tools > Settings > Torrent automation**. Automation is disabled by default. Enable **Dry run** first to record and display routing decisions without sending torrents.

## Routing

Available strategies are priority order, least busy, most free space, even round-robin, priority-biased distribution and balanced. A client must be enabled both in **Torrent clients** and in the automation client table. Per-client controls cover active downloads, aggregate download rate, managed torrent count, minimum free space, target free-space percentage, request timeout and retry count. Automation priority 1 is highest; it is separate from the notification-button order. Balanced routing scores the free-space ratio, active and queued downloads, aggregate download rate and configured priority.

Use **Process automatically** on a new-article notification or in the main article context menu for approval-based routing. A destination assessment uses blue for the recommended client, green for another suitable client, amber for a soft limit that can be overridden manually, and red for an unavailable or disallowed client. The state and reason are always written beside the colour. Direct named-client actions remain available and bypass soft automation limits.

qBittorrent, Transmission, Flood, rTorrent/ruTorrent, Deluge, rQBit and Porla expose live workload and torrent-list information. qBittorrent, Transmission and Porla expose live disk information directly; Flood supplies it from its activity stream when the server makes mount information available. rTorrent and rQBit use the optional configured-capacity estimate because their portable APIs do not expose filesystem free space.

Removal API support is capability-reported rather than assumed. The test is deliberately non-destructive, so it cannot prove that a particular server account has permission to remove a torrent. rTorrent can remove a torrent entry through XML-RPC, but generic rTorrent XML-RPC cannot prove that downloaded files were deleted; therefore ``delete downloaded data`` is rejected for that adapter with an explanation. rQBit removal is unavailable when its server was started in read-only mode.

Rules are checked from top to bottom and can match selected feeds, required/excluded text and a title regular expression. The rule editor lists feeds already added to RSS Guard and stores their internal IDs automatically; users do not enter feed URLs or IDs. A rule can restrict the destination pool. With no rules, all new articles containing a recognised torrent link are eligible.

**Test selected client** and **Test all clients** perform non-destructive capability discovery. The saved result shows connection/authentication, live workload, free-space, torrent-list and safe-removal availability. A test never adds or deletes a torrent, and running it again replaces the previous result.

Tests honour the configured global or per-client status retry allowance, delay, request timeout and optional backoff. When the API reports an exact total storage capacity, the test fills **Capacity GB** automatically. Existing manual capacity remains unchanged when the API reports only free space or no filesystem information. rTorrent and rQBit normally require manual capacity because their portable APIs do not expose filesystem totals. A removal-API tick means the adapter supports removal and its listing call succeeded; it is not proof of server-side removal permission.

Each client row displays the configured name and its underlying client type. **Run dry test now** uses live client status and the most recently fetched eligible RSS items to produce short **DRY RUN** routing and cleanup decisions in Activity. It never adds or removes a torrent. Refresh feeds first if no recent items are available.

Every control, table heading and capability indicator has mouse-over help. Client colours can be shown independently on notification buttons, article context menus and settings/automation lists. Notification actions have clear pressed/in-progress states, and successfully processed rows receive a green completion band.

Every successful client destination receives its own persistent green tick. The notification buttons and the main article **Send to torrent client** menu use the same saved history, including sends initiated by automation, so both locations remain synchronized.

The runtime ledger records processed links, routing activity, managed allocations and pending retries so restarting RSS Guard does not resend the same URL or lose queued work. Retry timing, attempt count, exponential backoff and request timeout are configurable globally, with per-client overrides. A definite transient failure can fail over to the next suitable client. After an ambiguous timeout, a magnet's info hash is checked on the original client before failover; an ambiguous direct `.torrent` URL is not automatically resent because its outcome cannot be verified safely.

## Cleanup safety

Cleanup is disabled by default and dry-run mode never removes anything. Automatic removal considers only completed torrents carrying the `rssguard-auto` marker. Existing and manually added torrents are not eligible.

The seeding-age, ratio, inactivity, per-run removal limit and target-free-space controls can each be enabled or disabled. Every enabled eligibility condition must pass; disabled conditions are ignored. Disabling the chosen per-run limit still leaves an internal hard maximum of 25 removals. **Delete downloaded data** is a separate destructive option.

Potentially dangerous changes display warnings when selected. Keep **Ask before every removal** and **Dry run** enabled while validating a configuration. Deleting downloaded data always requires confirmation, as does cleanup with every eligibility filter disabled. Unknown torrent sizes reserve 10 GB by default (configurable); an exact `xl` value in a magnet link takes precedence.

Eligible completed automation-managed torrents are considered oldest-first. Optional upload protection skips a candidate whose current upload rate is at or above the configured threshold until a later cleanup pass; unknown upload speeds can also be protected conservatively. The free-space target can combine a fixed minimum, a percentage of detected/configured capacity, and a percentage-based cleanup batch.

The cleanup checkbox for a client remains disabled until a capability test confirms listing and safe-removal support. Actual cleanup still requires a completed torrent to carry RSS Guard's automation marker, so an adapter that cannot prove ownership remains effectively routing-only.

Automation runs only while RSS Guard is running and Windows is awake.

## Theme and proxy checks

RSS Guard already includes minimal-light and minimal-dark skins. The fork adds a button at the top-right of the main window to switch between them immediately. Full skin selection remains available under **Tools > Settings > User interface**.

Under **Tools > Settings > Network & web > Network proxy**, **Test proxy connection** uses the values currently displayed to make a timed public-IP request. The result identifies success, elapsed time and the public IP, or provides the connection error. Proxy passwords are never displayed.
