#pragma once
#include "common.h"
#include "lexer.h"
#include "preprocessor.h"
#include "ast.h"
#include "type.h"

namespace qc {

class Parser {
public:
    Parser(Preprocessor& pp, TypeContext& types, DiagEngine& diag);

    Ptr<TranslationUnit> parse();

private:
    // --- Top-level ---
    DeclPtr      parseTopLevelDecl();
    DeclPtr      parseNamespaceDecl();

    // --- Declarations ---
    struct DeclSpec {
        TypePtr          baseType;
        ExprPtr          alignasExpr; // For _Alignas
        bool isExtern    = false;
        bool isStatic    = false;
        bool isInline    = false;
        bool isConstexpr = false;
        bool isTypedef   = false;
        bool isVirtual   = false;
        bool isExplicit  = false;
        bool isMutable   = false;
        bool isFriend    = false;
        bool isNoreturn  = false;
    };

    DeclSpec     parseDeclSpec();
    TypePtr      parseTypeName();
    TypePtr      parseTypeSpecifier(DeclSpec& ds);
    TypePtr      parseDeclarator(TypePtr base, std::string& nameOut);
    TypePtr      parseAbstractDeclarator(TypePtr base);
    TypePtr      parsePointerSuffix(TypePtr base);
    TypePtr      parseFunctionSuffix(TypePtr base, std::string& nameOut);
    TypePtr      parseArraySuffix(TypePtr base);

    DeclPtr      parseDeclaration();
    Ptr<FuncDecl> parseFunctionDecl(DeclSpec& ds, TypePtr fnType, std::string name, SourceLocation loc);
    Ptr<VarDecl>  parseVarDecl(DeclSpec& ds, TypePtr type, std::string name, SourceLocation loc);

    Ptr<RecordDecl> parseRecordDecl(TypeKind k);
    Ptr<EnumDecl>   parseEnumDecl();

    // --- Statements ---
    StmtPtr parseStmt();
    StmtPtr parseCompoundStmt();
    StmtPtr parseIfStmt();
    StmtPtr parseWhileStmt();
    StmtPtr parseDoWhileStmt();
    StmtPtr parseForStmt();
    StmtPtr parseSwitchStmt();
    StmtPtr parseReturnStmt();
    StmtPtr parseBreakStmt();
    StmtPtr parseContinueStmt();
    StmtPtr parseGotoStmt();
    StmtPtr parseTryStmt();

    // --- Expressions ---
    ExprPtr parseExpr();
    ExprPtr parseAssignExpr();
    ExprPtr parseTernaryExpr();
    ExprPtr parseLogOrExpr();
    ExprPtr parseLogAndExpr();
    ExprPtr parseBitorExpr();
    ExprPtr parseXorExpr();
    ExprPtr parseBitandExpr();
    ExprPtr parseEqExpr();
    ExprPtr parseRelExpr();
    ExprPtr parseShiftExpr();
    ExprPtr parseAddExpr();
    ExprPtr parseMulExpr();
    ExprPtr parseCastExpr();
    ExprPtr parseUnaryExpr();
    ExprPtr parsePostfixExpr();
    ExprPtr parsePrimaryExpr();
    ExprPtr parseInitListExpr();

    std::vector<ExprPtr> parseArgList();

    // --- Helpers ---
    Token  cur()  { return pp_.peek(); }
    Token  next() { return pp_.next(); }
    Token  peek(); // peek at token after cur()
    Token  consume(TokenKind k, const char* msg);
    bool   check(TokenKind k) { return cur().is(k); }
    bool   match(TokenKind k);
    bool   atEnd() { return cur().is(TokenKind::Eof); }

    TypePtr lookupTypeName(std::string_view name);
    bool         isTypeName(const Token& tok);
    bool         isStartOfDeclaration();
    bool         isStartOfDeclaration(const Token& t);
    bool         isStartOfParameterList();


    Preprocessor& pp_;
    TypeContext& types_;
    DiagEngine& diag_;

    // Symbol table (typedef names, tag types)
    struct Scope {
        std::unordered_map<std::string, TypePtr>  typedefs;
        std::unordered_map<std::string, Ref<RecordType>> tags;
        std::unordered_map<std::string, Ref<EnumType>>   enums;
    };
    std::vector<Scope> scopes_;
    std::vector<DeclPtr> pendingDecls_;
    RecordDecl* currentRecord_ = nullptr;

    void pushScope() { scopes_.emplace_back(); }
    void popScope()  { scopes_.pop_back(); }
    void defineTypedef(std::string name, TypePtr t) {
        scopes_.back().typedefs[name] = std::move(t);
    }
};

} // namespace qc
