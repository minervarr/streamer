#include "query_dsl.hh"

#include <algorithm>
#include <cstdint>

namespace query_dsl {

// ── UTF-8 <-> codepoint helpers ────────────────────────────────────────────
// Tokenization and case-folding need to reason about whole codepoints, not
// UTF-8 bytes (a multi-byte accented character must never be split mid-
// sequence, and case-folding must not be done byte-wise). No external
// dependency: just enough of a UTF-8 codec to decode/encode/fold.

namespace {

std::u32string Utf8Decode(const std::string& s) {
    std::u32string out;
    out.reserve(s.size());
    size_t i = 0, n = s.size();
    while (i < n) {
        unsigned char b0 = (unsigned char)s[i];
        char32_t cp; int len;
        if (b0 < 0x80)      { cp = b0; len = 1; }
        else if ((b0 & 0xE0) == 0xC0) { cp = b0 & 0x1F; len = 2; }
        else if ((b0 & 0xF0) == 0xE0) { cp = b0 & 0x0F; len = 3; }
        else if ((b0 & 0xF8) == 0xF0) { cp = b0 & 0x07; len = 4; }
        else { ++i; continue; } // invalid lead byte — skip
        if (i + (size_t)len > n) break;
        bool ok = true;
        for (int k = 1; k < len; ++k) {
            unsigned char bk = (unsigned char)s[i + (size_t)k];
            if ((bk & 0xC0) != 0x80) { ok = false; break; }
            cp = (cp << 6) | (bk & 0x3F);
        }
        if (!ok) { ++i; continue; }
        out.push_back(cp);
        i += (size_t)len;
    }
    return out;
}

std::string Utf8Encode(const std::u32string& s) {
    std::string out;
    out.reserve(s.size());
    for (char32_t cp : s) {
        if (cp < 0x80) {
            out += (char)cp;
        } else if (cp < 0x800) {
            out += (char)(0xC0 | (cp >> 6));
            out += (char)(0x80 | (cp & 0x3F));
        } else if (cp < 0x10000) {
            out += (char)(0xE0 | (cp >> 12));
            out += (char)(0x80 | ((cp >> 6) & 0x3F));
            out += (char)(0x80 | (cp & 0x3F));
        } else {
            out += (char)(0xF0 | (cp >> 18));
            out += (char)(0x80 | ((cp >> 12) & 0x3F));
            out += (char)(0x80 | ((cp >> 6) & 0x3F));
            out += (char)(0x80 | (cp & 0x3F));
        }
    }
    return out;
}

// ASCII + Latin-1 Supplement case folding — covers accented Western
// European letters (À-Þ / à-þ) with a simple codepoint arithmetic, which is
// the whole point of this port: unlike the old Win32 GUI's hardcoded
// Spanish-vowel whitelist, this treats any Latin-1 accented letter as a
// normal word character and folds it correctly, not just the six the old
// code special-cased.
char32_t ToLowerCp(char32_t c) {
    if (c >= U'A' && c <= U'Z') return c + 32;
    if (c >= 0xC0 && c <= 0xDE && c != 0xD7) return c + 0x20; // À-Þ (skip ×)
    return c;
}

bool IsWordCp(char32_t c) {
    if ((c >= U'0' && c <= U'9') || (c >= U'a' && c <= U'z') || (c >= U'A' && c <= U'Z'))
        return true;
    if (c == U'_' || c == U'.' || c == U'/') return true;
    return c >= 0x80; // any non-ASCII codepoint: accented letters, CJK, etc.
}

bool IsSpaceCp(char32_t c) { return c == U' ' || c == U'\t' || c == U'\n' || c == U'\r'; }

std::u32string ToLowerStr(const std::u32string& s) {
    std::u32string out(s);
    for (auto& c : out) c = ToLowerCp(c);
    return out;
}

// Diacritic stripping for search matching only (never for display/storage):
// a US/ASCII keyboard has no way to type "í", so a bare "luisa" must still
// find "Luísa" — case-folding alone isn't enough since 'í' != 'i' even
// lowercased. Input is expected already lowercased by ToLowerCp, so only
// the lowercase accented forms need mapping here.
char32_t StripAccentCp(char32_t c) {
    switch (c) {
        case U'à': case U'á': case U'â': case U'ã': case U'ä': case U'å': return U'a';
        case U'è': case U'é': case U'ê': case U'ë':                     return U'e';
        case U'ì': case U'í': case U'î': case U'ï':                     return U'i';
        case U'ò': case U'ó': case U'ô': case U'õ': case U'ö':          return U'o';
        case U'ù': case U'ú': case U'û': case U'ü':                     return U'u';
        case U'ý': case U'ÿ':                                            return U'y';
        case U'ñ':                                                       return U'n';
        case U'ç':                                                       return U'c';
        default: return c;
    }
}

std::u32string FoldForSearch(const std::u32string& s) {
    std::u32string out(s);
    for (auto& c : out) c = StripAccentCp(ToLowerCp(c));
    return out;
}

bool ContainsI(const std::string& haystack, const std::string& needle) {
    if (needle.empty()) return true;
    std::u32string h = FoldForSearch(Utf8Decode(haystack));
    std::u32string n = FoldForSearch(Utf8Decode(needle));
    return h.find(n) != std::u32string::npos;
}

bool EqualsI(const std::string& a, const std::string& b) {
    return FoldForSearch(Utf8Decode(a)) == FoldForSearch(Utf8Decode(b));
}

// ── Field / value normalization (ES -> EN, mirrors QueryParser.cpp) ───────

std::string NormalizeField(const std::string& f) {
    if (f == "artista")                         return "artist";
    if (f == "titulo" || f == "título")         return "title";
    if (f == "album"  || f == "álbum")          return "album";
    if (f == "genero" || f == "género")         return "genre";
    if (f == "sello")                           return "label";
    if (f == "tipo")                            return "type";
    if (f == "pais"   || f == "país")           return "country";
    if (f == "año"    || f == "anio")           return "year";
    if (f == "duracion" || f == "duración")     return "duration";
    if (f == "alta_res" || f == "altares")      return "hires";
    if (f == "explicito" || f == "explícito"
        || f == "explicita" || f == "explícita") return "explicit";
    return f;
}

std::string NormalizeBool(const std::string& v) {
    std::string l = Utf8Encode(ToLowerStr(Utf8Decode(v)));
    if (l == "verdadero" || l == "sí" || l == "si" || l == "verdad") return "true";
    if (l == "falso" || l == "no") return "false";
    return l;
}

std::string NormalizeType(const std::string& v) {
    std::string l = Utf8Encode(ToLowerStr(Utf8Decode(v)));
    if (l == "álbum"  || l == "album"  || l == "álbumes" || l == "albumes") return "album";
    if (l == "pista"  || l == "pistas")  return "track";
    if (l == "artista"|| l == "artistas") return "artist";
    if (l == "lista"  || l == "listas" || l == "playlist") return "playlist";
    if (l == "todos"  || l == "todo")    return "all";
    return l;
}

// ── Tokenizer ────────────────────────────────────────────────────────────

enum class TokKind { Word, Quoted, Colon, Gt, Lt, Gte, Lte, Dash, Eq,
                     And, Or, Not, LParen, RParen, Eof };

struct Token { TokKind kind; std::string val; };

std::vector<Token> Tokenize(const std::u32string& input) {
    std::vector<Token> toks;
    size_t i = 0, n = input.size();
    auto skip = [&]() { while (i < n && IsSpaceCp(input[i])) ++i; };

    while (i < n) {
        skip();
        if (i >= n) break;
        char32_t c = input[i];

        if (c == U'"') {
            ++i;
            std::u32string s;
            while (i < n && input[i] != U'"') {
                if (input[i] == U'\\' && i + 1 < n) ++i;
                s += input[i++];
            }
            if (i < n) ++i; // closing quote
            toks.push_back({TokKind::Quoted, Utf8Encode(s)});
            continue;
        }

        if (c == U':') { toks.push_back({TokKind::Colon,  ":"}); ++i; continue; }
        if (c == U'(') { toks.push_back({TokKind::LParen, "("}); ++i; continue; }
        if (c == U')') { toks.push_back({TokKind::RParen, ")"}); ++i; continue; }
        if (c == U'-') { toks.push_back({TokKind::Dash,   "-"}); ++i; continue; }
        if (c == U'=') { toks.push_back({TokKind::Eq,     "="}); ++i; continue; }

        if (c == U'>') {
            if (i + 1 < n && input[i+1] == U'=') { toks.push_back({TokKind::Gte, ">="}); i += 2; }
            else                                    { toks.push_back({TokKind::Gt,  ">"});  ++i; }
            continue;
        }
        if (c == U'<') {
            if (i + 1 < n && input[i+1] == U'=') { toks.push_back({TokKind::Lte, "<="}); i += 2; }
            else                                    { toks.push_back({TokKind::Lt,  "<"});  ++i; }
            continue;
        }
        if (c == U'&' && i + 1 < n && input[i+1] == U'&') { toks.push_back({TokKind::And, "AND"}); i += 2; continue; }
        if (c == U'|' && i + 1 < n && input[i+1] == U'|') { toks.push_back({TokKind::Or,  "OR"});  i += 2; continue; }
        if (c == U'!' && (i + 1 >= n || input[i+1] != U'=')) { toks.push_back({TokKind::Not, "NOT"}); ++i; continue; }

        if (IsWordCp(c)) {
            std::u32string w;
            while (i < n && IsWordCp(input[i])) w += input[i++];
            std::u32string wl = ToLowerStr(w);
            if (wl == U"and" || wl == U"y") toks.push_back({TokKind::And, Utf8Encode(w)});
            else if (wl == U"or" || wl == U"o") toks.push_back({TokKind::Or, Utf8Encode(w)});
            else if (wl == U"not") toks.push_back({TokKind::Not, Utf8Encode(w)});
            else toks.push_back({TokKind::Word, Utf8Encode(w)});
            continue;
        }

        ++i; // skip unknown char
    }

    toks.push_back({TokKind::Eof, ""});
    return toks;
}

// ── Recursive-descent parser (same grammar as QueryParser.cpp) ────────────
//   expr    := or_expr
//   or_expr := and_expr (OR and_expr)*
//   and_expr:= not_expr (AND? not_expr)*   (AND implicit between adjacent terms)
//   not_expr:= NOT not_expr | atom
//   atom    := LPAREN expr RPAREN | filter | term
//   filter  := WORD COLON (GT|LT|GTE|LTE)? (WORD|QUOTED) (DASH (WORD|QUOTED))?
//   term    := WORD | QUOTED

struct Parser {
    std::vector<Token> toks;
    size_t pos = 0;

    Token& cur() { return toks[pos]; }
    Token consume() { return toks[pos++]; }
    bool at(TokKind k) { return cur().kind == k; }
    bool atValue() { return at(TokKind::Word) || at(TokKind::Quoted); }
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
            n->kind = NodeKind::Or;
            n->left = std::move(node);
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
            n->kind = NodeKind::And;
            n->left = std::move(node);
            n->right = std::move(rhs);
            node = std::move(n);
        }
        return node;
    }

    std::unique_ptr<QueryNode> parseNot() {
        if (at(TokKind::Not)) {
            consume();
            auto n = std::make_unique<QueryNode>();
            n->kind = NodeKind::Not;
            n->left = parseNot();
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

        if (at(TokKind::Word) && pos + 1 < toks.size() && toks[pos+1].kind == TokKind::Colon) {
            std::string field = NormalizeField(Utf8Encode(ToLowerStr(Utf8Decode(consume().val))));
            consume(); // colon

            auto n = std::make_unique<QueryNode>();
            n->kind = NodeKind::Filter;
            n->filter.field = field;

            if (at(TokKind::Gt))       { n->filter.op = ">";  consume(); }
            else if (at(TokKind::Lt))  { n->filter.op = "<";  consume(); }
            else if (at(TokKind::Gte)) { n->filter.op = ">="; consume(); }
            else if (at(TokKind::Lte)) { n->filter.op = "<="; consume(); }
            else if (at(TokKind::Eq))  { n->filter.op = "=";  consume(); }
            else                        { n->filter.op = ":"; }

            if (atValue()) {
                n->filter.value = consume().val;
                if (at(TokKind::Dash)) {
                    consume();
                    if (atValue()) {
                        n->filter.value2 = consume().val;
                        n->filter.op = "-";
                    }
                }
            }
            return n;
        }

        if (atValue()) {
            auto n = std::make_unique<QueryNode>();
            n->kind = NodeKind::Term;
            n->term = consume().val;
            return n;
        }

        auto n = std::make_unique<QueryNode>();
        n->kind = NodeKind::Term;
        return n;
    }
};

bool MatchFilter(const FilterNode& f, const SearchResult& r) {
    const std::string& field = f.field;

    if (field == "title")  return f.op == "=" ? EqualsI(r.title,  f.value) : ContainsI(r.title,  f.value);
    if (field == "artist") return f.op == "=" ? EqualsI(r.artist, f.value) : ContainsI(r.artist, f.value);
    if (field == "album")  return f.op == "=" ? EqualsI(r.album,  f.value) : ContainsI(r.album,  f.value);
    if (field == "genre")  return f.op == "=" ? EqualsI(r.genre,  f.value) : ContainsI(r.genre,  f.value);
    if (field == "label")  return f.op == "=" ? EqualsI(r.label,  f.value) : ContainsI(r.label,  f.value);
    if (field == "type")    return f.op == "=" ? EqualsI(r.type, NormalizeType(f.value))
                                                 : ContainsI(r.type, NormalizeType(f.value));
    // `country:` is primarily a *routing* term — SearchController reads it via
    // ExtractCountryHint to pick which account(s) to ask, and tags each row
    // with the account that served it. It then also filters here, which is
    // what makes `country:NZ` meaningful against a multi-country result set.
    // `country:all` is the routing keyword for "every account", so it must
    // pass everything through rather than look for a region literally named
    // "all" and hide every row.
    if (field == "country") {
        if (EqualsI(f.value, "all")) return true;
        return f.op == "=" ? EqualsI(r.country, f.value) : ContainsI(r.country, f.value);
    }

    if (field == "hires") {
        std::string v = NormalizeBool(f.value);
        bool want = (v == "true" || v == "1");
        return r.hires == want;
    }
    if (field == "explicit") {
        std::string v = NormalizeBool(f.value);
        bool want = (v == "true" || v == "1");
        return r.explicit_ == want;
    }

    int numField = (field == "year") ? r.year : (field == "duration") ? r.duration : -1;
    if (numField < 0) return true; // unknown field — pass

    if (f.op == "-") {
        int lo = 0, hi = 0;
        try { lo = std::stoi(f.value);  } catch (...) {}
        try { hi = std::stoi(f.value2); } catch (...) {}
        return numField >= lo && numField <= hi;
    }
    int cmp = 0;
    try { cmp = std::stoi(f.value); } catch (...) { return true; }
    if (f.op == ">")  return numField >  cmp;
    if (f.op == "<")  return numField <  cmp;
    if (f.op == ">=") return numField >= cmp;
    if (f.op == "<=") return numField <= cmp;
    return numField == cmp; // ":" for numeric — exact match
}

// Filter-derived values (artist:/title:/album:) go in `filterTerms`, bare
// keywords in `bareTerms` — kept separate so ExtractBaseTerm can prefer the
// former. An unquoted multi-word filter value like `artist:=Poppy Ajudha`
// parses (by grammar design — see this file's header) as
// `artist:=Poppy AND Ajudha`, i.e. a filter value "Poppy" plus a leftover
// bare term "Ajudha". Ranking every term by length alone, as before, let
// that trailing bare word outrank the actual filter value whenever it
// happened to be longer, sending the remote search off on a completely
// unrelated term.
void CollectTerms(const QueryNode& node, std::vector<std::string>& filterTerms,
                  std::vector<std::string>& bareTerms) {
    switch (node.kind) {
    case NodeKind::Term:
        if (!node.term.empty()) bareTerms.push_back(node.term);
        break;
    case NodeKind::Filter: {
        const auto& f = node.filter.field;
        if ((f == "artist" || f == "title" || f == "album") && !node.filter.value.empty())
            filterTerms.push_back(node.filter.value);
        break;
    }
    case NodeKind::And:
    case NodeKind::Or:
        if (node.left)  CollectTerms(*node.left,  filterTerms, bareTerms);
        if (node.right) CollectTerms(*node.right, filterTerms, bareTerms);
        break;
    case NodeKind::Not:
        break; // don't use negated terms as the search base
    }
}

void CollectTypeHints(const QueryNode& node, std::vector<std::string>& out) {
    if (node.kind == NodeKind::Filter && node.filter.field == "type") {
        out.push_back(NormalizeType(node.filter.value));
        return;
    }
    if (node.left)  CollectTypeHints(*node.left,  out);
    if (node.right) CollectTypeHints(*node.right, out);
}

void CollectCountryHints(const QueryNode& node, std::vector<std::string>& out) {
    if (node.kind == NodeKind::Filter && node.filter.field == "country") {
        out.push_back(node.filter.value);
        return;
    }
    if (node.left)  CollectCountryHints(*node.left,  out);
    if (node.right) CollectCountryHints(*node.right, out);
}

} // namespace

// ── Public API ──────────────────────────────────────────────────────────

QueryNode Parse(const std::string& input) {
    if (input.empty()) { QueryNode n; n.kind = NodeKind::Term; return n; }
    Parser p;
    p.toks = Tokenize(Utf8Decode(input));
    auto root = p.parseExpr();
    if (root) return std::move(*root);
    QueryNode n; n.kind = NodeKind::Term; return n;
}

bool Match(const QueryNode& node, const SearchResult& r) {
    switch (node.kind) {
    case NodeKind::Term:
        if (node.term.empty()) return true;
        return ContainsI(r.title, node.term) || ContainsI(r.artist, node.term);
    case NodeKind::Filter:
        return MatchFilter(node.filter, r);
    case NodeKind::And:
        return node.left && node.right && Match(*node.left, r) && Match(*node.right, r);
    case NodeKind::Or:
        return node.left && node.right && (Match(*node.left, r) || Match(*node.right, r));
    case NodeKind::Not:
        return node.left && !Match(*node.left, r);
    }
    return true;
}

std::string ExtractBaseTerm(const QueryNode& node) {
    std::vector<std::string> filterTerms, bareTerms;
    CollectTerms(node, filterTerms, bareTerms);
    // A field-scoped value is a much stronger signal than an incidental
    // bare word beside it — prefer it whenever one exists.
    std::vector<std::string>& terms = !filterTerms.empty() ? filterTerms : bareTerms;
    if (terms.empty()) return "";
    return *std::max_element(terms.begin(), terms.end(),
        [](const std::string& a, const std::string& b) { return a.size() < b.size(); });
}

std::string ExtractTypeHint(const QueryNode& node) {
    std::vector<std::string> hints;
    CollectTypeHints(node, hints);
    return hints.empty() ? "" : hints[0];
}

std::string ExtractCountryHint(const QueryNode& node) {
    std::vector<std::string> hints;
    CollectCountryHints(node, hints);
    return hints.empty() ? "" : hints[0];
}

int Score(const QueryNode& node, const SearchResult& r) {
    switch (node.kind) {
    case NodeKind::Term:
        return (ContainsI(r.title, node.term) || ContainsI(r.artist, node.term)) ? 1 : 0;
    case NodeKind::Filter:
        return MatchFilter(node.filter, r) ? 1 : 0;
    case NodeKind::And:
    case NodeKind::Or:
        return (node.left ? Score(*node.left, r) : 0) + (node.right ? Score(*node.right, r) : 0);
    case NodeKind::Not:
        return 0;
    }
    return 0;
}

} // namespace query_dsl
