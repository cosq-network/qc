#pragma once
#include "common.h"
#include "type.h"
#include <string>
#include <vector>
#include <list>

namespace qc {

// ---- Values ----
enum class IRValueKind {
    Void,
    Constant,   // compile-time integer constant
    FConst,     // float constant
    Register,   // virtual register (SSA)
    Global,     // global symbol
    Label,      // basic block label
};

struct IRValue {
    IRValueKind kind;
    TypePtr     type;
    u64         id;      // register id or constant value
    double      fval;    // for FConst
    std::string name;    // for globals/labels

    static IRValue voidVal()          { IRValue r; r.kind=IRValueKind::Void; return r; }
    static IRValue constant(TypePtr t, u64 v)  { IRValue r; r.kind=IRValueKind::Constant; r.type=t; r.id=v; return r; }
    static IRValue fconst(TypePtr t, double v) { IRValue r; r.kind=IRValueKind::FConst; r.type=t; r.fval=v; return r; }
    static IRValue reg(TypePtr t, u64 id)      { IRValue r; r.kind=IRValueKind::Register; r.type=t; r.id=id; return r; }
    static IRValue global(TypePtr t, std::string n) { IRValue r; r.kind=IRValueKind::Global; r.type=t; r.name=n; return r; }
    static IRValue label(std::string n)        { IRValue r; r.kind=IRValueKind::Label; r.name=n; return r; }

    bool isVoid()    const { return kind == IRValueKind::Void; }
    bool isConst()   const { return kind == IRValueKind::Constant || kind == IRValueKind::FConst; }
    bool isReg()     const { return kind == IRValueKind::Register; }
    bool isGlobal()  const { return kind == IRValueKind::Global; }
};

// ---- Instructions ----
enum class IROpcode {
    // Memory
    Alloca,   // dst = alloca type
    Load,     // dst = load ptr
    Store,    // store val, ptr
    // Arithmetic
    Add, Sub, Mul, Div, Mod, UDiv, UMod,
    And, Or, Xor, Shl, Shr, AShr,
    Neg, Not,
    FAdd, FSub, FMul, FDiv, FNeg,
    // Compare
    IEq, INe, ILt, ILe, IGt, IGe,
    IULt, IULe, IUGt, IUGe,
    FEq, FNe, FLt, FLe, FGt, FGe,
    // Cast
    Trunc, ZExt, SExt, FPTrunc, FPExt,
    FPToSI, FPToUI, SIToFP, UIToFP,
    PtrToInt, IntToPtr, Bitcast,
    // Control
    Br,         // br label
    CondBr,     // condbr cond, true_label, false_label
    Ret,        // ret [val]
    Unreachable,
    // Calls
    Call,       // dst = call fn(args...)
    // Phi (for SSA)
    Phi,
    // Address
    GEP,        // dst = gep base, [indices...]
    // Intrinsics
    MemCopy, MemSet,
};

struct IRInstr {
    IROpcode             op;
    IRValue              dst;  // result register (void if no result)
    std::vector<IRValue> srcs; // operands
    TypePtr              opType; // for alloca/load/store/cast

    std::string label; // for Br/CondBr targets
    std::string label2;
    u32         align = 0; // for Alloca/Load/Store
    SourceLocation   loc;       // source location for debug info

    bool hasDst() const { return !dst.isVoid(); }
};

// ---- Basic blocks ----
struct IRBlock {
    std::string            name;
    std::vector<IRInstr>   instrs;
    std::vector<IRBlock*>  preds;
    std::vector<IRBlock*>  succs;
};

// ---- Function parameter ----
struct IRParam {
    std::string name;
    TypePtr     type;
    u64         reg;  // virtual register holding parameter
};

// ---- Function ----
struct IRGlobal;

struct IRFunction {
    std::string          name;
    TypePtr              retType;
    std::vector<IRParam> params;
    bool                 isVariadic = false;
    bool                 isExtern   = false;

    std::list<IRBlock>   blocks;
    u64                  nextReg = 0;

    IRBlock* entry()  { return blocks.empty() ? nullptr : &blocks.front(); }
    IRBlock* addBlock(std::string name = {});

    u64 newReg() { return nextReg++; }
};

// ---- Global variable ----
struct IRGlobal {
    std::string name;
    TypePtr     type;
    bool        isConst    = false;
    bool        isExtern   = false;
    bool        isZeroInit = false;
    std::vector<u8> initData; // raw bytes for initializer
    // For string literals / compound init
    std::string stringInit;
    bool        hasStringInit = false;
    u32         align         = 0;
};

// ---- Module ----
struct IRModule {
    std::string                    name;
    std::vector<IRFunction>        functions;
    std::vector<IRGlobal>          globals;
    TargetInfo                     target;

    IRFunction* findFunction(std::string_view n);
    IRGlobal*   findGlobal(std::string_view n);
};

// ---- Builder ----
class IRBuilder {
public:
    IRBuilder() = default;

    void setFunction(IRFunction* fn) { fn_ = fn; bb_ = nullptr; }
    void setBlock(IRBlock* bb) { bb_ = bb; }
    void setLoc(SourceLocation loc) { loc_ = loc; }

    IRBlock* block() const { return bb_; }
    IRFunction* function() const { return fn_; }
    SourceLocation   loc() const { return loc_; }

    IRBlock* newBlock(std::string name = {});

    IRValue makeAlloca(TypePtr ty, std::string name = {});
    void    store(IRValue val, IRValue ptr);
    IRValue load(TypePtr ty, IRValue ptr);

    IRValue binop(IROpcode op, TypePtr ty, IRValue lhs, IRValue rhs);
    IRValue unop(IROpcode op, TypePtr ty, IRValue val);
    IRValue cmp(IROpcode op, IRValue lhs, IRValue rhs);

    IRValue cast(IROpcode op, TypePtr dstTy, IRValue val);

    IRValue call(TypePtr retTy, IRValue fn, std::vector<IRValue> args);
    IRValue gep(TypePtr elemTy, IRValue base, std::vector<IRValue> idx);

    void    br(IRBlock* target);
    void    condBr(IRValue cond, IRBlock* trueB, IRBlock* falseB);
    void    ret(IRValue val = IRValue::voidVal());
    void    unreachable();

    IRValue constant(TypePtr ty, u64 v);
    IRValue fconst(TypePtr ty, double v);

    bool    hasTerminator() const;

private:
    IRInstr& emit(IRInstr instr);
    IRValue  newReg(TypePtr ty);

    IRFunction* fn_ = nullptr;
    IRBlock*    bb_ = nullptr;
    SourceLocation   loc_;
};

void dumpModule(const IRModule& mod, FILE* out = stderr);
void dumpFunction(const IRFunction& fn, FILE* out = stderr);

} // namespace qc
