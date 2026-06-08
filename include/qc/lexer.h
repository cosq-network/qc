#pragma once
#include "common.h"
#include "token.h"
#include "source.h"

namespace qc {

class Lexer {
public:
    Lexer(const SourceFile& src, DiagEngine& diag, bool cxxMode = true);

    Token next();
    Token peek();
    void  putBack(Token tok);

    bool isCxxMode() const { return cxxMode_; }
    const SourceFile& source() const { return src_; }

private:
    Token lexToken();
    Token lexIdentOrKeyword();
    Token lexNumericLiteral();
    Token lexStringLiteral(bool wide = false);
    Token lexCharLiteral(bool wide = false);
    Token lexOperatorOrPunct();

    void skipLineComment();
    void skipBlockComment();
    void skipWhitespace();
    char lexEscapeSequence(std::string& raw);

    char   cur()  const { return pos_ < end_ ? *pos_ : '\0'; }
    char   peek1()const { return pos_ + 1 < end_ ? *(pos_+1) : '\0'; }
    char   peek2()const { return pos_ + 2 < end_ ? *(pos_+2) : '\0'; }
    char   advance() { char c = *pos_++; if (c == '\n') { ++line_; col_ = 1; } else { ++col_; } return c; }
    bool   atEnd() const { return pos_ >= end_; }

    SourceLocation curLoc() const {
        SourceLocation l;
        l.line   = line_;
        l.column = col_;
        l.offset = static_cast<u32>(pos_ - src_.data());
        l.file   = src_.path().data();
        return l;
    }

    Token makeToken(TokenKind kind, SourceLocation start, std::string text = {}) {
        Token t;
        t.kind = kind;
        t.loc  = start;
        t.text = std::move(text);
        return t;
    }

    Token errorToken(SourceLocation loc, std::string msg);

    static TokenKind identToKeyword(std::string_view id, bool cxx);

    const SourceFile& src_;
    DiagEngine&       diag_;
    bool              cxxMode_;

    const char* pos_;
    const char* end_;
    u32         line_ = 1;
    u32         col_  = 1;

    std::optional<Token> putBack_;
};

} // namespace qc
