// streamer GUI — portable skeleton. Owns the dirty-flag frame loop; the
// window, the pump, the input and the paths come from app_shell
// (framework/app_shell), shared with Matrix Player. StreamerApp
// (streamer_app.hh) is this app's end of that seam: it implements app_shell's
// AppView through FrameInputView, so everything below still reads a FrameInput
// exactly as it did when streamer carried its own host layer.
//
// Search screen is the default mode: query text field -> SearchController,
// sortable results table, cheatsheet panel, in-process download dispatch.
// Click/hover dispatch follows the engine's established idiom (scanersito's
// gui_main.cc): match the PREVIOUS frame's Hit rects against THIS frame's
// pointer state, then draw fresh Hit rects for the next iteration — this
// avoids needing draw() to return geometry before it has computed it.

#include "account_pool.hh"
#include "config.hh"
#include "download.hh"
#include "fonts.hh"
#include "library.hh"
#include "library_controller.hh"
#include "library_view.hh"
#include "query_dsl.hh"
#include "search_controller.hh"
#include "service_factory.hh"
#include "streamer_app.hh"
#include "ui_orientation.hh"  // app_shell: the ONE portrait/landscape rule
#include "theme.hh"
#include "views.hh"

#include "canvas.hh"
#include "frame_input.hh"
#include "keys.hh"
#include "widgets.hh"

#include <api/service.hh>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <memory>
#include <set>
#include <string>
#include <thread>
#include <vector>

namespace {

// Headless smoke-test entry point (scanersito's convention): exercises
// pure-logic pieces with no window/GPU/network. query_dsl assertions cover
// every operator and field, including the exact accent-preservation
// scenario that motivated this port (see query_dsl.hh) — typing "Luísa
// Sonza" must match a row for "Luísa Sonza" without corruption, and an
// ALL-CAPS accented query must still fold correctly against a lowercase
// accented row.
int g_fail_count = 0;
int g_check_count = 0;

void check(bool cond, const char* what) {
    ++g_check_count;
    if (!cond) { std::fprintf(stderr, "FAIL: %s\n", what); ++g_fail_count; }
}

int run_selftest() {
    using namespace query_dsl;

    SearchResult luisa;
    luisa.title = "Chico Não Vou Fazer Nada";
    luisa.artist = "Luísa Sonza";
    luisa.type = "track";
    luisa.year = 2023;
    luisa.duration = 195;
    luisa.hires = false;

    // Bare term, accent preserved through tokenizer + case-fold.
    check(Match(Parse("Sonza"), luisa), "bare term 'Sonza' matches artist");
    check(Match(Parse("Luísa Sonza"), luisa), "bare terms 'Luísa Sonza' match (accent intact)");
    check(Match(Parse("LUÍSA"), luisa), "uppercase accented 'LUÍSA' folds to match 'Luísa'");
    check(!Match(Parse("Shakira"), luisa), "unrelated term does not match");

    // Diacritic-insensitive matching: a US/ASCII keyboard has no way to type
    // "í", so an unaccented "luisa sonza" must still find "Luísa Sonza".
    check(Match(Parse("Luisa Sonza"), luisa), "unaccented 'Luisa Sonza' matches accented 'Luísa Sonza'");
    check(Match(Parse("luisa"), luisa), "unaccented lowercase 'luisa' matches 'Luísa'");
    check(Match(Parse("artist:=Luisa Sonza"), luisa) == false,
         "artist:=Luisa Sonza (exact, unquoted) still splits into two terms — quoting note applies regardless of accents");
    check(Match(Parse("artist:=\"Luisa Sonza\""), luisa), "artist:=\"Luisa Sonza\" (exact, quoted, unaccented) matches accented 'Luísa Sonza'");

    // Quoted filter value, accent preserved.
    check(Match(Parse("artist:\"Luísa Sonza\""), luisa), "artist:\"Luísa Sonza\" matches");
    check(!Match(Parse("artist:\"Anitta\""), luisa), "artist:\"Anitta\" does not match");

    // Exact-match operator '=': field:value stays "contains", field:=value
    // is exact/case-insensitive — the bug this fixes is artist:"poppy"
    // matching both "Poppy" and "Poppy Ajudha" with no way to ask for just
    // the former.
    SearchResult poppy;
    poppy.title = "Poppy";
    poppy.artist = "Poppy";
    poppy.type = "artist";

    SearchResult poppyAjudha;
    poppyAjudha.title = "Trouble";
    poppyAjudha.artist = "Poppy Ajudha";
    poppyAjudha.type = "track";

    check(Match(Parse("artist:poppy"), poppy), "artist:poppy (contains) matches 'Poppy'");
    check(Match(Parse("artist:poppy"), poppyAjudha), "artist:poppy (contains) also matches 'Poppy Ajudha'");
    check(Match(Parse("artist:=poppy"), poppy), "artist:=poppy (exact) matches 'Poppy'");
    check(!Match(Parse("artist:=poppy"), poppyAjudha), "artist:=poppy (exact) does not match 'Poppy Ajudha'");
    check(Match(Parse("artist:=\"Luísa Sonza\""), luisa), "artist:=\"Luísa Sonza\" (exact, quoted) matches the full name");
    check(!Match(Parse("artist:=Luísa"), luisa), "artist:=Luísa (exact, partial) does not match 'Luísa Sonza'");

    // ── Multi-column stacked sort: CycleSortKey's 3-state-per-column cycle
    // (ascending -> descending -> removed), and ApplySort's stacked
    // comparator (earlier keys in `keys` take priority; later ones only
    // break ties). Pure functions — no SearchController/network needed.
    {
        using namespace search;

        std::vector<SortKey> keys;
        CycleSortKey(keys, SortColumn::Artist);
        check(keys.size() == 1 && keys[0].col == SortColumn::Artist && keys[0].asc,
             "1st click on Artist: appended, ascending");

        CycleSortKey(keys, SortColumn::Duration);
        check(keys.size() == 2 && keys[0].col == SortColumn::Artist &&
             keys[1].col == SortColumn::Duration && keys[1].asc,
             "1st click on Duration: appended as secondary, Artist stays primary");

        CycleSortKey(keys, SortColumn::Artist);
        check(keys.size() == 2 && keys[0].col == SortColumn::Artist && !keys[0].asc,
             "2nd click on Artist: flips to descending in place, Duration still secondary");

        CycleSortKey(keys, SortColumn::Artist);
        check(keys.size() == 1 && keys[0].col == SortColumn::Duration,
             "3rd click on Artist: removed from the stack, Duration promoted to primary");

        query_dsl::SearchResult ana, bea1, bea2;
        ana.artist = "Ana";  ana.duration = 300;
        bea1.artist = "Bea"; bea1.duration = 200;
        bea2.artist = "Bea"; bea2.duration = 100;
        std::vector<query_dsl::SearchResult> results = {bea1, ana, bea2};

        ApplySort(results, {{SortColumn::Artist, true}});
        check(results[0].artist == "Ana" && results[1].artist == "Bea" && results[2].artist == "Bea",
             "ApplySort: single key groups by Artist ascending");

        ApplySort(results, {{SortColumn::Artist, true}, {SortColumn::Duration, true}});
        check(results[0].artist == "Ana" &&
             results[1].artist == "Bea" && results[1].duration == 100 &&
             results[2].artist == "Bea" && results[2].duration == 200,
             "ApplySort: Artist stays primary, Duration breaks ties within the Bea group ascending");
    }

    // Numeric range and comparison.
    check(Match(Parse("year:2020-2024"), luisa), "year:2020-2024 matches 2023");
    check(!Match(Parse("year:2000-2010"), luisa), "year:2000-2010 does not match 2023");
    check(Match(Parse("duration:>100"), luisa), "duration:>100 matches 195");
    check(!Match(Parse("duration:<100"), luisa), "duration:<100 does not match 195");

    // Boolean AND (implicit)/OR/NOT.
    check(Match(Parse("sonza year:2023"), luisa), "implicit AND: sonza year:2023");
    check(!Match(Parse("sonza year:1999"), luisa), "implicit AND fails on wrong year");
    check(Match(Parse("type:album or type:track"), luisa), "OR: type:album or type:track");
    check(Match(Parse("not hires:true"), luisa), "NOT: not hires:true (hires is false)");
    check(!Match(Parse("not sonza"), luisa), "NOT: not sonza excludes a matching row");

    // Base term / type hint extraction (used to build the API query).
    check(ExtractBaseTerm(Parse("artist:\"Luísa Sonza\" year:2023")) == "Luísa Sonza",
         "ExtractBaseTerm picks the artist filter value");
    check(ExtractTypeHint(Parse("type:album sonza")) == "album",
         "ExtractTypeHint reads type: filter");
    check(ExtractTypeHint(Parse("sonza")).empty(), "ExtractTypeHint empty with no type: filter");

    // ── Keystroke pipeline: CharEvent codepoint -> FrameInput -> the search
    // box's actual text buffer. This is the path a real user's typing goes
    // through — the previous assertions above prove Match/Parse handle
    // accented strings correctly, but not that typing them in produces the
    // same bytes. "Luísa Sonza" has both a 2-byte UTF-8 codepoint (í,
    // U+00ED) and a plain-ASCII run, so this exercises the UTF-8-boundary-
    // safe insert/backspace/cursor-move logic in textFieldHandleInput, not
    // just an already-correct hardcoded std::string.
    {
        widgets::TextFieldState field;
        FrameInput in;
        auto type = [&](const std::u32string& s) {
            in.beginFrame();
            for (char32_t cp : s) in.onChar(CharEvent{(uint32_t)cp});
            widgets::textFieldHandleInput(field, in);
        };
        type(U"Lu");
        type(U"í");
        type(U"sa Sonza");
        check(field.text == "Luísa Sonza", "typed codepoints assemble to correct UTF-8 bytes");
        check(field.cursorByte == field.text.size(), "cursor ends at the buffer's end");

        // Backspace after 'í' must remove the whole 2-byte sequence, not one
        // byte (which would leave a dangling continuation byte / mojibake).
        widgets::TextFieldState field2;
        auto type2 = [&](const std::u32string& s) {
            in.beginFrame();
            for (char32_t cp : s) in.onChar(CharEvent{(uint32_t)cp});
            widgets::textFieldHandleInput(field2, in);
        };
        type2(U"Luí");
        in.beginFrame();
        in.onKey(KeyEvent{key::Backspace, true});
        widgets::textFieldHandleInput(field2, in);
        check(field2.text == "Lu", "backspace after an accented char removes the whole codepoint");
        check(field2.cursorByte == 2, "cursor lands after 'u', not mid-codepoint");
    }

    // ── Text-field selection/clipboard/word-jump shortcuts ──────────────────
    {
        struct FakeClipboard : widgets::ClipboardIo {
            std::string stored;
            void setText(const std::string& s) override { stored = s; }
            std::string getText() override { return stored; }
        } clip;

        auto press = [](widgets::TextFieldState& f, int keyCode, bool ctrl, bool shift,
                        widgets::ClipboardIo* cb = nullptr) {
            FrameInput in;
            in.ctrlDown = ctrl;
            in.shiftDown = shift;
            in.onKey(KeyEvent{keyCode, true});
            widgets::textFieldHandleInput(f, in, cb);
        };
        auto type = [](widgets::TextFieldState& f, const std::string& s) {
            FrameInput in;
            for (char c : s) in.onChar(CharEvent{(uint32_t)(unsigned char)c});
            widgets::textFieldHandleInput(f, in);
        };

        // Ctrl+A selects all.
        widgets::TextFieldState f;
        type(f, "hello world");
        press(f, key::A, /*ctrl=*/true, /*shift=*/false);
        check(f.selectionAnchor == 0 && f.cursorByte == f.text.size(),
             "Ctrl+A selects the whole field");

        // Ctrl+C copies the selection; Ctrl+V pastes over it.
        press(f, key::C, true, false, &clip);
        check(clip.stored == "hello world", "Ctrl+C copies the current selection");
        f.selectionAnchor = 0; f.cursorByte = 5;   // select "hello"
        press(f, key::V, true, false, &clip);
        check(f.text == "hello world world", "Ctrl+V replaces the selection with clipboard text");

        // Ctrl+Left/Right jump by word; Ctrl+Shift+Left extends by word.
        widgets::TextFieldState g;
        type(g, "foo bar baz");
        g.cursorByte = g.text.size();
        g.selectionAnchor = g.cursorByte;
        press(g, key::Left, /*ctrl=*/true, /*shift=*/false);
        check(g.cursorByte == 8, "Ctrl+Left jumps to the start of the previous word");
        check(g.selectionAnchor == g.cursorByte, "a plain Ctrl+Left collapses any selection");
        press(g, key::Left, /*ctrl=*/true, /*shift=*/true);
        check(g.cursorByte == 4 && g.selectionAnchor == 8,
             "Ctrl+Shift+Left extends the selection by one more word");

        // textFieldHandleClick (single/double-click positioning + word
        // select) needs a live Canvas/font to measure text, so it isn't
        // exercised headlessly here — covered by manual QA instead.
    }

    // ── "Already downloaded" highlight: library::downloaded_track_ids /
    // downloaded_album_ids drive SearchController::refresh_downloaded's
    // no-re-buy guard. Exercised here against a throwaway library.db rather
    // than through SearchController itself, since populating results_
    // requires a live search() call.
    {
        std::string root = (std::filesystem::temp_directory_path() /
                            "streamer_gui_selftest_library").string();
        std::filesystem::remove_all(root);

        auto make_track = [](int id, const std::string& title) {
            auto t = std::make_shared<kb::Track>();
            t->id = id;
            t->title = title;
            return t;
        };

        // ALBUM1: both of its 2 tracks get downloaded at format 6 (flac).
        kb::Album album1;
        album1.id = "ALBUM1";
        album1.title = "Fully Downloaded";
        album1.tracks_count = 2;
        album1.tracks = kb::ItemSearchResult<std::shared_ptr<kb::Track>>{};
        album1.tracks->items = {make_track(101, "Track A"), make_track(102, "Track B")};
        library::record_download(root, album1, {"101.6.flac", "102.6.flac"}, "flac", 6, "US");

        // ALBUM2: only 1 of its 2 tracks gets downloaded — must not count as
        // a fully-downloaded album.
        kb::Album album2;
        album2.id = "ALBUM2";
        album2.title = "Partially Downloaded";
        album2.tracks_count = 2;
        album2.tracks = kb::ItemSearchResult<std::shared_ptr<kb::Track>>{};
        album2.tracks->items = {make_track(201, "Track C"), make_track(202, "Track D")};
        library::record_download(root, album2, {"201.6.flac"}, "flac", 6, "US");

        auto tracks_flac = library::downloaded_track_ids(root, {"101", "102", "201", "999"}, 6);
        check(tracks_flac == std::set<std::string>({"101", "102", "201"}),
             "downloaded_track_ids finds every downloaded id in the requested format, ignores unknown ids");

        auto tracks_ultra = library::downloaded_track_ids(root, {"101"}, 27);
        check(tracks_ultra.empty(),
             "downloaded_track_ids does not match a different format/quality than what's on disk");

        auto albums_flac = library::downloaded_album_ids(root, {"ALBUM1", "ALBUM2"}, 6);
        check(albums_flac == std::set<std::string>({"ALBUM1"}),
             "downloaded_album_ids only counts an album once every one of its tracks is downloaded");

        std::filesystem::remove_all(root);
    }

    // ── library::delete_album + LibraryController ───────────────────────────
    // The one destructive path in the app, so it is exercised against real
    // files in a temp tree rather than trusted to review. Uses the real
    // ID-addressed layout (<root>/<country>/<album_id>/<track>.<fmt>.<ext>)
    // because delete_album's directory cleanup depends on it.
    {
        namespace fs = std::filesystem;
        const std::string root =
            (fs::temp_directory_path() / "streamer_gui_selftest_delete").string();
        fs::remove_all(root);

        const fs::path albumDir = fs::path(root) / "US" / "ALBUM1";
        fs::create_directories(albumDir);
        auto write_file = [](const fs::path& p, size_t bytes) {
            std::FILE* f = std::fopen(p.string().c_str(), "wb");
            std::vector<char> data(bytes, 'x');
            if (f) { std::fwrite(data.data(), 1, data.size(), f); std::fclose(f); }
            return p.string();
        };
        const std::string trackA = write_file(albumDir / "101.6.flac", 1000);
        const std::string trackB = write_file(albumDir / "102.6.flac", 2000);
        const std::string cover  = write_file(albumDir / "cover.jpg", 500);

        kb::Album album;
        album.id = "ALBUM1";
        album.title = "Deletable";
        album.tracks_count = 2;
        album.tracks = kb::ItemSearchResult<std::shared_ptr<kb::Track>>{};
        {
            auto t1 = std::make_shared<kb::Track>(); t1->id = 101; t1->title = "A";
            auto t2 = std::make_shared<kb::Track>(); t2->id = 102; t2->title = "B";
            album.tracks->items = {t1, t2};
        }
        library::record_download(root, album, {trackA, trackB}, "flac", 6, "US");
        const std::string albumId = "ALBUM1";   // kb::Album::id is an optional
        library::record_asset(root, "cover", &albumId, nullptr, cover);

        auto listed = library::list_albums(root, 50);
        check(listed.size() == 1 && listed[0].country == "US",
             "list_albums reports the country tier the album's files live under");
        check(listed.size() == 1 && !listed[0].cover_path.empty(),
             "list_albums reports the registered cover asset's path");
        check(listed.size() == 1 && listed[0].bytes_on_disk == 3000,
             "list_albums sums the album's file sizes (cover art is not track bytes)");

        // Dry run must report exactly what a real run would, and touch nothing.
        auto dry = library::delete_album(root, "ALBUM1", /*dry_run=*/true);
        check(dry.files_removed == 2 && dry.assets_removed == 1 && dry.bytes_freed == 3000,
             "delete_album --dry-run reports the files, assets and bytes it would remove");
        check(fs::exists(trackA) && fs::exists(cover),
             "delete_album --dry-run leaves every file where it was");
        check(!library::list_albums(root, 50).empty(),
             "delete_album --dry-run leaves the catalog rows alone");

        // A stray file the catalog never knew about must survive, and must
        // keep the album directory alive with it.
        const std::string stray = write_file(albumDir / "notes.txt", 10);

        auto rep = library::delete_album(root, "ALBUM1");
        check(rep.rows_removed && rep.failed.empty(),
             "delete_album removes the catalog rows when every unlink succeeded");
        check(rep.files_removed == 2 && rep.assets_removed == 1 && rep.bytes_freed == 3000,
             "delete_album reports the files, assets and bytes it actually removed");
        check(!fs::exists(trackA) && !fs::exists(trackB) && !fs::exists(cover),
             "delete_album unlinks the album's tracks and its cover art");
        check(fs::exists(stray),
             "delete_album never touches a file the catalog did not list");
        check(fs::exists(albumDir),
             "delete_album leaves the album directory when something is still in it");
        check(library::list_albums(root, 50).empty(),
             "delete_album leaves no album rows behind");
        check(library::resolve(root, "101") == std::nullopt,
             "delete_album cascades to the album's tracks and files");

        // Deleting again, and deleting something the catalog never had, are
        // both no-ops rather than errors — the GUI can double-click Delete.
        auto again = library::delete_album(root, "ALBUM1");
        check(!again.rows_removed && again.files_removed == 0 && again.failed.empty(),
             "delete_album on an album the catalog no longer has is a silent no-op");
        auto unknown = library::delete_album(root, "NOPE");
        check(!unknown.rows_removed && unknown.failed.empty(),
             "delete_album on an unknown album id is a silent no-op");

        // An album whose directory is empty of strays goes away entirely.
        fs::remove(stray);
        const fs::path albumDir2 = fs::path(root) / "US" / "ALBUM2";
        fs::create_directories(albumDir2);
        const std::string only = write_file(albumDir2 / "201.6.flac", 700);
        kb::Album album2;
        album2.id = "ALBUM2";
        album2.title = "Sweepable";
        album2.tracks_count = 1;
        album2.tracks = kb::ItemSearchResult<std::shared_ptr<kb::Track>>{};
        {
            auto t = std::make_shared<kb::Track>(); t->id = 201; t->title = "C";
            album2.tracks->items = {t};
        }
        library::record_download(root, album2, {only}, "flac", 6, "US");
        library::delete_album(root, "ALBUM2");
        check(!fs::exists(albumDir2),
             "delete_album rmdirs the album directory once it is empty");

        // A file the catalog listed but disk had already lost is not a
        // failure: the row still has to go, it just frees nothing.
        const fs::path albumDir3 = fs::path(root) / "US" / "ALBUM3";
        fs::create_directories(albumDir3);
        const std::string vanishing = write_file(albumDir3 / "301.6.flac", 900);
        kb::Album album3;
        album3.id = "ALBUM3";
        album3.title = "Already Gone";
        album3.tracks_count = 1;
        album3.tracks = kb::ItemSearchResult<std::shared_ptr<kb::Track>>{};
        {
            auto t = std::make_shared<kb::Track>(); t->id = 301; t->title = "D";
            album3.tracks->items = {t};
        }
        library::record_download(root, album3, {vanishing}, "flac", 6, "US");
        fs::remove(vanishing);
        auto ghost = library::delete_album(root, "ALBUM3");
        check(ghost.rows_removed && ghost.failed.empty() && ghost.bytes_freed == 0 &&
              ghost.files_removed == 0,
             "delete_album prunes the row of a file disk already lost, freeing nothing");

        fs::remove_all(root);
    }

    // ── LibraryController: selection and the delete confirmation ────────────
    {
        namespace fs = std::filesystem;
        const std::string root =
            (fs::temp_directory_path() / "streamer_gui_selftest_libctl").string();
        fs::remove_all(root);

        auto add_album = [&](const std::string& id, int trackId, size_t bytes) {
            const fs::path dir = fs::path(root) / "US" / id;
            fs::create_directories(dir);
            const fs::path file = dir / (std::to_string(trackId) + ".6.flac");
            std::FILE* f = std::fopen(file.string().c_str(), "wb");
            std::vector<char> data(bytes, 'x');
            if (f) { std::fwrite(data.data(), 1, data.size(), f); std::fclose(f); }
            kb::Album a;
            a.id = id;
            a.title = "Album " + id;
            a.tracks_count = 1;
            a.tracks = kb::ItemSearchResult<std::shared_ptr<kb::Track>>{};
            auto t = std::make_shared<kb::Track>(); t->id = trackId; t->title = "T";
            a.tracks->items = {t};
            library::record_download(root, a, {file.string()}, "flac", 6, "US");
        };
        add_album("A1", 11, 100);
        add_album("A2", 22, 200);

        libmgr::LibraryController ctl(root);
        ctl.reload();
        check(ctl.albums().size() == 2, "LibraryController::reload reads the catalog");

        ctl.toggle_selected(0);
        check(ctl.selection_count() == 1 && ctl.is_selected(0),
             "toggling a row selects it");
        ctl.toggle_selected(0);
        check(ctl.selection_count() == 0, "toggling the same row again deselects it");

        ctl.select_all();
        check(ctl.selection_count() == 2 && ctl.selection_bytes() == 300,
             "select_all selects every album and sums their bytes");
        ctl.clear_selection();
        check(ctl.selection_count() == 0, "clear_selection empties the selection");

        // Delete refuses to happen without the confirmation step.
        ctl.toggle_selected(0);
        ctl.confirm_delete();
        check(ctl.albums().size() == 2,
             "confirm_delete does nothing unless a delete was requested first");

        ctl.request_delete();
        check(ctl.delete_pending(), "request_delete arms the confirmation");
        ctl.cancel_delete();
        check(!ctl.delete_pending() && ctl.albums().size() == 2,
             "cancel_delete disarms it and deletes nothing");

        ctl.request_delete();
        ctl.confirm_delete();
        check(ctl.albums().size() == 1 && ctl.selection_count() == 0,
             "confirm_delete removes the selected album and clears the selection");
        check(!ctl.status().empty() && !ctl.status_is_error(),
             "a successful delete leaves a non-error status line");

        ctl.clear_status();
        check(ctl.status().empty(), "clear_status dismisses the status line");

        // An empty selection must never arm the dialog — otherwise the modal
        // could come up offering to delete nothing.
        ctl.request_delete();
        check(!ctl.delete_pending(), "request_delete is a no-op with an empty selection");

        check(libmgr::format_bytes(0) == "0 B" && libmgr::format_bytes(1536) == "1.5 KB" &&
              libmgr::format_bytes(1024 * 1024) == "1.0 MB",
             "format_bytes scales to a readable unit with one decimal above bytes");

        fs::remove_all(root);
    }

    // ── account::Pool: failover policy ────────────────────────────────────
    // The bug this exists for: a stored token expires, its account happens to
    // sit first in config.toml, and every command dies on a bare 401 while a
    // working account sits one slot below. classify() and ranked() are pure,
    // so the whole policy is testable with no network and no config file.
    {
        using account::Failure;

        kb::Error e401 = kb::api_error_response(401, "User authentication is required", "error");
        check(account::classify(e401) == Failure::Auth,
             "a 401 from Qobuz classifies as an auth failure");
        check(account::classify(kb::not_found_error("album", "123")) == Failure::Unavailable,
             "a not-found classifies as regionally unavailable");
        check(account::classify(kb::rate_limit_error("slow down")) == Failure::Network,
             "rate limiting classifies as Network");

        check(account::should_failover(Failure::Auth) &&
              account::should_failover(Failure::Unavailable),
             "auth and availability failures are worth retrying on another account");
        // The important negative: the limit is per app_id and every account
        // shares one, so walking accounts multiplies the requests and cannot
        // possibly help. Same for a dead link.
        check(!account::should_failover(Failure::Network) &&
              !account::should_failover(Failure::Other),
             "network and rate-limit failures do NOT walk the account list");

        const int64_t now = 1'000'000'000;
        config::Config cfg;
        config::Account dead;  dead.country = "FR"; dead.auth_token = "t1";
        dead.last_fail = now - 60; dead.fail_reason = "auth";
        config::Account live;  live.country = "NZ"; live.auth_token = "t2";
        live.last_ok = now - 60;
        config::Account notok; notok.country = "US";  // no token — unusable
        cfg.accounts = {dead, live, notok};

        account::Pool pool(cfg);
        auto order = pool.ranked(now);
        check(order.size() == 2, "ranked() skips accounts with no auth token");
        check(order.size() == 2 && order[0] == 1 && order[1] == 0,
             "a recently auth-failed account ranks below a known-good one");

        // A revoked token can be repaired elsewhere, so a demotion must expire
        // — otherwise one bad day blacklists a good account permanently.
        auto later = pool.ranked(now + 7 * 60 * 60);
        check(later.size() == 2 && later[0] == 1 && later[1] == 0,
             "a known-good account still leads once the demotion expires");
        check(std::find(later.begin(), later.end(), 0) != later.end(),
             "an expired demotion puts the account back in the running");

        // An explicit --country is a preference, not a hard pin: the named
        // account leads, but the rest still back it up.
        auto pinned = pool.candidates(account::Selector::from_country("FR"));
        check(!pinned.empty() && pinned[0] == 0,
             "--country FR tries FR first");
        check(pinned.size() == 2,
             "--country FR still falls back to the other accounts behind it");

        check(account::Selector::from_country("all").mode == account::Selector::All,
             "--country all selects every account");
        check(account::Selector::from_country("").mode == account::Selector::Auto,
             "no --country means health-ranked auto selection");
    }

    // ── country:all merge ─────────────────────────────────────────────────
    // Asking every account for the same query returns the same release once
    // per region. Showing those as separate rows would bury the answer the
    // user actually wanted, which is *where* a release can be had.
    {
        auto row = [](const char* id, const char* type, const char* country) {
            query_dsl::SearchResult r;
            r.id = id; r.type = type; r.country = country;
            return r;
        };
        std::vector<query_dsl::SearchResult> rows = {
            row("A1", "album", "FR"),
            row("A2", "album", "FR"),
            row("A1", "album", "NZ"),   // same release, second region
            row("A1", "track", "NZ"),   // same id, different kind — must NOT merge
        };
        search::MergeByIdAcrossCountries(rows);

        check(rows.size() == 3, "the same release from two accounts collapses to one row");
        check(rows[0].id == "A1" && rows[0].country == "FR, NZ",
             "the merged row lists every region that offered it");
        check(rows[1].id == "A2" && rows[1].country == "FR",
             "a release only one account had keeps its single region");
        // Qobuz ids are only unique within a kind, so keying on id alone would
        // let an album and a track swallow each other.
        check(rows[2].id == "A1" && rows[2].type == "track",
             "an identical id of a different type stays its own row");
        check(rows[0].type == "album",
             "the merge preserves the first sighting's position and fields");

        // country: routes as well as filters, so the routing keyword has to
        // survive the filter pass. Treated naively, `country:all` would look
        // for a region literally named "all" and hide every single row.
        query_dsl::SearchResult nz;
        nz.country = "NZ"; nz.type = "album"; nz.title = "x";
        check(query_dsl::Match(query_dsl::Parse("country:all"), nz),
             "country:all is a routing keyword and filters nothing out");
        check(query_dsl::Match(query_dsl::Parse("country:NZ"), nz),
             "country:NZ matches a row served by the NZ account");
        check(!query_dsl::Match(query_dsl::Parse("country:FR"), nz),
             "country:FR excludes a row only NZ could serve");
        check(query_dsl::ExtractCountryHint(query_dsl::Parse("daft country:NZ")) == "NZ",
             "ExtractCountryHint pulls the routing country out of the query");
        check(query_dsl::ExtractCountryHint(query_dsl::Parse("daft punk")).empty(),
             "a query with no country: term routes by the picker instead");
    }

    // Input-method text entry (Android). An IME does not type characters, it
    // rewrites the pending run: composing 가 goes ㄱ -> 가, and the second state
    // is not the first plus something. These assert that a whole-buffer report
    // replaces the field rather than appending to it — the bug this seam exists
    // to prevent is a search box reading "ㄱ가" after one keystroke.
    {
        widgets::TextFieldState field;
        FrameInput imeInput;

        imeInput.beginFrame();
        imeInput.onTextEdit(TextEditEvent{"\xE3\x84\xB1", 3});           // ㄱ
        check(widgets::textFieldHandleInput(field, imeInput, nullptr),
             "an input method's first composing state enters the field");
        check(field.text == "\xE3\x84\xB1", "the field holds exactly what the IME reported");

        imeInput.beginFrame();
        imeInput.onTextEdit(TextEditEvent{"\xEA\xB0\x80", 3});           // 가
        check(widgets::textFieldHandleInput(field, imeInput, nullptr),
             "the next composing state is applied");
        check(field.text == "\xEA\xB0\x80",
             "composing REPLACES the pending run (가), it does not append to it (ㄱ가)");
        check(field.cursorByte == 3 && field.selectionAnchor == field.cursorByte,
             "the caret follows the IME and no selection is left behind");

        // Deletion arrives the same way — a soft keyboard's backspace reaches us
        // as a shorter buffer, never as a Backspace keycode.
        imeInput.beginFrame();
        imeInput.onTextEdit(TextEditEvent{"", 0});
        check(widgets::textFieldHandleInput(field, imeInput, nullptr) && field.text.empty(),
             "clearing via the IME empties the field with no Backspace keycode involved");

        // Idempotence: an IME re-reports unchanged text often (caret moves,
        // focus churn). Reporting `changed` there would repaint every frame.
        imeInput.beginFrame();
        imeInput.onTextEdit(TextEditEvent{"", 0});
        check(!widgets::textFieldHandleInput(field, imeInput, nullptr),
             "re-reporting identical text is not a change");

        // The delta path must be inert while an IME owns the buffer, or every
        // keystroke lands twice on a device with a hardware keyboard attached.
        field.text = "abc"; field.cursorByte = 3; field.selectionAnchor = 3;
        imeInput.beginFrame();
        imeInput.onChar(CharEvent{'d'});
        imeInput.onTextEdit(TextEditEvent{"abcd", 4});
        widgets::textFieldHandleInput(field, imeInput, nullptr);
        check(field.text == "abcd",
             "typedCodepoints is ignored when the IME reported the buffer (no double insert)");

        // Desktop must be untouched: nothing emits TextEditEvent there, so the
        // ordinary typing path has to behave exactly as it did before.
        widgets::TextFieldState desktop;
        FrameInput plain;
        plain.beginFrame();
        plain.onChar(CharEvent{'h'});
        plain.onChar(CharEvent{'i'});
        check(widgets::textFieldHandleInput(desktop, plain, nullptr) && desktop.text == "hi",
             "with no IME the normal typing path is unchanged");
    }

    // Which face serves which script, off the real fonts. No GPU and no
    // window: RasterFont bakes on the CPU, so the metrics that decide whether
    // Russian looks spread out are checkable here rather than by eye.
    glyph_selftest([](bool cond, const char* what) { check(cond, what); });

    // ── The standardisation rule, pinned ────────────────────────────────────
    //
    // One layout decided by the WINDOW, never by the platform. These assert it
    // in the only way that can be checked without a screen: the same width
    // must give the same answer whatever is running it.
    {
        const size_t all = gui::searchTableColumns().size();
        check(all == 11, "the search table defines eleven columns");

        // A desktop window affords every column.
        check(gui::searchTableColumnsFor(1920.0f).size() == all,
              "1920px shows every column");
        check(gui::searchTableColumnsFor(1280.0f).size() == all,
              "1280px still shows every column");

        // A phone upright does not, and drops rather than squeezing: eleven
        // across 720px gives the first one 32px, which draws an ellipsis and
        // nothing else.
        const auto narrow = gui::searchTableColumnsFor(720.0f);
        check(narrow.size() < all, "720px drops columns instead of squeezing");
        check(narrow.size() >= 3, "720px still keeps Sel, Title and Artist");
        check(narrow[0].label == "Sel" && narrow[1].label == "Title" &&
              narrow[2].label == "Artist",
              "the columns that survive are the first three, in order");

        // Monotonic: a wider window never shows FEWER columns. Without this a
        // resize could take information away as it gained room.
        size_t prev = 0;
        bool monotonic = true;
        for (float w = 200.0f; w <= 2000.0f; w += 20.0f) {
            const size_t n = gui::searchTableColumnsFor(w).size();
            if (n < prev) monotonic = false;
            prev = n;
        }
        check(monotonic, "widening the window never removes a column");

        // Degenerate widths answer, rather than looping or returning nothing.
        check(gui::searchTableColumnsFor(0.0f).size() == 3, "zero width keeps the three");
        check(gui::searchTableColumnsFor(-100.0f).size() == 3, "negative width keeps the three");

        // The orientation rule is the window's shape and nothing else, so a
        // desktop window this shape must answer exactly as the phone does.
        check(autoOrientationFor(720, 1640) == UiOrientation::Vertical,
              "a phone upright is Vertical");
        check(autoOrientationFor(720, 1640) == autoOrientationFor(720, 1640),
              "the same shape always answers the same");
        check(autoOrientationFor(1640, 720) == UiOrientation::Horizontal,
              "the same phone on its side is Horizontal, like a desktop");
    }

    if (g_fail_count == 0) {
        std::printf("selftest: ok (%d assertions)\n", g_check_count);
        return 0;
    }
    std::fprintf(stderr, "selftest: %d assertion(s) failed\n", g_fail_count);
    return 1;
}

// ── Dev-only fake data for --widget-preview (Phase 4's sortable-table check) ─

const std::vector<widgets::TableColumn>& previewColumns() {
    static const std::vector<widgets::TableColumn> cols = {
        {"Title", 2.2f}, {"Artist", 1.6f}, {"Label", 1.4f}, {"Date", 0.9f},
        {"Duration", 0.8f}, {"Genre", 1.1f}, {"Hi-Res", 0.7f}, {"Explicit", 0.8f},
        {"Type", 0.8f}, {"Country", 0.9f},
    };
    return cols;
}

std::string previewCell(int row, int col) {
    static const char* titles[] = {"Chico Não Vou Fazer Nada", "Cheguei", "Escândalo Íntimo",
                                   "Serena", "Anaconda", "Fallin'", "Doçura", "Boa Menina",
                                   "Modo Turbo", "Penhasco 2"};
    static const char* artists[] = {"Luísa Sonza", "MC Kevin", "Anitta", "Ivete Sangalo",
                                    "Marília Mendonça", "Ana Castela", "Pabllo Vittar",
                                    "Manu Bahtidão", "Wiu", "Djonga"};
    switch (col) {
        case 0: return titles[row % 10];
        case 1: return artists[row % 10];
        case 2: return "Warner Music";
        case 3: return std::to_string(2018 + (row % 7));
        case 4: return std::to_string(150 + row * 7);
        case 5: return "Pop";
        case 6: return (row % 3 == 0) ? "yes" : "no";
        case 7: return (row % 5 == 0) ? "yes" : "no";
        case 8: return "track";
        case 9: return "BR";
        default: return "";
    }
}

// Fire-and-forget background download: mirrors the CLI's `streamer download`
// dispatch (src/download.hh's dl::run), off the render thread so a slow
// transfer never blocks the UI. No progress UI in this phase — a Downloads
// screen is future work; this proves the in-process, no-subprocess wiring.
// Takes the pool by pointer rather than an account by value so a download
// started while the first account's token is dead still lands, on whichever
// account can actually serve it. The pool outlives every worker (it lives for
// the whole of main), and Pool's own locking makes the shared access safe.
// Everything the download worker has to say, and — until now — the only
// channel it had to say it on was stdout. A GUI started from a desktop icon
// has no console attached and an Android app has nowhere to attach one, so
// this is what the frame loop reads to draw the progress strip.
// What the progress strip draws, written by download threads and read by the
// frame loop.
//
// ── Why the mutex is private ────────────────────────────────────────────────
//
// Writing this is only half of a correct update. The other half is WAKING the
// frame loop, which is asleep in pump() with no timeout — a worker thread
// storing a number changes nothing on screen by itself.
//
// Six places used to lock `m` directly and exactly one of them woke the loop
// (the progress sink). The five that did not included the one that matters
// most: `active = false`, the moment a download FINISHES. The measured symptom
// was a strip stuck at "99%, ETA 00:00" that completed the instant the user
// touched the screen — the touch was a real event, and finishing was not.
//
// So the mutex is private and `update()` is the only way in. Locking it by
// hand no longer compiles, which is the point: this cannot be got wrong again
// by forgetting, only by deliberately adding a second path.
class DownloadUi {
public:
    dl::Progress p;
    bool active = false;
    int queueDone = 0, queueTotal = 0;  // position within a multi-album batch
    std::string note;                   // the last thing that went wrong

    // Set once by run(), before anything can start a download. Atomic because
    // the download threads read it while the UI thread wrote it.
    std::atomic<Host*> host{nullptr};

    // Mutate under the lock, then ring the doorbell. Safe from any thread.
    template <class Fn>
    void update(Fn&& fn) {
        {
            std::lock_guard<std::mutex> lk(m_);
            fn();
        }
        // OUTSIDE the lock: postAppEvent writes an eventfd, and doing that
        // while holding a mutex the UI thread is about to want turns a
        // wake-up into a stall.
        if (Host* h = host.load(std::memory_order_relaxed)) h->postAppEvent(0);
    }

    // One consistent copy for the frame about to be drawn — never field by
    // field, or the percentage and the "finished" flag could come from either
    // side of the same update.
    struct Snapshot {
        dl::Progress p;
        bool active = false;
        int queueDone = 0, queueTotal = 0;
        std::string note;
        bool done = false;   // finished, and there was something to finish
    };
    Snapshot read() const {
        std::lock_guard<std::mutex> lk(m_);
        Snapshot s;
        s.p = p; s.active = active;
        s.queueDone = queueDone; s.queueTotal = queueTotal; s.note = note;
        s.done = !active && queueTotal > 0;
        return s;
    }

private:
    mutable std::mutex m_;
};
DownloadUi g_dl;

void download_async(account::Pool* pool, const account::Selector& sel,
                    std::string quality, std::string download_dir,
                    int concurrency, std::vector<std::string> ids) {
    if (concurrency < 1) concurrency = 1;
    g_dl.update([&] {
        g_dl.active     = true;
        g_dl.queueDone  = 0;
        g_dl.queueTotal = (int)ids.size();
        g_dl.note.clear();
        g_dl.p = dl::Progress{};
        g_dl.p.label = "Starting ...";
    });
    std::thread([pool, sel, quality, download_dir, concurrency, ids]() {
        for (int idx : pool->candidates(sel)) {
            auto svc = pool->acquire(idx);
            if (!svc.ok()) {
                account::Failure f = account::classify(svc.error());
                pool->record_fail(idx, f, svc.error().message);
                std::fprintf(stderr, "[download] %s\n", svc.error().message.c_str());
                g_dl.update([&] { g_dl.note = svc.error().message; });
                if (!account::should_failover(f)) {
                    g_dl.update([&] { g_dl.active = false; });
                    return;
                }
                continue;
            }
            pool->record_ok(idx);
            // The country tier the files land under must be the account that
            // actually fetched them (<root>/<country>/<album>/…), or the
            // library catalog would record a region the bytes never came from.
            const std::string country = pool->config().accounts[idx].country;
            for (auto& id : ids) {
                bool ok = dl::run(*svc.value(), id, quality, download_dir, country,
                                  concurrency, true, true);
                std::fprintf(stderr, "[download] %s (%s): %s\n", id.c_str(),
                             country.c_str(), ok ? "done" : "failed");
                g_dl.update([&] {
                    ++g_dl.queueDone;
                    if (!ok) g_dl.note = "Failed: " + id;
                });
            }
            // The end of the run, and the update that used to be missed: the
            // last progress callback drew 99% and nothing woke the loop again.
            g_dl.update([&] { g_dl.active = false; });
            return;
        }
        std::fprintf(stderr, "[download] no account could authenticate\n");
        g_dl.update([&] {
            g_dl.active = false;
            g_dl.note = "No account could authenticate";
        });
    }).detach();
}

} // namespace

// ── Entry ───────────────────────────────────────────────────────────────────
//
// app_shell owns main()/WinMain() (os/wayland_host.cc, os/win32_host.cc) and
// calls this; on Android it owns android_main() and streamer's own six-line
// android_main.cc calls it. Either way the bootstrap — the log file, the crash
// handler, the window — has already happened by the time this runs, and what
// is left is the application's: parse the flags, build the app, loop.
//
// argc is 0 on Android: an app started by an Intent has no command line, so
// every flag here is desktop-only by construction. That is correct — they are
// development tooling.
int app_shell_main(int argc, char** argv) {
    StreamerApp::Options opts;
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--selftest") == 0) return run_selftest();
        if (std::strcmp(argv[i], "--theme-preview") == 0) opts.themePreview = true;
        if (std::strcmp(argv[i], "--widget-preview") == 0) opts.widgetPreview = true;
        if (std::strcmp(argv[i], "--demo") == 0) opts.demo = true;
        if (std::strcmp(argv[i], "--settings") == 0) opts.startSettings = true;
        if (std::strcmp(argv[i], "--capture") == 0 && i + 1 < argc) opts.capturePath = argv[++i];
    }

    StreamerApp app;
    // On Android the host is INJECTED rather than manufactured: an AndroidHost
    // needs the android_app* that only android_main() is ever handed, so
    // make_host() there returns null by design. g_injected_host is what
    // android_main.cc leaves for us.
    std::unique_ptr<Host> owned;
    Host* host = g_injected_host;
    if (!host) {
        owned = make_host();
        host = owned.get();
    }
    if (!host || !host->init(&app)) {
        std::fprintf(stderr, "[x] failed to initialize the GUI host\n");
        return 1;
    }
    if (!app.create(host)) return 1;
    host->showWindow();
    return app.run(opts);
}

int StreamerApp::run(const Options& opts) {
    const bool theme_preview  = opts.themePreview;
    const bool widget_preview = opts.widgetPreview;
    const bool demo           = opts.demo;
    const bool start_settings = opts.startSettings;
    const char* capture_path  = opts.capturePath;

    // Bridges textFieldHandleInput's platform-agnostic widgets::ClipboardIo
    // seam to this host's native clipboard, so core/ never links against
    // Wayland/Win32 directly.
    struct HostClipboardIo : widgets::ClipboardIo {
        Host* host;
        explicit HostClipboardIo(Host* h) : host(h) {}
        void setText(const std::string& utf8) override { host->setClipboardText(utf8); }
        std::string getText() override { return host->getClipboardText(); }
    } clipboardIo(host_);

    config::Config cfg = config::load();
    if (cfg.accounts.empty()) cfg.accounts.push_back({});
    // One long-lived pool, not a captured cfg.accounts.front(). The old code
    // bound the *first* account once at startup, so an expired token in slot 0
    // broke search and downloads for the whole session and the Settings
    // account picker had no effect on either. The pool re-picks per request,
    // fails over, and caches one authenticated service per account.
    account::Pool accountPool(cfg);
    kb::api::set_requests_per_minute(cfg.settings.requests_per_minute);

    std::string stale_msdf_cache = (config::config_path().parent_path() / "msdf.cache").string();
    init_fonts(host_->assetReader(), stale_msdf_cache);

    // The sizes text is actually drawn at. Cells are per-size, and the type
    // scale is a fraction of window height (theme::TypeScale), so this moves
    // with every resize — refresh_glyphs() starts the sheet over when it does.
    // Sizes not listed here (button labels derive theirs from box geometry)
    // arrive through the miss path instead of being guessed at.
    auto glyphSizesFor = [](float height) {
        theme::TypeScale ts = theme::TypeScale::fromHeight(height);
        std::vector<int> sizes = {(int)(ts.caption + 0.5f), (int)(ts.small + 0.5f),
                                  (int)(ts.body + 0.5f),    (int)(ts.title + 0.5f),
                                  (int)(ts.display + 0.5f)};
        std::sort(sizes.begin(), sizes.end());
        sizes.erase(std::unique(sizes.begin(), sizes.end()), sizes.end());
        return sizes;
    };

    // NewCM10 covers Latin, Greek and Cyrillic; Han, Kana and Hangul come
    // from the bundled serif CJK faces. Either way a codepoint has to be baked
    // before it can be drawn, so anything the user types or the API hands back
    // gets scanned through here.
    auto ensureGlyphsFor = [&](const std::string& utf8) {
        refresh_glyphs(renderer(), glyphSizesFor((float)renderer().height()),
                       {utf8});
    };
    // No eager bake here. Every call above needs renderer(), and on
    // Android there is no surface yet at this point in startup — the window
    // arrives later, with APP_CMD_INIT_WINDOW — so asking for one segfaults
    // on a null Renderer before a single frame is drawn. Nothing is lost:
    // the frame loop calls refresh_glyphs() with the eager charset before it
    // emits any quad, so the first frame bakes exactly what this line did,
    // against a Renderer that exists.

    search::SearchController searchCtl(accountPool);

    // Region picker for the search bar: "Auto" (health-ranked, fails over),
    // "All" (ask every account and merge), then one entry per configured
    // account. Rebuilt whenever accounts change, since it mirrors config.toml.
    std::vector<std::string> countryOptions;
    std::vector<std::string_view> countryOptionViews;
    int countryPickerIndex = 0;
    auto rebuildCountryOptions = [&]() {
        countryOptions.clear();
        countryOptions.emplace_back("Auto");
        countryOptions.emplace_back("All");
        for (const config::Account& a : cfg.accounts)
            countryOptions.push_back(a.country.empty() ? "?" : a.country);
        countryOptionViews.assign(countryOptions.begin(), countryOptions.end());
        if (countryPickerIndex >= (int)countryOptions.size()) countryPickerIndex = 0;
    };
    // Index 0/1 are the two modes; everything past them addresses an account
    // positionally, which is exactly why account order in config.toml is never
    // rewritten (see config::Account's health fields).
    auto selectorForCountryIndex = [&](int i) -> account::Selector {
        if (i <= 0) return account::Selector{};                       // Auto
        if (i == 1) return account::Selector{account::Selector::All}; // All
        return account::Selector{account::Selector::Index, "", i - 2};
    };
    rebuildCountryOptions();
    widgets::TextFieldState queryField;
    int typePickerIndex = 0;
    float tableScrollPx = 0.0f;

    settings::SettingsController settingsCtl;
    widgets::TextFieldState settingsFields[gui::FieldCount];
    settingsFields[gui::FieldDownloadDir].text = settingsCtl.config().settings.download_dir.string();
    int focusedField = -1;
    // Which text field the on-screen keyboard is attached to, and which one it
    // was attached to last frame. Both null on desktop, where there is no
    // on-screen keyboard and every field is typed into directly.
    widgets::TextFieldState* imeField = nullptr;
    widgets::TextFieldState* keyboardBoundField = nullptr;
    int loadedAccountIdx = -1;
    int accountListHover = -1;
    gui::TableInteraction tableInteraction;

    // SettingsController edits its own copy of the config; the account pool
    // borrows `cfg`. Anything that changes the account list has to make the
    // two agree again, or searches and downloads keep using the accounts as
    // they were at startup — which looks, from the outside, exactly like an
    // import that did nothing.
    auto syncAccounts = [&] {
        cfg = settingsCtl.config();
        if (cfg.accounts.empty()) cfg.accounts.push_back({});
        kb::api::set_requests_per_minute(cfg.settings.requests_per_minute);
        // Drops the cached services too: they still carry the credentials
        // they logged in with, so a repaired or replaced token would
        // otherwise only take effect after a restart.
        accountPool.reload();
        rebuildCountryOptions();
        loadedAccountIdx = -1;  // refill the edit form from the new list
        // A restore also brings the old machine's download directory with it,
        // and the field is filled once at startup, so it would otherwise keep
        // showing a path nothing is being written to.
        settingsFields[gui::FieldDownloadDir].text = cfg.settings.download_dir.string();
    };

    // Re-checks the current results against library.db for the active
    // quality setting — called after every search and after a quality
    // change, so already-owned rows never look selectable (see
    // SearchController::refresh_downloaded).
    auto refreshDownloaded = [&] {
        searchCtl.refresh_downloaded(settingsCtl.config().settings.download_dir.string(),
                                     settingsCtl.config().settings.quality);
    };

    // Downloaded/searched metadata (artist/album/track titles) can be CJK
    // even when the query that found it wasn't — bake glyphs for the result
    // set too, not just what the user typed.
    auto ensureGlyphsForResults = [&] {
        std::vector<std::string> scan;
        scan.reserve(searchCtl.results().size() * 5);
        for (const auto& r : searchCtl.results()) {
            // Every column that reaches the table, not just the first three:
            // a Japanese release routinely carries a Japanese label and genre
            // while its title is romanized.
            scan.push_back(r.title);
            scan.push_back(r.artist);
            scan.push_back(r.album);
            scan.push_back(r.label);
            scan.push_back(r.genre);
        }
        refresh_glyphs(renderer(),
                       glyphSizesFor((float)renderer().height()), scan);
    };

    // The library manager: the catalog under the configured download dir,
    // its cover textures, and the grid's scroll offset. Loaded lazily — the
    // first visit to the screen pays for the query, not every startup.
    libmgr::LibraryController libraryCtl(settingsCtl.config().settings.download_dir.string());
    // Bound to a Renderer by the frame loop, not here: on Android there is no
    // surface yet at this point (it arrives with APP_CMD_INIT_WINDOW), and the
    // one that eventually arrives can be replaced later.
    gui::CoverCache coverCache(nullptr);
    float libraryScrollPx = 0.0f;
    // Settings scrolls only when it has to (stacked on a narrow screen); on a
    // desktop window the content fits and the clamp below pins this at 0.
    float settingsScrollPx = 0.0f;
    float settingsContentH = 0.0f;   // reported by the last draw_settings
    float lastSettingsAreaH = 0.0f;  // and the height it had to fit into
    bool libraryLoaded = false;

    enum class Screen { Search, Settings, Library };
    Screen activeScreen = start_settings ? Screen::Settings : Screen::Search;
    constexpr int kActNavSearch = 9000, kActNavSettings = 9001, kActNavLibrary = 9002;

    // Everything that invalidates the on-screen library: entering the screen,
    // a delete, or the download directory changing under it.
    // A capture may be pointed at a fixture tree. Kept as an override here
    // rather than written into the config, so a screenshot run never edits
    // config.toml behind the user's back.
    const std::string& captureLibraryRoot = opts.captureLibraryRoot;
    auto reloadLibrary = [&] {
        libraryCtl.set_root(captureLibraryRoot.empty()
                                ? settingsCtl.config().settings.download_dir.string()
                                : captureLibraryRoot);
        libraryCtl.reload();
        coverCache.clear();
        libraryScrollPx = 0.0f;
        libraryLoaded = true;
        std::vector<std::string> scan;
        scan.reserve(libraryCtl.albums().size() * 2);
        for (const auto& a : libraryCtl.albums()) {
            scan.push_back(a.title);
            scan.push_back(a.artist_name);
        }
        refresh_glyphs(renderer(),
                       glyphSizesFor((float)renderer().height()), scan);
    };

    // ── Capture mode ────────────────────────────────────────────────────────
    //
    // streamer_gui_capture asks for a list of screens; the loop below draws
    // each one, hands it back through opts.onCaptured and moves on. Nothing
    // here reimplements a layout — this IS the layout, which is the whole
    // reason the tool was rebuilt this way.
    const bool capturing = !opts.captureScreens.empty();
    size_t captureIdx = 0;
    auto applyCaptureScreen = [&](const std::string& name) {
        if (name == "settings")     activeScreen = Screen::Settings;
        else if (name == "library") activeScreen = Screen::Library;
        else                        activeScreen = Screen::Search;
    };
    if (capturing) {
        if (!opts.captureQuery.empty()) {
            queryField.text = opts.captureQuery;
            queryField.cursorByte = queryField.text.size();
            searchCtl.search(queryField.text, "smart");
        }
        settingsScrollPx = opts.settingsScroll;
        applyCaptureScreen(opts.captureScreens[0]);
    }

    if (demo) {
        // Dev tooling only (visual verification, not shipped functionality):
        // types an accented query through the real CharEvent->FrameInput->
        // textFieldHandleInput pipeline, opens the cheatsheet, and submits a
        // search — proving accented text survives end-to-end and the error
        // path (no account configured in this environment) renders cleanly.
        FrameInput synth;
        synth.beginFrame();
        for (char32_t cp : std::u32string(U"artist:\"Luísa Sonza\" year:2020-2024"))
            synth.onChar(CharEvent{(uint32_t)cp});
        widgets::textFieldHandleInput(queryField, synth);
        searchCtl.toggle_cheatsheet();
        searchCtl.search(queryField.text, "smart");
        refreshDownloaded();
    }

    // Where the download threads find the loop they have to wake. Set BEFORE
    // the sink is installed and before any download can be started, which is
    // the only ordering that matters: a null host here means an update that
    // stores correctly and never appears.
    g_dl.host.store(host_, std::memory_order_relaxed);

    // Route the download meter here instead of to a terminal nobody is
    // looking at. Installed once, for the life of the process; the sink runs
    // on the download worker threads, and update() both stores and wakes.
    dl::set_progress_sink([](const dl::Progress& p) {
        g_dl.update([&] {
            if (!p.error.empty()) {
                // A failure ends the attempt as far as the user is concerned.
                // It has to replace the meter, not sit behind it: a bar frozen
                // at 0% says "slow", and 401 does not mean slow.
                g_dl.note   = p.error;
                g_dl.active = false;
            } else {
                g_dl.p = p;
            }
        });
    });

    // An alias, not an object: the FrameInput belongs to FrameInputView, which
    // is what the host's callbacks pour into. Binding a reference here is what
    // lets every `input.` below — and every widget it is handed to — stay
    // exactly as it was written against streamer's own host layer.
    FrameInput& input = FrameInputView::input();
    bool first_frame = true;
    std::vector<gui::Hit> hits;  // filled by the PREVIOUS frame's draw
    auto lastFrameTime = std::chrono::steady_clock::now();
    float lastPointerX = 0.0f, lastPointerY = 0.0f;
    Renderer* boundRenderer = nullptr;   // what coverCache/the atlas are bound to

    while (running() && !host_->quitRequested()) {
        beginFrame();
        // Glyphs the last frame asked for and did not have are a reason to
        // redraw as much as a click is, and a reason not to sit in the event
        // wait first: this loop is event-driven, so without both of these a
        // label at a never-before-drawn size would stay blank until the user
        // happened to move the pointer. Resolving takes a few frames (filling
        // a glyph changes the advances after it, hence new subpixel phases),
        // and back-to-back frames make that imperceptible.
        bool glyphsPending = g_text_ok && g_text.hasMisses();
        // A download reports from another thread and cannot wake this one, so
        // while one is running the loop stops waiting a full second for an
        // input event it will not get — otherwise the percentage would step
        // once a second and read as a hang rather than as progress.
        const DownloadUi::Snapshot dls = g_dl.read();
        const dl::Progress& dlp = dls.p;
        const bool dlActive = dls.active, dlDone = dls.done;
        const int dlQueueDone = dls.queueDone, dlQueueTotal = dls.queueTotal;
        const std::string& dlNote = dls.note;
        // haveWork = "do not sleep, I have a frame to build". Everything else
        // blocks in the kernel until something happens, which is what keeps an
        // idle window (and a phone's battery) at zero cost.
        //
        // Only glyph misses set it. A running download used to as well, via a
        // 100 ms timeout — but progress arrives on the download thread, and it
        // wakes this one through Host::postAppEvent() now (see the sink
        // installed above), so the frame happens when the numbers change
        // rather than ten times a second in the hope that they have.
        host_->pump(/*haveWork=*/glyphsPending);

        // No surface, no frame. Android destroys the Renderer under us on
        // APP_CMD_TERM_WINDOW; everything below this line — starting with
        // renderer() — assumes one exists. pump() blocks while surfaceless, so
        // this is a wait, not a spin.
        //
        // Checked AFTER the pump and never before: the pump is where "your
        // surface is gone" is delivered, so anything decided before it cannot
        // be trusted after it.
        if (!renderable()) continue;

        bool dirty = takeDirty() || glyphsPending || dlActive;

        // A restore running on its own thread has rewritten config.toml.
        // Adopt it here, on the thread that owns the pool and does the
        // drawing, so the accounts it brought back appear as soon as they
        // exist rather than at the next launch.
        if (settingsCtl.take_config_reload()) {
            syncAccounts();
            dirty = true;
        }
        // Plain pointer motion (no click/wheel/key) never sets `dirty` — the
        // host only marks that on resize — so hover-only feedback (button
        // hover, table row hover, the ghost popup's hover timer) never
        // updated without an unrelated click first forcing a redraw. Treat a
        // moved pointer as its own reason to redraw.
        lastPointerX = input.pointerX; lastPointerY = input.pointerY;
        // inputArrived(), not a list of the FrameInput fields that might have
        // changed. That list used to name pointerWentDown, wheelDelta,
        // typedCodepoints, keysWentDown and a moved pointer — five things,
        // complete-looking, and it MISSED TYPING ENTIRELY on a phone: an input
        // method produces no typed codepoints, it rewrites the whole buffer
        // (FrameInput::textEdited). The screen only caught up when the user
        // happened to tap something.
        //
        // The bug is not the missing sixth entry; it is that there was a list.
        // Forgetting to extend it is not a compile error, so the next kind of
        // input would have broken the same way. FrameInputView answers this
        // itself now — every callback it has marks it — so a callback added
        // later is covered by having been written.
        bool interacted = inputArrived();
        if (!dirty && !interacted && !first_frame) continue;
        first_frame = false;

        auto now = std::chrono::steady_clock::now();
        float dtSeconds = std::chrono::duration<float>(now - lastFrameTime).count();
        lastFrameTime = now;

        Renderer& r = renderer();
        // A replaced Renderer invalidates every GPU object built against the
        // old one. Detected by identity here rather than announced by the
        // host, which has no idea who is holding what. On desktop this fires
        // exactly once, on the first frame.
        if (&r != boundRenderer) {
            if (g_text_ok) upload_glyphs(r);
            coverCache.rebind(&r);
            boundRenderer = &r;
        }
        float w = (float)r.width(), h = (float)r.height();
        // Controls used to be sized purely off window height, which is a fine
        // proxy for "how big is the screen" on a desktop window and a bad one
        // on a phone held upright: a 720x1640 display made every row 74px tall
        // inside 720px of width, so labels ran into each other and out of
        // their boxes. Taking the smaller of the two keeps desktop sizing
        // exactly as it was (height wins on any landscape window) and makes
        // portrait proportionate to the dimension that is actually scarce.
        float rowH = std::min(h * 0.045f, w * 0.075f);
        // The display cutout, and the on-screen keyboard, kept apart because
        // they are different in kind. The cutout is glass: hardware, permanent,
        // there on every frame. The keyboard is software and only ever eats the
        // bottom. Both are zero on a desktop, which is not a stub — a desktop
        // window is a rectangle we own completely.
        //
        // They used to be summed into one inset set, which buys a choice of two
        // bugs: a permanent dead strip along the bottom once the keyboard has
        // been up, or a notch that stops being avoided the moment it goes down.
        const SafeInsets cutout = host_->safeInsets();
        struct { float left, top, right, bottom; } safe = {
            (float)cutout.left, (float)cutout.top, (float)cutout.right,
            (float)(cutout.bottom + host_->keyboardInset())};

        // Glyphs BEFORE any quad is emitted, in both directions:
        //  - a resize moved the type scale, so the per-size cells for the new
        //    sizes do not exist yet;
        //  - last frame asked for a size nobody predicted (button labels size
        //    themselves off their box) and drew nothing.
        // Both grow the atlas and move cells, so doing either after the Canvas
        // has emitted quads would leave those quads sampling a layout that no
        // longer exists.
        refresh_glyphs(r, glyphSizesFor(h), {});
        bake_glyph_misses(r);

        // ── Dispatch against LAST frame's hits before drawing this frame ────
        // (nav bar + whichever screen was active when those hits were built —
        // switching screens only takes effect below, after dispatch, so the
        // screen that drew `hits` last frame is still the right one to
        // interpret them against.)
        int hoverRow = -1, hoverHeaderCol = -1, hoveredAction = -1;
        for (auto& hit : hits) {
            if (!hit.rect.contains(input.pointerX, input.pointerY)) continue;
            hoveredAction = hit.action;
            if (activeScreen == Screen::Search) {
                if (hit.action >= gui::ActTableHeaderBase && hit.action < gui::ActTableRowBase)
                    hoverHeaderCol = hit.action - gui::ActTableHeaderBase;
                else if (hit.action >= gui::ActTableRowBase)
                    hoverRow = hit.action - gui::ActTableRowBase;
            } else if (activeScreen == Screen::Settings &&
                       hit.action >= gui::ActAccountListBase && hit.action < gui::ActQualityBase) {
                accountListHover = hit.action - gui::ActAccountListBase;
            }

            if (!input.pointerWentDown) continue;

            if (hit.action == kActNavSearch) { activeScreen = Screen::Search; continue; }
            if (hit.action == kActNavSettings) { activeScreen = Screen::Settings; continue; }
            if (hit.action == kActNavLibrary) {
                activeScreen = Screen::Library;
                if (!libraryLoaded) reloadLibrary();
                continue;
            }

            if (activeScreen == Screen::Library) {
                if (hit.action >= gui::ActLibTileBase) {
                    libraryCtl.toggle_selected(hit.action - gui::ActLibTileBase);
                } else if (hit.action == gui::ActLibDeleteSelected) {
                    libraryCtl.request_delete();
                } else if (hit.action == gui::ActLibSelectAll) {
                    libraryCtl.select_all();
                } else if (hit.action == gui::ActLibClearSelection) {
                    libraryCtl.clear_selection();
                } else if (hit.action == gui::ActLibRefresh) {
                    reloadLibrary();
                } else if (hit.action == gui::ActLibCancelDelete) {
                    libraryCtl.cancel_delete();
                } else if (hit.action == gui::ActLibDismissStatus) {
                    libraryCtl.clear_status();
                } else if (hit.action == gui::ActLibConfirmDelete) {
                    libraryCtl.confirm_delete();
                    // The deleted albums' textures are now stale, and the
                    // grid's rows have shifted under the scroll offset.
                    coverCache.clear();
                    libraryScrollPx = 0.0f;
                    // Search results may have shown "Tuyo" for tracks that
                    // just went away.
                    refreshDownloaded();
                }
                continue;
            }

            if (activeScreen == Screen::Search) {
                if (hit.action == gui::ActToggleCheatsheet) {
                    searchCtl.toggle_cheatsheet();
                } else if (hit.action == gui::ActSubmitSearch) {
                    searchCtl.search(queryField.text, std::string(gui::kTypePickerOptions[(size_t)typePickerIndex]));
                    refreshDownloaded();
                    ensureGlyphsForResults();
                    tableScrollPx = 0.0f;
                } else if (hit.action >= gui::ActTypePickerBase && hit.action < gui::ActTableHeaderBase) {
                    typePickerIndex = hit.action - gui::ActTypePickerBase;
                } else if (hit.action >= gui::ActTableHeaderBase && hit.action < gui::ActTableRowBase) {
                    auto sc = gui::sortColumnForTableIndex(hit.action - gui::ActTableHeaderBase);
                    if (sc != search::SortColumn::None) searchCtl.sort(sc);
                // Must precede the ActTableRowBase catch-all below: the row
                // range is open-ended (one action per result), so anything
                // tested after it would never be reached.
                } else if (hit.action >= gui::ActCountryPickerBase) {
                    countryPickerIndex = hit.action - gui::ActCountryPickerBase;
                    searchCtl.set_selector(selectorForCountryIndex(countryPickerIndex));
                } else if (hit.action >= gui::ActTableRowBase) {
                    searchCtl.toggle_selected(hit.action - gui::ActTableRowBase);
                } else if (hit.action == gui::ActDownloadSelected && !searchCtl.selected().empty()) {
                    std::vector<std::string> ids(searchCtl.selected().begin(), searchCtl.selected().end());
                    // Same selector the results came from, so a row found via
                    // NZ is downloaded through NZ rather than through whatever
                    // account happens to sit first.
                    download_async(&accountPool, searchCtl.selector(),
                                  settingsCtl.config().settings.quality,
                                  settingsCtl.config().settings.download_dir.string(),
                                  (int)settingsCtl.config().settings.concurrency, ids);
                    searchCtl.clear_selection();
                }
            } else {  // Screen::Settings
                if (hit.action >= gui::ActAccountListBase && hit.action < gui::ActQualityBase) {
                    const int picked = hit.action - gui::ActAccountListBase;
                    settingsCtl.select_account(picked);
                    // Picking an account here used to change only which
                    // account the edit form showed — search and downloads
                    // stayed pinned to accounts.front() regardless. Routing it
                    // into the search selector is what makes the control mean
                    // what it looks like it means.
                    searchCtl.set_selector(
                        account::Selector{account::Selector::Index, "", picked});
                    // Keep the search bar's region picker showing the same
                    // account, so the two controls never disagree about which
                    // one searches are going to.
                    countryPickerIndex = picked + 2;  // past Auto/All
                } else if (hit.action == gui::ActAddAccount) {
                    settingsCtl.add_account();
                } else if (hit.action == gui::ActRemoveAccount) {
                    settingsCtl.remove_account(settingsCtl.current_account_index());
                } else if (hit.action == gui::ActLoginWithToken) {
                    settingsCtl.login_with_token(settingsFields[gui::FieldUserId].text,
                                                 settingsFields[gui::FieldAuthToken].text);
                } else if (hit.action == gui::ActExportAccounts) {
                    settingsCtl.export_to((config::config_path().parent_path() / "accounts-export.toml").string());
                } else if (hit.action == gui::ActImportAccounts) {
                    if (settingsCtl.import_from((config::config_path().parent_path() / "accounts-export.toml").string()))
                        syncAccounts();
                } else if (hit.action == gui::ActBackupLibrary) {
                    settingsCtl.backup_to();
                } else if (hit.action == gui::ActRestoreBackup) {
                    settingsCtl.restore_from();
                } else if (hit.action == gui::ActReadableList) {
                    settingsCtl.write_readable();
                } else if (hit.action == gui::ActBrowseDownloadDir) {
                    host_->pickDirectory([&](const std::string& path) {
                        if (!path.empty()) settingsFields[gui::FieldDownloadDir].text = path;
                    });
                } else if (hit.action >= gui::ActQualityBase && hit.action < gui::ActLanguageBase) {
                    settingsCtl.mutable_settings().quality = gui::kQualityValues[hit.action - gui::ActQualityBase];
                    refreshDownloaded();
                } else if (hit.action >= gui::ActLanguageBase && hit.action < gui::ActConcurrencyMinus) {
                    settingsCtl.mutable_settings().language = gui::kLanguageValues[hit.action - gui::ActLanguageBase];
                } else if (hit.action == gui::ActConcurrencyMinus) {
                    auto& s = settingsCtl.mutable_settings();
                    if (s.concurrency > 1) s.concurrency--;
                } else if (hit.action == gui::ActConcurrencyPlus) {
                    settingsCtl.mutable_settings().concurrency++;
                } else if (hit.action == gui::ActRpmMinus) {
                    auto& s = settingsCtl.mutable_settings();
                    if (s.requests_per_minute > 0) s.requests_per_minute--;
                } else if (hit.action == gui::ActRpmPlus) {
                    settingsCtl.mutable_settings().requests_per_minute++;
                } else if (hit.action == gui::ActSettingsSave) {
                    settingsCtl.mutable_settings().download_dir = settingsFields[gui::FieldDownloadDir].text;
                    settingsCtl.save();
                    syncAccounts();
                } else if (hit.action >= gui::ActFieldFocusBase && hit.action < gui::ActFieldFocusBase + gui::FieldCount) {
                    focusedField = hit.action - gui::ActFieldFocusBase;
                }
            }
        }

        if (activeScreen == Screen::Search) {
            // Enter submits the search regardless of pointer position.
            if (input.keyWentDown(key::Enter)) {
                searchCtl.search(queryField.text, std::string(gui::kTypePickerOptions[(size_t)typePickerIndex]));
                refreshDownloaded();
                tableScrollPx = 0.0f;
            }
            if (widgets::textFieldHandleInput(queryField, input, &clipboardIo))
                ensureGlyphsFor(queryField.text);
            if (input.wheelDelta != 0.0f) {
                Rect area = {w * 0.04f, h * 0.03f, w * 0.92f, h * 0.94f};
                // wheelDelta is positive for a physical "scroll up" (toward
                // earlier content) — that must DECREASE scrollPx (scrollPx=0
                // is the top of the list), so subtract, not add.
                tableScrollPx -= input.wheelDelta * rowH;
                float maxScroll = std::max(0.0f, (float)searchCtl.results().size() * rowH - area.h * 0.5f);
                tableScrollPx = std::clamp(tableScrollPx, 0.0f, maxScroll);
            }
        } else if (activeScreen == Screen::Library) {
            // Escape backs out of the confirm modal — a destructive dialog
            // must be dismissible without aiming at a button.
            if (input.keyWentDown(key::Escape)) libraryCtl.cancel_delete();
            // Scrolling under an open modal would move the list the dialog is
            // describing out from under it.
            if (input.wheelDelta != 0.0f && !libraryCtl.delete_pending()) {
                Rect area = {w * 0.04f, h * 0.02f + rowH + h * 0.02f, w * 0.92f,
                             h * 0.94f - rowH - h * 0.02f};
                libraryScrollPx -= input.wheelDelta * rowH;  // see the search table's note on sign
                float contentH = gui::libraryContentHeight(
                    area, (int)libraryCtl.albums().size(), rowH);
                float maxScroll = std::max(0.0f, contentH - area.h * 0.5f);
                libraryScrollPx = std::clamp(libraryScrollPx, 0.0f, maxScroll);
            }
        } else {
            // Touch drags arrive here as wheel events too (android_host
            // synthesises them past the tap slop), so this is what makes the
            // stacked settings screen reachable with a finger.
            if (input.wheelDelta != 0.0f) {
                settingsScrollPx -= input.wheelDelta * rowH;
                float maxScroll = std::max(0.0f, settingsContentH - lastSettingsAreaH);
                settingsScrollPx = std::clamp(settingsScrollPx, 0.0f, maxScroll);
            }

            // Reload the account edit fields from the controller whenever
            // the selected account changed (switching accounts must not
            // clobber in-progress edits, but must pick up the new one).
            if (settingsCtl.current_account_index() != loadedAccountIdx) {
                // current_account() (not accounts()[index]) because it
                // self-heals an empty account list (fresh install, no
                // config.toml yet) by lazily creating a blank account —
                // accounts() alone would still be empty here and indexing
                // it with current_account_index()'s default of 0 crashes.
                const config::Account& a = settingsCtl.current_account();
                settingsFields[gui::FieldCountry].text = a.country;
                settingsFields[gui::FieldEmail].text = a.email;
                settingsFields[gui::FieldAppId].text = a.app_id;
                settingsFields[gui::FieldAppSecret].text = a.app_secret;
                settingsFields[gui::FieldUserId].text = a.user_id;
                settingsFields[gui::FieldAuthToken].text = a.auth_token;
                for (int fi = gui::FieldCountry; fi <= gui::FieldAuthToken; fi++)
                    settingsFields[fi].cursorByte = settingsFields[fi].text.size();
                loadedAccountIdx = settingsCtl.current_account_index();
            }
            if (focusedField >= 0 &&
                widgets::textFieldHandleInput(settingsFields[focusedField], input, &clipboardIo))
                ensureGlyphsFor(settingsFields[focusedField].text);
            // Keep the account struct in sync with the edit fields every
            // frame (not just on Save) so Login with Token always sees the
            // latest typed app_id/app_secret/user_id/auth_token.
            config::Account& a = settingsCtl.current_account();
            a.country = settingsFields[gui::FieldCountry].text;
            a.email = settingsFields[gui::FieldEmail].text;
            a.app_id = settingsFields[gui::FieldAppId].text;
            a.app_secret = settingsFields[gui::FieldAppSecret].text;
        }

        std::vector<float> curves;
        std::vector<float> shapeVerts;
        std::vector<float> msdfQuads;
        // Album art. Canvas::imageFg() is a no-op until useImagesFg() is
        // given somewhere to put the draws — the library grid is this app's
        // first consumer of the image layer. It uses the FOREGROUND layer
        // because background images composite before the vector overlay, and
        // the clear() below would paint straight over them.
        std::vector<ImageDraw> images, imagesFg;
        Canvas canvas(curves, r.width(), r.height(), g_font_ok ? &g_font : nullptr,
                      /*insetTop=*/0, /*insetBottom=*/0,
                      /*insetLeft=*/0, /*insetRight=*/0);
        canvas.useShapes(&shapeVerts);
        canvas.useImages(&images);
        canvas.useImagesFg(&imagesFg);
        if (g_text_ok) canvas.useMsdf(&g_text, &msdfQuads);
        canvas.clear(theme::kBackground);

        // ── Text-field click-to-position (Canvas needed to measure text, so
        // this can't happen in the hit-dispatch loop above) — reuses LAST
        // frame's `hits` (not cleared until below) to find the clicked
        // field's rect, same convention as the dispatch loop.
        if (input.pointerWentDown) {
            int wantAction = -1;
            widgets::TextFieldState* field = nullptr;
            if (activeScreen == Screen::Search) {
                wantAction = gui::ActQueryFieldClick;
                field = &queryField;
            } else if (activeScreen == Screen::Settings && focusedField >= 0) {
                wantAction = gui::ActFieldFocusBase + focusedField;
                field = &settingsFields[focusedField];
            }
            // A tap anywhere else takes the keyboard away, which is why this
            // starts at null every time rather than only being set on a hit.
            imeField = nullptr;
            if (wantAction >= 0) {
                for (auto& hit : hits) {
                    if (hit.action != wantAction) continue;
                    double nowSeconds = std::chrono::duration<double>(now.time_since_epoch()).count();
                    widgets::textFieldHandleClick(*field, canvas, hit.rect, input, nowSeconds);
                    imeField = field;
                    break;
                }
            }
        }
        // The IME reports its own dismissal (back gesture) as Escape; without
        // this the app would still think the keyboard is up and never re-open it.
        if (input.keyWentDown(key::Escape)) imeField = nullptr;

        // Raise or dismiss the on-screen keyboard to match what is focused.
        // Edge-triggered: show_keyboard() seeds the platform's edit buffer, so
        // calling it every frame would fight the IME for the caret. A no-op on
        // desktop, where AppHost's default implementations do nothing.
        if (imeField != keyboardBoundField) {
            if (imeField) host_->showKeyboard(imeField->text, imeField->cursorByte);
            else          host_->hideKeyboard();
            keyboardBoundField = imeField;
        }

        if (widget_preview) {
            static int sortCol = 1;
            static bool sortAsc = true;
            static float scrollPx = 0.0f;
            const auto& cols = previewColumns();
            const int rowCount = 24;
            Rect area = {w * 0.05f, h * 0.08f, w * 0.9f, h * 0.8f};
            float prowH = h * 0.05f;
            Rect header = widgets::tableHeaderRow(area, prowH);
            auto headerRects = widgets::tableHeaderColumnRects(header, cols);
            int hHover = -1;
            for (size_t i = 0; i < headerRects.size(); i++) {
                if (headerRects[i].contains(input.pointerX, input.pointerY)) {
                    hHover = (int)i;
                    if (input.pointerWentDown) {
                        if (sortCol == (int)i) sortAsc = !sortAsc;
                        else { sortCol = (int)i; sortAsc = true; }
                    }
                }
            }
            if (input.wheelDelta != 0.0f) {
                scrollPx -= input.wheelDelta * prowH;  // see the search table's note on sign
                float maxScroll = std::max(0.0f, rowCount * prowH - (area.h - prowH));
                scrollPx = std::clamp(scrollPx, 0.0f, maxScroll);
            }
            int rHover = -1;
            Rect body = {area.x, area.y + prowH, area.w, area.h - prowH};
            if (body.contains(input.pointerX, input.pointerY))
                rHover = (int)((input.pointerY - body.y + scrollPx) / prowH);
            widgets::drawSortableTable(canvas, area, cols, previewCell, rowCount,
                                       sortCol, sortAsc, scrollPx, prowH, rHover, hHover);
        } else if (theme_preview) {
            // Dev tooling: one of each button kind/state, to eyeball the
            // OLED-friendly bar treatment instead of the old gradient fill.
            float bw = w * 0.18f, bh = h * 0.08f, gap = w * 0.02f;
            float x = w * 0.05f, y = h * 0.08f;
            theme::button(canvas, x + 0 * (bw + gap), y, bw, bh, "Primary",
                         theme::ButtonKind::Primary, {}, bh * 0.25f);
            theme::button(canvas, x + 1 * (bw + gap), y, bw, bh, "Secondary",
                         theme::ButtonKind::Secondary, {}, bh * 0.25f);
            theme::button(canvas, x + 2 * (bw + gap), y, bw, bh, "Danger",
                         theme::ButtonKind::Danger, {}, bh * 0.25f);
            theme::button(canvas, x + 3 * (bw + gap), y, bw, bh, "Hovered",
                         theme::ButtonKind::Primary, {/*hovered=*/true, false, false}, bh * 0.25f);
            y += bh + h * 0.03f;
            theme::button(canvas, x + 0 * (bw + gap), y, bw, bh, "Pressed",
                         theme::ButtonKind::Primary, {true, /*pressed=*/true, false}, bh * 0.25f);
            theme::button(canvas, x + 1 * (bw + gap), y, bw, bh, "Disabled",
                         theme::ButtonKind::Primary, {false, false, /*disabled=*/true}, bh * 0.25f);
        } else {
            hits.clear();

            // Nav bar (always visible, every screen). Three labels in 30% of
            // the width is comfortable on a desktop window and unreadable on
            // a phone, where 30% of 720px cannot hold "Search Library
            // Settings" — so upright, it gets the full content width.
            const bool narrow =
                autoOrientationFor((int)w, (int)h) == UiOrientation::Vertical;
            const float contentX = w * 0.04f + safe.left;
            const float contentW = w * 0.92f - safe.left - safe.right;
            Rect navRow = {contentX, h * 0.02f + safe.top,
                           narrow ? contentW : w * 0.3f, rowH};
            int navIdx = activeScreen == Screen::Search   ? 0
                       : activeScreen == Screen::Library  ? 1
                                                          : 2;
            // Geometry now, paint later: a scrolling screen slides content up
            // underneath this, and it has to be drawn over that, not under it.
            auto navRects = widgets::segmentRects(navRow, 3);
            hits.push_back({navRects[0], kActNavSearch});
            hits.push_back({navRects[1], kActNavLibrary});
            hits.push_back({navRects[2], kActNavSettings});

            // The strip is an overlay only in the sense that it outlives the
            // screen that started the download; it still gets its own space,
            // because a progress bar drawn on top of the last row of results
            // costs the user the row.
            const bool showStrip = dlActive || dlDone || !dlNote.empty();
            const float stripH = showStrip ? rowH * 0.95f + h * 0.024f : 0.0f;

            const float areaY = navRow.y + navRow.h + h * 0.02f;
            const float areaBottom = h * 0.96f - safe.bottom - stripH;
            Rect area = {contentX, areaY, contentW, areaBottom - areaY};
            gui::PointerState ptr{input.pointerX, input.pointerY, input.pointerDown,
                                 input.pointerWentDown, input.pointerWentUp};
            if (activeScreen == Screen::Search) {
                gui::draw_search(canvas, area, searchCtl, queryField, /*queryFocused=*/true,
                                 typePickerIndex, tableScrollPx, rowH,
                                 hoverRow, hoverHeaderCol, hoveredAction,
                                 ptr, dtSeconds, tableInteraction,
                                 countryOptionViews, countryPickerIndex, hits);
            } else if (activeScreen == Screen::Library) {
                coverCache.beginFrame();
                gui::draw_library(canvas, area, libraryCtl, coverCache, libraryScrollPx, rowH,
                                  hoveredAction, input.pointerDown, hits);
            } else {
                const size_t hitsBefore = hits.size();
                lastSettingsAreaH = area.h;
                settingsContentH = gui::draw_settings(
                    canvas, area, settingsCtl, settingsFields, focusedField,
                    accountListHover, hoveredAction, input.pointerDown, rowH,
                    settingsScrollPx, hits);
                // Scrolled-away controls are clipped out of the drawing but
                // their rects are still absolute, so without this a tap on
                // the nav bar could also press a button that is no longer
                // under it.
                hits.erase(std::remove_if(hits.begin() + (long)hitsBefore, hits.end(),
                                          [&](const gui::Hit& hh) {
                                              return hh.rect.y + hh.rect.h < area.y ||
                                                     hh.rect.y > area.y + area.h;
                                          }),
                           hits.end());
            }

            // The chrome, last. Only the SDF layer honours Canvas::setClip —
            // MSDF text is a separate buffer — so scrolled-away labels reach
            // up here regardless of the clip inside draw_settings. Painting an
            // opaque band and occluding the glyphs under it is how the modal
            // path already solves exactly this, and it costs one rect.
            canvas.rect(0.0f, 0.0f, w, area.y, theme::kBackground);
            widgets::drawSegmented(canvas, navRow, {"Search", "Library", "Settings"}, navIdx,
                                   theme::kSegmented);

            // Download progress, over every screen rather than only the one it
            // was started from: a download begun in Search keeps running while
            // the user browses Library or edits Settings, and "how far along is
            // it" is the question they will actually have. The CLI answers it
            // with a carriage-return meter on a terminal; there is none here,
            // and on Android there is nowhere for one to be.
            if (showStrip) {
                const float sh = rowH * 0.95f;
                const float sy = h * 0.96f - safe.bottom - sh;
                const float sx = contentX, sw = contentW;
                // Same reason as the nav band above: text does not clip.
                canvas.rect(0.0f, sy, w, h - sy, theme::kBackground);
                canvas.rect(sx, sy, sw, sh, theme::kPanel, sh * 0.2f);

                // Bytes, not tracks: a 20-track album whose first track is
                // half done is 2% through, and a bar that says otherwise is
                // the kind of progress bar people learn to distrust.
                float frac = dlp.total_bytes > 0
                    ? (float)((double)dlp.bytes / (double)dlp.total_bytes) : 0.0f;
                if (frac > 1.0f) frac = 1.0f;
                if (!dlActive && dlNote.empty()) frac = 1.0f;
                canvas.rect(sx, sy + sh - sh * 0.12f, sw * frac, sh * 0.12f,
                            dlNote.empty() ? theme::kAccent : theme::kDanger);

                std::string line;
                if (!dlNote.empty() && !dlActive) {
                    line = dlNote;
                } else {
                    if (dlQueueTotal > 1) {
                        int at = dlQueueDone + (dlActive ? 1 : 0);
                        if (at > dlQueueTotal) at = dlQueueTotal;
                        line = "[" + std::to_string(at) + "/" +
                               std::to_string(dlQueueTotal) + "] ";
                    }
                    line += dlp.label;
                    if (dlp.total_tracks > 1)
                        line += "  " + std::to_string(dlp.done_tracks) + "/" +
                                std::to_string(dlp.total_tracks);
                    char buf[128];
                    std::snprintf(buf, sizeof buf, "   %d%%   %.1f MB   %.1f MB/s",
                                  (int)(frac * 100.0f + 0.5f),
                                  (double)dlp.bytes / 1048576.0,
                                  dlp.speed_bps / 1048576.0);
                    line += buf;
                    if (dlActive && dlp.eta_seconds >= 0) {
                        int secs = (int)(dlp.eta_seconds + 0.5);
                        std::snprintf(buf, sizeof buf, "   ETA %02d:%02d",
                                      secs / 60, secs % 60);
                        line += buf;
                    }
                    if (!dlActive) line = "Done — " + line;
                }
                canvas.text(line, sx + sh * 0.4f, sy + sh * 0.32f, sh * 0.34f,
                            dlNote.empty() ? theme::kText : theme::kDanger);
            }
        }

        r.draw(curves, /*overlay_rotation_deg=*/0, images, imagesFg,
              msdfQuads, shapeVerts);

        if (capture_path) {
            // Dev tooling only: dump the frame just drawn as raw RGBA8 —
            // this repo has no PNG encoder wired in yet, ffmpeg converts it.
            std::vector<uint8_t> rgba;
            uint32_t cw = 0, ch = 0;
            if (r.readbackLastFrame(rgba, cw, ch)) {
                FILE* f = std::fopen(capture_path, "wb");
                if (f) { std::fwrite(rgba.data(), 1, rgba.size(), f); std::fclose(f); }
                std::printf("captured %ux%u to %s\n", cw, ch, capture_path);
            }
            return 0;
        }

        if (capturing) {
            // Only once the atlas has settled. A glyph that was missing when
            // this frame was built drew nothing and, worse, advanced by zero —
            // so the line it is on is laid out wrongly, not merely blank.
            // glyphsPending is the loop's own answer to that, and it forces
            // another frame, so waiting here costs a pass or two and never
            // hangs: the misses are finite.
            if (g_text_ok && g_text.hasMisses()) continue;

            std::vector<uint8_t> rgba;
            uint32_t cw = 0, ch = 0;
            if (!r.readbackLastFrame(rgba, cw, ch)) {
                std::fprintf(stderr, "[x] readback failed for screen '%s'\n",
                             opts.captureScreens[captureIdx].c_str());
                return 1;
            }
            if (opts.onCaptured &&
                !opts.onCaptured(opts.captureScreens[captureIdx], rgba, cw, ch))
                return 1;

            if (++captureIdx >= opts.captureScreens.size()) return 0;
            applyCaptureScreen(opts.captureScreens[captureIdx]);
            // The next screen has to be DRAWN, and nothing in a headless run
            // will mark the frame dirty on its own — there is no pointer to
            // move and no compositor to send an expose.
            markDirty();
        }
    }

    return 0;
}
