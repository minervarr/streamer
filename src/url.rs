/// A resolved download target parsed from a URL or bare ID.
#[derive(Debug)]
pub enum DownloadTarget {
    Track(i32),
    Album(String),
    Artist(i32),
    Playlist(String),
}

/// Parse a Qobuz URL or bare ID into a `DownloadTarget`.
///
/// Supported URL forms:
/// - `https://www.qobuz.com/{locale}/album/{slug}/{id}`
/// - `https://www.qobuz.com/{locale}/track/{slug}/{id}`
/// - `https://www.qobuz.com/{locale}/interpreter/{slug}/{id}`
/// - `https://open.qobuz.com/album/{id}`
/// - `https://open.qobuz.com/track/{id}`
/// - `https://open.qobuz.com/artist/{id}`
/// - `https://open.qobuz.com/playlist/{id}`
///
/// Bare ID fallback: all-digits → Track, otherwise → Album.
pub fn parse(input: &str) -> Option<DownloadTarget> {
    let input = input.trim();

    if input.starts_with("http://") || input.starts_with("https://") {
        return parse_url(input);
    }

    // Short numeric ID (fits i32) → track; everything else → album
    if input.chars().all(|c| c.is_ascii_digit()) {
        if let Ok(id) = input.parse::<i32>() {
            return Some(DownloadTarget::Track(id));
        }
    }
    Some(DownloadTarget::Album(input.to_string()))
}

fn parse_url(url: &str) -> Option<DownloadTarget> {
    let url = url.trim_end_matches('/');
    let path = url
        .split_once("qobuz.com/")
        .map(|(_, p)| p)?;

    let segments: Vec<&str> = path.split('/').collect();

    // open.qobuz.com/{type}/{id}
    if url.contains("open.qobuz.com") {
        match segments.as_slice() {
            ["album", id] => return Some(DownloadTarget::Album(id.to_string())),
            ["track", id] => return id.parse().ok().map(DownloadTarget::Track),
            ["artist", id] => return id.parse().ok().map(DownloadTarget::Artist),
            ["playlist", id] => return Some(DownloadTarget::Playlist(id.to_string())),
            _ => return None,
        }
    }

    // www.qobuz.com/{locale}/{type}/{slug}/{id}
    // or www.qobuz.com/{type}/{slug}/{id}
    // Find the type keyword and take the segment after the slug
    for (i, seg) in segments.iter().enumerate() {
        match *seg {
            "album" => {
                let id = segments.get(i + 2).or_else(|| segments.get(i + 1))?;
                return Some(DownloadTarget::Album(id.to_string()));
            }
            "track" => {
                let id = segments.get(i + 2).or_else(|| segments.get(i + 1))?;
                return id.parse().ok().map(DownloadTarget::Track);
            }
            "interpreter" | "artist" => {
                let id = segments.get(i + 2).or_else(|| segments.get(i + 1))?;
                return id.parse().ok().map(DownloadTarget::Artist);
            }
            "playlist" | "playlists" => {
                let id = segments.get(i + 2).or_else(|| segments.get(i + 1))?;
                return Some(DownloadTarget::Playlist(id.to_string()));
            }
            _ => {}
        }
    }

    None
}

#[cfg(test)]
mod tests {
    use super::*;

    fn matches_track(input: &str, expected_id: i32) {
        match parse(input) {
            Some(DownloadTarget::Track(id)) => assert_eq!(id, expected_id, "input: {input}"),
            other => panic!("expected Track({expected_id}), got {other:?} for input: {input}"),
        }
    }
    fn matches_album(input: &str, expected_id: &str) {
        match parse(input) {
            Some(DownloadTarget::Album(id)) => assert_eq!(id, expected_id, "input: {input}"),
            other => panic!("expected Album({expected_id}), got {other:?} for input: {input}"),
        }
    }
    fn matches_artist(input: &str, expected_id: i32) {
        match parse(input) {
            Some(DownloadTarget::Artist(id)) => assert_eq!(id, expected_id, "input: {input}"),
            other => panic!("expected Artist({expected_id}), got {other:?} for input: {input}"),
        }
    }
    fn matches_playlist(input: &str, expected_id: &str) {
        match parse(input) {
            Some(DownloadTarget::Playlist(id)) => assert_eq!(id, expected_id, "input: {input}"),
            other => panic!("expected Playlist({expected_id}), got {other:?} for input: {input}"),
        }
    }

    // --- open.qobuz.com ---

    #[test]
    fn open_album() {
        matches_album("https://open.qobuz.com/album/0060075362362", "0060075362362");
    }

    #[test]
    fn open_album_trailing_slash() {
        matches_album("https://open.qobuz.com/album/0060075362362/", "0060075362362");
    }

    #[test]
    fn open_track() {
        matches_track("https://open.qobuz.com/track/12345678", 12345678);
    }

    #[test]
    fn open_artist() {
        matches_artist("https://open.qobuz.com/artist/9876543", 9876543);
    }

    #[test]
    fn open_playlist() {
        matches_playlist("https://open.qobuz.com/playlist/plist-abc", "plist-abc");
    }

    // --- www.qobuz.com localized ---

    #[test]
    fn www_album_with_locale_and_slug() {
        matches_album(
            "https://www.qobuz.com/us-en/album/dark-side-of-the-moon-pink-floyd/0060075362362",
            "0060075362362",
        );
    }

    #[test]
    fn www_track_with_locale_and_slug() {
        matches_track(
            "https://www.qobuz.com/us-en/track/money-pink-floyd/12345678",
            12345678,
        );
    }

    #[test]
    fn www_interpreter() {
        matches_artist(
            "https://www.qobuz.com/us-en/interpreter/pink-floyd/9876543",
            9876543,
        );
    }

    #[test]
    fn www_playlist() {
        matches_playlist(
            "https://www.qobuz.com/us-en/playlist/my-mix/playlist-xyz",
            "playlist-xyz",
        );
    }

    // --- bare IDs ---

    #[test]
    fn bare_numeric_id_is_track() {
        matches_track("12345678", 12345678);
    }

    #[test]
    fn bare_string_id_is_album() {
        matches_album("0060075362362abc", "0060075362362abc");
    }

    #[test]
    fn bare_string_with_spaces_trimmed() {
        matches_album("  my-album-id  ", "my-album-id");
    }

    // --- edge cases ---

    #[test]
    fn unknown_url_returns_none() {
        assert!(parse("https://open.qobuz.com/unknown/segment/extra").is_none());
    }

    #[test]
    fn non_qobuz_url_returns_none() {
        assert!(parse("https://example.com/album/123").is_none());
    }
}
