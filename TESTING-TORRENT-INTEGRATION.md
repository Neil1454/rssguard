# Torrent integration testing

## Completed in this environment

- Confirmed upstream commit/version and dedicated feature branch.
- Inspected current `MessagesView` selection/context-menu path, `Message` enclosure fields, settings panels, settings encryption convention, and `BaseNetworkAccessManager` proxy/TLS behavior.
- Verified adapter request shapes against current upstream qBittorrent WebUI API wiki, Transmission `docs/rpc-spec.md`, Flood route schemas/handlers, and rTorrent/Flood XML-RPC implementation.
- Added `test_torrentextractor` for enclosure, magnet, direct `.torrent`, false-positive article URL, bulk selection, and duplicate behavior.
- Ran `git diff --check` successfully.

## Environment limitation

The provided Linux work environment has no CMake or Qt development installation, so a compile and QtTest run could not be performed here. The Windows workflow is included to perform the authoritative Qt 6/MSVC build using RSS Guard's own current packaging scripts.

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

- qBittorrent: login with Origin/SID; newline bulk add; reverse-proxy subpath.
- Transmission: first-call HTTP 409 retry; Basic auth; duplicate response; custom RPC path.
- Flood: password cookie and token-cookie modes; HTTP 207 partial response.
- rTorrent: XML-RPC fault response; HTTP Basic gateway; directory/custom1 commands.

## Release gate

Do not replace the user's existing RSS Guard portable build until:

1. Qt 6/MSVC compilation and all unit tests pass.
2. Settings can be added, edited, removed, persisted, and reopened.
3. At least one real instance of each supported client passes connection and send tests.
4. Proxy-on and proxy-off behavior is packet/log verified.
5. A copied `data5` profile opens safely in the separate test directory.
