Notifications
=============
RSS Guard allows you to customize desktop notifications. There are a number of events that can be configured:
* New unread articles fetched
* Fetching of articles started
* OAuth login tokens refreshed
* A new RSS Guard version is available
* etc.

<img alt="alt-img" src="images/notif.png" width="600px">

Your notifications can also play custom sounds. You can place them in your [user data](userdata) folder and reference them via the special [placeholder](userdata.md#data-placeholder). WAV files are supported directly; other formats depend on the multimedia backend available on your operating system.

In the Neil1454 torrent-integration fork, a new-article notification shows **Process automatically** plus a two-column button grid for configured torrent clients when the selected notification article contains a magnet or recognised torrent link. The automatic action assesses workload and capacity and asks for the destination using a labelled blue/green/amber/red suitability display; the named buttons send directly. Buttons have hover, pressed and in-progress feedback. A completed article receives a green band, and each successful destination gets a persistent green tick shared with the main-window **Send to torrent client** menu. Client colours and the persistent-until-dismissed behaviour are optional. See [Torrent clients](torrent-clients.md).
