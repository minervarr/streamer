#pragma once
// Search screen — pure drawing (View half of the Controller/View split).
// Draws from SearchController's state and a caller-owned TextFieldState for
// the query box; decides nothing, only renders and reports click targets
// into `hits` (scanersito's Hit/Action convention).

#include "search_controller.hh"
#include "settings_controller.hh"

#include "animated_float.hh"
#include "canvas.hh"
#include "widgets.hh"

#include <string>
#include <string_view>
#include <vector>

namespace gui {

// Raw per-frame pointer state, needed by the search table for column-border
// drag-to-resize and the hover-reveal popup — both live entirely inside
// draw_search since only it knows the table's live geometry (see
// TableInteraction below). Everything else in this file works off
// pre-derived hover ints instead, per the "draws from state, decides
// nothing" convention this file otherwise follows.
struct PointerState {
  float x = 0.0f, y = 0.0f;
  bool down = false, wentDown = false, wentUp = false;
};

// Session-only interactive state for the search results table: column
// widths the user has dragged (seeded from the columns' weights on first
// use, rescaled proportionally if the table area is resized), an in-progress
// column-border drag, and the hover-reveal ("ghost") popup for a truncated
// cell. Owned by the caller (gui_main.cc) across frames, same as
// tableScrollPx; draw_search both reads and mutates it because the table's
// geometry — row rects, column boundaries — only exists inside draw_search.
struct TableInteraction {
  std::vector<float> columnWidthsPx;

  int   resizeBoundary = -1;  // index of the left column of the dragged boundary, -1 = none
  float resizeStartX = 0.0f;
  std::vector<float> resizeStartWidths;

  int   hoverCellRow = -1, hoverCellCol = -1;
  float hoverAccum = 0.0f;   // seconds the pointer has sat on this cell
  bool  poppedIn = false;
  AnimatedFloat popupT{0.0f};  // 0..1 pop-in progress for the ghost popup
};

enum Action : int {
    ActToggleCheatsheet = 1,
    ActSubmitSearch,
    ActDownloadSelected,
    ActTypePickerBase = 100,      // + index (0..5: Smart/Albums/Tracks/Artists/Playlists/All)
    ActTableHeaderBase = 200,     // + column index
    ActTableRowBase    = 300,     // + row index (toggles selection)
};

// Settings screen actions — separate numbering from Action above (only one
// screen's hits are live at a time, but keeping the ranges apart avoids any
// ambiguity if that ever changes).
enum SettingsAction : int {
    ActSettingsSave = 1,
    ActAddAccount,
    ActRemoveAccount,
    ActLoginWithToken,
    ActExportAccounts,
    ActImportAccounts,
    ActBrowseDownloadDir,
    ActAccountListBase   = 100,  // + account index
    ActQualityBase       = 200,  // + quality index (mp3/flac/flac-hi/flac-ultra)
    ActLanguageBase      = 300,  // + language index (en/es)
    ActConcurrencyMinus  = 400,
    ActConcurrencyPlus   = 401,
    ActRpmMinus          = 402,
    ActRpmPlus           = 403,
    ActFieldFocusBase    = 500,  // + field index, see SettingsFields below
};

// Index into the settings screen's per-frame TextFieldState array.
enum SettingsField : int {
    FieldDownloadDir = 0, FieldCountry, FieldEmail, FieldAppId, FieldAppSecret,
    FieldUserId, FieldAuthToken, FieldCount
};

struct Hit { Rect rect; int action; };

constexpr std::string_view kTypePickerOptions[] = {
    "Smart", "Albums", "Tracks", "Artists", "Playlists", "All"
};
constexpr int kTypePickerCount = 6;

// Filled with the streamer results table's columns (Title/Artist/Label/...).
const std::vector<widgets::TableColumn>& searchTableColumns();

// Maps a clicked table-header column index (see ActTableHeaderBase) back to
// the SearchController::SortColumn it represents (None for the "Sel" column,
// which isn't sortable).
search::SortColumn sortColumnForTableIndex(int col);

// Draws the search screen: query text field, cheatsheet toggle (+ panel if
// open), type picker, sortable results table, selection state, a download
// button, and any error from the last search. `typePickerIndex` is the
// caller-owned current selection (0..5, see kTypePickerOptions). `scrollPx`
// is the caller-owned table scroll offset. `hoveredAction` (-1 = none) is
// whichever Hit's action the pointer sat over last frame, for button
// hover/press feedback. `pointer`/`dtSeconds`/`table` drive the results
// table's column-resize drag and hover-reveal popup (see TableInteraction).
// Fills `hits` for this frame.
void draw_search(Canvas& c, const Rect& area, const search::SearchController& ctl,
                 const widgets::TextFieldState& queryField, bool queryFocused,
                 int typePickerIndex, float tableScrollPx, float rowH,
                 int hoverRow, int hoverHeaderCol, int hoveredAction,
                 const PointerState& pointer, float dtSeconds,
                 TableInteraction& table,
                 std::vector<Hit>& hits);

constexpr std::string_view kQualityOptions[] = {"MP3", "FLAC", "FLAC-HI", "FLAC-ULTRA"};
constexpr const char* kQualityValues[]  = {"mp3", "flac", "flac-hi", "flac-ultra"};
constexpr int kQualityCount = 4;
constexpr std::string_view kLanguageOptions[] = {"EN", "ES"};
constexpr const char* kLanguageValues[] = {"en", "es"};
constexpr int kLanguageCount = 2;

// Settings screen: account list + edit form, global settings (quality,
// concurrency, requests/min, download dir, language), save/export/import.
// `fields` are the caller-owned per-frame text buffers (see SettingsField);
// `focusedField` (-1 = none) drives which one shows a cursor. `hoveredAction`
// (-1 = none) and `pointerDown` drive button hover/press feedback. Fills
// `hits`.
void draw_settings(Canvas& c, const Rect& area, const settings::SettingsController& ctl,
                   const widgets::TextFieldState fields[FieldCount], int focusedField,
                   int accountListHover, int hoveredAction, bool pointerDown, float rowH,
                   std::vector<Hit>& hits);

} // namespace gui
