use qobuz_api::api::service::QobuzApiService;

use crate::{errors::AppError, i18n::t};

// ── Duration parsing ──────────────────────────────────────────────────────────

/// Parse a human-readable duration string into seconds.
///
/// Accepted formats:
///   - `600`       → 600 s (bare integer = seconds)
///   - `10m`       → 600 s
///   - `1h30m`     → 5400 s
///   - `2h`        → 7200 s
///   - `1:30`      → 90 s  (mm:ss)
///   - `1:30:00`   → 5400 s (hh:mm:ss)
pub fn parse_duration(s: &str) -> Result<i32, String> {
    let s = s.trim();

    // hh:mm:ss or mm:ss
    if s.contains(':') {
        let parts: Vec<&str> = s.split(':').collect();
        return match parts.len() {
            2 => {
                let m = parts[0].parse::<i32>().map_err(|_| format!("invalid duration: {s}"))?;
                let sec = parts[1].parse::<i32>().map_err(|_| format!("invalid duration: {s}"))?;
                Ok(m * 60 + sec)
            }
            3 => {
                let h = parts[0].parse::<i32>().map_err(|_| format!("invalid duration: {s}"))?;
                let m = parts[1].parse::<i32>().map_err(|_| format!("invalid duration: {s}"))?;
                let sec = parts[2].parse::<i32>().map_err(|_| format!("invalid duration: {s}"))?;
                Ok(h * 3600 + m * 60 + sec)
            }
            _ => Err(format!("invalid duration: {s}")),
        };
    }

    // Unit-based: e.g. "1h30m", "10m", "2h", "90s", "90"
    let mut total = 0i32;
    let mut num_buf = String::new();
    let mut has_unit = false;
    for ch in s.chars() {
        if ch.is_ascii_digit() {
            num_buf.push(ch);
        } else {
            let n: i32 = num_buf.parse().map_err(|_| format!("invalid duration: {s}"))?;
            num_buf.clear();
            match ch {
                'h' | 'H' => { total += n * 3600; has_unit = true; }
                'm' | 'M' => { total += n * 60;   has_unit = true; }
                's' | 'S' => { total += n;         has_unit = true; }
                _ => return Err(format!("unknown unit '{ch}' in duration: {s}")),
            }
        }
    }
    // Trailing digits with no unit → seconds
    if !num_buf.is_empty() {
        let n: i32 = num_buf.parse().map_err(|_| format!("invalid duration: {s}"))?;
        if has_unit {
            return Err(format!("trailing number without unit in duration: {s}"));
        }
        total += n;
    }
    Ok(total)
}

// ── Duration display ──────────────────────────────────────────────────────────

fn fmt_duration(secs: i32) -> String {
    let h = secs / 3600;
    let m = (secs % 3600) / 60;
    let s = secs % 60;
    if h > 0 {
        format!("{h}:{m:02}:{s:02}")
    } else {
        format!("{m}:{s:02}")
    }
}

// ── Filter helpers ────────────────────────────────────────────────────────────

fn passes(duration_secs: i32, min: Option<i32>, max: Option<i32>) -> bool {
    min.map_or(true, |m| duration_secs >= m) && max.map_or(true, |m| duration_secs <= m)
}

// ── Public entry point ────────────────────────────────────────────────────────

fn normalize_type(kind: &str) -> &str {
    match kind.to_ascii_lowercase().as_str() {
        "álbumes" | "albumes" | "álbum" | "album" | "albums" => "albums",
        "pistas" | "pista" | "tracks" => "tracks",
        "artistas" | "artista" | "artists" => "artists",
        "listas" | "lista" | "playlists" => "playlists",
        "todos" | "todo" | "all" => "all",
        _ => kind,
    }
}

pub fn run(
    api: &mut QobuzApiService,
    query: &str,
    kind: &str,
    tsv: bool,
    limit: u32,
    min_secs: Option<i32>,
    max_secs: Option<i32>,
) -> Result<(), AppError> {
    let kind = normalize_type(kind);
    let types: &[&str] = if kind == "all" {
        &["albums", "tracks", "artists", "playlists"]
    } else {
        std::slice::from_ref(&kind)
    };
    for kind in types {
        run_one(api, query, kind, tsv, limit, min_secs, max_secs)?;
    }
    Ok(())
}

fn run_one(
    api: &mut QobuzApiService,
    query: &str,
    kind: &str,
    tsv: bool,
    limit: u32,
    min_secs: Option<i32>,
    max_secs: Option<i32>,
) -> Result<(), AppError> {
    let filtering = min_secs.is_some() || max_secs.is_some();
    // Fetch more from the API when filtering to compensate for post-filter drop-off
    let fetch_limit = if filtering { Some((limit * 5).min(500) as i32) } else { Some(limit as i32) };

    match kind {
        "tracks" => {
            let all = api.search_tracks(query, fetch_limit, None)?.items.unwrap_or_default();
            let items: Vec<_> = all.into_iter()
                .filter(|t| passes(t.duration.unwrap_or(0), min_secs, max_secs))
                .take(limit as usize)
                .collect();
            if tsv {
                for t in &items {
                    let id        = t.id.unwrap_or(0).to_string();
                    let raw_title = esc(t.title.as_deref());
                    let version   = t.version.as_deref().unwrap_or("");
                    let title     = if version.is_empty() { raw_title.clone() } else { format!("{raw_title} ({version})") };
                    let artist    = esc(t.performer.as_ref().and_then(|p| p.name.as_deref())
                                     .or_else(|| t.album.as_ref().and_then(|a| a.artist.as_ref()).and_then(|ar| ar.name.as_deref())));
                    let label     = esc(t.album.as_ref().and_then(|a| a.label.as_ref()).and_then(|l| l.name.as_deref()));
                    let date      = esc(t.album.as_ref().and_then(|a| a.release_date_original.as_deref()));
                    let duration  = t.duration.unwrap_or(0).to_string();
                    let genre     = esc(t.album.as_ref().and_then(|a| a.genre.as_ref()).and_then(|g| g.name.as_deref()));
                    let hires     = t.hires.unwrap_or(false).to_string();
                    let explicit  = if t.parental_warning == Some(true) { "true" } else { "" };
                    println!("{id}\t{title}\t{artist}\t{label}\t{date}\t{duration}\t{genre}\t{hires}\ttrack\t{label}\t{explicit}");
                }
            } else {
                println!("{} ({} {}):", t("search_tracks"), items.len(), t("results_suffix"));
                for t in &items {
                    let title  = t.title.as_deref().unwrap_or("?");
                    let artist = t.performer.as_ref().and_then(|p| p.name.as_deref())
                                   .or_else(|| t.album.as_ref().and_then(|a| a.artist.as_ref()).and_then(|ar| ar.name.as_deref()))
                                   .unwrap_or("?");
                    let id = t.id.unwrap_or(0);
                    if filtering {
                        let dur = fmt_duration(t.duration.unwrap_or(0));
                        println!("  [{id}] {title} — {artist}  [{dur}]");
                    } else {
                        println!("  [{id}] {title} — {artist}");
                    }
                }
            }
        }
        "artists" => {
            // Artists have no duration — fetch and display as-is, duration filter is a no-op
            let items = api.search_artists(query, Some(limit as i32), None)?.items.unwrap_or_default();
            if tsv {
                for a in &items {
                    let id   = a.id.unwrap_or(0).to_string();
                    let name = esc(a.name.as_deref());
                    println!("{id}\t{name}\t\t\t\t\t\t\tartist\t\t");
                }
            } else {
                println!("{} ({} {}):", t("search_artists"), items.len(), t("results_suffix"));
                for a in &items {
                    println!("  [{}] {}", a.id.unwrap_or(0), a.name.as_deref().unwrap_or("?"));
                }
            }
        }
        "playlists" => {
            let all = api.search_playlists(query, fetch_limit, None)?.items.unwrap_or_default();
            let items: Vec<_> = all.into_iter()
                .filter(|p| passes(p.duration.unwrap_or(0), min_secs, max_secs))
                .take(limit as usize)
                .collect();
            if tsv {
                for p in &items {
                    let id       = esc(p.id.as_deref());
                    let name     = esc(p.name.as_deref());
                    let duration = p.duration.unwrap_or(0).to_string();
                    println!("{id}\t{name}\t\t\t\t{duration}\t\t\tplaylist\t\t");
                }
            } else {
                println!("{} ({} {}):", t("search_playlists"), items.len(), t("results_suffix"));
                for p in &items {
                    let id   = p.id.as_deref().unwrap_or("?");
                    let name = p.name.as_deref().unwrap_or("?");
                    if filtering {
                        let dur = fmt_duration(p.duration.unwrap_or(0));
                        println!("  [{id}] {name}  [{dur}]");
                    } else {
                        println!("  [{id}] {name}");
                    }
                }
            }
        }
        _ => {
            // "albums" or default
            let all = api.search_albums(query, fetch_limit, None)?.items.unwrap_or_default();
            let items: Vec<_> = all.into_iter()
                .filter(|a| passes(a.duration.unwrap_or(0), min_secs, max_secs))
                .take(limit as usize)
                .collect();
            if tsv {
                for a in &items {
                    let id        = esc(a.id.as_deref());
                    let raw_title = esc(a.title.as_deref());
                    let version   = a.version.as_deref().unwrap_or("");
                    let title     = if version.is_empty() { raw_title.clone() } else { format!("{raw_title} ({version})") };
                    let artist    = esc(a.artist.as_ref().and_then(|ar| ar.name.as_deref()));
                    let label     = esc(a.label.as_ref().and_then(|l| l.name.as_deref()));
                    let date      = esc(a.release_date_original.as_deref());
                    let duration  = a.duration.unwrap_or(0).to_string();
                    let genre     = esc(a.genre.as_ref().and_then(|g| g.name.as_deref()));
                    let hires     = a.hires.unwrap_or(false).to_string();
                    let explicit  = if a.parental_warning == Some(true) { "true" } else { "" };
                    println!("{id}\t{title}\t{artist}\t{label}\t{date}\t{duration}\t{genre}\t{hires}\talbum\t{label}\t{explicit}");
                }
            } else {
                println!("{} ({} {}):", t("search_albums"), items.len(), t("results_suffix"));
                for a in &items {
                    let title  = a.title.as_deref().unwrap_or("?");
                    let artist = a.artist.as_ref().and_then(|ar| ar.name.as_deref()).unwrap_or("?");
                    let year   = a.release_date_original.as_deref().unwrap_or("?");
                    let id     = a.id.as_deref().unwrap_or("?");
                    if filtering {
                        let dur = fmt_duration(a.duration.unwrap_or(0));
                        println!("  [{id}] {title} — {artist} ({year})  [{dur}]");
                    } else {
                        println!("  [{id}] {title} — {artist} ({year})");
                    }
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

// ── Tests ─────────────────────────────────────────────────────────────────────

#[cfg(test)]
mod tests {
    use super::parse_duration;

    #[test]
    fn parse_seconds_bare()      { assert_eq!(parse_duration("90").unwrap(),    90); }
    #[test]
    fn parse_minutes()           { assert_eq!(parse_duration("10m").unwrap(),   600); }
    #[test]
    fn parse_hours()             { assert_eq!(parse_duration("2h").unwrap(),    7200); }
    #[test]
    fn parse_hours_and_minutes() { assert_eq!(parse_duration("1h30m").unwrap(), 5400); }
    #[test]
    fn parse_colon_mmss()        { assert_eq!(parse_duration("1:30").unwrap(),  90); }
    #[test]
    fn parse_colon_hhmmss()      { assert_eq!(parse_duration("1:30:00").unwrap(), 5400); }
    #[test]
    fn parse_with_seconds_unit() { assert_eq!(parse_duration("90s").unwrap(),   90); }
    #[test]
    fn parse_full_combo()        { assert_eq!(parse_duration("1h2m3s").unwrap(), 3723); }
    #[test]
    fn parse_invalid()           { assert!(parse_duration("abc").is_err()); }
}
