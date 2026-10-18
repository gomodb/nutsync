# NutSync

[![CI](https://github.com/gomodb/nutsync/actions/workflows/ci.yml/badge.svg)](https://github.com/gomodb/nutsync/actions/workflows/ci.yml)

**English** · [简体中文](README.zh-CN.md)

A **two-way WebDAV synchronization** desktop application for Linux and KDE Plasma, built with Qt 6 and KDE Frameworks.

![NutSync main window](docs/main-window.png)

NutSync is designed for continuous synchronization rather than one-off uploads or downloads. It maintains a reliable three-way baseline between:

1. the local directory;
2. the remote WebDAV directory;
3. the per-entry state recorded after the last successful sync.

Every sync compares both sides against that baseline before deciding whether to upload, download, delete, or resolve a conflict. The remote protocol is isolated behind the `RemoteBackend` interface, so additional protocols or Rust-backed implementations can be added without changing the synchronization engine.

## Features

- **Two-way synchronization**: changes on either the local or WebDAV side propagate to the other side.
- **Deletion policy**: each sync pair can enable or disable synchronized deletion. It is disabled by default.
  - When enabled, local deletions propagate to the remote side.
  - When disabled, a local deletion only affects the local copy; the remote file is kept and restored by the next sync.
  - Remote deletions always propagate to the local side.
  - Directory deletions only run when the entire subtree is unchanged. Subtrees containing exclusions or conflicts are skipped to avoid accidental data loss.
- **Conflict protection**: the default policy is `KeepBoth`. The remote version is saved as `name.conflict-YYYYMMDD-HHMMSS.ext` while the local version is uploaded, so both versions remain available on both sides.
  - Other policies: prefer local, prefer remote, and newer wins.
- **Content-based first-sync deduplication**: when a file exists on both sides, SHA-256 comparison updates the baseline without creating a conflict copy if the contents are identical.
- **Safety checks**:
  - Synchronization stops if the local directory is not readable.
  - A missing remote root with existing baseline state stops the sync instead of treating local files as remote deletions.
  - A newly created pair creates the remote root with `MKCOL` when it does not exist.
  - Uploads and deletions use `If-Match` / `If-None-Match` conditional requests to avoid overwriting concurrent changes.
  - If a local file changes again during a sync, that operation is skipped and retried on the next sync.
- **Exclusion rules**: supports patterns such as `.git`, `node_modules`, `*.tmp`, and `cache/`. Excluded remote content blocks recursive deletion of its parent directory and is not watched by inotify.
- **KDE integration**: KStatusNotifierItem tray, KNotification desktop notifications, KWallet / Secret Service password storage, and optional per-user start at login.
- **Languages**: English and Simplified Chinese interface. The initial language follows the system locale; Chinese systems use Simplified Chinese and all other/unknown locales fall back to English. The choice can be overridden in Preferences and takes effect after restart.
- **Network configuration**: system, direct, or manual HTTP proxy with optional credentials; custom PEM CA bundle; TLS verification is never disabled.
- **Reliability controls**: configurable automatic retry count with exponential backoff for failed automatic syncs, plus selectable log levels.
- **Monochrome panel icons**: the system tray uses a monochrome NutSync icon that follows the active KDE dark or light color scheme, including live theme changes.
- **Automatic synchronization**: per-pair intervals, sync on application startup, and recursive Linux inotify monitoring with debounce.
- **Request rate limiting**: each pair can allow at most a configurable number of sync starts per time window. The limit is disabled by default. Scheduled, inotify, and startup syncs consume the quota; manual syncs are always allowed but still consume it. Quota history is persisted across restarts.
- **Credential safety**: passwords are never written to the JSON configuration; they are stored in the system keychain.
- **Single instance**: a lock file prevents two instances from writing the state database at the same time.

## Build

Requirements:

- CMake ≥ 3.21, Ninja, and a C++20 compiler
- Qt 6.4+: Core, Gui, Widgets, Network, Sql (SQLite driver), Concurrent, Test, LinguistTools, and the SVG icon plugin (`qt6-svg` / `libqt6svg6`)
- Optional: QtKeychain for password storage
- Optional: KStatusNotifierItem and KNotifications for native KDE tray and notifications

Arch Linux:

```bash
sudo pacman -S --needed base-devel cmake ninja qt6-base qt6-tools qtkeychain-qt6 \
    kstatusnotifieritem knotifications qt6-svg qt6-translations
```

Debian / Ubuntu (package names may vary by release):

```bash
sudo apt install build-essential cmake ninja-build qt6-base-dev \
    qt6-tools-dev qt6-tools-dev-tools libqt6sql6-sqlite qtkeychain-qt6-dev \
    libkf6statusnotifieritem-dev libkf6notifications-dev libqt6svg6 \
    qt6-translations-l10n
```

The test suite starts a local Python 3 WebDAV server, so `python3` must be available.

Build and test:

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
```

Run:

```bash
./build/nutsync
```

Install:

```bash
sudo cmake --install build
```

The installation provides the `nutsync` executable, desktop entry, AppStream metadata, notification configuration, and icons.

## Usage

Click **Add sync pair** and fill in:

- **Local folder**: the local directory to synchronize.
- **WebDAV URL**: the full URL of the remote root, for example:
  - Nextcloud: `https://cloud.example.com/remote.php/dav/files/<username>/<folder>/`
  - ownCloud: `https://cloud.example.com/remote.php/webdav/<folder>/`
  - Generic WebDAV: `https://dav.example.com/sync/`
- **Username / password**: an application-specific password is recommended. Nextcloud/ownCloud users with two-factor authentication usually need an app password.
- **Conflict policy**: the default **Keep both copies** is recommended.
- **Synchronized deletion**: disabled by default. When disabled, local deletions do not delete remote files and are restored by the next sync; remote deletions still propagate locally.
- **Sync interval**: `0` disables scheduled synchronization. inotify, startup sync, and manual sync remain available.
- **Watch local changes**: enabled by default. Recursively watches the local directory with inotify and triggers a debounced sync.
- **Request limit**: disabled by default. When enabled, for example, allow at most 10 sync starts per hour.
- **Exclusion patterns**: one per line, for example:

```text
.git
node_modules
*.tmp
.cache/
```

Use **Test connection** to validate the URL and credentials before saving. After saving, press F5 or click **Sync now** for the first sync.

During the first sync:

- entries that exist only locally are uploaded;
- entries that exist only remotely are downloaded;
- files that exist on both sides with different contents create conflict copies;
- identical files only update the baseline.

## Arch Linux / AUR

AUR metadata lives in [packaging/arch/PKGBUILD](packaging/arch/PKGBUILD). The packaging files are kept in a subdirectory because `makepkg` creates `src/` and `pkg/` in its working directory.

The package source is the GitHub tag archive:

```text
https://github.com/gomodb/nutsync/archive/refs/tags/v0.4.0.tar.gz
```

Build from the project directory:

```bash
cd packaging/arch
makepkg -si
```

Build without installing:

```bash
makepkg -f
```

The AUR repository separately contains the `PKGBUILD`, `.SRCINFO`, and the 0BSD packaging license. Application source remains GPL-3.0-or-later; AUR packaging files are 0BSD.

The first AUR release for this version is `0.4.0-1`. For a new release, update `pkgver`, `source`, and `sha256sums`, then run:

```bash
makepkg --printsrcinfo > .SRCINFO
```

## Configuration and data locations

NutSync follows the XDG directory specification:

| Content | Path |
| --- | --- |
| Configuration | `~/.config/nutsync/config.json` |
| Sync state database | `~/.local/share/nutsync/state.db` |
| Log | `~/.local/share/nutsync/nutsync.log` |
| Rate-limit history | `~/.local/share/nutsync/rate_limits.json` |
| Autostart entry | `~/.config/autostart/io.github.gomodb.NutSync.desktop` |
| Temporary conflict downloads | `~/.cache/nutsync/` |
| Passwords | KWallet / Secret Service (`io.github.gomodb.NutSync`) |

The state database is SQLite and isolates state by sync-pair ID. If a pair's local or remote location changes, NutSync asks to clear that pair's baseline without deleting files. The baseline can also be reset from the context menu or **Edit → Clear sync baseline…**.

## Design

```text
src/core/
  remote_backend.h       Protocol-independent remote backend interface
  webdav_backend.*       WebDAV/HTTP implementation (PROPFIND/GET/PUT/MKCOL/DELETE)
  sync_planner.*         Pure three-way comparison and action planning
  sync_engine.*          Scanning, plan execution, conflict handling, state updates
  local_change_monitor.* Recursive Linux inotify monitoring with debounce
  sync_rate_limiter.*    Persistent sync-start rate limiting
  sync_state_store.*     SQLite baseline state
  config_store.*         JSON configuration
  credential_store.*     QtKeychain wrapper
src/ui/
  main_window.*          Main window, tray, scheduling
  pair_dialog.*          Sync-pair editor and connection test
  sync_controller.*      Background worker queue
  tray_controller.*      KStatusNotifierItem / QSystemTrayIcon
```

The planner depends only on the local snapshot, remote snapshot, and baseline, so it can be unit-tested independently. The engine runs on a background thread and reports progress and logs to the GUI through signals.

### WebDAV details

- Recursive scanning uses one `PROPFIND Depth: 1` request per directory, for compatibility with servers that reject `Depth: infinity`.
- Downloads use `GET` with atomic writes; uploads use `PUT` with `Content-Length`.
- ETag, Last-Modified, and Content-Length metadata are used for baseline comparisons.
- Basic authentication is supported. HTTPS is strongly recommended.
- TLS certificate errors are never ignored.

## Code formatting

The root [.clang-format](.clang-format) is based on Clang's WebKit style with Qt/KDE conventions:

- 4-space indentation and a 100-column limit;
- function, class, struct, enum, and namespace braces on their own line;
- control-flow and lambda braces attach to the statement;
- pointers and references bind to the variable name, for example `QString *value` and `const QString &value`;
- manually maintained include groups are preserved;
- C++20.

Format `src/` and `tests/`:

```bash
find src tests -type f \( -name '*.cpp' -o -name '*.h' \) -print0 \
    | xargs -0 clang-format -i
```

References:

- [Clang-Format Style Options](https://clang.llvm.org/docs/ClangFormatStyleOptions.html)
- [Qt Coding Style](https://wiki.qt.io/Qt_Coding_Style)
- [KDE Frameworks Coding Style](https://community.kde.org/Policies/Frameworks_Coding_Style)

## Network and logging

Network options are available in **Preferences → Network**:

- **Proxy mode**: system proxy, no proxy, or a manual HTTP proxy with host, port, username, and password.
- **Proxy password**: stored in the system keychain, never in `config.json`.
- **CA certificate file**: an additional PEM bundle appended to the system CA certificates. Certificate verification is always enforced.
- **Retry attempts / base delay**: failed automatic syncs retry with exponential backoff (30 s, 60 s, 120 s, ... up to 10 minutes). Manual syncs are never retried automatically.
- **Log level**: Debug, Info, Warning, or Error.

Proxy and CA changes take effect after restart. The log level is applied immediately.

## Language

The interface language follows the system locale:

- Chinese locales use Simplified Chinese;
- all other or unsupported locales use English.

The language can be overridden in **Preferences → Language** with **System default**, **English**, or **简体中文**. A restart is required for the change to take effect.

## Preferences

The preferences dialog contains global options for desktop notifications, close-to-tray behavior, sync on startup, and start at login. Start at login uses a standard XDG autostart entry in the current user's configuration directory and launches NutSync with `--hidden`:

```text
~/.config/autostart/io.github.gomodb.NutSync.desktop
```

The option is disabled by default. It takes effect at the next login, and disabling it removes the autostart entry.

## Tests

The test suite covers:

- path normalization, exclusions, URL encoding, and HTTP date parsing;
- three-way plans for additions, modifications, deletions, recursive directory deletion, type conflicts, and conflict detection;
- deletion policy behavior: local deletion propagation, restoration when disabled, remote deletion propagation, and deletion safety under conflict policies;
- the complete sync engine with an in-memory remote backend: bidirectional propagation, deletions, empty directories, conflict copies, exclusion protection, and missing remote-root protection;
- inotify monitoring: file changes, exclusions, suppression during sync, recursive monitoring of new and renamed directories;
- rate limiting: window calculation, quota exhaustion, manual sync quota usage, persistence, and clearing;
- start at login: no entry by default, XDG autostart creation, content validation, and removal;
- real HTTP WebDAV integration through `tests/mock_webdav_server.py`, including Basic authentication, Unicode and space-containing paths, scanning, downloading, uploading, conditional requests, and deletion.

```bash
ctest --test-dir build --output-on-failure
```

## Known limitations

- File-versus-directory type conflicts are not resolved destructively; they are reported for manual handling.
- Symbolic links and special files are skipped, and directory deletion is blocked when they are present.
- Directory deletion concurrency protection depends on the server honoring `If-Match`; the next sync corrects eventual inconsistencies.
- inotify is Linux-only and is limited by `max_user_watches`. Very large trees may only be partially watched; the UI reports monitoring errors and scheduled sync remains available.
- Local monitoring is suppressed during a sync and for about two seconds afterward so NutSync's own downloads and deletions are not treated as user changes. Changes made during that window are handled by the next scheduled or manual sync.
- With synchronized deletion disabled, a local deletion is restored by the next sync. If inotify is enabled, this can happen quickly. Enable synchronized deletion to delete both local and remote copies permanently.
- Uploads and downloads execute sequentially; the first sync of a large tree or a high-latency server may be slow.
- OAuth2 and Digest challenge authentication are not supported. Nextcloud app passwords with Basic authentication work.
- Rate limiting counts sync starts, not individual HTTP requests within a sync.

## License

NutSync is licensed under **GPL-3.0-or-later**. AUR packaging files are licensed under **0BSD**.
