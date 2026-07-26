#include "views.hh"
#include "theme.hh"

#include <search.hh>  // search::fmt_duration

namespace gui {

const std::vector<widgets::TableColumn>& searchTableColumns() {
    static const std::vector<widgets::TableColumn> cols = {
        {"Sel", 0.5f}, {"Title", 2.2f}, {"Artist", 1.6f}, {"Label", 1.3f},
        {"Date", 0.8f}, {"Duration", 0.8f}, {"Genre", 1.0f}, {"Hi-Res", 0.7f},
        {"Explicit", 0.8f}, {"Type", 0.8f}, {"Country", 0.8f},
    };
    return cols;
}

namespace {

std::string cellForResult(const search::SearchController& ctl, int row, int col) {
    const auto& r = ctl.results()[(size_t)row];
    switch (col) {
        case 0:  return ctl.is_selected(row) ? "[x]" : "[ ]";
        case 1:  return r.title;
        case 2:  return r.artist;
        case 3:  return r.label;
        case 4:  return r.date;
        case 5:  return search::fmt_duration(r.duration);
        case 6:  return r.genre;
        case 7:  return r.hires ? "yes" : "no";
        case 8:  return r.explicit_ ? "yes" : "no";
        case 9:  return r.type;
        case 10: return r.country;
        default: return "";
    }
}

// widgets::SortColumn ordering must track search::SortColumn (see the table
// header -> ActTableHeaderBase + col dispatch in gui_main.cc); this maps a
// clicked column index back to what SearchController::sort() expects.
search::SortColumn sortColumnForIndex(int col) {
    using SC = search::SortColumn;
    switch (col) {
        case 1: return SC::Title;
        case 2: return SC::Artist;
        case 3: return SC::Label;
        case 4: return SC::Date;
        case 5: return SC::Duration;
        case 6: return SC::Genre;
        case 7: return SC::HiRes;
        case 8: return SC::Explicit;
        case 9: return SC::Type;
        case 10: return SC::Country;
        default: return SC::None;
    }
}

int indexForSortColumn(search::SortColumn sc) {
    using SC = search::SortColumn;
    switch (sc) {
        case SC::Title: return 1; case SC::Artist: return 2; case SC::Label: return 3;
        case SC::Date: return 4; case SC::Duration: return 5; case SC::Genre: return 6;
        case SC::HiRes: return 7; case SC::Explicit: return 8; case SC::Type: return 9;
        case SC::Country: return 10; default: return -1;
    }
}

} // namespace

// Exposed so gui_main.cc's click dispatch can map a header hit back to the
// SearchController::SortColumn without duplicating the table's column layout.
search::SortColumn sortColumnForTableIndex(int col) { return sortColumnForIndex(col); }

void draw_search(Canvas& c, const Rect& area, const search::SearchController& ctl,
                 const widgets::TextFieldState& queryField, bool queryFocused,
                 int typePickerIndex, float tableScrollPx, float rowH,
                 int hoverRow, int hoverHeaderCol,
                 std::vector<Hit>& hits) {
    float pad = area.h * 0.015f;
    float y = area.y;

    // ── Search box + cheatsheet toggle + submit ─────────────────────────────
    float fieldH = rowH * 1.2f;
    float cheatBtnW = fieldH, submitBtnW = area.w * 0.12f;
    Rect fieldRect = {area.x, y, area.w - cheatBtnW - submitBtnW - pad * 2.0f, fieldH};
    Rect cheatRect = {fieldRect.x + fieldRect.w + pad, y, cheatBtnW, fieldH};
    Rect submitRect = {cheatRect.x + cheatRect.w + pad, y, submitBtnW, fieldH};

    widgets::drawTextField(c, fieldRect, queryField, queryFocused,
                           "Search... (try: artist:\"name\" year:2020-2024)",
                           theme::kTextField);
    theme::accentButton(c, cheatRect.x, cheatRect.y, cheatRect.w, cheatRect.h, "?", fieldH * 0.2f);
    theme::accentButton(c, submitRect.x, submitRect.y, submitRect.w, submitRect.h, "Search", fieldH * 0.2f);
    hits.push_back({cheatRect, ActToggleCheatsheet});
    hits.push_back({submitRect, ActSubmitSearch});
    y += fieldH + pad;

    // ── Type picker ──────────────────────────────────────────────────────────
    Rect typeRow = {area.x, y, area.w, rowH * 0.9f};
    widgets::drawSegmented(c, typeRow, kTypePickerOptions, kTypePickerCount, typePickerIndex,
                           theme::kSegmented);
    auto typeRects = widgets::segmentRects(typeRow, kTypePickerCount);
    for (int i = 0; i < kTypePickerCount; i++) hits.push_back({typeRects[(size_t)i], ActTypePickerBase + i});
    y += typeRow.h + pad;

    // ── Cheatsheet panel (expandable, per the "?" toggle) ───────────────────
    if (ctl.cheatsheet_open()) {
        float panelH = rowH * 4.4f;
        Rect panel = {area.x, y, area.w, panelH};
        c.rect(panel.x, panel.y, panel.w, panel.h, theme::kPanel, area.h * 0.01f);
        float ls = rowH * 0.34f, lx = panel.x + area.w * 0.015f, ly = panel.y + rowH * 0.15f;
        const char* lines[] = {
            "Fields: artist, title, album, genre, label, country, type, year, duration, hires, explicit",
            "Operators:  field:value (contains)   field:>N  field:<N  field:>=N  field:<=N (numeric)   field:N-M (range)",
            "Boolean:  space = AND   or / \"or\" = OR   not / \"!\" = NOT   ( ) for grouping",
            "Examples:  artist:\"Luísa Sonza\" year:2020-2024   duration:>3m hires:true   not explicit:true",
        };
        for (const char* line : lines) {
            c.text(line, lx, ly, ls, theme::kText);
            ly += ls * 1.6f;
        }
        y += panelH + pad;
    }

    // ── Error from the last search, if any ──────────────────────────────────
    if (!ctl.last_error().empty()) {
        c.text(ctl.last_error(), area.x, y, rowH * 0.34f, theme::kAccentHi);
        y += rowH * 0.7f;
    }

    // ── Results table ────────────────────────────────────────────────────────
    float bottomBarH = rowH * 1.2f;
    Rect tableArea = {area.x, y, area.w, (area.y + area.h) - y - bottomBarH - pad};
    auto cols = searchTableColumns();
    widgets::TableStyle tstyle;
    tstyle.headerBg = theme::kPanel; tstyle.headerText = theme::kText;
    tstyle.headerHover = theme::kTrack; tstyle.sortGlyph = theme::kAccentLo;
    tstyle.rowBg = theme::kPanel; tstyle.rowText = theme::kText;
    tstyle.hoverBg = theme::kTrack; tstyle.gridLine = theme::kDim;

    int rowCount = (int)ctl.results().size();
    auto cellFn = [&ctl](int row, int col) { return cellForResult(ctl, row, col); };
    auto visibleRows = widgets::drawSortableTable(
        c, tableArea, cols, cellFn, rowCount,
        indexForSortColumn(ctl.sort_column()), ctl.sort_ascending(),
        tableScrollPx, rowH, hoverRow, hoverHeaderCol, tstyle);

    Rect header = widgets::tableHeaderRow(tableArea, rowH);
    auto headerCols = widgets::tableHeaderColumnRects(header, cols);
    for (size_t i = 0; i < headerCols.size(); i++)
        hits.push_back({headerCols[i], ActTableHeaderBase + (int)i});
    for (auto& vr : visibleRows)
        hits.push_back({vr.rect, ActTableRowBase + vr.index});

    y = tableArea.y + tableArea.h + pad;

    // ── Bottom bar: selection count + download button ──────────────────────
    Rect dlRect = {area.x, y, area.w * 0.2f, bottomBarH};
    theme::accentButton(c, dlRect.x, dlRect.y, dlRect.w, dlRect.h,
                        "Download (" + std::to_string(ctl.selected().size()) + ")", bottomBarH * 0.2f);
    hits.push_back({dlRect, ActDownloadSelected});
}

} // namespace gui
