use qobuz_api::api::service::QobuzApiService;

use crate::{errors::AppError, search::fmt_duration, url::DownloadTarget};

const DIM: &str = "\x1b[2m";
const RESET: &str = "\x1b[0m";
const BOLD: &str = "\x1b[1m";
const RED: &str = "\x1b[31m";

pub fn run(api: &mut QobuzApiService, target: DownloadTarget, tsv: bool) -> Result<(), AppError> {
    let album_id = match target {
        DownloadTarget::Album(ref id) => id.clone(),
        DownloadTarget::Track(id) => {
            let track = api.get_track(id)?;
            track
                .album
                .as_ref()
                .and_then(|a| a.id.clone())
                .ok_or_else(|| AppError::Other("Track has no associated album".into()))?
        }
        _ => return Err(AppError::Other("inspect only supports album or track URLs".into())),
    };

    let album = api.get_album(&album_id, None)?;

    let artist = album.artist.as_ref().and_then(|a| a.name.as_deref()).unwrap_or("?");
    let title = album.title.as_deref().unwrap_or("?");
    let year = album
        .release_date_original
        .as_deref()
        .and_then(|d| d.split('-').next())
        .unwrap_or("?");
    let label = album.label.as_ref().and_then(|l| l.name.as_deref()).unwrap_or("?");
    let release_type = album
        .release_type
        .as_deref()
        .or(album.product_type.as_deref())
        .unwrap_or("album");
    let hires = album.hires_streamable == Some(true);
    let multi_disc = album.media_count.unwrap_or(1) > 1;

    let tracks = album.tracks.and_then(|t| t.items).unwrap_or_default();
    let locked_count = tracks.iter().filter(|t| t.streamable != Some(true)).count();

    if tsv {
        // Header row — album metadata
        println!(
            "album\t{}\t{}\t{}\t{}\t{}\t{}\t{}",
            esc(title),
            esc(artist),
            esc(year),
            esc(label),
            esc(release_type),
            if hires { "1" } else { "0" },
            locked_count,
        );
        // One row per track:
        // track_num \t disc \t title \t duration_secs \t streamable \t downloadable \t hires \t explicit
        for t in &tracks {
            let num = t.track_number.unwrap_or(0);
            let disc = t.media_number.unwrap_or(1);
            let track_title = t.title.as_deref().unwrap_or("?");
            let version = t.version.as_deref().unwrap_or("");
            let full_title = if version.is_empty() {
                track_title.to_string()
            } else {
                format!("{track_title} ({version})")
            };
            let dur = t.duration.unwrap_or(0);
            let streamable = if t.streamable == Some(true) { "1" } else { "0" };
            let downloadable = if t.downloadable == Some(true) { "1" } else { "0" };
            let track_hires = if t.hires == Some(true) { "1" } else { "0" };
            let explicit = if t.parental_warning == Some(true) { "1" } else { "0" };
            println!(
                "track\t{num}\t{disc}\t{}\t{dur}\t{streamable}\t{downloadable}\t{track_hires}\t{explicit}",
                esc(&full_title),
            );
        }
    } else {
        println!();
        println!("  {BOLD}{artist} — {title}{RESET}  ({year})  {label}  [{release_type}]{}",
            if hires { "  [Hi-Res]" } else { "" });
        println!("  {} track(s)", tracks.len());
        println!();

        if tracks.is_empty() {
            println!("  {DIM}(No track details returned){RESET}");
            println!();
            return Ok(());
        }

        for t in &tracks {
            let num = t.track_number.unwrap_or(0);
            let disc = t.media_number.unwrap_or(1);
            let track_title = t.title.as_deref().unwrap_or("?");
            let version = t.version.as_deref().unwrap_or("");
            let full_title = if version.is_empty() {
                track_title.to_string()
            } else {
                format!("{track_title} ({version})")
            };
            let dur = fmt_duration(t.duration.unwrap_or(0));
            let streamable = t.streamable == Some(true);
            let downloadable = t.downloadable == Some(true);
            let track_hires = t.hires == Some(true);
            let explicit = t.parental_warning == Some(true);

            let disc_prefix = if multi_disc { format!("{disc}-") } else { String::new() };

            let mut badges = String::new();
            if track_hires  { badges.push_str(" [hi-res]"); }
            if explicit     { badges.push_str(" [e]"); }
            if streamable && !downloadable { badges.push_str(" [stream-only]"); }

            if streamable {
                println!("  {disc_prefix}{num:02}. {full_title}  {DIM}{dur}{badges}{RESET}");
            } else {
                println!("  {DIM}{disc_prefix}{num:02}. {full_title}  {dur}{badges}  {RED}✗ locked{RESET}");
            }
        }

        println!();
        if locked_count == 0 {
            println!("  All {} track(s) available in this region.", tracks.len());
        } else {
            println!("  {RED}{locked_count} locked{RESET} / {} total — some tracks unavailable in this region.",
                tracks.len());
        }
        println!();
    }

    Ok(())
}

fn esc(s: &str) -> String {
    s.replace('\t', " ").replace('\n', " ").replace('\r', "")
}
