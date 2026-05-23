use qobuz_api::api::service::QobuzApiService;

use crate::errors::AppError;

pub fn run(api: &mut QobuzApiService, query: &str, kind: &str, tsv: bool, limit: u32) -> Result<(), AppError> {
    let lim = Some(limit as i32);

    let types: &[&str] = if kind == "all" {
        &["albums", "tracks", "artists", "playlists"]
    } else {
        std::slice::from_ref(&kind)
    };

    for t in types {
        run_one(api, query, t, tsv, lim)?;
    }
    Ok(())
}

fn run_one(api: &mut QobuzApiService, query: &str, kind: &str, tsv: bool, lim: Option<i32>) -> Result<(), AppError> {
    match kind {
        "tracks" => {
            let items = api.search_tracks(query, lim, None)?.items.unwrap_or_default();
            if tsv {
                for t in &items {
                    let id       = t.id.unwrap_or(0).to_string();
                    let title    = esc(t.title.as_deref());
                    let artist   = esc(t.performer.as_ref().and_then(|p| p.name.as_deref())
                                    .or_else(|| t.album.as_ref().and_then(|a| a.artist.as_ref()).and_then(|ar| ar.name.as_deref())));
                    let album    = esc(t.album.as_ref().and_then(|a| a.title.as_deref()));
                    let year     = t.album.as_ref().and_then(|a| a.release_date_original.as_deref())
                                    .and_then(|d| d.get(..4)).unwrap_or("");
                    let duration = t.duration.unwrap_or(0).to_string();
                    let genre    = esc(t.album.as_ref().and_then(|a| a.genre.as_ref()).and_then(|g| g.name.as_deref()));
                    let hires    = t.hires.unwrap_or(false).to_string();
                    let label    = esc(t.album.as_ref().and_then(|a| a.label.as_ref()).and_then(|l| l.name.as_deref()));
                    println!("{id}\t{title}\t{artist}\t{album}\t{year}\t{duration}\t{genre}\t{hires}\ttrack\t{label}");
                }
            } else {
                println!("Tracks ({} results):", items.len());
                for t in &items {
                    let title  = t.title.as_deref().unwrap_or("?");
                    let artist = t.performer.as_ref().and_then(|p| p.name.as_deref())
                                  .or_else(|| t.album.as_ref().and_then(|a| a.artist.as_ref()).and_then(|ar| ar.name.as_deref()))
                                  .unwrap_or("?");
                    let id = t.id.unwrap_or(0);
                    println!("  [{id}] {title} — {artist}");
                }
            }
        }
        "artists" => {
            let items = api.search_artists(query, lim, None)?.items.unwrap_or_default();
            if tsv {
                for a in &items {
                    let id   = a.id.unwrap_or(0).to_string();
                    let name = esc(a.name.as_deref());
                    println!("{id}\t{name}\t\t\t\t\t\t\tartist\t");
                }
            } else {
                println!("Artists ({} results):", items.len());
                for a in &items {
                    println!("  [{}] {}", a.id.unwrap_or(0), a.name.as_deref().unwrap_or("?"));
                }
            }
        }
        "playlists" => {
            let items = api.search_playlists(query, lim, None)?.items.unwrap_or_default();
            if tsv {
                for p in &items {
                    let id       = esc(p.id.as_deref());
                    let name     = esc(p.name.as_deref());
                    let duration = p.duration.unwrap_or(0).to_string();
                    println!("{id}\t{name}\t\t\t\t{duration}\t\t\tplaylist\t");
                }
            } else {
                println!("Playlists ({} results):", items.len());
                for p in &items {
                    println!("  [{}] {}", p.id.as_deref().unwrap_or("?"), p.name.as_deref().unwrap_or("?"));
                }
            }
        }
        _ => {
            // "albums" or default
            let items = api.search_albums(query, lim, None)?.items.unwrap_or_default();
            if tsv {
                for a in &items {
                    let id       = esc(a.id.as_deref());
                    let title    = esc(a.title.as_deref());
                    let artist   = esc(a.artist.as_ref().and_then(|ar| ar.name.as_deref()));
                    let year     = a.release_date_original.as_deref().and_then(|d| d.get(..4)).unwrap_or("");
                    let duration = a.duration.unwrap_or(0).to_string();
                    let genre    = esc(a.genre.as_ref().and_then(|g| g.name.as_deref()));
                    let hires    = a.hires.unwrap_or(false).to_string();
                    let label    = esc(a.label.as_ref().and_then(|l| l.name.as_deref()));
                    println!("{id}\t{title}\t{artist}\t{title}\t{year}\t{duration}\t{genre}\t{hires}\talbum\t{label}");
                }
            } else {
                println!("Albums ({} results):", items.len());
                for a in &items {
                    let title  = a.title.as_deref().unwrap_or("?");
                    let artist = a.artist.as_ref().and_then(|ar| ar.name.as_deref()).unwrap_or("?");
                    let year   = a.release_date_original.as_deref().unwrap_or("?");
                    let id     = a.id.as_deref().unwrap_or("?");
                    println!("  [{id}] {title} — {artist} ({year})");
                }
            }
        }
    }
    Ok(())
}

// Sanitize a field: replace tabs and newlines so TSV stays valid
fn esc(s: Option<&str>) -> String {
    s.unwrap_or("").replace('\t', " ").replace('\n', " ").replace('\r', "")
}
