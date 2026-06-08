#pragma once
#include "common.h"
#include "token.h"
#include "type.h"
#include <vector>
#include <string>

namespace qc {

// Forward declarations
class Expr;  class Stmt;  class Decl;
using ExprPtr = Ptr<Expr>;
using StmtPtr = Ptr<Stmt>;
using DeclPtr = Ptr<Decl>;

// ============================================================
// Expressions
// ============================================================
enum class ExprKind {
    IntLit, FloatLit, StringLit, CharLit, BoolLit, NullptrLit,
    Ident, Binary, Unary, Ternary,
    Call, Index, Member, MemberPtr,
    Cast, CStyle_Cast, Sizeof, Alignof,
    New, Delete,
    Assign,
    Compound, // {1,2,3}
    InitList, Generic
};

class Expr {
public:
    virtual ~Expr() = default;
    virtual ExprKind kind() const = 0;
    SourceLocation loc;
    TypePtr        type;   // set by sema
    bool           isLValue = false;
};

struct IntLitExpr : Expr {
    u64  value;
    bool isSigned;
    ExprKind kind() const override { return ExprKind::IntLit; }
};

struct FloatLitExpr : Expr {
    double value;
    ExprKind kind() const override { return ExprKind::FloatLit; }
};

struct StringLitExpr : Expr {
    std::string value; // UTF-8
    bool        wide;
    ExprKind kind() const override { return ExprKind::StringLit; }
};

struct CharLitExpr : Expr {
    u32  value;
    bool wide;
    ExprKind kind() const override { return ExprKind::CharLit; }
};

struct BoolLitExpr : Expr {
    bool value;
    ExprKind kind() const override { return ExprKind::BoolLit; }
};

struct NullptrLitExpr : Expr {
    ExprKind kind() const override { return ExprKind::NullptrLit; }
};

struct IdentExpr : Expr {
    std::string name;
    Decl*       decl = nullptr; // resolved by sema
    ExprKind kind() const override { return ExprKind::Ident; }
};

enum class BinaryOp {
    Add, Sub, Mul, Div, Mod,
    And, Or, Xor, Shl, Shr,
    LAnd, LOr,
    Eq, Ne, Lt, Le, Gt, Ge,
    Comma,
};

struct BinaryExpr : Expr {
    BinaryOp op;
    ExprPtr  lhs, rhs;
    ExprKind kind() const override { return ExprKind::Binary; }
};

enum class UnaryOp {
    PreInc, PreDec, PostInc, PostDec,
    Plus, Neg, Not, LNot,
    Deref, AddrOf,
};

struct UnaryExpr : Expr {
    UnaryOp op;
    ExprPtr operand;
    ExprKind kind() const override { return ExprKind::Unary; }
};

struct TernaryExpr : Expr {
    ExprPtr cond, then, els;
    ExprKind kind() const override { return ExprKind::Ternary; }
};

struct CallExpr : Expr {
    ExprPtr              callee;
    std::vector<ExprPtr> args;
    ExprKind kind() const override { return ExprKind::Call; }
};

struct IndexExpr : Expr {
    ExprPtr base, index;
    ExprKind kind() const override { return ExprKind::Index; }
};

struct MemberExpr : Expr {
    ExprPtr     base;
    std::string member;
    bool        isArrow; // -> vs .
    FieldInfo*  field = nullptr; // resolved by sema
    ExprKind kind() const override { return ExprKind::Member; }
};

struct CastExpr : Expr {
    enum class CastKind { CStyle, Static, Reinterpret, Const, Dynamic };
    CastKind castKind;
    TypePtr  targetType;
    ExprPtr  operand;
    ExprKind kind() const override { return ExprKind::Cast; }
};

struct SizeofExpr : Expr {
    bool    isType;
    TypePtr targetType;  // if isType
    ExprPtr operand;     // if !isType
    ExprKind kind() const override { return ExprKind::Sizeof; }
};

struct AlignofExpr : Expr {
    bool    isType;
    TypePtr targetType;  // if isType
    ExprPtr operand;     // if !isType
    ExprKind kind() const override { return ExprKind::Alignof; }
};

struct NewExpr : Expr {
    TypePtr              allocType;
    std::vector<ExprPtr> placement;
    ExprPtr              initializer; // may be null
    bool                 isArray;
    ExprKind kind() const override { return ExprKind::New; }
};

struct DeleteExpr : Expr {
    ExprPtr operand;
    bool    isArray;
    ExprKind kind() const override { return ExprKind::Delete; }
};

struct GenericExpr : Expr {
    ExprPtr controlExpr;
    struct Association {
        TypePtr type; // nullptr if default
        ExprPtr expr;
    };
    std::vector<Association> associations;
    ExprPtr resultExpr; // resolved during sema
    ExprKind kind() const override { return ExprKind::Generic; }
};


enum class AssignOp {
    Assign,
    AddAssign, SubAssign, MulAssign, DivAssign, ModAssign,
    AndAssign, OrAssign, XorAssign, ShlAssign, ShrAssign,
};

struct AssignExpr : Expr {
    AssignOp op;
    ExprPtr  lhs, rhs;
    ExprKind kind() const override { return ExprKind::Assign; }
};

struct InitListExpr : Expr {
    std::vector<ExprPtr> inits;
    ExprKind kind() const override { return ExprKind::InitList; }
};

// ============================================================
// Statements
// ============================================================
enum class StmtKind {
    Compound, Expr, Decl,
    If, While, DoWhile, For, Switch,
    Case, Default, Break, Continue,
    Return, Goto, Label,
    Try, Throw,
    Null,
};

class Stmt {
public:
    virtual ~Stmt() = default;
    virtual StmtKind kind() const = 0;
    SourceLocation loc;
};

struct CompoundStmt : Stmt {
    std::vector<StmtPtr> stmts;
    StmtKind kind() const override { return StmtKind::Compound; }
};

struct ExprStmt : Stmt {
    ExprPtr expr;
    StmtKind kind() const override { return StmtKind::Expr; }
};

struct DeclStmt : Stmt {
    std::vector<DeclPtr> decls;
    StmtKind kind() const override { return StmtKind::Decl; }
};

struct IfStmt : Stmt {
    ExprPtr cond;
    StmtPtr then;
    StmtPtr els; // may be null
    StmtKind kind() const override { return StmtKind::If; }
};

struct WhileStmt : Stmt {
    ExprPtr cond;
    StmtPtr body;
    StmtKind kind() const override { return StmtKind::While; }
};

struct DoWhileStmt : Stmt {
    StmtPtr body;
    ExprPtr cond;
    StmtKind kind() const override { return StmtKind::DoWhile; }
};

struct ForStmt : Stmt {
    StmtPtr init;  // DeclStmt or ExprStmt or null
    ExprPtr cond;  // may be null
    ExprPtr step;  // may be null
    StmtPtr body;
    StmtKind kind() const override { return StmtKind::For; }
};

struct SwitchStmt : Stmt {
    ExprPtr              cond;
    StmtPtr              body;
    StmtKind kind() const override { return StmtKind::Switch; }
};

struct CaseStmt : Stmt {
    ExprPtr  value;  // null → default
    StmtPtr  body;
    bool     isDefault;
    StmtKind kind() const override { return isDefault ? StmtKind::Default : StmtKind::Case; }
};

struct ReturnStmt : Stmt {
    ExprPtr value; // may be null
    StmtKind kind() const override { return StmtKind::Return; }
};

struct BreakStmt : Stmt {
    StmtKind kind() const override { return StmtKind::Break; }
};

struct ContinueStmt : Stmt {
    StmtKind kind() const override { return StmtKind::Continue; }
};

struct GotoStmt : Stmt {
    std::string label;
    StmtKind kind() const override { return StmtKind::Goto; }
};

struct LabelStmt : Stmt {
    std::string label;
    StmtPtr     body;
    StmtKind kind() const override { return StmtKind::Label; }
};

struct ThrowStmt : Stmt {
    ExprPtr value; // null → rethrow
    StmtKind kind() const override { return StmtKind::Throw; }
};

struct TryStmt : Stmt {
    struct CatchClause {
        TypePtr     type; // null → catch(...)
        std::string name;
        StmtPtr     body;
    };
    StmtPtr                 body;
    std::vector<CatchClause> catches;
    StmtKind kind() const override { return StmtKind::Try; }
};

struct NullStmt : Stmt {
    StmtKind kind() const override { return StmtKind::Null; }
};

// ============================================================
// Declarations
// ============================================================
enum class DeclKind {
    Var, Function, Param,
    Struct, Union, Class, Enum, Field,
    Typedef, Namespace,
    Using, Template, StaticAssert,
};

class Decl {
public:
    virtual ~Decl() = default;
    virtual DeclKind kind() const = 0;
    SourceLocation   loc;
    std::string      name;
    TypePtr          type;
    bool             isExtern   = false;
    bool             isStatic   = false;
    bool             isInline   = false;
    bool             isConstexpr= false;
    bool             isMutable  = false;
    bool             isNoreturn = false;
    ExprPtr          alignasExpr;
    u32              alignment = 0; // resolved by sema
};

struct VarDecl : Decl {
    ExprPtr  init;    // may be null
    bool     isGlobal = false;
    i32      stackOffset = 0; // set during codegen
    DeclKind kind() const override { return DeclKind::Var; }
};

struct ParamDecl : Decl {
    u32      index = 0;
    DeclKind kind() const override { return DeclKind::Param; }
};

struct FuncDecl : Decl {
    std::vector<Ptr<ParamDecl>> params;
    StmtPtr                     body;   // null → declaration only
    bool                        isVirtual  = false;
    bool                        isOverride = false;
    bool                        isPureVirtual = false;
    bool                        isVariadic = false;
    DeclKind kind() const override { return DeclKind::Function; }
};

struct RecordDecl : Decl {
    std::vector<DeclPtr>    members;
    Ref<RecordType>         recordType;
    std::vector<RecordDecl*> bases;    // base classes
    DeclKind kind() const override {
        if (name.empty()) return DeclKind::Struct;
        return DeclKind::Struct; // refined by RecordType::kind()
    }
};

struct EnumDecl : Decl {
    Ref<EnumType> enumType;
    DeclKind kind() const override { return DeclKind::Enum; }
};

struct TypedefDecl : Decl {
    TypePtr aliasedType;
    DeclKind kind() const override { return DeclKind::Typedef; }
};

struct NamespaceDecl : Decl {
    std::vector<DeclPtr> members;
    DeclKind kind() const override { return DeclKind::Namespace; }
};

struct StaticAssertDecl : Decl {
    ExprPtr expr;
    std::string message;
    DeclKind kind() const override { return DeclKind::StaticAssert; }
};

// ============================================================
// Translation unit
// ============================================================
struct TranslationUnit {
    std::vector<DeclPtr> decls;
};

} // namespace qc
