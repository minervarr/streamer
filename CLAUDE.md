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

**External dependency**: `KawusapiCC/` (git submodule) — the C++ Qobuz API library (C++17).

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
| `url.hh/.cpp` | URL parser for Qobuz links and bare IDs |
| `i18n.hh/.cpp` | EN/ES translation table, OS language detection |

### Config

Config lives at:
- **Windows**: `%APPDATA%\streamer\config.toml`
- **Linux/macOS**: `~/.config/streamer/config.toml`

Fields: `[settings]` (`download_dir`, `quality`, `requests_per_minute`, `concurrency`, `language`) and `[[accounts]]` (`app_id`, `app_secret`, `user_id`, `auth_token`, `country`, `email`). `app_id`/`app_secret` must be set manually before running `streamer login`.

### Kobuzapi++ internals

- `kobuzapi/src/main/cpp/api/service.hh` — `kb::QobuzApiService`: central service class. `with_credentials(Config)` builds it; `login_with_token()` authenticates it.
- `kobuzapi/src/main/cpp/download/download.hh` — `kb::download_track/album/playlist/artist` free functions.
- `kobuzapi/src/main/cpp/core/models.hh` — all model structs (`Album`, `Artist`, `Track`, `Playlist`, `FileUrl`, …).
- `kobuzapi/src/main/cpp/metadata/config.hh` — `kb::MetadataConfig` / `kb::MetadataField` for tag embedding.
- `kobuzapi/src/main/cpp/api/requests.hh` — `kb::api::signed_get_raw` for raw signed API calls.
- `engine/archive_engine/src/main/cpp/` — `ae_util`, `ae_net` (libcurl wrapper), `ae_tag` (TagLib wrapper).

### Quality values

`mp3` (320 kbps) | `flac` (16-bit) | `flac-hi` (24/96) | `flac-ultra` (24/192)

Format IDs: mp3=5, flac=6, flac-hi=7, flac-ultra=27.
