#pragma once
#include "common.h"
#include "ast.h"
#include "type.h"

namespace qc {

// Symbol table entry
struct Symbol {
    std::string name;
    Decl*       decl;
    TypePtr     type;
};

class Sema {
public:
    Sema(TypeContext& types, DiagEngine& diag, const TargetInfo& target);

    void analyse(TranslationUnit& tu);

private:
    // --- Declarations ---
    void analyseDecl(Decl* d);
    void analyseVarDecl(VarDecl* d);
    void analyseFuncDecl(FuncDecl* d);
    void analyseRecordDecl(RecordDecl* d);
    void analyseEnumDecl(EnumDecl* d);
    void analyseNamespaceDecl(NamespaceDecl* d);
    void analyseStaticAssertDecl(StaticAssertDecl* d);

    // Statements

    void analyseStmt(Stmt* s);
    void analyseCompoundStmt(CompoundStmt* s);
    void analyseIfStmt(IfStmt* s);
    void analyseWhileStmt(WhileStmt* s);
    void analyseForStmt(ForStmt* s);
    void analyseDoWhileStmt(DoWhileStmt* s);
    void analyseSwitchStmt(SwitchStmt* s);
    void analyseReturnStmt(ReturnStmt* s);

    // --- Expressions ---
    void     analyseExpr(Expr* e);
    void     analyseIntLit(IntLitExpr* e);
    void     analyseFloatLit(FloatLitExpr* e);
    void     analyseStringLit(StringLitExpr* e);
    void     analyseIdent(IdentExpr* e);
    void     analyseBinary(BinaryExpr* e);
    void     analyseUnary(UnaryExpr* e);
    void     analyseCall(CallExpr* e);
    void     analyseIndex(IndexExpr* e);
    void     analyseMember(MemberExpr* e);
    void     analyseCast(CastExpr* e);
    void     analyseSizeof(SizeofExpr* e);
    void     analyseAssign(AssignExpr* e);
    void     analyseTernary(TernaryExpr* e);
    void     analyseInitList(InitListExpr* e);
    void     analyseGeneric(GenericExpr* e);


    // --- Type helpers ---
    TypePtr  usualArithmeticConv(TypePtr a, TypePtr b);
    TypePtr  integerPromotion(TypePtr t);
    bool     isAssignable(const Expr* lhs, const Expr* rhs);
    bool     implicitlyConvertible(Type* from, Type* to);
    TypePtr  decayType(TypePtr t);

    // --- Scope ---
    struct Scope {
        std::unordered_map<std::string, Symbol> syms;
        bool isLoop   = false;
        bool isSwitch = false;
    };

    void   pushScope() { scopes_.emplace_back(); }
    void   popScope()  { scopes_.pop_back(); }
    void   define(Symbol sym);
    Symbol* lookup(std::string_view name);

    TypeContext&        types_;
    DiagEngine&         diag_;
    const TargetInfo&   target_;
    std::vector<Scope>  scopes_;
    FuncDecl*           currentFunc_ = nullptr;
    bool                inLoop_      = false;
    bool                inSwitch_    = false;
};

} // namespace qc
