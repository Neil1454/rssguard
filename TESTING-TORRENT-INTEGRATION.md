# Torrent integration testing

## Completed in this environment

- Confirmed upstream commit/version and dedicated feature branch.
- Inspected current `MessagesView` selection/context-menu path, `Message` enclosure fields, settings panels, settings encryption convention, and `BaseNetworkAccessManager` proxy/TLS behavior.
- Verified adapter request shapes against current upstream qBittorrent WebUI API wiki, Transmission `docs/rpc-spec.md`, Flood route schemas/handlers, and rTorrent/Flood XML-RPC implementation.
- Added `test_torrentextractor` for enclosure, magnet, direct `.torrent`, false-positive article URL, bulk selection, and duplicate behavior.
- Added `test_torrentclientconfig` for Transmission, ruTorrent and qBittorrent endpoint suggestions and custom-endpoint preservation.
- Ran `git diff --check` successfully.

## Build verification

The dedicated GitHub Actions workflow builds Build 68 with Qt 6, MSVC, and WebEngine on Windows. A change to the build-trigger file on `feature/torrent-automation` starts the authoritative portable workflow; it can also be started manually. Live client behaviour still requires the matrix below because server versions, reverse proxies, paths, and authentication policies differ.

On a clean profile, confirm the first-launch welcome credits Martin Rotter as the original application creator and Neil Sampson with `Neil1454@yahoo.com` for the torrent-related feature concept, direction and design. Confirm the same separate, non-replacing credits appear under **Help > About application**.

Confirm **File > Export/backup settings** explains that Settings includes every torrent section and encrypted credentials. Restore that backup into a separate portable copy and verify clients, credentials, notification choice, rules, routing, retries, schedules, storage limits, cleanup, retention, protections, processed history, managed allocations and pending work are retained after restart.

Export the safe JSON from **Torrent automation**, inspect that it contains all non-secret client options, usernames, the success-notification preference and the full automation configuration, and verify it contains no password or API token. Import into a clean profile and verify every portable option is restored. Import a v1 file for backward compatibility; change a client's endpoint in the file and confirm its capability results require a fresh test.

Confirm dry-run notification pop-ups use a prominent outcome heading, clean line-separated sections and an explicit **TEST ONLY** footer for sends, waits, duplicates, blocked routes and cleanup outcomes.

For dry-run review, confirm the **Simple** tab shows concise colour-coded outcomes and that **WOULD DELETE** rows name the torrent, client, estimated size and matched cleanup reason. Hover each row and compare it with the full **Activity** record.
When no newly fetched batch exists, open a feed with older articles and confirm the test automatically uses a clearly labelled historical sample without changing sent history, processed markers, queues or clients.

## Required automated checks

```bash
cmake -S . -B build -G Ninja \
  -DCMAKE_BUILD_TYPE=Debug \
  -DENABLE_TESTING=ON \
  -DWEB_ARTICLE_VIEWER_WEBENGINE=OFF \
  -DENABLE_MEDIAPLAYER_LIBMPV=OFF
cmake --build build
ctest --test-dir build --output-on-failure
```

## Live matrix

For each client, test both **Use RSS Guard proxy** enabled and disabled where routing permits:

| Case | Expected result |
|---|---|
| Correct credentials | Test reports success and client/version where supported |
| Wrong credentials | Authentication failure; no credential in log |
| Unreachable host | Host/network error without UI freeze |
| Invalid TLS certificate | Certificate error; verification is not bypassed |
| One magnet selected | Exactly one torrent is added |
| Multiple selected articles | All usable unique links are sent |
| Duplicate links | One copy sent and skipped count reported |
| Mixed valid/invalid articles | Valid links sent and no-link article count reported |
| No torrent links | No request; explanatory dialog shown |
| Optional save/category/tags | Client receives supported optional fields |

Client-specific checks:

- qBittorrent: matching Origin/Referer; special-character credentials; old HTTP 200/`SID` and 5.2 HTTP 204/`QBT_SID_...` login; newline bulk add; successful HTTP 2xx add responses including hosted HTTP 202; reverse-proxy subpath.
- Transmission: first-call HTTP 409 retry; pre-emptive Basic and challenged Basic/Digest auth; duplicate response; custom RPC path; Transmission 3.00 behind an HTTPS reverse proxy.
- Flood: password cookie and token-cookie modes; HTTP 200 accepted, HTTP 202 queued, HTTP 207 partial, and ambiguous HTTP 500 behavior.
- Notifications: the first row is selected automatically; rows are visibly selectable; Ctrl/Shift multi-selection works; buttons follow all selected rows, disable when no selected row has a torrent link, and send extracted links to the named client.
- Notification layout: client buttons wrap into two columns, use equal available width, show full ordinary instance names, and remain usable with odd and even client counts.
- Persistent notifications: when enabled, new-article notifications ignore the timeout and right-click dismissal but still close from their close button; ordinary notifications retain normal timing.
- Confirmations: success suppression persists and can be restored in settings; failures are never suppressed.
- Client editor: changing the confirmation checkbox enables Apply; each client type changes the URL example and greys unsupported fields.
- Client state/order: disabling a client preserves it but removes its menu and notification button; priorities reorder both locations with 1 first.
- Disabled-list layout: enabled entries are numbered contiguously; disabled entries are greyed, unnumbered, and placed at the bottom.
- Client colours: every common colour and the system default persist after restart; notification-button, article-menu and settings-list placement can be enabled independently.
- Notification send feedback: buttons visibly depress, show an in-progress label, and fully successful article rows turn green; failures and partial successes do not.
- Send history: a successful destination shows a green tick in both the notification and main article context menu, persists after restart, and updates both locations regardless of where the send began.
- Capability reliability: configured global/per-client status retries and request timeouts are honoured; transfer-rate capability is ticked only when values are returned; exact server-reported total capacity populates Capacity GB; unavailable filesystem totals retain manual capacity and display a reason; removal support is not mistaken for tested delete permission.
- Cleanup integrity: after each successful removal, the same torrent cannot be selected again and estimated managed capacity falls by the removed allocation; a failed removal tries another eligible client or enters the bounded retry queue.
- URL assistance: common Transmission Web UI, ruTorrent homepage and qBittorrent `/api/v2` entries offer a corrected endpoint; custom endpoints can be retained.
- Client identity: automation rows display the configured name, configured colour and underlying client type.
- Clients and limits layout: each client appears once as a readable two-line name/type item; colour swatches and text do not overlap at Windows display scaling; row height follows the active font; limit columns size to their contents and remain horizontally scrollable on narrower windows.
- Flexible units: switch Clients and limits storage display/input between MiB, GiB and TiB; verify values convert without changing stored bytes; enter explicit suffixes such as 2048 MiB, 750 GiB and 1.8 TiB; enter rates such as 500 MiB/s and 1.2 GiB/s; invalid text must be rejected rather than saved as zero.
- Cleanup switches: each of the five cleanup controls can be disabled independently; enabled eligibility controls are ANDed; disabled removal limit still stops at the internal 25-item cap.
- Manual dry test: evaluates the latest fetched items including already-processed ones; names the matched/unmatched rule; reports every client's status/restriction and selected destination; lists every torrent that would be removed and every protected/skipped torrent with its exact settings-based reason; simulates enough removals to reach the configured target; and performs no add, remove, data-delete, queue, grace-mark, processed-marker, reconciliation or client-health-state write.
- Unknown-size reservation: magnet `xl` is honoured and links without a declared size reserve the configured fallback.
- Proxy test: correct, incorrect, timed-out and authenticated SOCKS5/HTTP proxy settings report clear results without exposing credentials.
- Theme toggle: the top-right control switches immediately between bundled minimal-light and minimal-dark skins and persists the selection.
- Test all: only enabled clients are checked and the combined dialog identifies every success and failure.
- Notification preview: applying settings uses the configured screen, position, width and opacity; the optional button preview matches enabled clients and priority order.
- Windows palette: unselected client rows do not display forced dark alternate bands.
- rTorrent/ruTorrent: XML-RPC fault response; direct and challenged Basic/Digest authentication; ruTorrent `/plugins/httprpc/action.php` and `/RPC2`; incorrect homepage URL guidance; directory/custom1 commands.
- Deluge: Web password login, automatic configured-daemon connection, version/status test, magnet/URL add, remote download location, and invalid-password error.
- rQBit: server/version detection, optional Basic authentication, magnet/URL add, output folder, and rejected credentials.
- Porla: required JWT authentication, `sys.versions`, magnet add, remote `.torrent` download/base64 submission, save path, preset, and invalid-token response.
- Transmission versions: RPC 16/Transmission 3.00 adds without labels; RPC 17+ adds configured labels.
- Approval routing: **Process automatically** is present in both the notification and article context menu; blue identifies the recommendation, green a suitable alternative, amber a manually overridable soft limit and red a blocked destination; each state includes explanatory text.
- Balanced load: priority, active/queued downloads, aggregate download rate and free-space ratio affect the recommendation as configured.
- Soft-limit override: an amber active-download, aggregate-rate, managed-count or reserve warning can be overridden in approval mode; red unavailable, disallowed and physically insufficient-space states cannot.
- Timeouts and retry: global and per-client request timeouts apply; retry count and exponential backoff persist; a temporary preferred-client failure considers the next healthy client.
- Retry restart: close RSS Guard with an item queued, reopen it and confirm the pending retry remains scheduled without duplicating the item.
- Ambiguous magnet timeout: simulate a lost response after acceptance and confirm RSS Guard finds the info hash on the original client before failover.
- Ambiguous direct torrent URL: simulate a lost response and confirm RSS Guard reports an uncertain outcome instead of automatically risking a duplicate.
- Percentage capacity: the fixed free-space reserve and target-free percentage use the larger resulting target; fallback configured capacity is used only when live totals are unavailable.
- Upload-aware cleanup: eligible managed torrents are ordered oldest-first; a torrent at or above the configured upload threshold is skipped until another cleanup pass; unknown speed protection behaves conservatively.
- Cleanup batching: the requested recovery target rounds upward by the configured percentage of total/fallback capacity.
- Direct/manual independence: a named-client button performs a real manual send even while Torrent automation is configured for dry-run mode.
- Reconciliation: change progress and remove a managed torrent outside RSS Guard; the next reachable status pass updates remaining-byte reservations and removes the stale ledger entry without adopting unrelated torrents.
- Storage source: verify each client row accurately distinguishes live space, all-torrent estimate, conservative estimate and unknown; rTorrent/rQBit fallback budgets subtract every listed torrent and pending managed reservations.
- Duplicate policy: an unattended magnet already present on any reachable client is skipped by info hash; a direct named-client action can intentionally create another copy.
- Persistent queue controls: restart with scheduled work, then verify its reason/time and retry-now, choose-client and cancel actions without duplicating completed sends.
- Circuit breaker: fail one client for the configured number of checks, confirm other clients continue, then confirm the sidelined client is reused only after the configured consecutive recovery successes.
- Operating windows: outside routing/cleanup hours, verify work is deferred to the next local opening and survives restart; direct named-client sending remains available.
- Protected cleanup: verify exact tag/label and case-insensitive tracker exclusions, recent-upload retention and minimum-copy protection across reachable clients.
- Cleanup grace: verify the first pass only marks a candidate, the later pass rechecks current conditions, and a newly protected or active torrent is not removed.
- Configuration transfer: export JSON, confirm no username/password/token fields are present, import it into a clean profile, and verify rules/layout while entering credentials separately.
- Rule dry test: refresh feeds, run **Test rules against latest items**, and confirm Activity identifies the matching rule without an add/remove/delete request.
- Rule size and order: configure minimum/maximum sizes, including two torrent links in one article, and verify each extracted torrent uses the first matching enabled rule; duplicate and reorder rules and confirm the visible order persists.
- Readiness audit: verify blocking issues, warnings and passed checks for untested/stale clients, missing capacity, invalid imported rules, queued work and dangerous cleanup combinations.
- Queue bulk controls: retry all queued items through normal limits, cancel all without changing existing torrents, and confirm old scheduled timers cannot reinsert cancelled work.
- Activity reporting: export history and queued summaries to JSON, confirm no stored credentials/tokens appear, then clear history without clearing allocations, processed-link protection or queued work.

## Release gate

Do not replace the user's existing RSS Guard portable build until:

1. The latest Qt 6/MSVC workflow build and packaging steps pass.
2. Settings can be added, edited, removed, persisted, and reopened.
3. At least one real instance of each supported client passes connection and send tests.
4. Proxy-on and proxy-off behavior is packet/log verified.
5. A copied `data5` profile opens safely in the separate test directory.

## Build 65 setup-wizard checks

- Open **Tools > Settings > Torrent automation** and select **Start guided setup wizard**.
- Confirm Back/Next navigation covers safety, routing, client limits, rules, retries, maintenance, scheduling, cleanup and cleanup protections.
- Change representative values, finish the wizard, and confirm the matching controls change on the main settings page.
- Cancel a second wizard run after editing values and rules; confirm none of those cancelled changes remain.
- Confirm finishing does not itself send a torrent, run cleanup or save until **Apply** or **OK** is selected.

## Build 66 walkthrough and presentation checks

- Confirm the wizard remains readable at normal Windows scaling and at 125%, 150% and 200% scaling, in both light and dark themes.
- Confirm every page has a useful **Explain this page** description and every individual editor has a tooltip.
- Change the routing strategy and confirm the visible plain-English explanation follows the selected strategy.
- Toggle retries, reconciliation, circuit breaker, schedules and cleanup safeguards; confirm only the relevant follow-up editors enable or disable.
- Select each Clients and limits column and confirm the help card explains that exact setting.
- Confirm the Dry-run and cleanup banners change immediately and clearly distinguish safe test mode, live routing, cleanup-only removal and permanent data deletion.
- Confirm Cancel restores rule edits and leaves all other main-page settings unchanged.

## Build 67 retention and storage checks

- Enable maximum retention with a short test value and confirm dry run identifies expired completed managed torrents even when free space is healthy.
- Confirm firm deadline mode ignores ratio, inactivity and upload-activity postponements but still honours protected tags, protected tracker text and minimum-copy protection.
- Confirm the cleanup maintenance window and maximum-removals limit apply to time-based cleanup.
- Confirm **Delete downloaded data** off removes only the torrent job and never reports recovered disk space; with it on, confirm the client is asked to delete data.
- On a client without live free-space reporting, confirm the fallback estimate subtracts manually added torrents as well as RSS Guard-managed torrents and pending reservations.
- Complete the new ten-stage wizard and confirm maximum-retention choices copy correctly to the main settings page.
- On the General tab, review each Quick Set preset and confirm its exact behaviour, preserved settings and deletion risk are shown before Apply is available.
- Apply each preset and confirm Dry run is always on, client rows and RSS rules are unchanged, and the documented cleanup/retention values are filled correctly.
- Repeat from the wizard's optional Quick Set page; confirm later pages reflect the preset, remain editable, and cancelling the wizard leaves the main settings unchanged.
