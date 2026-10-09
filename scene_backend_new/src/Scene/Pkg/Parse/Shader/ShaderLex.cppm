module;

export module wescene.pkg.parse:shader_lex;
import rstd;

using namespace rstd::prelude;
using namespace rstd::literals;

export namespace owe::shader_lex
{

inline bool IsHSpace(char c) { return c == ' ' || c == '\t'; }
inline bool IsVSpace(char c) { return c == '\n' || c == '\r'; }
inline bool IsIdStart(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_';
}
inline bool IsIdCont(char c) { return IsIdStart(c) || (c >= '0' && c <= '9'); }
inline bool IsDigit(char c) { return c >= '0' && c <= '9'; }

inline bool IsPrecisionQualifier(ref<str> ident) noexcept {
    return ident == "lowp"_str || ident == "mediump"_str || ident == "highp"_str;
}

struct TypeName {
    ref<str> type;
    ref<str> name;
};

// Hand-rolled scanner over a string slice. Pos always points at the next byte
// to consume; Skip*/Match*/Read* primitives advance on success and stay put
// on failure so the caller can probe alternatives without explicit Save.
class Cursor {
public:
    explicit Cursor(ref<str> src) noexcept: m_src(src) {}
    Cursor(ref<str> src, usize pos) noexcept: m_src(src), m_pos(pos) {}

    usize    Pos() const noexcept { return m_pos; }
    ref<str> Source() const noexcept { return m_src; }
    bool     Eof() const noexcept { return m_pos >= m_src.size(); }
    char     Peek(usize off = {}) const noexcept {
        return (m_pos + off < m_src.size()) ? ByteAt(m_pos + off) : '\0';
    }
    void SeekTo(usize pos) noexcept { m_pos = pos > m_src.size() ? m_src.size() : pos; }
    void Advance(usize n = usize(1)) noexcept { SeekTo(m_pos + n); }

    bool AtLineStart() const noexcept {
        return m_pos == usize() || ByteAt(m_pos - usize(1)) == '\n';
    }
    usize LineStart() const noexcept {
        auto pos = m_pos;
        while (pos > usize() && ByteAt(pos - usize(1)) != '\n') --pos;
        return pos;
    }
    usize LineEnd() const noexcept {
        auto pos = m_pos;
        while (pos < m_src.size() && ByteAt(pos) != '\n') ++pos;
        return pos;
    }
    ref<str> CurrentLine() const noexcept { return *m_src.get(LineStart(), LineEnd()); }
    void     SkipLine() noexcept {
        usize e = LineEnd();
        SeekTo(e < m_src.size() ? e + usize(1) : e);
    }

    void SkipHSpace() noexcept {
        while (m_pos < m_src.size() && IsHSpace(ByteAt(m_pos))) ++m_pos;
    }
    void SkipToEol() noexcept {
        while (m_pos < m_src.size() && ByteAt(m_pos) != '\n') ++m_pos;
    }
    void SkipAllTrivia() noexcept {
        while (m_pos < m_src.size()) {
            char c = ByteAt(m_pos);
            if (IsHSpace(c) || IsVSpace(c)) {
                ++m_pos;
                continue;
            }
            if (c == '/' && m_pos + usize(1) < m_src.size()) {
                if (ByteAt(m_pos + usize(1)) == '/') {
                    SkipToEol();
                    continue;
                }
                if (ByteAt(m_pos + usize(1)) == '*') {
                    m_pos += usize(2);
                    while (m_pos + usize(1) < m_src.size() &&
                           ! (ByteAt(m_pos) == '*' && ByteAt(m_pos + usize(1)) == '/')) {
                        ++m_pos;
                    }
                    if (m_pos + usize(1) < m_src.size())
                        m_pos += usize(2);
                    else
                        m_pos = m_src.size();
                    continue;
                }
            }
            break;
        }
    }

    Option<ref<str>> ReadIdent() noexcept {
        if (m_pos >= m_src.size() || ! IsIdStart(ByteAt(m_pos))) return None();
        usize s = m_pos++;
        while (m_pos < m_src.size() && IsIdCont(ByteAt(m_pos))) ++m_pos;
        return m_src.get(s, m_pos);
    }
    Option<ref<str>> ReadInt() noexcept {
        if (m_pos >= m_src.size() || ! IsDigit(ByteAt(m_pos))) return None();
        usize s = m_pos++;
        while (m_pos < m_src.size() && IsDigit(ByteAt(m_pos))) ++m_pos;
        return m_src.get(s, m_pos);
    }
    // Match `[...]`. Returns the bracketed text including brackets. Content
    // between [] isn't validated as integer — the caller decides.
    Option<ref<str>> ReadArraySuffix() noexcept {
        if (m_pos >= m_src.size() || ByteAt(m_pos) != '[') return None();
        usize s = m_pos;
        usize p = m_pos + usize(1);
        while (p < m_src.size() && ByteAt(p) != ']' && ByteAt(p) != '\n') ++p;
        if (p >= m_src.size() || ByteAt(p) != ']') return None();
        ++p;
        m_pos = p;
        return m_src.get(s, p);
    }

    bool MatchChar(char c) noexcept {
        if (m_pos < m_src.size() && ByteAt(m_pos) == c) {
            ++m_pos;
            return true;
        }
        return false;
    }
    bool MatchPunct(ref<str> s) noexcept {
        if (m_pos + s.size() > m_src.size()) return false;
        if (*m_src.get(m_pos, m_pos + s.size()) != s) return false;
        m_pos += s.size();
        return true;
    }
    // Match a keyword at an identifier boundary, so "uniform" does not match "uniformly".
    bool MatchKeyword(ref<str> kw) noexcept {
        if (m_pos + kw.size() > m_src.size()) return false;
        if (*m_src.get(m_pos, m_pos + kw.size()) != kw) return false;
        if (m_pos + kw.size() < m_src.size() && IsIdCont(ByteAt(m_pos + kw.size()))) return false;
        m_pos += kw.size();
        return true;
    }
    // Match `#`, optional horizontal space and a directive name at an identifier boundary.
    bool MatchHashDirective(ref<str> name) noexcept {
        usize save = m_pos;
        SkipHSpace();
        if (m_pos >= m_src.size() || ByteAt(m_pos) != '#') {
            m_pos = save;
            return false;
        }
        ++m_pos;
        SkipHSpace();
        if (! MatchKeyword(name)) {
            m_pos = save;
            return false;
        }
        return true;
    }

    struct Saved {
        usize pos;
    };
    Saved Save() const noexcept { return { m_pos }; }
    void  Restore(Saved s) noexcept { m_pos = s.pos; }

private:
    char ByteAt(usize pos) const noexcept { return static_cast<char>(m_src[pos].to_primitive()); }

    ref<str> m_src;
    usize    m_pos {};
};

inline Option<TypeName> ReadTypeName(Cursor& c) noexcept {
    auto type = c.ReadIdent();
    if (! type) return None();
    c.SkipHSpace();
    if (IsPrecisionQualifier(*type)) {
        type = c.ReadIdent();
        if (! type) return None();
        c.SkipHSpace();
    }
    auto name = c.ReadIdent();
    if (! name) return None();
    return Some(TypeName { *type, *name });
}

// Walks one source line at a time. Tracks block-comment state so that a
// `/* ... \n ... */` spanning multiple physical lines turns every line it
// covers into the empty view — the WE annotation collector relies on this
// so `uniform vec4 X;` inside a block comment never gets emitted.
class LineWalker {
public:
    explicit LineWalker(ref<str> src) noexcept: m_src(src) { Recompute(); }
    bool     Done() const noexcept { return m_pos > m_src.size(); }
    usize    LineStart() const noexcept { return m_line_start; }
    usize    LineEnd() const noexcept { return m_line_end; }
    ref<str> Line() const noexcept {
        // When the entire line is masked by an enclosing block comment, hide
        // it from callers so token scans never see the masked text.
        if (m_line_masked) return {};
        return *m_src.get(m_line_start, m_line_end);
    }
    // Raw line text including any masked content. Stripper passes use this
    // so the original bytes (including block-comment text) survive into the
    // output unchanged.
    ref<str> RawLine() const noexcept { return *m_src.get(m_line_start, m_line_end); }
    Cursor   LineCursor() const noexcept {
        // The returned Cursor scans only the visible part of the line.
        return Cursor { Line() };
    }
    void Step() noexcept {
        m_pos = (m_line_end < m_src.size()) ? m_line_end + usize(1) : m_src.size() + usize(1);
        Recompute();
    }

private:
    void Recompute() noexcept {
        if (m_pos > m_src.size()) {
            m_line_start = m_line_end = m_src.size();
            m_line_masked             = false;
            return;
        }
        m_line_start = m_pos;
        m_line_end   = m_pos;
        while (m_line_end < m_src.size() && ByteAt(m_line_end) != '\n') ++m_line_end;

        // Walk the line characterwise to update block-comment state.
        m_line_masked         = m_in_block;
        usize p               = m_line_start;
        bool  starts_in_block = m_in_block;
        while (p < m_line_end) {
            if (m_in_block) {
                if (p + usize(1) < m_line_end && ByteAt(p) == '*' && ByteAt(p + usize(1)) == '/') {
                    m_in_block = false;
                    p += usize(2);
                    // If a `*/` closes mid-line, content after it is visible —
                    // unmask the line.
                    if (starts_in_block) m_line_masked = false;
                } else {
                    ++p;
                }
            } else {
                if (p + usize(1) < m_line_end && ByteAt(p) == '/' && ByteAt(p + usize(1)) == '/') {
                    // Line comment terminates the line for block-state purposes.
                    p = m_line_end;
                    break;
                }
                if (p + usize(1) < m_line_end && ByteAt(p) == '/' && ByteAt(p + usize(1)) == '*') {
                    m_in_block = true;
                    p += usize(2);
                } else {
                    ++p;
                }
            }
        }
        // Note: if `m_in_block` is true on EOL the rest of the line is in a
        // block comment. We still expose the visible prefix via Line() — only
        // lines that started inside a block comment are masked.
    }

    char ByteAt(usize pos) const noexcept { return static_cast<char>(m_src[pos].to_primitive()); }

    ref<str> m_src;
    usize    m_pos {};
    usize    m_line_start {};
    usize    m_line_end {};
    bool     m_in_block { false };
    bool     m_line_masked { false };
};

// Token stream layered over Cursor. Tokens preserve the underlying bytes
// (text + offset) so callers can splice / re-emit the source unchanged. The
// lexer is line-aware: Newline / HSpace / LineComment / BlockComment are
// each their own kind so directive parsing stays straightforward.
enum class TokenKind : rstd::uint8_t
{
    Eof,
    Newline,      // single '\n'
    HSpace,       // run of ' ' / '\t'
    LineComment,  // includes the leading '//' and runs up to (not including) '\n'
    BlockComment, // includes leading '/*' and trailing '*/'
    Ident,        // [A-Za-z_][A-Za-z0-9_]*
    Int,          // [0-9]+
    String,       // "..." including quotes; consumed up to '\n' / EOF if unterminated
    Hash,         // single '#'
    Punct,        // any single-byte ASCII punctuation not covered above
    Unknown,      // any other byte (non-ASCII, control chars)
};

struct Token {
    TokenKind kind;
    ref<str>  text;
    usize     offset;
};

class Lexer {
public:
    explicit Lexer(ref<str> src) noexcept: m_src(src) {}

    ref<str> Source() const noexcept { return m_src; }
    usize    Pos() const noexcept { return m_pos; }
    void     SeekTo(usize p) noexcept { m_pos = p > m_src.size() ? m_src.size() : p; }
    bool     Eof() const noexcept { return m_pos >= m_src.size(); }

    Token Peek() const noexcept {
        usize save                      = m_pos;
        Token t                         = const_cast<Lexer*>(this)->ScanOne();
        const_cast<Lexer*>(this)->m_pos = save;
        return t;
    }
    Token Next() noexcept { return ScanOne(); }

    // Convenience: skip tokens whose kind matches the predicate, then return
    // the next "interesting" token. Used by directive parsing to skip HSpace.
    template<typename Pred>
    Token NextSkip(Pred pred) noexcept {
        for (;;) {
            Token t = ScanOne();
            if (! pred(t.kind)) return t;
            if (t.kind == TokenKind::Eof) return t;
        }
    }

    struct Saved {
        usize pos;
    };
    Saved Save() const noexcept { return { m_pos }; }
    void  Restore(Saved s) noexcept { m_pos = s.pos; }

private:
    Token ScanOne() noexcept {
        if (m_pos >= m_src.size()) return { TokenKind::Eof, {}, m_src.size() };
        usize start = m_pos;
        char  c0    = ByteAt(m_pos);

        if (c0 == '\n') {
            ++m_pos;
            return { TokenKind::Newline, *m_src.get(start, m_pos), start };
        }
        if (IsHSpace(c0)) {
            while (m_pos < m_src.size() && IsHSpace(ByteAt(m_pos))) ++m_pos;
            return { TokenKind::HSpace, *m_src.get(start, m_pos), start };
        }
        if (c0 == '/' && m_pos + usize(1) < m_src.size() && ByteAt(m_pos + usize(1)) == '/') {
            m_pos += usize(2);
            while (m_pos < m_src.size() && ByteAt(m_pos) != '\n') ++m_pos;
            return { TokenKind::LineComment, *m_src.get(start, m_pos), start };
        }
        if (c0 == '/' && m_pos + usize(1) < m_src.size() && ByteAt(m_pos + usize(1)) == '*') {
            m_pos += usize(2);
            while (m_pos + usize(1) < m_src.size() &&
                   ! (ByteAt(m_pos) == '*' && ByteAt(m_pos + usize(1)) == '/'))
                ++m_pos;
            if (m_pos + usize(1) < m_src.size())
                m_pos += usize(2);
            else
                m_pos = m_src.size();
            return { TokenKind::BlockComment, *m_src.get(start, m_pos), start };
        }
        if (IsIdStart(c0)) {
            ++m_pos;
            while (m_pos < m_src.size() && IsIdCont(ByteAt(m_pos))) ++m_pos;
            return { TokenKind::Ident, *m_src.get(start, m_pos), start };
        }
        if (IsDigit(c0)) {
            ++m_pos;
            while (m_pos < m_src.size() && IsDigit(ByteAt(m_pos))) ++m_pos;
            return { TokenKind::Int, *m_src.get(start, m_pos), start };
        }
        if (c0 == '"') {
            ++m_pos;
            while (m_pos < m_src.size() && ByteAt(m_pos) != '"' && ByteAt(m_pos) != '\n') ++m_pos;
            if (m_pos < m_src.size() && ByteAt(m_pos) == '"') ++m_pos;
            return { TokenKind::String, *m_src.get(start, m_pos), start };
        }
        if (c0 == '#') {
            ++m_pos;
            return { TokenKind::Hash, *m_src.get(start, m_pos), start };
        }
        if ((unsigned char)c0 >= 0x20 && (unsigned char)c0 < 0x7f) {
            ++m_pos;
            return { TokenKind::Punct, *m_src.get(start, m_pos), start };
        }
        auto [code_point, width] =
            rstd::char_::decode_utf8(m_src.data() + m_pos.to_primitive(), m_src.size() - m_pos);
        (void)code_point;
        m_pos += width;
        return { TokenKind::Unknown, *m_src.get(start, m_pos), start };
    }

    char ByteAt(usize pos) const noexcept { return static_cast<char>(m_src[pos].to_primitive()); }

    ref<str> m_src;
    usize    m_pos {};
};

enum class PpKind
{
    None,
    If,
    Ifdef,
    Ifndef,
    Elif,
    Else,
    Endif,
    Define,
    Undef,
    Include,
    Require,
    Pragma,
    Extension,
    Version,
    Other,
};

inline PpKind ClassifyPreproc(Cursor c) noexcept {
    Lexer lx(*c.Source().get(c.Pos(), c.Source().size()));
    // First non-HSpace token must be Hash.
    Token t = lx.NextSkip([](TokenKind k) {
        return k == TokenKind::HSpace;
    });
    if (t.kind != TokenKind::Hash) return PpKind::None;
    // Directive name after optional HSpace.
    t = lx.NextSkip([](TokenKind k) {
        return k == TokenKind::HSpace;
    });
    if (t.kind != TokenKind::Ident) return PpKind::Other;
    auto id = t.text;
    if (id == "if"_str) return PpKind::If;
    if (id == "ifdef"_str) return PpKind::Ifdef;
    if (id == "ifndef"_str) return PpKind::Ifndef;
    if (id == "elif"_str) return PpKind::Elif;
    if (id == "else"_str) return PpKind::Else;
    if (id == "endif"_str) return PpKind::Endif;
    if (id == "define"_str) return PpKind::Define;
    if (id == "undef"_str) return PpKind::Undef;
    if (id == "include"_str) return PpKind::Include;
    if (id == "require"_str) return PpKind::Require;
    if (id == "pragma"_str) return PpKind::Pragma;
    if (id == "extension"_str) return PpKind::Extension;
    if (id == "version"_str) return PpKind::Version;
    return PpKind::Other;
}

} // namespace owe::shader_lex
