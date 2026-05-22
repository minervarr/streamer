use std::{fs, path::Path};

use qobuz_api::{
    api::service::QobuzApiService,
    metadata::config::{MetadataConfig, MetadataField},
    models::album::Image,
};

use crate::{errors::AppError, extras, url::DownloadTarget};

pub fn quality_to_format_id(quality: &str) -> i32 {
    match quality {
        "mp3" => 5,
        "flac-hi" => 7,
        "flac-ultra" => 27,
        _ => 6, // "flac" default
    }
}

fn no_art_config() -> MetadataConfig {
    let mut config = MetadataConfig::default();
    config.set(MetadataField::CoverArt, false);
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
        eprintln!("Warning: could not save {}: {e}", path.display());
    } else {
        println!("Saved: {}", path.display());
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
                    eprintln!("Warning: could not save {label}: {e}");
                } else {
                    println!("Saved: {}", dest.display());
                }
            }
            Err(e) => eprintln!("Warning: could not read {label} bytes: {e}"),
        },
        Err(e) => eprintln!("Warning: could not download {label}: {e}"),
    }
}

fn save_album_extras(api: &QobuzApiService, album_id: &str, album_dir: &Path) {
    match extras::fetch_album_extras(api, album_id) {
        Ok(ex) => {
            if let Some(desc) = ex.description.as_deref() {
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
        Err(e) => eprintln!("Warning: could not fetch album extras: {e}"),
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
        Err(e) => eprintln!("Warning: could not fetch artist extras: {e}"),
    }
}

pub fn run(
    api: &mut QobuzApiService,
    target: DownloadTarget,
    quality: &str,
    output_dir: &Path,
    concurrency: usize,
) -> Result<(), AppError> {
    let format_id = quality_to_format_id(quality);
    let meta = no_art_config();
    let conc = Some(concurrency);

    match target {
        DownloadTarget::Track(id) => {
            println!("Downloading track {id}...");
            let track = api.get_track(id)?;
            let path = api.download_track_cancellable(id, format_id, output_dir, Some(&meta), None)?;
            if let Some(url) = track
                .album
                .as_ref()
                .and_then(|a| a.image.as_ref())
                .and_then(|img| best_cover_url(img))
            {
                let dir = path.parent().unwrap_or(output_dir);
                save_url(&url, &dir.join("cover.jpg"), "cover");
            }
            println!("Saved: {}", path.display());
        }
        DownloadTarget::Album(ref id) => {
            println!("Downloading album {id}...");
            let album = api.get_album(id, None)?;
            let cover_url = album
                .image
                .as_ref()
                .and_then(|img| best_cover_url(img));
            let artist_id = album.artist.as_ref().and_then(|a| a.id);
            let paths = api.download_album_cancellable(id, format_id, output_dir, Some(&meta), conc, None)?;
            println!("Downloaded {} track(s).", paths.len());
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
        }
        DownloadTarget::Artist(id) => {
            println!("Downloading artist {id} (full discography)...");
            let paths = api.download_artist_cancellable(id, format_id, output_dir, Some(&meta), conc, None)?;
            println!("Downloaded {} track(s).", paths.len());
            let artist_dir = paths
                .first()
                .and_then(|p| p.parent())
                .and_then(|p| p.parent());
            if let Some(dir) = artist_dir {
                save_artist_extras(api, id, dir);
            }
        }
        DownloadTarget::Playlist(ref id) => {
            println!("Downloading playlist {id}...");
            let paths = api.download_playlist_cancellable(id, format_id, output_dir, Some(&meta), conc, None)?;
            println!("Downloaded {} track(s).", paths.len());
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
