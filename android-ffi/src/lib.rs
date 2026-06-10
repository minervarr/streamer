use std::{
    ffi::{CStr, CString, c_char, c_void},
    fs,
    panic::{self, AssertUnwindSafe},
    path::Path,
    sync::{Arc, atomic::AtomicBool},
    time::{SystemTime, UNIX_EPOCH},
};

use qobuz_api::{
    api::{requests::build_url_with_params, service::QobuzApiService},
    models::album::Image,
    signing::sign_request,
};

// ── Android TLS init ──────────────────────────────────────────────────────────

#[cfg(target_os = "android")]
#[unsafe(no_mangle)]
pub unsafe extern "C" fn qobuz_init_android(raw_vm: *mut c_void, raw_context: *mut c_void) {
    use jni::{JavaVM, objects::JObject};
    let jvm = unsafe { JavaVM::from_raw(raw_vm.cast()) };
    let _ = jvm.attach_current_thread(|env| -> Result<(), jni::errors::Error> {
        let context = unsafe { JObject::from_raw(env, raw_context.cast()) };
        rustls_platform_verifier::android::init_with_env(env, context)
    });
}

// ── Memory helpers ────────────────────────────────────────────────────────────

#[unsafe(no_mangle)]
pub unsafe extern "C" fn qobuz_free_string(s: *mut c_char) {
    if !s.is_null() {
        unsafe { drop(CString::from_raw(s)) };
    }
}

fn ok_ptr(s: String) -> *mut c_char {
    CString::new(s)
        .unwrap_or_else(|_| CString::new("error: result contained null byte").unwrap())
        .into_raw()
}

fn catch<F: FnOnce() -> Result<String, Box<dyn std::error::Error>>>(f: F) -> *mut c_char {
    let result = panic::catch_unwind(AssertUnwindSafe(|| {
        f().unwrap_or_else(|e| format!("error: {e}"))
    }))
    .unwrap_or_else(|e| {
        if let Some(s) = e.downcast_ref::<String>() {
            format!("error: panic: {s}")
        } else if let Some(s) = e.downcast_ref::<&str>() {
            format!("error: panic: {s}")
        } else {
            "error: panic (no message)".to_string()
        }
    });
    ok_ptr(result)
}

unsafe fn cstr<'a>(p: *const c_char) -> Result<&'a str, Box<dyn std::error::Error>> {
    Ok(unsafe { CStr::from_ptr(p) }.to_str()?)
}

fn build_service(
    app_id: &str,
    app_secret: &str,
    user_id: &str,
    auth_token: &str,
) -> Result<QobuzApiService, Box<dyn std::error::Error>> {
    let mut service = QobuzApiService::with_credentials(app_id, app_secret)?;
    service.login_with_token(user_id, auth_token)?;
    Ok(service)
}

fn quality_to_format_id(quality: &str) -> i32 {
    match quality {
        "mp3"        => 5,
        "flac-hi"    => 7,
        "flac-ultra" => 27,
        _            => 6,
    }
}

// ── Cover art ─────────────────────────────────────────────────────────────────

// Prefer `large` and rewrite `_600` → `_org` to get original resolution.
fn best_cover_url(image: &Image) -> Option<String> {
    let base = image.large.as_deref()
        .or(image.extra_large.as_deref())
        .or(image.mega.as_deref())
        .or(image.medium.as_deref())
        .or(image.thumbnail.as_deref())
        .or(image.small.as_deref())?;
    let idx = base.rfind("600");
    Some(match idx {
        Some(i) => format!("{}org{}", &base[..i], &base[i + 3..]),
        None    => base.to_owned(),
    })
}

fn save_cover_jpg(url: &str, auth_token: &str, dest: &Path) {
    if dest.exists() { return; }
    if let Ok(data) = http_get(url, auth_token) {
        let _ = fs::write(dest, data);
    }
}

// ── Artist extras ─────────────────────────────────────────────────────────────

#[derive(serde::Deserialize)]
struct ArtistExtrasData {
    biography: Option<ArtistBio>,
    image:     Option<ArtistImg>,
}

#[derive(serde::Deserialize)]
struct ArtistBio {
    text:    Option<String>,
    summary: Option<String>,
}

impl ArtistBio {
    fn best_text(&self) -> Option<&str> {
        self.text.as_deref().or(self.summary.as_deref())
    }
}

#[derive(serde::Deserialize)]
struct ArtistImg {
    mega:       Option<String>,
    #[serde(rename = "extralarge")] extra_large: Option<String>,
    large:      Option<String>,
    medium:     Option<String>,
}

impl ArtistImg {
    fn best_url(&self) -> Option<&str> {
        self.mega.as_deref()
            .or(self.extra_large.as_deref())
            .or(self.large.as_deref())
            .or(self.medium.as_deref())
    }
}

fn save_artist_extras(service: &QobuzApiService, artist_id: i32, artist_dir: &Path) {
    let token = match service.require_auth_token() {
        Ok(t) => t,
        Err(_) => return,
    };
    let url = signed_url(
        service.base_url(), "/artist/get",
        vec![("artist_id".to_string(), artist_id.to_string())],
        &service.app_id, service.app_secret(),
    );
    let extras: ArtistExtrasData = match http_get(&url, token)
        .and_then(|b| Ok(serde_json::from_slice::<ArtistExtrasData>(&b)?))
    {
        Ok(e) => e,
        Err(_) => return,
    };

    if let Some(text) = extras.biography.as_ref().and_then(|b| b.best_text()) {
        let dest = artist_dir.join("artist_bio.txt");
        if !dest.exists() {
            let _ = fs::write(dest, text);
        }
    }

    if let Some(img_url) = extras.image.as_ref().and_then(|img| img.best_url()) {
        let dest = artist_dir.join("artist.jpg");
        if !dest.exists() {
            // Artist images are public CDN URLs — no auth header required.
            if let Ok(resp) = reqwest::blocking::get(img_url) {
                if let Ok(bytes) = resp.bytes() {
                    let _ = fs::write(dest, bytes.as_ref());
                }
            }
        }
    }
}

// ── Album extras (description + booklet) ─────────────────────────────────────

fn now_ts() -> String {
    SystemTime::now()
        .duration_since(UNIX_EPOCH)
        .unwrap_or_default()
        .as_secs()
        .to_string()
}

fn signed_url(
    base_url: &str,
    endpoint: &str,
    mut params: Vec<(String, String)>,
    app_id: &str,
    app_secret: &str,
) -> String {
    params.push(("app_id".to_string(), app_id.to_string()));
    params.push(("request_ts".to_string(), now_ts()));
    let sig = sign_request("GET", endpoint, &mut params, app_secret);
    params.push(("request_sig".to_string(), sig));
    build_url_with_params(base_url, endpoint, &params)
}

fn http_get(url: &str, auth_token: &str) -> Result<Vec<u8>, Box<dyn std::error::Error>> {
    let resp = reqwest::blocking::Client::new()
        .get(url)
        .header("X-User-Auth-Token", auth_token)
        .send()?;
    Ok(resp.bytes()?.to_vec())
}

fn save_album_extras(service: &QobuzApiService, album_id: &str, album_dir: &Path) {
    use serde::Deserialize;

    #[derive(Deserialize)]
    struct Goody {
        #[serde(default)] url: String,
        #[serde(default)] original_url: String,
        file_format_id: Option<u32>,
    }
    #[derive(Deserialize)]
    struct AlbumExtras {
        description: Option<String>,
        #[serde(default)] goodies: Option<Vec<Goody>>,
    }

    let token = match service.require_auth_token() {
        Ok(t) => t,
        Err(_) => return,
    };
    let url = signed_url(
        service.base_url(), "/album/get",
        vec![("album_id".to_string(), album_id.to_string())],
        &service.app_id, service.app_secret(),
    );
    let extras: AlbumExtras = match http_get(&url, token)
        .and_then(|b| Ok(serde_json::from_slice::<AlbumExtras>(&b)?))
    {
        Ok(e) => e,
        Err(_) => return,
    };

    if let Some(desc) = extras.description.filter(|d| !d.is_empty()) {
        let dest = album_dir.join("album_description.txt");
        if !dest.exists() {
            let _ = fs::write(dest, desc);
        }
    }

    for goody in extras.goodies.unwrap_or_default() {
        let best = if !goody.original_url.is_empty() { &goody.original_url } else { &goody.url };
        if best.is_empty() { continue; }
        let is_pdf = best.ends_with(".pdf") || goody.file_format_id == Some(21);
        let filename = if is_pdf { "booklet.pdf" } else { "goody" };
        let dest = album_dir.join(filename);
        if !dest.exists() {
            if let Ok(data) = http_get(best, token) {
                let _ = fs::write(dest, data);
            }
        }
    }
}

// ── Search ────────────────────────────────────────────────────────────────────

/// Searches Qobuz and returns a JSON array:
/// `[{"id":"...","title":"...","artist":"...","year":"..."}]`
///
/// `search_type` is one of: `"albums"`, `"artists"`, `"tracks"`.
/// Returns JSON on success or `"error: ..."` on failure.
/// Caller must free with `qobuz_free_string`.
///
/// # Safety
/// All pointer arguments must be valid non-null null-terminated C strings.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn qobuz_search(
    app_id: *const c_char,
    app_secret: *const c_char,
    user_id: *const c_char,
    auth_token: *const c_char,
    query: *const c_char,
    search_type: *const c_char,
) -> *mut c_char {
    catch(|| {
        let app_id     = unsafe { cstr(app_id)? };
        let app_secret = unsafe { cstr(app_secret)? };
        let user_id    = unsafe { cstr(user_id)? };
        let token      = unsafe { cstr(auth_token)? };
        let query      = unsafe { cstr(query)? };
        let stype      = unsafe { cstr(search_type)? };

        let service = build_service(app_id, app_secret, user_id, token)?;

        let json = match stype {
            "artists" => {
                let results = service.search_artists(query, Some(30), None)?;
                let items = results.items.unwrap_or_default();
                let v: Vec<_> = items.iter().map(|a| {
                    serde_json::json!({
                        "id":     a.id.map(|i| i.to_string()).unwrap_or_default(),
                        "title":  a.name.as_deref().unwrap_or(""),
                        "artist": "",
                        "year":   "",
                    })
                }).collect();
                serde_json::to_string(&v)?
            }
            "tracks" => {
                let results = service.search_tracks(query, Some(30), None)?;
                let items = results.items.unwrap_or_default();
                let v: Vec<_> = items.iter().map(|t| {
                    let artist = t.performer.as_ref()
                        .and_then(|p| p.name.as_deref())
                        .or_else(|| t.album.as_ref().and_then(|a| a.artist.as_ref()).and_then(|a| a.name.as_deref()))
                        .unwrap_or("");
                    let year = t.album.as_ref()
                        .and_then(|a| a.release_date_original.as_deref())
                        .and_then(|d| d.get(..4))
                        .unwrap_or("");
                    serde_json::json!({
                        "id":     t.id.map(|i| i.to_string()).unwrap_or_default(),
                        "title":  t.title.as_deref().unwrap_or(""),
                        "artist": artist,
                        "year":   year,
                    })
                }).collect();
                serde_json::to_string(&v)?
            }
            _ => { // "albums"
                let results = service.search_albums(query, Some(30), None)?;
                let items = results.items.unwrap_or_default();
                let v: Vec<_> = items.iter().map(|a| {
                    let artist = a.artist.as_ref().and_then(|a| a.name.as_deref()).unwrap_or("");
                    let year = a.release_date_original.as_deref().and_then(|d| d.get(..4)).unwrap_or("");
                    serde_json::json!({
                        "id":     a.id.as_deref().unwrap_or(""),
                        "title":  a.title.as_deref().unwrap_or(""),
                        "artist": artist,
                        "year":   year,
                    })
                }).collect();
                serde_json::to_string(&v)?
            }
        };

        Ok(json)
    })
}

// ── Download ──────────────────────────────────────────────────────────────────

/// Downloads a track or album to `output_dir` with full metadata + extras.
///
/// `item_type` is one of: `"album"`, `"track"`.
/// `quality`   is one of: `"mp3"`, `"flac"`, `"flac-hi"`, `"flac-ultra"`.
/// Returns `"ok"` on success or `"error: ..."` on failure.
/// Caller must free with `qobuz_free_string`.
///
/// # Safety
/// All pointer arguments must be valid non-null null-terminated C strings.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn qobuz_download(
    app_id: *const c_char,
    app_secret: *const c_char,
    user_id: *const c_char,
    auth_token: *const c_char,
    item_id: *const c_char,
    item_type: *const c_char,
    output_dir: *const c_char,
    quality: *const c_char,
) -> *mut c_char {
    catch(|| {
        let app_id     = unsafe { cstr(app_id)? };
        let app_secret = unsafe { cstr(app_secret)? };
        let user_id    = unsafe { cstr(user_id)? };
        let token      = unsafe { cstr(auth_token)? };
        let item_id    = unsafe { cstr(item_id)? };
        let itype      = unsafe { cstr(item_type)? };
        let out_dir    = unsafe { cstr(output_dir)? };
        let quality    = unsafe { cstr(quality)? };

        let mut service = build_service(app_id, app_secret, user_id, token)?;
        let format_id   = quality_to_format_id(quality);
        let dir         = Path::new(out_dir);
        let cancel      = Arc::new(AtomicBool::new(false));

        match itype {
            "track" => {
                let track     = service.get_track(track_id_parse(item_id)?)?;
                let cover_url = track.album.as_ref()
                    .and_then(|a| a.image.as_ref())
                    .and_then(best_cover_url);
                let path = service.download_track_cancellable(track_id_parse(item_id)?, format_id, dir, None, None)?;
                let track_dir = path.parent().unwrap_or(dir);
                if let Some(url) = cover_url {
                    let auth = service.require_auth_token().unwrap_or("");
                    save_cover_jpg(&url, auth, &track_dir.join("cover.jpg"));
                }
            }
            _ => { // "album"
                let album     = service.get_album(item_id, None)?;
                let cover_url = album.image.as_ref().and_then(best_cover_url);
                let artist_id = album.artist.as_ref().and_then(|a| a.id);
                let paths     = service.download_album_cancellable(item_id, format_id, dir, None, None, Some(cancel))?;
                let album_dir = paths.first().and_then(|p| p.parent()).unwrap_or(dir);
                let auth      = service.require_auth_token().unwrap_or("");
                if let Some(url) = cover_url {
                    save_cover_jpg(&url, auth, &album_dir.join("cover.jpg"));
                }
                save_album_extras(&service, item_id, album_dir);
                if let (Some(aid), Some(artist_dir)) = (artist_id, album_dir.parent()) {
                    save_artist_extras(&service, aid, artist_dir);
                }
            }
        }

        Ok("ok".to_string())
    })
}

fn track_id_parse(s: &str) -> Result<i32, Box<dyn std::error::Error>> {
    Ok(s.parse()?)
}
