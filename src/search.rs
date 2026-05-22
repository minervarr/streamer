use qobuz_api::api::service::QobuzApiService;

use crate::errors::AppError;

pub fn run(api: &mut QobuzApiService, query: &str, kind: &str) -> Result<(), AppError> {
    match kind {
        "tracks" => {
            let results = api.search_tracks(query, Some(20), None)?;
            let items = results.items.unwrap_or_default();
            println!("Tracks ({} results):", items.len());
            for t in &items {
                let title = t.title.as_deref().unwrap_or("?");
                let artist = t
                    .performer
                    .as_ref()
                    .and_then(|p| p.name.as_deref())
                    .or_else(|| {
                        t.album
                            .as_ref()
                            .and_then(|a| a.artist.as_ref())
                            .and_then(|ar| ar.name.as_deref())
                    })
                    .unwrap_or("?");
                let id = t.id.unwrap_or(0);
                println!("  [{id}] {title} — {artist}");
            }
        }
        "artists" => {
            let results = api.search_artists(query, Some(20), None)?;
            let items = results.items.unwrap_or_default();
            println!("Artists ({} results):", items.len());
            for a in &items {
                let name = a.name.as_deref().unwrap_or("?");
                let id = a.id.unwrap_or(0);
                println!("  [{id}] {name}");
            }
        }
        "playlists" => {
            let results = api.search_playlists(query, Some(20), None)?;
            let items = results.items.unwrap_or_default();
            println!("Playlists ({} results):", items.len());
            for p in &items {
                let name = p.name.as_deref().unwrap_or("?");
                let id = p.id.as_deref().unwrap_or("?");
                println!("  [{id}] {name}");
            }
        }
        _ => {
            // "albums" or default
            let results = api.search_albums(query, Some(20), None)?;
            let items = results.items.unwrap_or_default();
            println!("Albums ({} results):", items.len());
            for a in &items {
                let title = a.title.as_deref().unwrap_or("?");
                let artist = a
                    .artist
                    .as_ref()
                    .and_then(|ar| ar.name.as_deref())
                    .unwrap_or("?");
                let year = a.release_date_original.as_deref().unwrap_or("?");
                let id = a.id.as_deref().unwrap_or("?");
                println!("  [{id}] {title} — {artist} ({year})");
            }
        }
    }
    Ok(())
}
