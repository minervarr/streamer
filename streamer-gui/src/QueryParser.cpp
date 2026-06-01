#include "QueryParser.h"
#include <algorithm>
#include <sstream>
#include <stdexcept>

// ── Field / value normalization (ES → EN) ────────────────────────────────────

static std::wstring NormalizeField(const std::wstring& f) {
    if (f == L"artista")                         return L"artist";
    if (f == L"titulo" || f == L"título")        return L"title";
    if (f == L"album"  || f == L"álbum")         return L"album";
    if (f == L"genero" || f == L"género")        return L"genre";
    if (f == L"sello")                           return L"label";
    if (f == L"tipo")                            return L"type";
    if (f == L"pais"   || f == L"país")          return L"country";
    if (f == L"año"    || f == L"anio")          return L"year";
    if (f == L"duracion" || f == L"duración")    return L"duration";
    if (f == L"alta_res" || f == L"altares")     return L"hires";
    if (f == L"explicito" || f == L"explícito"
        || f == L"explicita" || f == L"explícita") return L"explicit";
    return f;
}

static std::wstring NormalizeBool(const std::wstring& v) {
    std::wstring l = v;
    for (auto& c : l) c = (wchar_t)std::towlower(c);
    if (l == L"verdadero" || l == L"sí" || l == L"si" || l == L"verdad") return L"true";
    if (l == L"falso" || l == L"no")  return L"false";
    return l;
}

static std::wstring NormalizeType(const std::wstring& v) {
    std::wstring l = v;
    for (auto& c : l) c = (wchar_t)std::towlower(c);
    if (l == L"álbum"  || l == L"album"  || l == L"álbumes" || l == L"albumes") return L"album";
    if (l == L"pista"  || l == L"pistas")  return L"track";
    if (l == L"artista"|| l == L"artistas") return L"artist";
    if (l == L"lista"  || l == L"listas" || l == L"playlist") return L"playlist";
    if (l == L"todos"  || l == L"todo")    return L"all";
    return l;
}

// ── Tokenizer ─────────────────────────────────────────────────────────────────

enum class TokKind { Word, Quoted, Colon, Gt, Lt, Gte, Lte, Dash,
                     And, Or, Not, LParen, RParen, Eof };

struct Token {
    TokKind      kind;
    std::wstring val;
};

static std::wstring ToLower(std::wstring s) {
    for (auto& c : s) c = (wchar_t)std::towlower(c);
    return s;
}

static bool IsIdChar(wchar_t c) {
    return std::iswalnum(c) || c == L'_' || c == L'.' || c == L'/' || c == L'á'
        || c == L'é' || c == L'í' || c == L'ó' || c == L'ú' || c == L'ñ'
        || c == L'Á' || c == L'É' || c == L'Í' || c == L'Ó' || c == L'Ú' || c == L'Ñ'
        || c == L'ü' || c == L'Ü';
}

static std::vector<Token> Tokenize(const std::wstring& input) {
    std::vector<Token> toks;
    size_t i = 0, n = input.size();

    auto skip = [&]() { while (i < n && std::iswspace(input[i])) ++i; };

    while (i < n) {
        skip();
        if (i >= n) break;

        wchar_t c = input[i];

        if (c == L'"') {
            ++i;
            std::wstring s;
            while (i < n && input[i] != L'"') {
                if (input[i] == L'\\' && i + 1 < n) { ++i; }
                s += input[i++];
            }
            if (i < n) ++i; // closing "
            toks.push_back({TokKind::Quoted, s});
            continue;
        }

        if (c == L':')  { toks.push_back({TokKind::Colon,  L":"}); ++i; continue; }
        if (c == L'(')  { toks.push_back({TokKind::LParen, L"("}); ++i; continue; }
        if (c == L')')  { toks.push_back({TokKind::RParen, L")"}); ++i; continue; }
        if (c == L'-')  { toks.push_back({TokKind::Dash,   L"-"}); ++i; continue; }

        if (c == L'>') {
            if (i + 1 < n && input[i+1] == L'=') { toks.push_back({TokKind::Gte, L">="}); i += 2; }
            else                                   { toks.push_back({TokKind::Gt,  L">"});  ++i; }
            continue;
        }
        if (c == L'<') {
            if (i + 1 < n && input[i+1] == L'=') { toks.push_back({TokKind::Lte, L"<="}); i += 2; }
            else                                   { toks.push_back({TokKind::Lt,  L"<"});  ++i; }
            continue;
        }
        if (c == L'&' && i + 1 < n && input[i+1] == L'&') { toks.push_back({TokKind::And, L"AND"}); i += 2; continue; }
        if (c == L'|' && i + 1 < n && input[i+1] == L'|') { toks.push_back({TokKind::Or,  L"OR"});  i += 2; continue; }
        if (c == L'!' && (i + 1 >= n || input[i+1] != L'=')) { toks.push_back({TokKind::Not, L"NOT"}); ++i; continue; }

        if (std::iswalnum(c) || c == L'_') {
            std::wstring w;
            while (i < n && IsIdChar(input[i])) w += input[i++];
            std::wstring wl = ToLower(w);
            if (wl == L"and" || wl == L"y") toks.push_back({TokKind::And, w});
            else if (wl == L"or" || wl == L"o")  toks.push_back({TokKind::Or,  w});
            else if (wl == L"not")               toks.push_back({TokKind::Not, w});
            else                                  toks.push_back({TokKind::Word, w});
            continue;
        }

        ++i; // skip unknown char
    }

    toks.push_back({TokKind::Eof, L""});
    return toks;
}

// ── Recursive-descent parser ──────────────────────────────────────────────────
// Grammar:
//   expr   := or_expr
//   or_expr := and_expr (OR and_expr)*
//   and_expr:= not_expr (AND? not_expr)*    (AND is implicit between adjacent terms)
//   not_expr:= NOT not_expr | atom
//   atom   := LPAREN expr RPAREN | filter | term
//   filter := WORD COLON (GT|LT|GTE|LTE)? (WORD|QUOTED) (DASH (WORD|QUOTED))?
//   term   := WORD | QUOTED

struct Parser {
    std::vector<Token> toks;
    size_t pos = 0;

    Token& cur()  { return toks[pos]; }
    Token& peek() { return toks[pos]; }
    Token consume() { return toks[pos++]; }

    bool at(TokKind k) { return cur().kind == k; }

    bool atValue() {
        return at(TokKind::Word) || at(TokKind::Quoted);
    }

    bool atAtomStart() {
        return at(TokKind::Word) || at(TokKind::Quoted) ||
               at(TokKind::LParen) || at(TokKind::Not);
    }

    std::unique_ptr<QueryNode> parseExpr() { return parseOr(); }

    std::unique_ptr<QueryNode> parseOr() {
        auto node = parseAnd();
        while (at(TokKind::Or)) {
            consume();
            auto rhs = parseAnd();
            auto n = std::make_unique<QueryNode>();
            n->kind  = NodeKind::Or;
            n->left  = std::move(node);
            n->right = std::move(rhs);
            node = std::move(n);
        }
        return node;
    }

    std::unique_ptr<QueryNode> parseAnd() {
        auto node = parseNot();
        while (!at(TokKind::Eof) && !at(TokKind::Or) && !at(TokKind::RParen)) {
            bool explicit_and = at(TokKind::And);
            if (explicit_and) consume();
            if (!atAtomStart()) break;
            auto rhs = parseNot();
            auto n = std::make_unique<QueryNode>();
            n->kind  = NodeKind::And;
            n->left  = std::move(node);
            n->right = std::move(rhs);
            node = std::move(n);
        }
        return node;
    }

    std::unique_ptr<QueryNode> parseNot() {
        if (at(TokKind::Not)) {
            consume();
            auto n = std::make_unique<QueryNode>();
            n->kind  = NodeKind::Not;
            n->left  = parseNot();
            return n;
        }
        return parseAtom();
    }

    std::unique_ptr<QueryNode> parseAtom() {
        if (at(TokKind::LParen)) {
            consume();
            auto n = parseExpr();
            if (at(TokKind::RParen)) consume();
            return n;
        }

        // filter: WORD COLON ...
        if (at(TokKind::Word) && pos + 1 < toks.size() && toks[pos+1].kind == TokKind::Colon) {
            std::wstring field = NormalizeField(ToLower(consume().val));
            consume(); // colon

            auto n = std::make_unique<QueryNode>();
            n->kind = NodeKind::Filter;
            n->filter.field = field;

            // optional comparison op
            if (at(TokKind::Gt))       { n->filter.op = L">";  consume(); }
            else if (at(TokKind::Lt))  { n->filter.op = L"<";  consume(); }
            else if (at(TokKind::Gte)) { n->filter.op = L">="; consume(); }
            else if (at(TokKind::Lte)) { n->filter.op = L"<="; consume(); }
            else                        { n->filter.op = L":"; }

            // value
            if (atValue()) {
                n->filter.value = consume().val;
                // range: value DASH value
                if (at(TokKind::Dash)) {
                    consume();
                    if (atValue()) {
                        n->filter.value2 = consume().val;
                        n->filter.op = L"-";
                    }
                }
            }
            return n;
        }

        // bare term
        if (atValue()) {
            auto n = std::make_unique<QueryNode>();
            n->kind = NodeKind::Term;
            n->term = consume().val;
            return n;
        }

        // fallback: empty term
        auto n = std::make_unique<QueryNode>();
        n->kind = NodeKind::Term;
        return n;
    }
};

// ── Public API implementations ────────────────────────────────────────────────

namespace QueryParser {

QueryNode Parse(const std::wstring& input) {
    if (input.empty()) {
        QueryNode n; n.kind = NodeKind::Term; return n;
    }
    Parser p;
    p.toks = Tokenize(input);
    auto root = p.parseExpr();
    if (root) return std::move(*root);
    QueryNode n; n.kind = NodeKind::Term; return n;
}

// ── Matching ──────────────────────────────────────────────────────────────────

static bool ContainsI(const std::wstring& haystack, const std::wstring& needle) {
    if (needle.empty()) return true;
    auto h = ToLower(haystack);
    auto n = ToLower(needle);
    return h.find(n) != std::wstring::npos;
}

static bool MatchFilter(const FilterNode& f, const SearchResult& r) {
    const std::wstring& field = f.field;

    // String fields
    if (field == L"title")  return ContainsI(r.title,  f.value);
    if (field == L"artist") return ContainsI(r.artist, f.value);
    if (field == L"album")  return ContainsI(r.album,  f.value);
    if (field == L"genre")  return ContainsI(r.genre,  f.value);
    if (field == L"label")  return ContainsI(r.label,  f.value);
    if (field == L"type")    return ContainsI(r.type,    NormalizeType(f.value));
    if (field == L"country") return ContainsI(r.country, f.value);

    if (field == L"hires") {
        std::wstring v = NormalizeBool(f.value);
        bool want = (v == L"true" || v == L"1");
        return r.hires == want;
    }
    if (field == L"explicit") {
        std::wstring v = NormalizeBool(f.value);
        bool want = (v == L"true" || v == L"1");
        return r.explicit_ == want;
    }

    // Numeric fields: year, duration
    int numField = (field == L"year") ? r.year : (field == L"duration") ? r.duration : -1;
    if (numField < 0) return true; // unknown field — pass

    if (f.op == L"-") {
        // range
        int lo = 0, hi = 0;
        try { lo = std::stoi(f.value);  } catch (...) {}
        try { hi = std::stoi(f.value2); } catch (...) {}
        return numField >= lo && numField <= hi;
    }
    int cmp = 0;
    try { cmp = std::stoi(f.value); } catch (...) { return true; }
    if (f.op == L">")  return numField >  cmp;
    if (f.op == L"<")  return numField <  cmp;
    if (f.op == L">=") return numField >= cmp;
    if (f.op == L"<=") return numField <= cmp;
    // ":" for numeric — exact match
    return numField == cmp;
}

bool Match(const QueryNode& node, const SearchResult& r) {
    switch (node.kind) {
    case NodeKind::Term:
        if (node.term.empty()) return true;
        return ContainsI(r.title, node.term) || ContainsI(r.artist, node.term);
    case NodeKind::Filter:
        return MatchFilter(node.filter, r);
    case NodeKind::And:
        return node.left  && node.right &&
               Match(*node.left, r) && Match(*node.right, r);
    case NodeKind::Or:
        return node.left  && node.right &&
               (Match(*node.left, r) || Match(*node.right, r));
    case NodeKind::Not:
        return node.left && !Match(*node.left, r);
    }
    return true;
}

// ── Base term extraction ──────────────────────────────────────────────────────
// Walk the tree collecting all bare terms and string-field values,
// return the longest one (most informative for the API search).

static void CollectTerms(const QueryNode& node, std::vector<std::wstring>& out) {
    switch (node.kind) {
    case NodeKind::Term:
        if (!node.term.empty()) out.push_back(node.term);
        break;
    case NodeKind::Filter: {
        const auto& f = node.filter.field;
        if (f == L"artist" || f == L"title" || f == L"album")
            if (!node.filter.value.empty()) out.push_back(node.filter.value);
        break;
    }
    case NodeKind::And:
    case NodeKind::Or:
        if (node.left)  CollectTerms(*node.left,  out);
        if (node.right) CollectTerms(*node.right, out);
        break;
    case NodeKind::Not:
        // don't use negated terms as the search base
        break;
    }
}

std::wstring ExtractBaseTerm(const QueryNode& node) {
    std::vector<std::wstring> terms;
    CollectTerms(node, terms);
    if (terms.empty()) return L"";
    // return the longest term as the most specific query
    return *std::max_element(terms.begin(), terms.end(),
        [](const std::wstring& a, const std::wstring& b){ return a.size() < b.size(); });
}

// ── Type hint extraction ──────────────────────────────────────────────────────

static void CollectTypeHints(const QueryNode& node, std::vector<std::wstring>& out) {
    if (node.kind == NodeKind::Filter && node.filter.field == L"type") {
        out.push_back(NormalizeType(node.filter.value));
        return;
    }
    if (node.left)  CollectTypeHints(*node.left,  out);
    if (node.right) CollectTypeHints(*node.right, out);
}

std::wstring ExtractTypeHint(const QueryNode& node) {
    std::vector<std::wstring> hints;
    CollectTypeHints(node, hints);
    if (hints.empty()) return L"";
    return hints[0];
}

// ── Scoring ───────────────────────────────────────────────────────────────────

int Score(const QueryNode& node, const SearchResult& r) {
    switch (node.kind) {
    case NodeKind::Term:
        return (ContainsI(r.title, node.term) || ContainsI(r.artist, node.term)) ? 1 : 0;
    case NodeKind::Filter:
        return MatchFilter(node.filter, r) ? 1 : 0;
    case NodeKind::And:
    case NodeKind::Or:
        return (node.left  ? Score(*node.left,  r) : 0) +
               (node.right ? Score(*node.right, r) : 0);
    case NodeKind::Not:
        return 0;
    }
    return 0;
}

} // namespace QueryParser
