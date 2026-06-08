// lexer.cpp — Lexer implementation for the qc compiler (C17 / C++17)
#include "qc/lexer.h"

#include <cctype>
#include <cassert>
#include <cstdlib>
#include <cstring>
#include <string>

namespace qc {

// ===========================================================================
// Helpers
// ===========================================================================

static bool isIdentStart(char c) {
    return std::isalpha(static_cast<unsigned char>(c)) || c == '_';
}

static bool isIdentCont(char c) {
    return std::isalnum(static_cast<unsigned char>(c)) || c == '_';
}

static bool isOctalDigit(char c) {
    return c >= '0' && c <= '7';
}

static bool isHexDigit(char c) {
    return (c >= '0' && c <= '9') ||
           (c >= 'a' && c <= 'f') ||
           (c >= 'A' && c <= 'F');
}

static int hexVal(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return 10 + (c - 'a');
    if (c >= 'A' && c <= 'F') return 10 + (c - 'A');
    return 0;
}

// ===========================================================================
// Constructor
// ===========================================================================

Lexer::Lexer(const SourceFile& src, DiagEngine& diag, bool cxxMode)
    : src_(src), diag_(diag), cxxMode_(cxxMode),
      pos_(src.data()), end_(src.data() + src.size()),
      line_(1), col_(1)
{}

// ===========================================================================
// Public interface — next / peek / putBack
// ===========================================================================

Token Lexer::next() {
    if (putBack_.has_value()) {
        Token t = std::move(*putBack_);
        putBack_.reset();
        return t;
    }
    return lexToken();
}

Token Lexer::peek() {
    if (!putBack_.has_value())
        putBack_ = lexToken();
    return *putBack_;
}

void Lexer::putBack(Token tok) {
    // Only one token of look-ahead is supported.
    assert(!putBack_.has_value() && "putBack called with token already buffered");
    putBack_ = std::move(tok);
}

// ===========================================================================
// errorToken
// ===========================================================================

Token Lexer::errorToken(SourceLocation loc, std::string msg) {
    diag_.error(loc, std::move(msg));
    Token t;
    t.kind = TokenKind::Error;
    t.loc  = loc;
    return t;
}

// ===========================================================================
// Whitespace / comment skipping
// ===========================================================================

void Lexer::skipWhitespace() {
    while (!atEnd()) {
        char c = cur();
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n') {
            advance();
        } else {
            break;
        }
    }
}

void Lexer::skipLineComment() {
    // We are positioned right after the '//' sequence.
    while (!atEnd() && cur() != '\n')
        advance();
    // Do not consume the newline — the next skipWhitespace will do it.
}

void Lexer::skipBlockComment() {
    // We are positioned right after '/*'.
    SourceLocation start = curLoc();
    while (!atEnd()) {
        if (cur() == '*' && peek1() == '/') {
            advance(); // '*'
            advance(); // '/'
            return;
        }
        advance();
    }
    diag_.error(start, "unterminated block comment");
}

// ===========================================================================
// Main dispatch
// ===========================================================================

Token Lexer::lexToken() {
    for (;;) {
        skipWhitespace();

        if (atEnd()) {
            SourceLocation loc = curLoc();
            return makeToken(TokenKind::Eof, loc, "");
        }

        // Skip comments and retry.
        if (cur() == '/' && peek1() == '/') {
            advance(); advance();
            skipLineComment();
            continue;
        }
        if (cur() == '/' && peek1() == '*') {
            advance(); advance();
            skipBlockComment();
            continue;
        }

        break;
    }

    char c = cur();

    // Wide string / char literals: L"..." or L'...'
    if (c == 'L') {
        if (peek1() == '"') {
            advance(); // consume 'L'
            return lexStringLiteral(/*wide=*/true);
        }
        if (peek1() == '\'') {
            advance(); // consume 'L'
            return lexCharLiteral(/*wide=*/true);
        }
    }

    if (isIdentStart(c))
        return lexIdentOrKeyword();

    if (std::isdigit(static_cast<unsigned char>(c)) ||
        (c == '.' && std::isdigit(static_cast<unsigned char>(peek1()))))
        return lexNumericLiteral();

    if (c == '"')
        return lexStringLiteral();

    if (c == '\'')
        return lexCharLiteral();

    return lexOperatorOrPunct();
}

// ===========================================================================
// Identifiers and keywords
// ===========================================================================

Token Lexer::lexIdentOrKeyword() {
    SourceLocation start = curLoc();
    const char* begin = pos_;

    while (!atEnd() && isIdentCont(cur()))
        advance();

    std::string text(begin, pos_);
    TokenKind kind = identToKeyword(text, cxxMode_);
    return makeToken(kind, start, std::move(text));
}

// ---------------------------------------------------------------------------
// identToKeyword — complete keyword table
// ---------------------------------------------------------------------------

TokenKind Lexer::identToKeyword(std::string_view id, bool cxx) {
    // C keywords (always recognised in both modes)
    if (id == "auto")           return TokenKind::Kw_auto;
    if (id == "break")          return TokenKind::Kw_break;
    if (id == "case")           return TokenKind::Kw_case;
    if (id == "char")           return TokenKind::Kw_char;
    if (id == "const")          return TokenKind::Kw_const;
    if (id == "continue")       return TokenKind::Kw_continue;
    if (id == "default")        return TokenKind::Kw_default;
    if (id == "do")             return TokenKind::Kw_do;
    if (id == "double")         return TokenKind::Kw_double;
    if (id == "else")           return TokenKind::Kw_else;
    if (id == "enum")           return TokenKind::Kw_enum;
    if (id == "extern")         return TokenKind::Kw_extern;
    if (id == "float")          return TokenKind::Kw_float;
    if (id == "for")            return TokenKind::Kw_for;
    if (id == "goto")           return TokenKind::Kw_goto;
    if (id == "if")             return TokenKind::Kw_if;
    if (id == "inline")         return TokenKind::Kw_inline;
    if (id == "int")            return TokenKind::Kw_int;
    if (id == "long")           return TokenKind::Kw_long;
    if (id == "register")       return TokenKind::Kw_register;
    if (id == "restrict")       return TokenKind::Kw_restrict;
    if (id == "return")         return TokenKind::Kw_return;
    if (id == "short")          return TokenKind::Kw_short;
    if (id == "signed")         return TokenKind::Kw_signed;
    if (id == "sizeof")         return TokenKind::Kw_sizeof;
    if (id == "static")         return TokenKind::Kw_static;
    if (id == "struct")         return TokenKind::Kw_struct;
    if (id == "switch")         return TokenKind::Kw_switch;
    if (id == "typedef")        return TokenKind::Kw_typedef;
    if (id == "union")          return TokenKind::Kw_union;
    if (id == "unsigned")       return TokenKind::Kw_unsigned;
    if (id == "void")           return TokenKind::Kw_void;
    if (id == "volatile")       return TokenKind::Kw_volatile;
    if (id == "while")          return TokenKind::Kw_while;
    if (id == "_Bool")          return TokenKind::Kw__Bool;
    if (id == "_Complex")       return TokenKind::Kw__Complex;
    if (id == "_Alignas")       return TokenKind::Kw__Alignas;
    if (id == "_Alignof")       return TokenKind::Kw__Alignof;
    if (id == "_Atomic")        return TokenKind::Kw__Atomic;
    if (id == "_Static_assert") return TokenKind::Kw__Static_assert;
    if (id == "_Noreturn")      return TokenKind::Kw__Noreturn;
    if (id == "_Generic")       return TokenKind::Kw__Generic;

    // C++ keywords (only recognised when cxx is true)
    if (cxx) {
        if (id == "alignas")          return TokenKind::Kw_alignas;
        if (id == "alignof")          return TokenKind::Kw_alignof;
        if (id == "and")              return TokenKind::Kw_and;
        if (id == "and_eq")           return TokenKind::Kw_and_eq;
        if (id == "asm")              return TokenKind::Kw_asm;
        if (id == "bitand")           return TokenKind::Kw_bitand;
        if (id == "bitor")            return TokenKind::Kw_bitor;
        if (id == "bool")             return TokenKind::Kw_bool;
        if (id == "catch")            return TokenKind::Kw_catch;
        if (id == "class")            return TokenKind::Kw_class;
        if (id == "compl")            return TokenKind::Kw_compl;
        if (id == "concept")          return TokenKind::Kw_concept;
        if (id == "constexpr")        return TokenKind::Kw_constexpr;
        if (id == "consteval")        return TokenKind::Kw_consteval;
        if (id == "constinit")        return TokenKind::Kw_constinit;
        if (id == "co_await")         return TokenKind::Kw_co_await;
        if (id == "co_return")        return TokenKind::Kw_co_return;
        if (id == "co_yield")         return TokenKind::Kw_co_yield;
        if (id == "decltype")         return TokenKind::Kw_decltype;
        if (id == "delete")           return TokenKind::Kw_delete;
        if (id == "dynamic_cast")     return TokenKind::Kw_dynamic_cast;
        if (id == "explicit")         return TokenKind::Kw_explicit;
        if (id == "export")           return TokenKind::Kw_export;
        if (id == "false")            return TokenKind::Kw_false;
        if (id == "final")            return TokenKind::Kw_final;
        if (id == "friend")           return TokenKind::Kw_friend;
        if (id == "mutable")          return TokenKind::Kw_mutable;
        if (id == "namespace")        return TokenKind::Kw_namespace;
        if (id == "new")              return TokenKind::Kw_new;
        if (id == "noexcept")         return TokenKind::Kw_noexcept;
        if (id == "not")              return TokenKind::Kw_not;
        if (id == "not_eq")           return TokenKind::Kw_not_eq;
        if (id == "nullptr")          return TokenKind::Kw_nullptr;
        if (id == "operator")         return TokenKind::Kw_operator;
        if (id == "or")               return TokenKind::Kw_or;
        if (id == "or_eq")            return TokenKind::Kw_or_eq;
        if (id == "override")         return TokenKind::Kw_override;
        if (id == "private")          return TokenKind::Kw_private;
        if (id == "protected")        return TokenKind::Kw_protected;
        if (id == "public")           return TokenKind::Kw_public;
        if (id == "reinterpret_cast") return TokenKind::Kw_reinterpret_cast;
        if (id == "requires")         return TokenKind::Kw_requires;
        if (id == "static_assert")    return TokenKind::Kw_static_assert;
        if (id == "static_cast")      return TokenKind::Kw_static_cast;
        if (id == "template")         return TokenKind::Kw_template;
        if (id == "this")             return TokenKind::Kw_this;
        if (id == "throw")            return TokenKind::Kw_throw;
        if (id == "true")             return TokenKind::Kw_true;
        if (id == "try")              return TokenKind::Kw_try;
        if (id == "typename")         return TokenKind::Kw_typename;
        if (id == "using")            return TokenKind::Kw_using;
        if (id == "virtual")          return TokenKind::Kw_virtual;
        if (id == "xor")              return TokenKind::Kw_xor;
        if (id == "xor_eq")           return TokenKind::Kw_xor_eq;
    }

    return TokenKind::Identifier;
}

// ===========================================================================
// Numeric literals
// ===========================================================================

Token Lexer::lexNumericLiteral() {
    SourceLocation start = curLoc();
    const char* begin = pos_;

    bool isFloat   = false;
    bool isHex     = false;
    bool isBinary  = false;
    bool isOctal   = false;

    u64    intVal   = 0;
    double floatVal = 0.0;

    // Determine base from prefix
    if (cur() == '0') {
        char next = peek1();
        if (next == 'x' || next == 'X') {
            // Hexadecimal
            isHex = true;
            advance(); // '0'
            advance(); // 'x'
            if (!isHexDigit(cur())) {
                return errorToken(start, "invalid hexadecimal literal");
            }
            while (!atEnd() && (isHexDigit(cur()) || cur() == '\''))
                advance();
            // hex float?
            if (!atEnd() && cur() == '.') {
                isFloat = true;
                advance();
                while (!atEnd() && (isHexDigit(cur()) || cur() == '\''))
                    advance();
            }
            if (!atEnd() && (cur() == 'p' || cur() == 'P')) {
                isFloat = true;
                advance();
                if (!atEnd() && (cur() == '+' || cur() == '-'))
                    advance();
                while (!atEnd() && std::isdigit(static_cast<unsigned char>(cur())))
                    advance();
            }
        } else if (next == 'b' || next == 'B') {
            // Binary
            isBinary = true;
            advance(); // '0'
            advance(); // 'b'
            if (cur() != '0' && cur() != '1') {
                return errorToken(start, "invalid binary literal");
            }
            while (!atEnd() && (cur() == '0' || cur() == '1' || cur() == '\''))
                advance();
        } else if (isOctalDigit(next) || next == '\'') {
            // Octal
            isOctal = true;
            advance(); // '0'
            while (!atEnd() && (isOctalDigit(cur()) || cur() == '\''))
                advance();
        } else {
            // Just '0'
            advance();
        }

        if (!isHex && !isBinary && !isOctal) {
            // Check for fractional part or exponent even if it started with '0'
            if (!atEnd() && cur() == '.') {
                isFloat = true;
                advance();
                while (!atEnd() && (std::isdigit(static_cast<unsigned char>(cur())) || cur() == '\''))
                    advance();
            }
            if (!atEnd() && (cur() == 'e' || cur() == 'E')) {
                isFloat = true;
                advance();
                if (!atEnd() && (cur() == '+' || cur() == '-'))
                    advance();
                while (!atEnd() && std::isdigit(static_cast<unsigned char>(cur())))
                    advance();
            }
        }
    } else if (cur() != '.') {
        // Decimal integer part
        while (!atEnd() && (std::isdigit(static_cast<unsigned char>(cur())) || cur() == '\''))
            advance();

        // Fractional part?
        if (!atEnd() && cur() == '.') {
            isFloat = true;
            advance();
            while (!atEnd() && (std::isdigit(static_cast<unsigned char>(cur())) || cur() == '\''))
                advance();
        }

        // Exponent?
        if (!atEnd() && (cur() == 'e' || cur() == 'E')) {
            isFloat = true;
            advance();
            if (!atEnd() && (cur() == '+' || cur() == '-'))
                advance();
            while (!atEnd() && std::isdigit(static_cast<unsigned char>(cur())))
                advance();
        }
    } else {
        // Starts with '.digit'
        isFloat = true;
        advance(); // '.'
        while (!atEnd() && std::isdigit(static_cast<unsigned char>(cur())))
            advance();
        if (!atEnd() && (cur() == 'e' || cur() == 'E')) {
            advance();
            if (!atEnd() && (cur() == '+' || cur() == '-'))
                advance();
            while (!atEnd() && std::isdigit(static_cast<unsigned char>(cur())))
                advance();
        }
    }

    // For non-prefix decimal: check for fractional / exponent if not yet set
    // (handled above)

    // Collect suffix
    bool suffixUnsigned = false;
    bool suffixLong     = false;
    bool suffixLongLong = false;
    bool suffixFloat    = false;

    // Float suffix: f / F / l / L
    if (isFloat) {
        if (!atEnd() && (cur() == 'f' || cur() == 'F')) {
            suffixFloat = true;
            advance();
        } else if (!atEnd() && (cur() == 'l' || cur() == 'L')) {
            suffixLong = true;
            advance();
        }
    } else {
        // Integer suffix: u/U, l/L, ll/LL, ul/UL, ull/ULL (any order)
        for (int i = 0; i < 3 && !atEnd(); ++i) {
            char s = cur();
            if (s == 'u' || s == 'U') {
                if (suffixUnsigned) break;
                suffixUnsigned = true;
                advance();
            } else if (s == 'l' || s == 'L') {
                if (suffixLongLong) break;
                if (suffixLong) {
                    suffixLong     = false;
                    suffixLongLong = true;
                } else {
                    // Check for ll/LL
                    if (peek1() == s) {
                        suffixLongLong = true;
                        advance(); advance();
                    } else {
                        suffixLong = true;
                        advance();
                    }
                }
            } else {
                break;
            }
        }
    }

    // Build the raw text (stripped of digit-separators for parsing)
    std::string raw(begin, pos_);

    // Parse the numeric value
    if (isFloat) {
        // Build a clean string without digit separators for strtod
        std::string clean;
        clean.reserve(raw.size());
        for (char c : raw) {
            if (c != '\'') clean += c;
        }
        // Remove any suffix characters before conversion
        while (!clean.empty()) {
            char last = clean.back();
            if (last == 'f' || last == 'F' || last == 'l' || last == 'L')
                clean.pop_back();
            else
                break;
        }
        floatVal = std::strtod(clean.c_str(), nullptr);

        Token t = makeToken(TokenKind::FloatLit, start, raw);
        t.value.floatVal = floatVal;
        t.isLong         = suffixLong;
        return t;
    } else {
        // Build clean integer string
        std::string clean;
        clean.reserve(raw.size());
        // Skip any suffix (not digits/letters of the number itself)
        // We'll strip trailing suffix chars (u,l) later; for now build all digits
        for (char c : raw) {
            if (c != '\'') clean += c;
        }
        // Remove integer suffixes from clean
        while (!clean.empty()) {
            char last = clean.back();
            if (last == 'u' || last == 'U' || last == 'l' || last == 'L')
                clean.pop_back();
            else
                break;
        }

        char* endptr = nullptr;
        if (isHex) {
            intVal = std::strtoull(clean.c_str(), &endptr, 16);
        } else if (isBinary) {
            // strtoull with base 2 from the digits part after 0b
            const char* digits = clean.c_str() + 2; // skip '0b'
            intVal = std::strtoull(digits, &endptr, 2);
        } else if (isOctal) {
            intVal = std::strtoull(clean.c_str(), &endptr, 8);
        } else {
            intVal = std::strtoull(clean.c_str(), &endptr, 10);
        }

        Token t = makeToken(TokenKind::IntLit, start, raw);
        t.value.intVal  = intVal;
        t.isUnsigned    = suffixUnsigned;
        t.isLong        = suffixLong;
        t.isLongLong    = suffixLongLong;
        return t;
    }
}

// ===========================================================================
// String literals
// ===========================================================================

// Helper: read one escape sequence after the backslash has been consumed.
// Uses the Lexer's advance() so line_/col_ stay correct.
// Appends raw source characters to `raw`; returns the decoded byte value.
// Declared as a lambda inside each caller to avoid adding to the header.

Token Lexer::lexStringLiteral(bool wide) {
    SourceLocation start = curLoc();
    assert(cur() == '"');
    advance(); // opening '"'

    std::string raw = wide ? "L\"" : "\"";

    // Lambda: consumes one escape sequence using this Lexer's advance().
    // The backslash itself must already be consumed before calling.
    auto readEscape = [&]() -> char {
        if (atEnd()) {
            diag_.error(curLoc(), "unexpected end of file in escape sequence");
            return '\0';
        }
        char esc = cur();
        raw += esc;
        advance();
        switch (esc) {
        case 'n':  return '\n';
        case 't':  return '\t';
        case 'r':  return '\r';
        case '\\': return '\\';
        case '\'': return '\'';
        case '"':  return '"';
        case '0':  return '\0';
        case 'a':  return '\a';
        case 'b':  return '\b';
        case 'f':  return '\f';
        case 'v':  return '\v';
        case 'x': {
            if (atEnd() || !isHexDigit(cur())) {
                diag_.error(curLoc(), "invalid hex escape sequence");
                return '\0';
            }
            unsigned val = 0;
            while (!atEnd() && isHexDigit(cur())) {
                raw += cur();
                val = val * 16 + static_cast<unsigned>(hexVal(cur()));
                advance();
            }
            return static_cast<char>(val & 0xFF);
        }
        default:
            if (isOctalDigit(esc)) {
                unsigned val = static_cast<unsigned>(esc - '0');
                for (int i = 0; i < 2 && !atEnd() && isOctalDigit(cur()); ++i) {
                    raw += cur();
                    val = val * 8 + static_cast<unsigned>(cur() - '0');
                    advance();
                }
                return static_cast<char>(val & 0xFF);
            }
            diag_.error(curLoc(), std::string("unknown escape sequence '\\") + esc + "'");
            return esc;
        }
    };

    while (!atEnd() && cur() != '"') {
        if (cur() == '\n') {
            return errorToken(start, "unterminated string literal");
        }
        if (cur() == '\\') {
            raw += '\\';
            advance(); // consume backslash
            readEscape();
        } else {
            raw += cur();
            advance();
        }
    }

    if (atEnd()) {
        return errorToken(start, "unterminated string literal");
    }
    raw += '"';
    advance(); // closing '"'

    return makeToken(wide ? TokenKind::WStringLit : TokenKind::StringLit, start, raw);
}

// ===========================================================================
// Character literals
// ===========================================================================

Token Lexer::lexCharLiteral(bool wide) {
    SourceLocation start = curLoc();
    assert(cur() == '\'');
    advance(); // opening '\''

    std::string raw = wide ? "L'" : "'";

    if (atEnd() || cur() == '\'') {
        return errorToken(start, "empty character literal");
    }

    // Same escape lambda as in lexStringLiteral (captures this, raw, diag_ etc.)
    auto readEscape = [&]() -> char {
        if (atEnd()) {
            diag_.error(curLoc(), "unexpected end of file in escape sequence");
            return '\0';
        }
        char esc = cur();
        raw += esc;
        advance();
        switch (esc) {
        case 'n':  return '\n';
        case 't':  return '\t';
        case 'r':  return '\r';
        case '\\': return '\\';
        case '\'': return '\'';
        case '"':  return '"';
        case '0':  return '\0';
        case 'a':  return '\a';
        case 'b':  return '\b';
        case 'f':  return '\f';
        case 'v':  return '\v';
        case 'x': {
            if (atEnd() || !isHexDigit(cur())) {
                diag_.error(curLoc(), "invalid hex escape sequence");
                return '\0';
            }
            unsigned val = 0;
            while (!atEnd() && isHexDigit(cur())) {
                raw += cur();
                val = val * 16 + static_cast<unsigned>(hexVal(cur()));
                advance();
            }
            return static_cast<char>(val & 0xFF);
        }
        default:
            if (isOctalDigit(esc)) {
                unsigned val = static_cast<unsigned>(esc - '0');
                for (int i = 0; i < 2 && !atEnd() && isOctalDigit(cur()); ++i) {
                    raw += cur();
                    val = val * 8 + static_cast<unsigned>(cur() - '0');
                    advance();
                }
                return static_cast<char>(val & 0xFF);
            }
            diag_.error(curLoc(), std::string("unknown escape sequence '\\") + esc + "'");
            return esc;
        }
    };

    char charVal = '\0';
    if (cur() == '\\') {
        raw += '\\';
        advance(); // consume backslash
        charVal = readEscape();
    } else {
        charVal = cur();
        raw += cur();
        advance();
    }

    if (atEnd() || cur() != '\'') {
        return errorToken(start, "unterminated or multi-character literal");
    }
    raw += '\'';
    advance(); // closing '\''

    Token t = makeToken(wide ? TokenKind::WCharLit : TokenKind::CharLit, start, raw);
    t.value.intVal = static_cast<u64>(static_cast<unsigned char>(charVal));
    return t;
}

// ===========================================================================
// Operators and punctuation
// ===========================================================================

Token Lexer::lexOperatorOrPunct() {
    SourceLocation start = curLoc();
    char c = cur();
    advance();

    switch (c) {
    case '(': return makeToken(TokenKind::LParen,    start, "(");
    case ')': return makeToken(TokenKind::RParen,    start, ")");
    case '{': return makeToken(TokenKind::LBrace,    start, "{");
    case '}': return makeToken(TokenKind::RBrace,    start, "}");
    case '[': return makeToken(TokenKind::LBracket,  start, "[");
    case ']': return makeToken(TokenKind::RBracket,  start, "]");
    case ';': return makeToken(TokenKind::Semicolon, start, ";");
    case ',': return makeToken(TokenKind::Comma,     start, ",");
    case '~': return makeToken(TokenKind::Tilde,     start, "~");
    case '@': return makeToken(TokenKind::At,        start, "@");
    case '?': return makeToken(TokenKind::Question,  start, "?");

    case ':':
        if (!atEnd() && cur() == ':') {
            advance();
            return makeToken(TokenKind::ColonColon, start, "::");
        }
        return makeToken(TokenKind::Colon, start, ":");

    case '.':
        if (!atEnd() && cur() == '.' && peek1() == '.') {
            advance(); advance();
            return makeToken(TokenKind::Ellipsis, start, "...");
        }
        if (!atEnd() && cur() == '*') {
            advance();
            return makeToken(TokenKind::DotStar, start, ".*");
        }
        return makeToken(TokenKind::Dot, start, ".");

    case '#':
        if (!atEnd() && cur() == '#') {
            advance();
            return makeToken(TokenKind::HashHash, start, "##");
        }
        return makeToken(TokenKind::Hash, start, "#");

    case '+':
        if (!atEnd() && cur() == '+') { advance(); return makeToken(TokenKind::PlusPlus,  start, "++"); }
        if (!atEnd() && cur() == '=') { advance(); return makeToken(TokenKind::PlusEq,    start, "+="); }
        return makeToken(TokenKind::Plus, start, "+");

    case '-':
        if (!atEnd() && cur() == '-') { advance(); return makeToken(TokenKind::MinusMinus, start, "--"); }
        if (!atEnd() && cur() == '>') {
            advance();
            if (!atEnd() && cur() == '*') { advance(); return makeToken(TokenKind::ArrowStar, start, "->*"); }
            return makeToken(TokenKind::Arrow, start, "->");
        }
        if (!atEnd() && cur() == '=') { advance(); return makeToken(TokenKind::MinusEq, start, "-="); }
        return makeToken(TokenKind::Minus, start, "-");

    case '*':
        if (!atEnd() && cur() == '=') { advance(); return makeToken(TokenKind::StarEq,  start, "*="); }
        return makeToken(TokenKind::Star, start, "*");

    case '/':
        if (!atEnd() && cur() == '=') { advance(); return makeToken(TokenKind::SlashEq, start, "/="); }
        return makeToken(TokenKind::Slash, start, "/");

    case '%':
        if (!atEnd() && cur() == '=') { advance(); return makeToken(TokenKind::PercentEq, start, "%="); }
        return makeToken(TokenKind::Percent, start, "%");

    case '&':
        if (!atEnd() && cur() == '&') { advance(); return makeToken(TokenKind::AmpAmp,  start, "&&"); }
        if (!atEnd() && cur() == '=') { advance(); return makeToken(TokenKind::AmpEq,   start, "&="); }
        return makeToken(TokenKind::Amp, start, "&");

    case '|':
        if (!atEnd() && cur() == '|') { advance(); return makeToken(TokenKind::PipePipe, start, "||"); }
        if (!atEnd() && cur() == '=') { advance(); return makeToken(TokenKind::PipeEq,   start, "|="); }
        return makeToken(TokenKind::Pipe, start, "|");

    case '^':
        if (!atEnd() && cur() == '=') { advance(); return makeToken(TokenKind::CaretEq, start, "^="); }
        return makeToken(TokenKind::Caret, start, "^");

    case '!':
        if (!atEnd() && cur() == '=') { advance(); return makeToken(TokenKind::BangEq, start, "!="); }
        return makeToken(TokenKind::Bang, start, "!");

    case '=':
        if (!atEnd() && cur() == '=') { advance(); return makeToken(TokenKind::EqEq, start, "=="); }
        return makeToken(TokenKind::Eq, start, "=");

    case '<':
        if (!atEnd() && cur() == '<') {
            advance();
            if (!atEnd() && cur() == '=') { advance(); return makeToken(TokenKind::LShiftEq, start, "<<="); }
            return makeToken(TokenKind::LShift, start, "<<");
        }
        if (!atEnd() && cur() == '=') {
            advance();
            if (!atEnd() && cur() == '>') { advance(); return makeToken(TokenKind::Spaceship, start, "<=>"); }
            return makeToken(TokenKind::LtEq, start, "<=");
        }
        return makeToken(TokenKind::Lt, start, "<");

    case '>':
        if (!atEnd() && cur() == '>') {
            advance();
            if (!atEnd() && cur() == '=') { advance(); return makeToken(TokenKind::RShiftEq, start, ">>="); }
            return makeToken(TokenKind::RShift, start, ">>");
        }
        if (!atEnd() && cur() == '=') { advance(); return makeToken(TokenKind::GtEq, start, ">="); }
        return makeToken(TokenKind::Gt, start, ">");

    default:
        break;
    }

    return errorToken(start, std::string("unexpected character '") + c + "'");
}

} // namespace qc
