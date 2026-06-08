#pragma once
#include "common.h"
#include "ast.h"
#include "ir.h"

namespace qc {

class IRGen {
public:
    IRGen(TypeContext& types, DiagEngine& diag, const TargetInfo& target);

    IRModule generate(const TranslationUnit& tu);

private:
    // --- Declarations ---
    void genDecl(const Decl* d);
    void genVarDecl(const VarDecl* d);
    void genFuncDecl(const FuncDecl* d);
    void genRecordDecl(const RecordDecl* d);

    // --- Statements ---
    void genStmt(const Stmt* s);
    void genCompoundStmt(const CompoundStmt* s);
    void genIfStmt(const IfStmt* s);
    void genWhileStmt(const WhileStmt* s);
    void genDoWhileStmt(const DoWhileStmt* s);
    void genForStmt(const ForStmt* s);
    void genSwitchStmt(const SwitchStmt* s);
    void genReturnStmt(const ReturnStmt* s);
    void genBreakStmt();
    void genContinueStmt();
    void genGotoStmt(const GotoStmt* s);
    void genLabelStmt(const LabelStmt* s);

    // --- Expressions ---
    IRValue genExpr(const Expr* e);
    IRValue genLValue(const Expr* e);   // returns pointer to lvalue
    IRValue genIntLit(const IntLitExpr* e);
    IRValue genFloatLit(const FloatLitExpr* e);
    IRValue genStringLit(const StringLitExpr* e);
    IRValue genIdent(const IdentExpr* e);
    IRValue genBinary(const BinaryExpr* e);
    IRValue genUnary(const UnaryExpr* e);
    IRValue genTernary(const TernaryExpr* e);
    IRValue genCall(const CallExpr* e);
    IRValue genIndex(const IndexExpr* e);
    IRValue genMember(const MemberExpr* e);
    IRValue genCast(const CastExpr* e);
    IRValue genSizeof(const SizeofExpr* e);
    IRValue genAlignof(const AlignofExpr* e);
    IRValue genAssign(const AssignExpr* e);
    IRValue genInitList(const InitListExpr* e, IRValue dest);

    // --- Helpers ---
    IRValue  coerce(IRValue v, Type* from, Type* to);
    IRValue  boolify(IRValue v);
    IROpcode selectIntCmp(BinaryOp op, bool isSigned);
    IROpcode selectFCmp(BinaryOp op);
    IROpcode selectCastOp(Type* from, Type* to);
    IROpcode selectBinOp(BinaryOp op, Type* ty);

    std::string makeLabel(const char* prefix = "L");
    std::string makeStringName();

    void enterLoop(IRBlock* cont, IRBlock* brk);
    void exitLoop();
    struct LoopCtx { IRBlock* cont; IRBlock* brk; };

    TypeContext&       types_;
    DiagEngine&        diag_;
    const TargetInfo&  target_;
    IRModule           mod_;
    IRBuilder          builder_;
    IRFunction*        curFn_    = nullptr;

    // Variable → alloca map
    std::unordered_map<const Decl*, IRValue> varMap_;
    // String literal → global name map
    std::unordered_map<std::string, std::string> stringMap_;
    // Label → block map
    std::unordered_map<std::string, IRBlock*> labelMap_;

    std::vector<LoopCtx>   loopStack_;
    std::vector<IRBlock*>  switchBreaks_;

    u32 labelCounter_  = 0;
    u32 stringCounter_ = 0;
};

} // namespace qc
