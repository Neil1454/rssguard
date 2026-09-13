# Torrent automation

The Neil1454 Windows fork can automatically route torrent links from newly fetched RSS articles to configured torrent clients.

Open **Tools > Settings > Torrent automation**. Automation is disabled by default. Enable **Dry run** first to record and display routing decisions without sending torrents.

## Routing

Available strategies are priority order, least busy, most free space, even round-robin, priority-biased distribution and balanced. A client must be enabled both in **Torrent clients** and in the automation client table. Per-client hard limits cover active downloads, managed torrent count and minimum free space. Automation priority 1 is highest; it is separate from the notification-button order.

qBittorrent, Transmission, Deluge and Porla expose live workload information. qBittorrent, Transmission and Porla also expose live disk information through their APIs. Other adapters use connection health and an optional configured capacity; this is shown as an estimate.

Rules are checked from top to bottom and can match selected feeds, required/excluded text and a title regular expression. The rule editor lists feeds already added to RSS Guard and stores their internal IDs automatically; users do not enter feed URLs or IDs. A rule can restrict the destination pool. With no rules, all new articles containing a recognised torrent link are eligible.

**Test selected client** and **Test all clients** perform non-destructive capability discovery. The saved result shows connection/authentication, live workload, free-space, torrent-list and safe-removal availability. A test never adds or deletes a torrent, and running it again replaces the previous result.

Every control, table heading and capability indicator has mouse-over help. Client colours can be shown independently on notification buttons, article context menus and settings/automation lists. Notification actions have clear pressed/in-progress states, and successfully processed rows receive a green completion band.

The runtime ledger records processed links, routing activity and managed allocations so restarting RSS Guard does not resend the same URL. Failed placement is held and retried with a bounded retry count.

## Cleanup safety

Cleanup is disabled by default and dry-run mode never removes anything. Automatic removal considers only completed torrents carrying the `rssguard-auto` marker. Existing and manually added torrents are not eligible.

Cleanup requires all configured seeding-age, ratio and inactivity conditions. It also has a per-run removal limit and can require confirmation for every removal. **Delete downloaded data** is a separate destructive option.

The cleanup checkbox for a client remains disabled until a capability test confirms listing and safe-removal support. Actual cleanup still requires a completed torrent to carry RSS Guard's automation marker, so an adapter that cannot prove ownership remains effectively routing-only.

Automation runs only while RSS Guard is running and Windows is awake.
