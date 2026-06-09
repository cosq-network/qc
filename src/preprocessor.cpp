#include "qc/preprocessor.h"
#include <cassert>
#include <fstream>
#include <filesystem>

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
    } else if (dir.text == "else") {
        if (condStack_.empty()) {
            diag_.error(dir.loc, "#else without #ifdef");
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
            diag_.error(dir.loc, "#endif without #ifdef");
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

} // namespace qc
