#pragma once
#include "common.h"
#include "token.h"
#include "lexer.h"
#include "source.h"
#include <unordered_map>
#include <vector>
#include <memory>
#include <optional>
#include <string>

namespace qc {

class Preprocessor {
public:
    Preprocessor(Lexer& mainLexer, DiagEngine& diag);

    void addIncludePath(std::string path) { includePaths_.push_back(std::move(path)); }

    Token next();
    Token peek();
    void  putBack(Token tok);

private:
    struct Macro {
        std::vector<Token> replacement;
    };

    struct IncludeState {
        std::unique_ptr<SourceFile> src;
        std::unique_ptr<Lexer>      lex;
    };

    Token lexRaw();
    void handleDirective(Token hashTok);
    void skipLine(u32 line);

    Lexer* currentLexer();

    Lexer&      mainLexer_;
    DiagEngine& diag_;

    std::vector<std::string>  includePaths_;
    std::vector<IncludeState> includeStack_;
    std::unordered_map<std::string, Macro> macros_;
    std::vector<Token>   putBackStack_;
    std::vector<Token>   macroExpansionTokens_; // currently expanded tokens

    struct CondState {
        bool isTrue;
        bool wasTrue;
    };
    std::vector<CondState> condStack_;
    bool isSkipping() const;
};

} // namespace qc
