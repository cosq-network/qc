#include "qc/preprocessor.h"
#include <cassert>
#include <fstream>
#include <filesystem>
#include <functional>

namespace qc {

Preprocessor::Preprocessor(Lexer& mainLexer, DiagEngine& diag)
    : mainLexer_(mainLexer), diag_(diag) {
}

bool Preprocessor::isSkipping() const {
    if (condStack_.empty()) return false;
    return !condStack_.back().isTrue;
}

Lexer* Preprocessor::currentLexer() {
    if (!includeStack_.empty()) {
        return includeStack_.back().lex.get();
    }
    return &mainLexer_;
}

Token Preprocessor::lexRaw() {
    Lexer* lex = currentLexer();
    Token t = lex->next();
    while (t.is(TokenKind::Eof) && !includeStack_.empty()) {
        includeStack_.pop_back();
        lex = currentLexer();
        t = lex->next();
    }
    return t;
}

Token Preprocessor::next() {
    if (!putBackStack_.empty()) {
        Token t = std::move(putBackStack_.back());
        putBackStack_.pop_back();
        return t;
    }

    for (;;) {
        if (!macroExpansionTokens_.empty()) {
            Token t = macroExpansionTokens_.back();
            macroExpansionTokens_.pop_back();
            return t;
        }

        Token t = lexRaw();

        if (t.is(TokenKind::Hash)) {
            handleDirective(t);
            continue;
        }

        if (isSkipping()) continue;

        if (t.is(TokenKind::Identifier)) {
            auto it = macros_.find(t.text);
            if (it != macros_.end()) {
                for (auto rit = it->second.replacement.rbegin(); rit != it->second.replacement.rend(); ++rit) {
                    macroExpansionTokens_.push_back(*rit);
                }
                continue;
            }
        }

        return t;
    }
}

Token Preprocessor::peek() {
    if (putBackStack_.empty()) {
        putBackStack_.push_back(next());
    }
    return putBackStack_.back();
}

void Preprocessor::putBack(Token tok) {
    putBackStack_.push_back(std::move(tok));
}

void Preprocessor::skipLine(u32 line) {
    while (true) {
        Token t = currentLexer()->peek();
        if (t.is(TokenKind::Eof) || t.loc.line > line) {
            break;
        }
        currentLexer()->next();
    }
}

void Preprocessor::handleDirective(Token hashTok) {
    Token dir = currentLexer()->next();
    u32 line = hashTok.loc.line;

    if (dir.text.empty()) {
        diag_.error(dir.loc, "expected preprocessor directive");
        skipLine(line);
        return;
    }

    if (dir.text == "define") {
        if (isSkipping()) { skipLine(line); return; }
        Token name = currentLexer()->next();
        if (name.isNot(TokenKind::Identifier)) {
            diag_.error(name.loc, "macro name must be an identifier");
            skipLine(line);
            return;
        }
        Macro m;
        while (true) {
            Token t = currentLexer()->peek();
            if (t.is(TokenKind::Eof) || t.loc.line > line) break;
            m.replacement.push_back(currentLexer()->next());
        }
        macros_[name.text] = std::move(m);
    } else if (dir.text == "ifdef" || dir.text == "ifndef") {
        bool isIfdef = (dir.text == "ifdef");
        Token name = currentLexer()->next();
        if (name.isNot(TokenKind::Identifier)) {
            diag_.error(name.loc, "expected identifier after #ifdef/#ifndef");
            skipLine(line);
            return;
        }
        bool hasMacro = macros_.count(name.text) > 0;
        bool cond = isIfdef ? hasMacro : !hasMacro;
        if (isSkipping()) {
            condStack_.push_back({false, true}); // already skipping, stay skipping
        } else {
            condStack_.push_back({cond, cond});
        }
        skipLine(line);
    } else if (dir.text == "if") {
        bool cond = evaluateExpression(line);
        if (isSkipping()) {
            condStack_.push_back({false, true});
        } else {
            condStack_.push_back({cond, cond});
        }
        skipLine(line);
    } else if (dir.text == "elif") {
        if (condStack_.empty()) {
            diag_.error(dir.loc, "#elif without #if");
            skipLine(line);
            return;
        }
        CondState& c = condStack_.back();
        bool cond = evaluateExpression(line);
        if (c.wasTrue) {
            c.isTrue = false;
        } else {
            bool parentSkipping = (condStack_.size() > 1 && !condStack_[condStack_.size() - 2].isTrue);
            if (parentSkipping) {
                c.isTrue = false;
            } else {
                c.isTrue = cond;
                if (cond) c.wasTrue = true;
            }
        }
        skipLine(line);
    } else if (dir.text == "else") {
        if (condStack_.empty()) {
            diag_.error(dir.loc, "#else without #if");
            skipLine(line);
            return;
        }
        CondState& c = condStack_.back();
        if (condStack_.size() > 1 && !condStack_[condStack_.size()-2].isTrue) {
            c.isTrue = false; // parent is false
        } else {
            c.isTrue = !c.wasTrue;
        }
        c.wasTrue = true; // prevent subsequent #elif
        skipLine(line);
    } else if (dir.text == "endif") {
        if (condStack_.empty()) {
            diag_.error(dir.loc, "#endif without #if");
            skipLine(line);
            return;
        }
        condStack_.pop_back();
        skipLine(line);
    } else if (dir.text == "include") {
        if (isSkipping()) { skipLine(line); return; }
        Token fileTok = currentLexer()->next();
        std::string filename;
        if (fileTok.is(TokenKind::StringLit)) {
            filename = fileTok.text;
            if (filename.size() >= 2 && filename.front() == '"' && filename.back() == '"') {
                filename = filename.substr(1, filename.size() - 2);
            }
        } else if (fileTok.is(TokenKind::Lt)) {
            // <file.h>
            while (true) {
                Token t = currentLexer()->next();
                if (t.is(TokenKind::Eof) || t.loc.line > line || t.is(TokenKind::Gt)) break;
                filename += t.text; // approximation
            }
        } else {
            diag_.error(fileTok.loc, "expected \"FILENAME\" or <FILENAME>");
            skipLine(line);
            return;
        }
        
        // Basic resolution: try current dir then include paths
        std::filesystem::path p = filename;
        bool found = std::filesystem::exists(p);
        
        if (!found) {
            for (const auto& path : includePaths_) {
                std::filesystem::path tp = std::filesystem::path(path) / filename;
                if (std::filesystem::exists(tp)) {
                    p = tp;
                    found = true;
                    break;
                }
            }
        }

        if (!found) {
            diag_.error(dir.loc, "cannot find included file: " + filename);
            return;
        }

        IncludeState is;
        is.src = std::make_unique<SourceFile>(p.string());
        if (!is.src->load()) {
            diag_.error(dir.loc, "failed to read included file");
            return;
        }
        is.lex = std::make_unique<Lexer>(*is.src, diag_, mainLexer_.isCxxMode());
        includeStack_.push_back(std::move(is));

    } else {
        // unsupported directive
        skipLine(line);
    }
}

bool Preprocessor::evaluateExpression(u32 line) {
    // Collect tokens for this line
    std::vector<Token> tokens;
    while (true) {
        Token t = currentLexer()->peek();
        if (t.is(TokenKind::Eof) || t.loc.line > line) break;
        tokens.push_back(currentLexer()->next());
    }

    if (tokens.empty()) return false;

    // Simple recursive descent-ish evaluator for:
    // expr   ::= logical_or
    // logical_or  ::= logical_and { "||" logical_and }
    // logical_and ::= unary { "&&" unary }
    // unary       ::= "!" unary | primary
    // primary     ::= "defined" "(" ID ")" | "defined" ID | ID | INT | "(" expr ")"

    size_t pos = 0;
    auto peek = [&]() -> Token { 
        if (pos < tokens.size()) return tokens[pos];
        return Token{TokenKind::Eof};
    };
    auto consume = [&]() -> Token {
        if (pos < tokens.size()) return tokens[pos++];
        return Token{TokenKind::Eof};
    };

    std::function<bool()> parseExpr;
    std::function<bool()> parseLogicalOr;
    std::function<bool()> parseLogicalAnd;
    std::function<bool()> parseUnary;
    std::function<bool()> parsePrimary;

    parsePrimary = [&]() -> bool {
        Token t = consume();
        if (t.is(TokenKind::LParen)) {
            bool res = parseExpr();
            if (peek().is(TokenKind::RParen)) consume();
            return res;
        }
        if (t.is(TokenKind::IntLit)) {
            return std::stoll(t.text) != 0;
        }
        if (t.is(TokenKind::Identifier)) {
            if (t.text == "defined") {
                Token nt = peek();
                bool hasParen = false;
                if (nt.is(TokenKind::LParen)) {
                    hasParen = true;
                    consume();
                    nt = peek();
                }
                bool res = false;
                if (nt.is(TokenKind::Identifier)) {
                    res = (macros_.count(nt.text) > 0);
                    consume();
                }
                if (hasParen && peek().is(TokenKind::RParen)) consume();
                return res;
            }
            // Standard: identifiers not matching defined or macros are 0
            // For now, let's just check if it's a known macro
            return (macros_.count(t.text) > 0);
        }
        return false;
    };

    parseUnary = [&]() -> bool {
        if (peek().is(TokenKind::Bang)) {
            consume();
            return !parseUnary();
        }
        return parsePrimary();
    };

    parseLogicalAnd = [&]() -> bool {
        bool left = parseUnary();
        while (peek().is(TokenKind::AmpAmp)) {
            consume();
            bool right = parseUnary();
            left = left && right;
        }
        return left;
    };

    parseLogicalOr = [&]() -> bool {
        bool left = parseLogicalAnd();
        while (peek().is(TokenKind::PipePipe)) {
            consume();
            bool right = parseLogicalAnd();
            left = left || right;
        }
        return left;
    };

    parseExpr = parseLogicalOr;

    return parseExpr();
}

} // namespace qc
