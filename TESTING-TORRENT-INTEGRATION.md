# Torrent integration testing

## Completed in this environment

- Confirmed upstream commit/version and dedicated feature branch.
- Inspected current `MessagesView` selection/context-menu path, `Message` enclosure fields, settings panels, settings encryption convention, and `BaseNetworkAccessManager` proxy/TLS behavior.
- Verified adapter request shapes against current upstream qBittorrent WebUI API wiki, Transmission `docs/rpc-spec.md`, Flood route schemas/handlers, and rTorrent/Flood XML-RPC implementation.
- Added `test_torrentextractor` for enclosure, magnet, direct `.torrent`, false-positive article URL, bulk selection, and duplicate behavior.
- Ran `git diff --check` successfully.

## Build verification

The dedicated GitHub Actions workflow has successfully compiled and packaged the integration with Qt 6, MSVC, and WebEngine on Windows. Each change to `master` triggers a new authoritative portable build. Live client behavior still requires the matrix below because server versions, reverse proxies, paths, and authentication policies differ.

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
- Button colours: every common colour and the system default persist after restart, appear in live notifications and previews, and retain readable text.
- Test all: only enabled clients are checked and the combined dialog identifies every success and failure.
- Notification preview: applying settings uses the configured screen, position, width and opacity; the optional button preview matches enabled clients and priority order.
- Windows palette: unselected client rows do not display forced dark alternate bands.
- rTorrent/ruTorrent: XML-RPC fault response; direct and challenged Basic/Digest authentication; ruTorrent `/plugins/httprpc/action.php` and `/RPC2`; incorrect homepage URL guidance; directory/custom1 commands.
- Deluge: Web password login, automatic configured-daemon connection, version/status test, magnet/URL add, remote download location, and invalid-password error.
- rQBit: server/version detection, optional Basic authentication, magnet/URL add, output folder, and rejected credentials.
- Porla: required JWT authentication, `sys.versions`, magnet add, remote `.torrent` download/base64 submission, save path, preset, and invalid-token response.
- Transmission versions: RPC 16/Transmission 3.00 adds without labels; RPC 17+ adds configured labels.

## Release gate

Do not replace the user's existing RSS Guard portable build until:

1. The latest Qt 6/MSVC workflow build and packaging steps pass.
2. Settings can be added, edited, removed, persisted, and reopened.
3. At least one real instance of each supported client passes connection and send tests.
4. Proxy-on and proxy-off behavior is packet/log verified.
5. A copied `data5` profile opens safely in the separate test directory.
