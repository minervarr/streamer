use std::{fs, path::Path};

use qobuz_api::{
    api::service::QobuzApiService,
    metadata::config::{MetadataConfig, MetadataField::CoverArt},
    models::album::Image,
    sanitize::sanitize_filename,
};

use crate::{errors::AppError, extras, history, i18n::t, url::DownloadTarget};

pub fn quality_to_format_id(quality: &str) -> i32 {
    match quality {
        "mp3" => 5,
        "flac-hi" => 7,
        "flac-ultra" => 27,
        _ => 6, // "flac" default
    }
}

fn full_meta_config() -> MetadataConfig {
    let mut config = MetadataConfig::all();
    config.set(CoverArt, false);
    config
}

fn best_cover_url(image: &Image) -> Option<String> {
    // Qobuz image URLs contain a size segment (e.g. "_600.jpg").
    // Replacing the last "600" with "org" gives the original full-resolution file.
    let base = image
        .large
        .as_deref()
        .or(image.extra_large.as_deref())
        .or(image.mega.as_deref())
        .or(image.medium.as_deref())
        .or(image.thumbnail.as_deref())
        .or(image.small.as_deref())?;

    let idx = base.rfind("600");
    Some(match idx {
        Some(i) => format!("{}org{}", &base[..i], &base[i + 3..]),
        None => base.to_owned(),
    })
}

fn save_text(text: &str, path: &Path) {
    if path.exists() {
        return;
    }
    if let Err(e) = fs::write(path, text) {
        eprintln!("{} {}: {e}", t("warning_could_not_save"), path.display());
    } else {
        println!("{} {}", t("saved"), path.display());
    }
}

fn save_url(url: &str, dest: &Path, label: &str) {
    if dest.exists() {
        return;
    }
    match reqwest::blocking::get(url) {
        Ok(resp) => match resp.bytes() {
            Ok(bytes) => {
                if let Err(e) = fs::write(dest, &bytes) {
                    eprintln!("{} {label}: {e}", t("warning_could_not_save"));
                } else {
                    println!("{} {}", t("saved"), dest.display());
                }
            }
            Err(e) => eprintln!("{} {label}: {e}", t("warning_could_not_read")),
        },
        Err(e) => eprintln!("{} {label}: {e}", t("warning_could_not_download")),
    }
}

fn save_album_extras(api: &QobuzApiService, album_id: &str, album_dir: &Path) {
    match extras::fetch_album_extras(api, album_id) {
        Ok(ex) => {
            if let Some(desc) = ex.description.as_deref().filter(|d| !d.is_empty()) {
                save_text(desc, &album_dir.join("album_description.txt"));
            }
            if let Some(url) = ex.goodies
                .as_deref()
                .unwrap_or(&[])
                .iter()
                .find(|g| g.is_pdf())
                .map(|g| g.best_url().to_owned())
            {
                save_url(&url, &album_dir.join("booklet.pdf"), "booklet");
            }
        }
        Err(e) => eprintln!("{} {e}", t("warning_album_extras")),
    }
}

fn save_artist_extras(api: &QobuzApiService, artist_id: i32, artist_dir: &Path) {
    match extras::fetch_artist_extras(api, artist_id) {
        Ok(ex) => {
            if let Some(text) = ex.biography.as_ref().and_then(|b| b.best_text()) {
                save_text(text, &artist_dir.join("artist_bio.txt"));
            }
            if let Some(url) = ex.image.as_ref().and_then(|img| img.best_url()) {
                save_url(url, &artist_dir.join("artist.jpg"), "artist image");
            }
        }
        Err(e) => eprintln!("{} {e}", t("warning_artist_extras")),
    }
}

fn quality_label(q: &str) -> &'static str {
    match q {
        "mp3" => t("quality_mp3"),
        "flac" => t("quality_flac"),
        "flac-hi" => t("quality_flac_hi"),
        "flac-ultra" => t("quality_flac_ultra"),
        _ => t("quality_flac"),
    }
}

pub fn run(
    api: &mut QobuzApiService,
    target: DownloadTarget,
    quality: &str,
    output_dir: &Path,
    concurrency: usize,
    country: Option<&str>,
) -> Result<(), AppError> {
    let format_id = quality_to_format_id(quality);
    let meta = full_meta_config();
    let conc = Some(concurrency);
    let ql = quality_label(quality);

    match target {
        DownloadTarget::Track(id) => {
            let track = api.get_track(id)?;
            let title = track.title.as_deref().unwrap_or("?");
            let artist = track.album.as_ref()
                .and_then(|a| a.artist.as_ref())
                .and_then(|a| a.name.as_deref())
                .unwrap_or("?");
            println!("\n  {} · {} — {} [{}]\n", t("downloading_track"), artist, title, ql);
            let track_dir = output_dir
                .join(sanitize_filename(artist))
                .join("Singles")
                .join(sanitize_filename(title));
            let path = api.download_track_cancellable(id, format_id, &track_dir, Some(&meta), None)?;
            if let Some(url) = track
                .album
                .as_ref()
                .and_then(|a| a.image.as_ref())
                .and_then(|img| best_cover_url(img))
            {
                let dir = path.parent().unwrap_or(output_dir);
                save_url(&url, &dir.join("cover.jpg"), "cover");
            }
            let _ = history::record(&id.to_string(), "track", Some(title), Some(artist), None, Some(1), quality, format_id, path.parent().and_then(|p| p.to_str()), country, true);
            println!("  {} {}\n", t("saved"), path.display());
        }
        DownloadTarget::Album(ref id) => {
            let album = api.get_album(id, None)?;
            let cover_url = album.image.as_ref().and_then(|img| best_cover_url(img));
            let artist_id = album.artist.as_ref().and_then(|a| a.id);
            let artist_name = album.artist.as_ref().and_then(|a| a.name.as_deref()).map(String::from);
            let album_title = album.title.clone();
            let track_count = album.tracks_count;
            let display_artist = artist_name.as_deref().unwrap_or("?");
            let display_title = album_title.as_deref().unwrap_or("?");
            let n_tracks = track_count.unwrap_or(0);
            println!("\n  {} · {} — {} ({} {}, {})\n",
                t("downloading_album"), display_artist, display_title, n_tracks, t("tracks_suffix"), ql);
            let paths = api.download_album_cancellable(id, format_id, output_dir, Some(&meta), conc, None)?;
            let success = !paths.is_empty();
            let album_dir = paths.first().and_then(|p| p.parent()).map(Path::to_owned);
            if let (Some(url), Some(dir)) = (cover_url, &album_dir) {
                save_url(&url, &dir.join("cover.jpg"), "cover");
            }
            if let Some(ref dir) = album_dir {
                save_album_extras(api, id, dir);
                if let (Some(aid), Some(artist_dir)) = (artist_id, dir.parent()) {
                    save_artist_extras(api, aid, artist_dir);
                }
            }
            println!("\n  ✓ {} — {} · {} {} {}\n",
                display_artist, display_title, t("downloaded_tracks"), paths.len(), t("tracks_suffix"));
            let _ = history::record(id, "album", album_title.as_deref(), artist_name.as_deref(), artist_id.map(|i| i as i64), track_count, quality, format_id, album_dir.as_deref().and_then(|p| p.to_str()), country, success);
        }
        DownloadTarget::Artist(id) => {
            let artist = api.get_artist(id, None)?;
            let name = artist.name.as_deref().unwrap_or("?");
            let n_albums = artist.albums_count.unwrap_or(0);
            println!("\n  {} · {} ({}, ~{} {})\n",
                t("downloading_artist"), name, t("full_discography"), n_albums, t("search_albums"));
            let paths = api.download_artist_cancellable(id, format_id, output_dir, Some(&meta), conc, None)?;
            let success = !paths.is_empty();
            let artist_dir = paths.first().and_then(|p| p.parent()).and_then(|p| p.parent());
            if let Some(dir) = artist_dir {
                save_artist_extras(api, id, dir);
            }
            println!("\n  ✓ {} · {} {} {}\n",
                name, t("downloaded_tracks"), paths.len(), t("tracks_suffix"));
            let _ = history::record(&id.to_string(), "artist", None, Some(name), Some(id as i64), Some(paths.len() as i32), quality, format_id, artist_dir.and_then(|p| p.to_str()), country, success);
        }
        DownloadTarget::Playlist(ref id) => {
            let playlist = api.get_playlist(id, None)?;
            let name = playlist.name.as_deref().unwrap_or("?");
            let n_tracks = playlist.tracks_count.unwrap_or(0);
            println!("\n  {} · {} ({} {}, {})\n",
                t("downloading_playlist"), name, n_tracks, t("tracks_suffix"), ql);
            let paths = api.download_playlist_cancellable(id, format_id, output_dir, Some(&meta), conc, None)?;
            let success = !paths.is_empty();
            println!("\n  ✓ {} · {} {} {}\n",
                name, t("downloaded_tracks"), paths.len(), t("tracks_suffix"));
            let _ = history::record(id, "playlist", Some(name), None, None, Some(paths.len() as i32), quality, format_id, Some(&output_dir.to_string_lossy()), country, success);
        }
    }

    Ok(())
}

#[cfg(test)]
mod tests {
    use qobuz_api::models::album::Image;

    use super::{best_cover_url, quality_to_format_id};

    // --- quality_to_format_id ---

    #[test]
    fn mp3_maps_to_5() { assert_eq!(quality_to_format_id("mp3"), 5); }

    #[test]
    fn flac_maps_to_6() { assert_eq!(quality_to_format_id("flac"), 6); }

    #[test]
    fn flac_hi_maps_to_7() { assert_eq!(quality_to_format_id("flac-hi"), 7); }

    #[test]
    fn flac_ultra_maps_to_27() { assert_eq!(quality_to_format_id("flac-ultra"), 27); }

    #[test]
    fn unknown_quality_defaults_to_6() { assert_eq!(quality_to_format_id("garbage"), 6); }

    // --- best_cover_url ---

    #[test]
    fn rewrites_600_to_org() {
        let img = Image { large: Some("https://static.qobuz.com/covers/abc_600.jpg".into()), ..Image::default() };
        assert_eq!(best_cover_url(&img), Some("https://static.qobuz.com/covers/abc_org.jpg".into()));
    }

    #[test]
    fn large_preferred_over_mega_for_rewrite() {
        let img = Image {
            large: Some("https://cdn/img_600.jpg".into()),
            mega: Some("https://cdn/img_mega.jpg".into()),
            ..Image::default()
        };
        assert_eq!(best_cover_url(&img), Some("https://cdn/img_org.jpg".into()));
    }

    #[test]
    fn falls_back_to_small_as_last_resort() {
        let img = Image { small: Some("https://cdn/img_600.jpg".into()), ..Image::default() };
        assert_eq!(best_cover_url(&img), Some("https://cdn/img_org.jpg".into()));
    }

    #[test]
    fn url_without_600_returned_as_is() {
        let img = Image { large: Some("https://cdn/img.jpg".into()), ..Image::default() };
        assert_eq!(best_cover_url(&img), Some("https://cdn/img.jpg".into()));
    }

    #[test]
    fn returns_none_when_all_fields_empty() {
        assert_eq!(best_cover_url(&Image::default()), None);
    }
}
