# Library manager — design

Date: 2026-08-06

## Problem

`library.db` records every downloaded album, but nothing can remove one. The
only way to reclaim space today is to delete directories by hand and then run
`streamer library scan` to prune the rows. There is also no way to *see* the
library: covers are on disk at `<root>/<country>/<album_id>/cover.jpg` and
never rendered anywhere.

## Scope

A third GUI screen (`Screen::Library`) beside Search and Settings:

- A scrolling grid of album tiles, each showing its real cover art.
- Albums with no readable `cover.jpg` show a solid `#000000` tile with a
  centered "No artwork" message, so a missing cover is visibly deliberate
  rather than a rendering bug.
- Click to toggle selection; a Delete button acts on the selection.
- Deleting removes both the files on disk and the catalog rows — a row-only
  delete would be re-adopted by the next `library scan`, so it would not
  stick.
- A modal confirm lists the albums and the total size before anything is
  unlinked.

Out of scope for this iteration: per-track deletion, a trash/undo directory,
editing metadata.

## Data layer (`src/library.hh` / `.cpp`)

```cpp
struct DeleteReport {
    int     files_removed  = 0;   // audio files unlinked
    int     assets_removed = 0;   // cover.jpg / booklet.pdf / album_description.txt
    int64_t bytes_freed    = 0;
    std::vector<std::string> failed;  // paths that would not unlink
    bool    rows_removed   = false;
};

DeleteReport delete_album(const std::string& root, const std::string& album_id,
                          bool dry_run = false);
```

Three properties this holds to:

1. **Never a recursive delete of a path.** It unlinks exactly the files the
   catalog lists for the album (`files` + `assets` rows), then `rmdir`s the
   album directory only if it came out empty. A stray file placed in there
   survives, and an empty or corrupt catalog cannot be talked into removing a
   directory it does not understand.
2. **Rows are deleted in one transaction, after the unlinks.** If unlinking
   fails halfway, the catalog still reflects what is actually on disk.
   Deleted: `files`, `assets`, `tracks`, `album_artists`, `albums` for that
   id. `artists` / `labels` / `genres` are shared and stay.
3. **Failures are reported, not swallowed.** The rest of `library.cpp`
   deliberately never throws, because a catalog hiccup must not lose a
   finished download. Here the user asked for something to be gone, so a
   silent failure is the wrong answer.

`AlbumEntry` grows three fields the grid needs and nothing currently supplies:

- `country` — the path segment, needed to locate the album directory.
- `cover_path` — relative path of the `cover` asset, empty when absent.
- `bytes_on_disk` — summed `files.bytes`, so the confirm dialog can state the
  size and `library list` gets it for free.

## GUI layer

- `gui/src/library_controller.{hh,cc}` — state and decisions: album list
  loaded from `library::list_albums`, the selection set, the cover texture
  cache, delete dispatch, and the resulting status line. No drawing.
- `gui/src/library_view.{hh,cc}` — drawing only, mirroring the
  Controller/View split `views.hh` already documents. A new file rather than
  growing `views.cc`, which is already 545 lines covering two screens.
- `gui_main.cc` gains `Screen::Library`, a third nav entry, and hit dispatch
  in a `LibraryAction` range distinct from the existing ones.

**Cover textures** load lazily, only for tiles currently on screen, via
`createTextureFromImageFile()` decoded at tile resolution. The cache is capped
(64 textures) and evicted least-recently-drawn, so a library of thousands of
albums does not exhaust VRAM. A decode failure is cached as a negative result
so a broken JPEG is not retried every frame.

**Deletion runs synchronously** on the frame thread. It is a handful of
`unlink` calls per album; the download path, which is genuinely slow, is the
one that needs a thread.

## Testing

`gui --selftest` already builds a throwaway `library.db` in a temp directory
for the downloaded-ids check (`gui_main.cc:261`). The same pattern covers
deletion headlessly: record an album, create its files, `delete_album`, then
assert the files are gone, the rows are gone, and `bytes_freed` is right —
plus a dry-run case that changes nothing and a case where a file is already
missing.
