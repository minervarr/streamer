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

namespace {

// Label + text field sharing one row, same proportions as
// widgets::drawDropdownField's label/value split.
void labeledField(Canvas& c, const Rect& row, std::string_view label,
                  const widgets::TextFieldState& field, bool focused,
                  std::vector<Hit>& hits, int action) {
    float fieldW = row.w * 0.68f;
    Rect labelZone = {row.x, row.y, row.w - fieldW - row.h * 0.3f, row.h};
    Rect fieldRect = {row.x + row.w - fieldW, row.y, fieldW, row.h};
    c.text(label, labelZone.x, labelZone.y + row.h * 0.3f, row.h * 0.4f, theme::kText);
    widgets::drawTextField(c, fieldRect, field, focused, "", theme::kTextField);
    hits.push_back({fieldRect, action});
}

std::string accountLabel(const config::Account& a, int idx) {
    if (!a.email.empty()) return a.email;
    if (!a.country.empty()) return "Account (" + a.country + ")";
    return "Account " + std::to_string(idx + 1);
}

} // namespace

void draw_settings(Canvas& c, const Rect& area, const settings::SettingsController& ctl,
                   const widgets::TextFieldState fields[FieldCount], int focusedField,
                   int accountListHover, float rowH,
                   std::vector<Hit>& hits) {
    float pad = rowH * 0.3f;
    float y = area.y;
    float halfW = area.w * 0.48f;
    Rect leftCol = {area.x, y, halfW, area.h};
    Rect rightCol = {area.x + area.w - halfW, y, halfW, area.h};

    // ── Left column: accounts ────────────────────────────────────────────────
    {
        float ly = leftCol.y;
        widgets::drawGroupHeader(c, {leftCol.x, ly, leftCol.w, rowH}, "Accounts");
        ly += rowH * 1.1f;

        std::vector<std::string> labels;
        const auto& accts = ctl.accounts();
        for (size_t i = 0; i < accts.size(); i++) labels.push_back(accountLabel(accts[i], (int)i));
        Rect listArea = {leftCol.x, ly, leftCol.w, rowH * 3.2f};
        auto rows = widgets::drawScrollList(c, listArea, labels, ctl.current_account_index(),
                                            0.0f, rowH, accountListHover, widgets::kTextFree,
                                            theme::kScrollList);
        for (auto& lr : rows) hits.push_back({lr.rect, ActAccountListBase + lr.index});
        ly += listArea.h + pad;

        Rect addRect = {leftCol.x, ly, leftCol.w * 0.48f, rowH};
        Rect delRect = {leftCol.x + leftCol.w * 0.52f, ly, leftCol.w * 0.48f, rowH};
        widgets::drawFitButton(c, addRect, "Add Account", theme::kPanel, theme::kText, rowH * 0.2f);
        widgets::drawFitButton(c, delRect, "Remove", theme::kPanel, theme::kText, rowH * 0.2f);
        hits.push_back({addRect, ActAddAccount});
        hits.push_back({delRect, ActRemoveAccount});
        ly += rowH + pad * 1.5f;

        widgets::drawGroupHeader(c, {leftCol.x, ly, leftCol.w, rowH}, "Account details");
        ly += rowH * 1.1f;

        Rect row = {leftCol.x, ly, leftCol.w, rowH};
        labeledField(c, row, "Country", fields[FieldCountry], focusedField == FieldCountry, hits, ActFieldFocusBase + FieldCountry);
        row.y += rowH * 1.15f;
        labeledField(c, row, "Email", fields[FieldEmail], focusedField == FieldEmail, hits, ActFieldFocusBase + FieldEmail);
        row.y += rowH * 1.15f;
        labeledField(c, row, "App ID", fields[FieldAppId], focusedField == FieldAppId, hits, ActFieldFocusBase + FieldAppId);
        row.y += rowH * 1.15f;
        labeledField(c, row, "App Secret", fields[FieldAppSecret], focusedField == FieldAppSecret, hits, ActFieldFocusBase + FieldAppSecret);
        row.y += rowH * 1.15f;
        labeledField(c, row, "User ID", fields[FieldUserId], focusedField == FieldUserId, hits, ActFieldFocusBase + FieldUserId);
        row.y += rowH * 1.15f;
        labeledField(c, row, "Auth Token", fields[FieldAuthToken], focusedField == FieldAuthToken, hits, ActFieldFocusBase + FieldAuthToken);
        row.y += rowH * 1.3f;

        Rect loginRect = {row.x, row.y, leftCol.w * 0.5f, rowH};
        theme::accentButton(c, loginRect.x, loginRect.y, loginRect.w, loginRect.h,
                            "Login with Token", rowH * 0.2f);
        hits.push_back({loginRect, ActLoginWithToken});
        row.y += rowH * 1.3f;

        Rect expRect = {row.x, row.y, leftCol.w * 0.48f, rowH};
        Rect impRect = {row.x + leftCol.w * 0.52f, row.y, leftCol.w * 0.48f, rowH};
        widgets::drawFitButton(c, expRect, "Export Accounts", theme::kPanel, theme::kText, rowH * 0.2f);
        widgets::drawFitButton(c, impRect, "Import Accounts", theme::kPanel, theme::kText, rowH * 0.2f);
        hits.push_back({expRect, ActExportAccounts});
        hits.push_back({impRect, ActImportAccounts});
    }

    // ── Right column: global settings ───────────────────────────────────────
    {
        float ry = rightCol.y;
        widgets::drawGroupHeader(c, {rightCol.x, ry, rightCol.w, rowH}, "Global Settings");
        ry += rowH * 1.1f;

        Rect qualityRow = {rightCol.x, ry, rightCol.w, rowH};
        int qIdx = 1; // flac default
        for (int i = 0; i < kQualityCount; i++) if (ctl.config().settings.quality == kQualityValues[i]) qIdx = i;
        widgets::drawSegmented(c, qualityRow, kQualityOptions, kQualityCount, qIdx, theme::kSegmented);
        auto qRects = widgets::segmentRects(qualityRow, kQualityCount);
        for (int i = 0; i < kQualityCount; i++) hits.push_back({qRects[(size_t)i], ActQualityBase + i});
        ry += rowH * 1.3f;

        widgets::drawStepper(c, {rightCol.x, ry, rightCol.w, rowH}, "Concurrency",
                             std::to_string(ctl.config().settings.concurrency), theme::kStepper);
        auto cg = widgets::stepperGeom({rightCol.x, ry, rightCol.w, rowH});
        hits.push_back({cg.minus, ActConcurrencyMinus});
        hits.push_back({cg.plus, ActConcurrencyPlus});
        ry += rowH * 1.15f;

        widgets::drawStepper(c, {rightCol.x, ry, rightCol.w, rowH}, "Requests/min (0=unlimited)",
                             std::to_string(ctl.config().settings.requests_per_minute), theme::kStepper);
        auto rg = widgets::stepperGeom({rightCol.x, ry, rightCol.w, rowH});
        hits.push_back({rg.minus, ActRpmMinus});
        hits.push_back({rg.plus, ActRpmPlus});
        ry += rowH * 1.3f;

        Rect dirRow = {rightCol.x, ry, rightCol.w, rowH};
        labeledField(c, dirRow, "Download Dir", fields[FieldDownloadDir],
                    focusedField == FieldDownloadDir, hits, ActFieldFocusBase + FieldDownloadDir);
        Rect browseRect = {dirRow.x, dirRow.y + rowH * 1.15f, rightCol.w * 0.3f, rowH};
        widgets::drawFitButton(c, browseRect, "Browse...", theme::kPanel, theme::kText, rowH * 0.2f);
        hits.push_back({browseRect, ActBrowseDownloadDir});
        ry += rowH * 2.45f;

        Rect langRow = {rightCol.x, ry, rightCol.w * 0.5f, rowH};
        int lIdx = ctl.config().settings.language == "es" ? 1 : 0;
        widgets::drawSegmented(c, langRow, kLanguageOptions, kLanguageCount, lIdx, theme::kSegmented);
        auto lRects = widgets::segmentRects(langRow, kLanguageCount);
        for (int i = 0; i < kLanguageCount; i++) hits.push_back({lRects[(size_t)i], ActLanguageBase + i});
        ry += rowH * 1.3f;

        Rect saveRect = {rightCol.x, ry, rightCol.w * 0.3f, rowH * 1.2f};
        theme::accentButton(c, saveRect.x, saveRect.y, saveRect.w, saveRect.h,
                            ctl.dirty() ? "Save*" : "Save", rowH * 0.2f);
        hits.push_back({saveRect, ActSettingsSave});
        ry += rowH * 1.6f;

        if (!ctl.last_error().empty())
            c.text(ctl.last_error(), rightCol.x, ry, rowH * 0.32f, theme::kAccentHi);
    }
}

} // namespace gui
