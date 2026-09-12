# Torrent automation

The Neil1454 Windows fork can automatically route torrent links from newly fetched RSS articles to configured torrent clients.

Open **Tools > Settings > Torrent automation**. Automation is disabled by default. Enable **Dry run** first to record and display routing decisions without sending torrents.

## Routing

Available strategies are priority order, least busy, most free space, even round-robin, weighted distribution and balanced. A client must be enabled both in **Torrent clients** and in the automation client table. Per-client hard limits cover active downloads, managed torrent count and minimum free space.

qBittorrent, Transmission, Deluge and Porla expose live workload information. qBittorrent, Transmission and Porla also expose live disk information through their APIs. Other adapters use connection health and an optional configured capacity; this is shown as an estimate.

Rules are checked from top to bottom and can match feed IDs, required/excluded text and a title regular expression. A rule can restrict the destination pool. With no rules, all new articles containing a recognised torrent link are eligible.

The runtime ledger records processed links, routing activity and managed allocations so restarting RSS Guard does not resend the same URL. Failed placement is held and retried with a bounded retry count.

## Cleanup safety

Cleanup is disabled by default and dry-run mode never removes anything. Automatic removal considers only completed torrents carrying the `rssguard-auto` marker. Existing and manually added torrents are not eligible.

Cleanup requires all configured seeding-age, ratio and inactivity conditions. It also has a per-run removal limit and can require confirmation for every removal. **Delete downloaded data** is a separate destructive option.

Safe marked cleanup is currently enabled only for qBittorrent and Transmission. Other adapters remain routing-only until they can reliably identify RSS Guard-managed torrents and remove their data without arbitrary filesystem commands.

Automation runs only while RSS Guard is running and Windows is awake.
