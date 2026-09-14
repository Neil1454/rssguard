# Security policy

## Supported fork build

Security and torrent-integration fixes are applied to the latest Windows test build on the `feature/torrent-automation` branch. Older fork builds are retained only as development history and should not be treated as supported releases.

## Reporting a security problem

Do not publish passwords, API tokens, authentication cookies, private tracker URLs, private feed contents or seedbox addresses in an issue, screenshot or log.

When reporting a potential vulnerability, provide only the information needed to reproduce it:

- RSS Guard fork build and commit;
- Windows version;
- affected torrent-client type and version;
- whether a reverse proxy or RSS Guard proxy is involved;
- redacted request/error details;
- expected and actual behaviour.

If a report would require exposing credentials or private service information, contact the repository owner privately before sharing those details. Revoke and replace any secret that has already been posted publicly.

General vulnerabilities in unmodified RSS Guard should also be checked against the [upstream RSS Guard project](https://github.com/martinrotter/rssguard).

## Security behaviour

- HTTPS certificate verification remains enabled.
- Client secrets use RSS Guard's encrypted-settings convention.
- Secrets must never be included in activity messages or support reports.
- Authentication and invalid-configuration failures are not repeatedly retried.
- Ambiguous magnet submissions are verified by info hash before failover.
- Ambiguous direct `.torrent` URL submissions are not resent automatically when doing so could create a duplicate.

## Automatic cleanup warning

Torrent cleanup is destructive and is disabled by default. Dry-run mode performs no send, remove or data-deletion operation.

Live cleanup is restricted to completed torrents carrying RSS Guard's automation marker. Deleting downloaded data is separately disabled by default and requires confirmation. Users should validate rules in dry-run mode, keep conservative limits and maintain backups of important data.
