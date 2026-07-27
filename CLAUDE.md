# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Commands

```bash
# Configure (vcpkg preset recommended; requires CURL and TagLib available)
cmake -B build -DCMAKE_BUILD_TYPE=Release

# Build
cmake --build build

# Run
./build/streamer <subcommand>
```

On Windows with vcpkg:
```powershell
cmake -B build -DCMAKE_TOOLCHAIN_FILE="$env:VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake" -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
```

## Architecture

This is a pure C++ project. The CLI binary (`streamer`) is built by `CMakeLists.txt` in the repo root.

**External dependency**: `first_party/KawusapiCC/` (git submodule) — the C++ Qobuz API library (C++17). Structured `core/` (platform-agnostic, consumed here via `add_subdirectory`) + `platform/android/` (Gradle/NDK JNI build) + `scripts/` (standalone build entry points), matching `framework/Vk_Canvas_Lb_LAW`'s convention. Its own `engine/archive_engine` submodule (`ae_util`/`ae_net`/`ae_tag`) is structured the same way; `core/net` and `core/tag` link against this repo's vendored `third_party/curl`/`third_party/taglib` on desktop (must be `add_subdirectory`'d before KawusapiCC's `core/`) and cross-compile their own on Android.

### Source layout (`src/`)

| File | Purpose |
|------|---------|
| `main.cpp` | CLI entry point, subcommand wiring via CLI11 |
| `config.hh/.cpp` | TOML config read/write (toml++), `Account`/`Settings`/`Config` structs |
| `download.hh/.cpp` | Download dispatch; calls `kb::download_*` functions |
| `search.hh/.cpp` | Search result display, duration parsing/formatting |
| `inspect.hh/.cpp` | Album track listing with availability info |
| `extras.hh/.cpp` | Cover art, booklet PDF, artist bio download |
| `history.hh/.cpp` | SQLite3 download history (record/list/export/import/clear) |
| `library.hh/.cpp` | SQLite3 library catalog — the real names behind the ID-addressed layout |
| `service_factory.hh/.cpp` | `qobuz::make_service()` — the one place a `QobuzApiService` is built |
| `url.hh/.cpp` | URL parser for Qobuz links and bare IDs |
| `i18n.hh/.cpp` | EN/ES translation table, OS language detection |

### Library layout

Downloads are **ID-addressed**, identically on every platform:

```
<download_dir>/<country>/<album_id>/<track_id>.<format_id>.<ext>
<download_dir>/<country>/<album_id>/cover.jpg | booklet.pdf | album_description.txt
<download_dir>/.streamer/library.db
<download_dir>/.streamer/artists/<artist_id>/{artist.jpg,artist_bio.txt}
```

No title ever reaches a path, so no filesystem's reserved characters can mangle one
(`F*CK U SKRILLEX … <3` used to land as `F_CK … _3`) and paths stay stable when upstream
metadata is corrected. `format_id` is in the filename so two qualities of the same track
coexist without the resume logic mistaking a finished file for a partial.

The real, byte-exact names and the release metadata live in `library.db`, versioned via
`PRAGMA user_version`. Start from the `view_library` view. `streamer library resolve <album-id
| track-id | path>` maps any ID back to its names.

### Config

Config lives at:
- **Windows**: `%APPDATA%\streamer\config.toml`
- **Linux/macOS**: `~/.config/streamer/config.toml`

Fields: `[settings]` (`download_dir`, `quality`, `requests_per_minute`, `concurrency`, `language`) and `[[accounts]]` (`app_id`, `app_secret`, `user_id`, `auth_token`, `country`, `email`). `app_id`/`app_secret` must be set manually before running `streamer login`.

### Kobuzapi++ internals

- `first_party/KawusapiCC/core/api/service.hh` — `kb::QobuzApiService`: central service class. `with_credentials(Config)` builds it; `login_with_token()` authenticates it.
- `first_party/KawusapiCC/core/download/download.hh` — `kb::download_track/album/playlist/artist` free functions.
- `first_party/KawusapiCC/core/core/models.hh` — all model structs (`Album`, `Artist`, `Track`, `Playlist`, `FileUrl`, …).
- `first_party/KawusapiCC/core/metadata/config.hh` — `kb::MetadataConfig` / `kb::MetadataField` for tag embedding.
- `first_party/KawusapiCC/core/api/requests.hh` — `kb::api::signed_get_raw` for raw signed API calls.
- `first_party/KawusapiCC/engine/archive_engine/core/` — `ae_util`, `ae_net` (libcurl wrapper), `ae_tag` (TagLib wrapper).

### Quality values

`mp3` (320 kbps) | `flac` (16-bit) | `flac-hi` (24/96) | `flac-ultra` (24/192)

Format IDs: mp3=5, flac=6, flac-hi=7, flac-ultra=27.
