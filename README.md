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

- **Sync modes**: two-way, local-to-remote only, or remote-to-local only. One-way modes treat the source side as authoritative: conflicts prefer the source, and destination-only changes are ignored. Deletion propagation still follows the per-pair synchronized-deletion setting.
- **Deletion policy**: each sync pair can enable or disable synchronized deletion. It is disabled by default.
  - When enabled, local deletions propagate to the remote side.
  - When disabled, a local deletion only affects the local copy; the remote file is kept and restored by the next sync.
  - Remote deletions always propagate to the local side.
  - Directory deletions only run when the entire subtree is unchanged. Subtrees containing exclusions or conflicts are skipped to avoid accidental data loss.
- **Recycle bin**: before a local or remote deletion, NutSync copies the affected files into `~/.local/share/nutsync/trash/<pair>/<timestamp>/`. The retention period is configurable per pair and defaults to 30 days. The details-panel recycle-bin dialog lists backups, restores selected entries into the local sync folder, and can open the trash directory.
- **Conflict protection**: the default policy is `KeepBoth`. The remote version is saved as `name.conflict-YYYYMMDD-HHMMSS.ext` while the local version is uploaded, so both versions remain available on both sides.
  - Other policies: prefer local, prefer remote, and newer wins.
  - The details panel exposes a **View conflicts** button listing the affected paths and conflict copies, with a shortcut to open the containing folder. Conflict records are persisted in the state database and survive restarts. A conflict copy can be resolved directly with **Use local version** or **Use remote version**, updating local files, remote files, and the baseline.
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
- **Reliability controls**: configurable per-request retry count with exponential backoff, `Retry-After`, persistent resumable downloads, and selectable log levels.
- **Monochrome panel icons**: the system tray uses a monochrome NutSync icon that follows the active KDE dark or light color scheme, including live theme changes.
- **Automatic synchronization**: per-pair intervals, sync on application startup, and recursive Linux inotify monitoring with debounce.
- **Request rate limiting**: each pair can allow at most a configurable number of sync starts per time window. The limit is disabled by default. Scheduled, inotify, and startup syncs consume the quota; manual syncs are always allowed but still consume it. Quota history is persisted across restarts.
- **Credential safety**: passwords are never written to the JSON configuration; they are stored in the system keychain.
- **Single instance**: a lock file prevents two instances from writing the state database at the same time.

## Build

Requirements:

- CMake ≥ 3.21, Ninja, and a C++20 compiler
- Qt 6.4+: Core, Gui, Widgets, Network, Sql (SQLite driver), Concurrent, Test, LinguistTools, and the SVG icon plugin (`qt6-svg` / `libqt6svg6`)
- Architecture: x86_64 and aarch64; CI builds and tests natively on Ubuntu 24.04 ARM64
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
https://github.com/gomodb/nutsync/archive/refs/tags/v0.6.0.tar.gz
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

The first AUR release for this version is `0.6.0-1`. The repository is prepared under `~/Projects/nutsync-aur` and expects the AUR SSH key to be registered before the first push.

For automated publishing, configure the repository secret `AUR_SSH_PRIVATE_KEY`, create an `aur` environment, and manually run the **Publish to AUR** workflow with the release tag. The workflow also supports creating the AUR repository on its first push.

### Release automation

The helper scripts reduce manual errors:

```bash
# 1. Prepare CMake, PKGBUILD, README and AppStream versions, with sha256sums=('SKIP')
scripts/bump-version.sh 0.4.3

# 2. Review the changes, commit, tag and push the tag
git add -A && git commit -m 'Prepare 0.4.3'
git tag -a v0.4.3 -m 'NutSync 0.4.3'
git push origin main v0.4.3

# 3. Download the tag archive, update the AUR checksum and regenerate .SRCINFO
scripts/update-aur-checksum.sh

# 4. Commit the checksum and validate all release metadata
git add packaging/arch/PKGBUILD packaging/arch/.SRCINFO
git commit -m 'Set AUR checksum for 0.4.3'
scripts/check-release.sh v0.4.3
```

For a privileged maintainer check, `scripts/clean-chroot-build.sh` builds the AUR package in a `devtools` clean chroot. It requires `sudo` and `devtools`; normal CI intentionally does not run it.

For a new release, update `pkgver`, `source`, and `sha256sums`, then run:

```bash
makepkg --printsrcinfo > .SRCINFO
```

## Configuration and data locations

AArch64 is supported: the code contains no x86-specific logic, CI builds and tests it natively on Ubuntu 24.04 ARM64, and the AUR package declares `aarch64`. Native Arch Linux ARM packaging should still be validated on an ALARM system before relying on it in production.

NutSync follows the XDG directory specification:

| Content | Path |
| --- | --- |
| Configuration | `~/.config/nutsync/config.json` |
| Sync state database | `~/.local/share/nutsync/state.db` |
| Log | `~/.local/share/nutsync/nutsync.log` |
| Rate-limit history | `~/.local/share/nutsync/rate_limits.json` |
| Autostart entry | `~/.config/autostart/io.github.gomodb.NutSync.desktop` |
| Download resume / conflict temporary files | `~/.cache/nutsync/` |
| Deletion backups | `~/.local/share/nutsync/trash/` |
| Passwords | KWallet / Secret Service (`io.github.gomodb.NutSync`) |
| Config backup | `~/.config/nutsync/config.json.bak` |
| State migration backup | `~/.local/share/nutsync/state.db.bak-*` |

The state database is SQLite and isolates state by sync-pair ID. Its schema version is checked when opened; a database from a newer NutSync version is rejected, and an older schema is backed up before migration. Configuration saves keep the previous file as `config.json.bak`. If a pair's local or remote location changes, NutSync asks to clear that pair's baseline without deleting files. The baseline can also be reset from the context menu or **Edit → Clear sync baseline…**.

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
- **Credential transport**: HTTP credentials are rejected outside loopback; cross-origin redirects are not followed.
- **Proxy password**: stored in the system keychain, never in `config.json`.
- **CA certificate file**: an additional PEM bundle appended to the system CA certificates. Certificate verification is always enforced.
- **Retry attempts / maximum delay**: each WebDAV request retries network errors, 408, 425, 429, and 5xx responses up to the configured count, using exponential backoff capped by the configured maximum delay and honoring `Retry-After`. Permanent errors such as 401, 403, 404, 409, 412, 423, and 507 are not retried. There is no separate whole-sync retry.
- **Resumable downloads**: partial downloads are kept in the cache as `.part` files, validated with `If-Range`, and resumed across syncs and restarts. Uploads restart from the beginning because WebDAV PUT has no standard resumable form.
- **Log level / rotation**: Debug, Info, Warning, or Error; maximum log size and the number of retained rotated files are configurable.
- **Network recovery**: optionally start a sync of all enabled pairs when the network becomes online again.
- **Transfer speed limit**: optional per-transfer byte limit for downloads and uploads; `0` means unlimited.
- **Parallel transfers**: 1–4 concurrent file uploads/downloads with live aggregate byte progress, while state writes remain serialized.
- **Move/rename detection**: a local rename with unchanged size/mtime is sent as a WebDAV MOVE instead of delete+re-upload.

Proxy and CA changes take effect after restart. The log level is applied immediately.

## Packaging status

See [docs/packaging-status.md](docs/packaging-status.md) for current artifacts and the AUR/Arch Linux ARM blockers.

## Diagnostics

**Help → Export diagnostics** writes a text report with the NutSync/Qt versions, OS, architecture, locale, sanitized settings, per-pair summary, and the tail of the log. Passwords, usernames, remote URL paths, and proxy credentials are redacted. Review the log tail before sharing it, since it may contain local paths or server error text.

## Configuration export and import

The **File** menu can export the current configuration to JSON and import it on another machine or after reinstalling. Passwords are never included in the export; existing keychain entries are reused by sync-pair ID. Import can either replace the current configuration or merge the imported sync pairs.

## Security

See [SECURITY.md](SECURITY.md) for the security model and vulnerability reporting process. In particular, HTTP credentials are accepted only for loopback hosts, TLS verification is always enforced, and cross-origin redirects are not followed.

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
