# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Commands

```bash
# Build
cargo build

# Run
cargo run -- <subcommand>

# Run tests
cargo test

# Run a single test
cargo test <test_name>

# Run tests in a specific module
cargo test --package qobuz-api
```

## Architecture

This is a two-crate workspace:

- **`streamer`** (binary) — CLI front-end using `clap`. Handles config loading, subcommand dispatch (`login`, `download`, `search`, `config`), and URL parsing.
- **`qobuz-api`** (library, local path dep) — All Qobuz API logic: authentication, HTTP, metadata embedding, and download I/O.

### Config

Config lives at `~/.config/streamer/config.toml` (via `dirs::config_dir()`). Fields: `[credentials]` (`app_id`, `app_secret`, `user_id`, `auth_token`) and `[settings]` (`download_dir`, `quality`, `requests_per_minute`, `concurrency`). `app_id`/`app_secret` must be set manually — they're not obtained via `streamer login`.

### Authentication flow

`QobuzApiService::with_credentials(app_id, app_secret)` builds the service; `login_with_token(user_id, token)` authenticates it. The `streamer login` command does both and persists the token. The `qobuz-api` library also supports extracting app credentials by scraping the Qobuz web player JS bundle (`credentials::web::extract_from_web_player`), but the CLI doesn't use this path — it requires the user to supply `app_id`/`app_secret` in the config.

### `qobuz-api` internals

- `api/service.rs` — `QobuzApiService`: central state (app credentials, user auth token, HTTP client). All public API methods are declared here via a `delegate!` macro that wraps free functions in submodules.
- `api/http_client.rs` — `HttpClient` trait + `ReqwestClient` impl. Cloning via `clone_box()` shares the connection pool across concurrent download tasks.
- `api/content/` — per-resource modules: albums, artists, tracks, playlists, catalog search, plus `*_download.rs` for multi-track download orchestration.
- `api/content/download_io.rs` — streaming download to disk with resume-on-partial-file, retry with exponential backoff (`MAX_DOWNLOAD_RETRIES=3`, base 2 s).
- `metadata/` — tag extraction (`extractor.rs`) and embedding into FLAC/MP3 files (`embedder/`).
- `signing.rs` — request signing logic required by the Qobuz API.

### Quality values

`mp3` | `flac` | `flac-hi` | `flac-ultra` — passed through to the Qobuz file URL endpoint.
