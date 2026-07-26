#pragma once
// Search filter DSL — UTF-8 port of streamer-gui/src/QueryParser.h/.cpp.
//
// Same grammar and field set as the Win32 GUI's QueryParser, ported from
// std::wstring to UTF-8 std::string so it runs identically on every
// platform (no wchar_t, no Windows codepage conversions). Tokenization and
// case-folding operate on decoded Unicode codepoints internally (utf8.hh),
// so accented names — the entire point of this port — compare correctly
// regardless of the caller's locale.
//
// Fields: artist, title, album, genre, label, country, type, year, duration,
// hires, explicit. Operators: ':' (contains / exact for numeric), '>' '<'
// '>=' '<=' (numeric), '-' (numeric range, e.g. year:2000-2010). Boolean
// AND (implicit or '&&'/"and"/"y"), OR ('||'/"or"/"o"), NOT ('!'/"not"),
// parentheses for grouping.

#include <memory>
#include <string>
#include <vector>

namespace query_dsl {

enum class NodeKind { And, Or, Not, Filter, Term };

struct FilterNode {
    std::string field;   // artist, title, album, genre, year, duration, type, hires, label, country, explicit
    std::string op;       // ":" | ">" | "<" | ">=" | "<=" | "-" (range)
    std::string value;
    std::string value2;   // only for range (year:2000-2010)
};

// Fields matched against — the GUI's SearchController fills one of these per
// result row (see search_controller.hh).
struct SearchResult {
    std::string id, title, artist, album, type;
    std::string genre, label, country, date;
    int  year     = 0;
    int  duration = 0;
    bool hires    = false;
    bool explicit_ = false;
};

struct QueryNode {
    NodeKind kind = NodeKind::Term;
    std::string term;         // bare keyword
    FilterNode  filter;
    std::unique_ptr<QueryNode> left, right;
};

// Parse a UTF-8 query string into an AST. Returns a Term("") node on empty
// or unparseable input — never throws, never null.
QueryNode Parse(const std::string& input);

// Evaluate the AST against a result row.
bool Match(const QueryNode& node, const SearchResult& r);

// Extract the best plain-text keyword to send to the remote API (the
// longest bare term or artist/title/album filter value found).
std::string ExtractBaseTerm(const QueryNode& node);

// Extract a type: filter if present ("album"/"track"/"artist"/"playlist"/"all"),
// empty string if none.
std::string ExtractTypeHint(const QueryNode& node);

// Count of filter/term nodes satisfied — higher = better match, for ranking
// results that pass Match() but should still be ordered by relevance.
int Score(const QueryNode& node, const SearchResult& r);

} // namespace query_dsl
