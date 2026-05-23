#pragma once
#include <string>
#include <memory>
#include <vector>
#include <cwctype>

// ── AST ──────────────────────────────────────────────────────────────────────

enum class NodeKind { And, Or, Not, Filter, Term };

struct FilterNode {
    std::wstring field;   // artist, title, album, genre, year, duration, type, hires, label
    std::wstring op;      // ":" | ">" | "<" | ">=" | "<=" | "-"(range)
    std::wstring value;
    std::wstring value2;  // only for range (year:2000-2010)
};

struct QueryNode {
    NodeKind kind = NodeKind::Term;
    std::wstring term;        // bare keyword
    FilterNode   filter;
    std::unique_ptr<QueryNode> left, right;
};

// ── SearchResult (full fields for matching) ──────────────────────────────────

struct SearchResult {
    std::wstring id, title, artist, album, type;
    std::wstring genre, label;
    int  year     = 0;
    int  duration = 0;
    bool hires    = false;
};

// ── Public API ────────────────────────────────────────────────────────────────

namespace QueryParser {
    // Parse a query string into an AST. Returns a Term("") node on empty input.
    QueryNode Parse(const std::wstring& input);

    // Evaluate the AST against a result row.
    bool Match(const QueryNode& node, const SearchResult& r);

    // Extract the best plain-text keyword to send to the API.
    std::wstring ExtractBaseTerm(const QueryNode& node);

    // Extract type: hint if present (returns L"" if none found).
    std::wstring ExtractTypeHint(const QueryNode& node);

    // Score: count of filter nodes satisfied — higher = better match
    int Score(const QueryNode& node, const SearchResult& r);
}
