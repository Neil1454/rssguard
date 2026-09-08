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

- qBittorrent: matching Origin/Referer; special-character credentials; old HTTP 200/`SID` and 5.2 HTTP 204/`QBT_SID_...` login; newline bulk add; reverse-proxy subpath.
- Transmission: first-call HTTP 409 retry; Basic auth; duplicate response; custom RPC path.
- Flood: password cookie and token-cookie modes; HTTP 200 accepted, HTTP 202 queued, HTTP 207 partial, and ambiguous HTTP 500 behavior.
- Notifications: buttons appear only for configured clients, follow the selected notification article, disable when no torrent link exists, and send to the named client.
- Confirmations: success suppression persists and can be restored in settings; failures are never suppressed.
- Windows palette: unselected client rows do not display forced dark alternate bands.
- rTorrent: XML-RPC fault response; HTTP Basic gateway; directory/custom1 commands.

## Release gate

Do not replace the user's existing RSS Guard portable build until:

1. The latest Qt 6/MSVC workflow build and packaging steps pass.
2. Settings can be added, edited, removed, persisted, and reopened.
3. At least one real instance of each supported client passes connection and send tests.
4. Proxy-on and proxy-off behavior is packet/log verified.
5. A copied `data5` profile opens safely in the separate test directory.
