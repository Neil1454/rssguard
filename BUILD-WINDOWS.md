# Building the Windows portable test package

The target matching the normal modern RSS Guard portable package is **Qt 6 + WebEngine + Windows 10/11 x64**. The build is independent of any installed/portable RSS Guard copy.

## Recommended: GitHub Actions

1. Open the fork's [Torrent integration Windows portable workflow](https://github.com/Neil1454/rssguard/actions/workflows/torrent-windows-portable.yml).
2. Open the newest successful run for `master`.
3. Download the `RSSGuard-Torrent-Test` artifact.
4. Extract the downloaded ZIP, then extract the `.7z` inside it into a new folder.
5. Run `rssguard.exe`. Do not extract over an existing RSS Guard folder.

The workflow runs automatically after a push to `master` and can also be started manually. It uses the current upstream `build-windows.ps1` and `package-windows.ps1`, including recursive submodules, Qt deployment, OpenSSL, WebEngine, plugins, article extractor, ICU, and libmpv. The workflow creates a temporary local package-version tag because the fork does not carry upstream tags. The resulting test binaries are unsigned; Windows may show a SmartScreen warning.

Only the Windows 10/11 x64 Qt 6 WebEngine portable package is required for this fork. No custom installer, Linux package, or macOS package is currently produced.

## Local Windows build

Requirements:

- Windows 10/11 x64
- Visual Studio 2022 with **Desktop development with C++**
- Git for Windows, Python 3.13, Ninja, and PowerShell
- Enough free disk space for Qt/WebEngine and dependencies
- A GitHub token in `GITHUB_TOKEN` because the upstream script queries release assets

From **Developer PowerShell for VS 2022**:

```powershell
git clone --recursive https://github.com/<your-account>/rssguard.git RSSGuard-Torrent-Source
Set-Location RSSGuard-Torrent-Source
git switch master
$env:GITHUB_TOKEN = "<your GitHub token>"
$env:FEEDLY_CLIENT_ID = ""
$env:FEEDLY_CLIENT_SECRET = ""
.\resources\scripts\github-actions\build-windows.ps1 "windows-2022" "OFF" "ON"
.\resources\scripts\github-actions\package-windows.ps1 "windows-2022" "OFF" "ON"
```

The portable archive is written under `rssguard-build\` with a name ending in `-web-qt6-win10.7z`. The unpacked application is also available at `rssguard-build\app\`.

## Safe first launch and profile migration

1. Back up the existing RSS Guard folder and profile.
2. Extract the test build to a completely separate writable folder.
3. Launch it once and check **Help > About application > Resources** to confirm its active data path.
4. Close both RSS Guard instances.
5. If the existing installation is portable, copy its `data5` directory into the test folder beside `rssguard.exe`.
6. Start only the test build and confirm feeds/settings before configuring torrent clients.

Important: if `%LOCALAPPDATA%\RSS Guard 5\data\config\config.ini` exists, RSS Guard may select that non-portable profile instead of the adjacent `data5` folder. Check the Resources page before assuming the profiles are isolated.

Never test by overwriting the current installation. Keep the original profile backup until the test build has been proven stable.
