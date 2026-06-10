// src/codegen/arm64_codegen.cpp
// ARM64 backend for the qc compiler.

#include "qc/codegen.h"
#include "qc/ir.h"
#include "qc/type.h"
#include "qc/common.h"
#include "qc/elf_writer.h"
#include "qc/pe_writer.h"
#include "qc/dwarf.h"

#include <cstdio>
#include <string>
#include <vector>
#include <unordered_map>
#include <algorithm>
#include <cassert>

namespace qc {

// ---------------------------------------------------------------------------
// ARM64 registers
// ---------------------------------------------------------------------------
static const char* xRegName(u32 r) {
    static const char* names[] = {
        "x0","x1","x2","x3","x4","x5","x6","x7",
        "x8","x9","x10","x11","x12","x13","x14","x15",
        "x16","x17","x18","x19","x20","x21","x22","x23",
        "x24","x25","x26","x27","x28","x29","x30","sp"
    };
    return (r < 32) ? names[r] : "??";
}

static const char* wRegName(u32 r) {
    static const char* names[] = {
        "w0","w1","w2","w3","w4","w5","w6","w7",
        "w8","w9","w10","w11","w12","w13","w14","w15",
        "w16","w17","w18","w19","w20","w21","w22","w23",
        "w24","w25","w26","w27","w28","w29","w30","wsp"
    };
    return (r < 32) ? names[r] : "??";
}

static const char* dRegName(u32 r) {
    static const char* names[] = {
        "d0","d1","d2","d3","d4","d5","d6","d7",
        "d8","d9","d10","d11","d12","d13","d14","d15",
        "d16","d17","d18","d19","d20","d21","d22","d23",
        "d24","d25","d26","d27","d28","d29","d30","d31"
    };
    return (r < 32) ? names[r] : "??";
}

// static constexpr u32 REG_X8  = 8;   // indirect result / scratch
static constexpr u32 REG_X9  = 9;   // scratch
static constexpr u32 REG_X10 = 10;  // scratch
static constexpr u32 REG_X11 = 11;  // scratch
// static constexpr u32 REG_X12 = 12;  // scratch
static constexpr u32 REG_V0  = 32;  // FP regs start at 32
static constexpr u32 REG_V8  = 32 + 8;

// Callee-saved integer regs (x19-x28) — x29/x30 handled by frame
[[maybe_unused]] static const u32 kCalleeGPR[] = { 19,20,21,22,23,24,25,26,27,28 };
[[maybe_unused]] static constexpr u32 kNumCalleeGPR = 10;

// Callee-saved FP regs v8-v15
[[maybe_unused]] static const u32 kCalleeFPR[] = { REG_V8,REG_V8+1,REG_V8+2,REG_V8+3,
                                    REG_V8+4,REG_V8+5,REG_V8+6,REG_V8+7 };
[[maybe_unused]] static constexpr u32 kNumCalleeFPR = 8;

// ---------------------------------------------------------------------------
// Type helpers
// ---------------------------------------------------------------------------
[[maybe_unused]] static bool typeIsFloat(const Type* t) {
    if (!t) return false;
    switch (t->kind()) {
        case TypeKind::Float:
        case TypeKind::Double:
        case TypeKind::LongDouble:
            return true;
        default: return false;
    }
}

[[maybe_unused]] static u32 typeSize(const Type* t) {
    return t ? t->size() : 8u;
}

// ---------------------------------------------------------------------------
// Per-function context
// ---------------------------------------------------------------------------
struct ARM64LineInfo {
    u64 offset;
    SourceLocation loc;
};

struct ARM64FuncCtx {
    std::unordered_map<u64, u32> virToPhys;
    std::unordered_map<u64, i32> spillSlots; // sp-relative offset
    std::unordered_map<u64, i32> allocaOffsets; // sp-relative offset
    i32  nextSpillOff = 0;   // grows upward from frame base (positive)
    i32  frameSize    = 0;   // total frame, must be 16-byte aligned
    bool usedCalleeGPR[32] = {};
    bool usedCalleeFPR[32] = {};

    std::vector<std::string> lines;
    std::vector<ARM64LineInfo> lineInfos;
    u64                      currentOffset = 0;
    SourceLocation           lastLoc;
    std::string fnName;

    void setLoc(SourceLocation loc) {
        if (loc.file && (loc.line != lastLoc.line || loc.file != lastLoc.file)) {
            lineInfos.push_back({currentOffset, loc});
            lastLoc = loc;
        }
    }

    void emit(std::string s) {
        currentOffset += s.length() + 1; // +1 for \n
        lines.push_back(std::move(s));
    }
    void emitLabel(const std::string& l) { emit(l + ":"); }
    void emitInst(const std::string& s)  { emit("    " + s); }
};

// ---------------------------------------------------------------------------
// ARM64CodeGen class
// ---------------------------------------------------------------------------
class ARM64CodeGen : public CodeGen {
public:
    explicit ARM64CodeGen(const TargetInfo& target, DiagEngine& diag)
        : target_(target), diag_(diag) {
        // Only use underscore prefix for systems that expect it (like Mach-O or Windows PE)
        // For now, we only support ELF and PE. ELF typically does not use underscores.
        isMachO_ = (target_.format == TargetFormat::PE); 
    }

    void compile(const IRModule& mod) override;
    void emitAssembly(FILE* out) override;
    std::vector<u8> emitObject() override;

private:
    void compileFunction(const IRFunction& fn);
    void compileGlobal(const IRGlobal& g);

    void allocateRegisters(const IRFunction& fn, ARM64FuncCtx& ctx);

    // Instruction emitters
    void emitInstr(const IRInstr& ins, ARM64FuncCtx& ctx);
    void emitAlloca(const IRInstr& ins, ARM64FuncCtx& ctx);
    void emitLoad(const IRInstr& ins, ARM64FuncCtx& ctx);
    void emitStore(const IRInstr& ins, ARM64FuncCtx& ctx);
    void emitBinop(const IRInstr& ins, ARM64FuncCtx& ctx);
    void emitUnop(const IRInstr& ins, ARM64FuncCtx& ctx);
    void emitCmp(const IRInstr& ins, ARM64FuncCtx& ctx);
    void emitBr(const IRInstr& ins, ARM64FuncCtx& ctx);
    void emitCondBr(const IRInstr& ins, ARM64FuncCtx& ctx);
    void emitRet(const IRInstr& ins, ARM64FuncCtx& ctx);
    void emitCall(const IRInstr& ins, ARM64FuncCtx& ctx);
    void emitGEP(const IRInstr& ins, ARM64FuncCtx& ctx);
    void emitCast(const IRInstr& ins, ARM64FuncCtx& ctx);

    // Helpers to get physical register string for a value
    std::string gprOf(const IRValue& v, ARM64FuncCtx& ctx, u32 scratch = REG_X9);
    std::string dregOf(const IRValue& v, ARM64FuncCtx& ctx, u32 scratch = REG_V0+14);
    // Ensure value is in a GPR, emitting load if spilled
    u32 loadGPR(const IRValue& v, ARM64FuncCtx& ctx, u32 scratch);
    // Ensure value is in a SIMD reg
    u32 loadFPR(const IRValue& v, ARM64FuncCtx& ctx, u32 scratch);

    std::string spOff(i32 off);
    std::string fpOff(i32 off, ARM64FuncCtx& ctx);
    std::string localLabel(const std::string& fn, const std::string& bb);

    // Saved output state
    struct LineInfo {
        u64 offset;
        SourceLocation loc;
    };
    struct FnOutput {
        std::string name;
        std::vector<std::string> lines;
        std::vector<LineInfo> lineInfos;
        bool isExtern = false;
    };

    struct GlobOutput {
        std::string name;
        std::vector<u8> initData;
        std::string stringInit;
        bool hasStringInit = false;
        std::vector<std::string> symbolInits; // NEW
        bool isZeroInit    = false;
        bool isConst       = false;
        bool isExtern      = false;
        u32  size          = 0;
        u32  align         = 0;
    };

    std::vector<FnOutput>   fnOutputs_;
    std::vector<GlobOutput> globOutputs_;

    TargetInfo  target_;
    [[maybe_unused]] DiagEngine& diag_;
    bool        debugEnabled_ = false;
    bool        isMachO_      = false;

public:
    void setDebugEnabled(bool e) override { debugEnabled_ = e; }
};

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------
std::string ARM64CodeGen::spOff(i32 off) {
    if (off == 0) return "[sp]";
    return "[sp, #" + std::to_string(off) + "]";
}

std::string ARM64CodeGen::fpOff(i32 off, ARM64FuncCtx& ctx) {
    // x29 points to [sp + (frameSize-16)]
    // so sp + off is x29 + (off - (frameSize-16))
    i32 relative = off - (ctx.frameSize - 16);
    if (relative == 0) return "[x29]";
    return "[x29, #" + std::to_string(relative) + "]";
}

std::string ARM64CodeGen::localLabel(const std::string& fn, const std::string& bb) {
    return ".L" + fn + "_" + bb;
}

// ---------------------------------------------------------------------------
// Register allocation (extremely simple: every virtual reg is spilled)
// ---------------------------------------------------------------------------
void ARM64CodeGen::allocateRegisters(const IRFunction& fn, ARM64FuncCtx& ctx) {
    // AAPCS64: x0-x7 args, x8 indirect result, x9-x15 scratch, x16-x17 IP, x18 platform
    // x19-x28 callee-saved, x29 FP, x30 LR, sp SP.
    // We spill everything to the stack for simplicity.

    u32 numValues = (u32)fn.nextReg;
    i32 spillSize = numValues * 8;
    i32 allocaSize = 0;
    for (auto& bb : fn.blocks) {
        for (auto& ins : bb.instrs) {
            if (ins.op == IROpcode::Alloca && ins.opType) {
                // simple layout: just pack them
                u32 align = ins.align > 0 ? ins.align : ins.opType->align();
                if (align < 1) align = 1;
                allocaSize = (allocaSize + align - 1) & ~(align - 1);
                ctx.allocaOffsets[ins.dst.id] = allocaSize;
                allocaSize += ins.opType->size();
            }
        }
    }

    ctx.frameSize = 16 + spillSize + allocaSize;
    ctx.frameSize = (ctx.frameSize + 15) & ~15;

    // Assign spill slots (after allocas)
    for (u64 i = 0; i < fn.nextReg; ++i) {
        ctx.spillSlots[i] = allocaSize + (i32)(i * 8);
    }

}

// ---------------------------------------------------------------------------
// loadGPR
// ---------------------------------------------------------------------------
u32 ARM64CodeGen::loadGPR(const IRValue& v, ARM64FuncCtx& ctx, u32 scratch) {
    if (v.kind == IRValueKind::Constant) {
        ctx.emitInst("mov " + std::string(xRegName(scratch)) + ", #" + std::to_string(v.id));
        return scratch;
    }
    if (v.kind == IRValueKind::Global) {
        std::string sym = v.name;
        if (isMachO_) sym = "_" + sym;
        ctx.emitInst("adrp " + std::string(xRegName(scratch)) + ", " + sym + "@PAGE");
        ctx.emitInst("add " + std::string(xRegName(scratch)) + ", " + xRegName(scratch) + ", " + sym + "@PAGEOFF");
        return scratch;
    }
    if (v.kind == IRValueKind::Register) {
        if (ctx.virToPhys.count(v.id)) return ctx.virToPhys[v.id];
        if (ctx.spillSlots.count(v.id)) {
            u32 size = v.type ? v.type->size() : 8;
            bool isSigned = v.type && v.type->isSigned();
            std::string ldr = "ldr";
            std::string reg = xRegName(scratch);
            if (size == 1)      { ldr = isSigned ? "ldrsb" : "ldrb"; reg = isSigned ? xRegName(scratch) : wRegName(scratch); }
            else if (size == 2) { ldr = isSigned ? "ldrsh" : "ldrh"; reg = isSigned ? xRegName(scratch) : wRegName(scratch); }
            else if (size == 4) { ldr = isSigned ? "ldrsw" : "ldr";  reg = isSigned ? xRegName(scratch) : wRegName(scratch); }

            ctx.emitInst(ldr + " " + reg + ", " + spOff(ctx.spillSlots[v.id]));
            return scratch;
        }
    }
    return scratch;
}

u32 ARM64CodeGen::loadFPR(const IRValue& v, ARM64FuncCtx& ctx, u32 scratch) {
    if (v.kind == IRValueKind::FConst) {
        // bit-cast float/double to int and mov to scratch, then fmov
        return scratch;
    }
    if (v.kind == IRValueKind::Register) {
        if (ctx.virToPhys.count(v.id)) return ctx.virToPhys[v.id];
        if (ctx.spillSlots.count(v.id)) {
            ctx.emitInst("ldr " + std::string(dRegName(scratch)) + ", " + spOff(ctx.spillSlots[v.id]));
            return scratch;
        }
    }
    return scratch;
}

std::string ARM64CodeGen::gprOf(const IRValue& v, ARM64FuncCtx& ctx, u32 scratch) {
    u32 r = loadGPR(v, ctx, scratch);
    return xRegName(r);
}

std::string ARM64CodeGen::dregOf(const IRValue& v, ARM64FuncCtx& ctx, u32 scratch) {
    u32 r = loadFPR(v, ctx, scratch);
    return dRegName(r);
}

// ---------------------------------------------------------------------------
// compileFunction
// ---------------------------------------------------------------------------
void ARM64CodeGen::compileFunction(const IRFunction& fn) {
    if (fn.isExtern) {
        FnOutput fo;
        fo.name = fn.name;
        fo.isExtern = true;
        fnOutputs_.push_back(std::move(fo));
        return;
    }

    ARM64FuncCtx ctx;
    ctx.fnName = fn.name;
    allocateRegisters(fn, ctx);

    // --- Prologue ---
    ctx.emitLabel(fn.name);

    // sub sp, sp, #frameSize
    ctx.emitInst("sub sp, sp, #" + std::to_string(ctx.frameSize));
    // stp x29, x30, [sp, #(frameSize-16)]
    ctx.emitInst("stp x29, x30, [sp, #" + std::to_string(ctx.frameSize - 16) + "]");
    // add x29, sp, #(frameSize-16)
    ctx.emitInst("add x29, sp, #" + std::to_string(ctx.frameSize - 16));

    // Move arguments from x0-x7 to stack slots
    for (size_t i = 0; i < fn.params.size() && i < 8; ++i) {
        if (ctx.spillSlots.count(fn.params[i].reg)) {
            ctx.emitInst("str " + std::string(xRegName((u32)i)) + ", " + spOff(ctx.spillSlots[fn.params[i].reg]));
        }
    }

    // --- Blocks ---
    for (auto& bb : fn.blocks) {
        ctx.emitLabel(localLabel(fn.name, bb.name));
        for (auto& ins : bb.instrs) {
            emitInstr(ins, ctx);
        }
    }

    FnOutput fo;
    fo.name  = fn.name;
    fo.lines = std::move(ctx.lines);
    if (debugEnabled_) {
        for (const auto& li : ctx.lineInfos) {
            fo.lineInfos.push_back({li.offset, li.loc});
        }
    }
    fnOutputs_.push_back(std::move(fo));
}

void ARM64CodeGen::emitInstr(const IRInstr& ins, ARM64FuncCtx& ctx) {
    if (debugEnabled_) ctx.setLoc(ins.loc);
    switch (ins.op) {
        case IROpcode::Alloca:  emitAlloca(ins, ctx); break;
        case IROpcode::Load:    emitLoad(ins, ctx);   break;
        case IROpcode::Store:   emitStore(ins, ctx);  break;
        case IROpcode::Add:
        case IROpcode::Sub:
        case IROpcode::Mul:
        case IROpcode::Div:
        case IROpcode::UDiv:
        case IROpcode::Mod:
        case IROpcode::UMod:
        case IROpcode::And:
        case IROpcode::Or:
        case IROpcode::Xor:     emitBinop(ins, ctx);  break;
        case IROpcode::Neg:
        case IROpcode::Not:     emitUnop(ins, ctx);   break;
        case IROpcode::IEq:
        case IROpcode::INe:
        case IROpcode::ILt:
        case IROpcode::ILe:
        case IROpcode::IGt:
        case IROpcode::IGe:     emitCmp(ins, ctx);    break;
        case IROpcode::Br:      emitBr(ins, ctx);      break;
        case IROpcode::CondBr:  emitCondBr(ins, ctx);  break;
        case IROpcode::Ret:     emitRet(ins, ctx);     break;
        case IROpcode::Call:    emitCall(ins, ctx);    break;
        case IROpcode::GEP:     emitGEP(ins, ctx); break;
        case IROpcode::Trunc:
        case IROpcode::ZExt:
        case IROpcode::SExt:
        case IROpcode::Bitcast: emitCast(ins, ctx);    break;
        default: break;
    }
}

void ARM64CodeGen::emitAlloca(const IRInstr& ins, ARM64FuncCtx& ctx) {
    u32 dst = REG_X9;
    i32 off = ctx.allocaOffsets[ins.dst.id];
    ctx.emitInst("add " + std::string(xRegName(dst)) + ", sp, #" + std::to_string(off));
    ctx.emitInst("str " + std::string(xRegName(dst)) + ", " + spOff(ctx.spillSlots[ins.dst.id]));
}

void ARM64CodeGen::emitLoad(const IRInstr& ins, ARM64FuncCtx& ctx) {
    u32 ptrReg = loadGPR(ins.srcs[0], ctx, REG_X9);
    u32 dstReg = REG_X10;

    u32 size = ins.opType ? ins.opType->size() : 8;
    std::string ldr = "ldr";
    std::string reg = xRegName(dstReg);
    if (size == 1) { ldr = "ldrb"; reg = wRegName(dstReg); }
    else if (size == 2) { ldr = "ldrh"; reg = wRegName(dstReg); }
    else if (size == 4) { reg = wRegName(dstReg); }

    ctx.emitInst(ldr + " " + reg + ", [" + xRegName(ptrReg) + "]");
    ctx.emitInst("str " + std::string(xRegName(dstReg)) + ", " + spOff(ctx.spillSlots[ins.dst.id]));
}

void ARM64CodeGen::emitStore(const IRInstr& ins, ARM64FuncCtx& ctx) {
    u32 valReg = loadGPR(ins.srcs[0], ctx, REG_X9);
    u32 ptrReg = loadGPR(ins.srcs[1], ctx, REG_X10);

    u32 size = ins.srcs[0].type ? ins.srcs[0].type->size() : 8;
    std::string st = "str";
    std::string reg = xRegName(valReg);
    if (size == 1) { st = "strb"; reg = wRegName(valReg); }
    else if (size == 2) { st = "strh"; reg = wRegName(valReg); }
    else if (size == 4) { reg = wRegName(valReg); }

    ctx.emitInst(st + " " + reg + ", [" + xRegName(ptrReg) + "]");
}

void ARM64CodeGen::emitBinop(const IRInstr& ins, ARM64FuncCtx& ctx) {
    u32 lhs = loadGPR(ins.srcs[0], ctx, REG_X9);
    u32 rhs = loadGPR(ins.srcs[1], ctx, REG_X10);
    u32 dst = REG_X9;

    std::string op;
    switch (ins.op) {
        case IROpcode::Add: op = "add"; break;
        case IROpcode::Sub: op = "sub"; break;
        case IROpcode::Mul: op = "mul"; break;
        case IROpcode::Div: op = "sdiv"; break;
        case IROpcode::UDiv: op = "udiv"; break;
        case IROpcode::Mod:
        case IROpcode::UMod: {
            bool isSigned = (ins.op == IROpcode::Mod);
            u32 quot = REG_X11;
            ctx.emitInst(std::string(isSigned ? "sdiv " : "udiv ") + xRegName(quot) + ", " + xRegName(lhs) + ", " + xRegName(rhs));
            ctx.emitInst("msub " + std::string(xRegName(dst)) + ", " + xRegName(quot) + ", " + xRegName(rhs) + ", " + xRegName(lhs));
            ctx.emitInst("str " + std::string(xRegName(dst)) + ", " + spOff(ctx.spillSlots[ins.dst.id]));
            return;
        }
        case IROpcode::And: op = "and"; break;
        case IROpcode::Or:  op = "orr"; break;
        case IROpcode::Xor: op = "eor"; break;
        default: return;
    }

    ctx.emitInst(op + " " + xRegName(dst) + ", " + xRegName(lhs) + ", " + xRegName(rhs));
    ctx.emitInst("str " + std::string(xRegName(dst)) + ", " + spOff(ctx.spillSlots[ins.dst.id]));
}

void ARM64CodeGen::emitUnop(const IRInstr& ins, ARM64FuncCtx& ctx) {
    u32 src = loadGPR(ins.srcs[0], ctx, REG_X9);
    u32 dst = REG_X10;

    if (ins.op == IROpcode::Neg) {
        ctx.emitInst("neg " + std::string(xRegName(dst)) + ", " + xRegName(src));
    } else {
        ctx.emitInst("mvn " + std::string(xRegName(dst)) + ", " + xRegName(src));
    }
    ctx.emitInst("str " + std::string(xRegName(dst)) + ", " + spOff(ctx.spillSlots[ins.dst.id]));
}

void ARM64CodeGen::emitCmp(const IRInstr& ins, ARM64FuncCtx& ctx) {
    u32 lhs = loadGPR(ins.srcs[0], ctx, REG_X9);
    u32 rhs = loadGPR(ins.srcs[1], ctx, REG_X10);
    ctx.emitInst("cmp " + std::string(xRegName(lhs)) + ", " + xRegName(rhs));

    std::string cond;
    switch (ins.op) {
        case IROpcode::IEq:  cond = "eq"; break;
        case IROpcode::INe:  cond = "ne"; break;
        case IROpcode::ILt:  cond = "lt"; break;
        case IROpcode::ILe:  cond = "le"; break;
        case IROpcode::IGt:  cond = "gt"; break;
        case IROpcode::IGe:  cond = "ge"; break;
        case IROpcode::IULt: cond = "lo"; break;
        case IROpcode::IULe: cond = "ls"; break;
        case IROpcode::IUGt: cond = "hi"; break;
        case IROpcode::IUGe: cond = "hs"; break;
        default: cond = "al"; break;
    }

    u32 dst = REG_X9;
    ctx.emitInst("cset " + std::string(xRegName(dst)) + ", " + cond);
    ctx.emitInst("str " + std::string(xRegName(dst)) + ", " + spOff(ctx.spillSlots[ins.dst.id]));
}

void ARM64CodeGen::emitBr(const IRInstr& ins, ARM64FuncCtx& ctx) {
    ctx.emitInst("b " + localLabel(ctx.fnName, ins.label));
}

void ARM64CodeGen::emitCondBr(const IRInstr& ins, ARM64FuncCtx& ctx) {
    u32 cond = loadGPR(ins.srcs[0], ctx, REG_X9);
    ctx.emitInst("cmp " + std::string(xRegName(cond)) + ", #0");
    ctx.emitInst("b.ne " + localLabel(ctx.fnName, ins.label));
    ctx.emitInst("b " + localLabel(ctx.fnName, ins.label2));
}

void ARM64CodeGen::emitRet(const IRInstr& ins, ARM64FuncCtx& ctx) {
    if (!ins.srcs.empty()) {
        u32 r = loadGPR(ins.srcs[0], ctx, 0); // result in x0
        if (r != 0) ctx.emitInst("mov x0, " + std::string(xRegName(r)));
    }

    // Epilogue
    // ldp x29, x30, [sp, #(frameSize-16)]
    ctx.emitInst("ldp x29, x30, [sp, #" + std::to_string(ctx.frameSize - 16) + "]");
    // add sp, sp, #frameSize
    ctx.emitInst("add sp, sp, #" + std::to_string(ctx.frameSize));
    ctx.emitInst("ret");
}

void ARM64CodeGen::emitCall(const IRInstr& ins, ARM64FuncCtx& ctx) {
    // AAPCS64: x0-x7 for args.
    for (size_t i = 1; i < ins.srcs.size() && i <= 8; ++i) {
        u32 r = loadGPR(ins.srcs[i], ctx, (u32)(i - 1));
        if (r != (u32)(i - 1))
            ctx.emitInst("mov " + std::string(xRegName((u32)(i - 1))) + ", " + xRegName(r));
    }

    if (ins.srcs[0].kind == IRValueKind::Global) {
        std::string sym = ins.srcs[0].name;
        if (isMachO_) sym = "_" + sym;
        ctx.emitInst("bl " + sym);
    } else {
        u32 fnReg = loadGPR(ins.srcs[0], ctx, REG_X9);
        ctx.emitInst("blr " + std::string(xRegName(fnReg)));
    }

    if (ins.dst.kind == IRValueKind::Register) {
        ctx.emitInst("str x0, " + spOff(ctx.spillSlots[ins.dst.id]));
    }
}

void ARM64CodeGen::emitGEP(const IRInstr& ins, ARM64FuncCtx& ctx) {
    // Extremely simplified GEP
    u32 base = loadGPR(ins.srcs[0], ctx, REG_X9);
    u32 off  = loadGPR(ins.srcs[1], ctx, REG_X10);
    u32 dst  = REG_X9;

    ctx.emitInst("add " + std::string(xRegName(dst)) + ", " + xRegName(base) + ", " + xRegName(off));
    ctx.emitInst("str " + std::string(xRegName(dst)) + ", " + spOff(ctx.spillSlots[ins.dst.id]));
}

void ARM64CodeGen::emitCast(const IRInstr& ins, ARM64FuncCtx& ctx) {
    u32 src = loadGPR(ins.srcs[0], ctx, REG_X9);
    u32 dst = REG_X9;
    // Just a move for now
    ctx.emitInst("mov " + std::string(xRegName(dst)) + ", " + xRegName(src));
    ctx.emitInst("str " + std::string(xRegName(dst)) + ", " + spOff(ctx.spillSlots[ins.dst.id]));
}

// ---------------------------------------------------------------------------
// compileGlobal
// ---------------------------------------------------------------------------
void ARM64CodeGen::compileGlobal(const IRGlobal& g) {
    GlobOutput go;
    go.name          = g.name;
    go.initData      = g.initData;
    go.stringInit    = g.stringInit;
    go.hasStringInit = g.hasStringInit;
    go.symbolInits   = g.symbolInits;
    go.isZeroInit    = g.isZeroInit;
    go.isConst       = g.isConst;
    go.isExtern      = g.isExtern;
    go.size          = g.type ? g.type->size() : 0;
    go.align         = g.align > 0 ? g.align : (g.type ? g.type->align() : 1);
    globOutputs_.push_back(std::move(go));
}

// ---------------------------------------------------------------------------
// compile
// ---------------------------------------------------------------------------
void ARM64CodeGen::compile(const IRModule& mod) {
    for (auto& fn : mod.functions) {
        compileFunction(fn);
    }
    for (auto& g  : mod.globals) {
        compileGlobal(g);
    }
}

// ---------------------------------------------------------------------------
// emitAssembly — GAS-compatible AArch64 syntax
// ---------------------------------------------------------------------------
void ARM64CodeGen::emitAssembly(FILE* out) {
    fprintf(out, "// qc ARM64 assembly (AAPCS64, GAS syntax)\n\n");

    // .data
    bool hasData = false;
    for (auto& g : globOutputs_) {
        if (!g.isZeroInit && !g.isConst && (!g.initData.empty() || g.hasStringInit)) {
            if (!hasData) {
                if (isMachO_) fprintf(out, "\n.section __DATA,__data\n");
                else          fprintf(out, "\n.section .data\n");
                hasData = true;
            }
            std::string symName = g.name;
            if (isMachO_) symName = "_" + symName;
            fprintf(out, ".global %s\n%s:\n", symName.c_str(), symName.c_str());
            if (g.hasStringInit) {
                std::string s = g.stringInit;
                if (s.size() >= 2 && s.front() == '"' && s.back() == '"') s = s.substr(1, s.size() - 2);
                fprintf(out, "    .ascii \"%s\"\n    .byte 0\n", s.c_str());
            } else {
                fprintf(out, "    .byte");
                for (size_t i = 0; i < g.initData.size(); ++i)
                    fprintf(out, "%s%u", (i?",":" "), g.initData[i]);
                fprintf(out, "\n");
            }
        }
    }

    // .bss
    bool hasBss = false;
    for (auto& g : globOutputs_) {
        if (g.isZeroInit) {
            if (!hasBss) {
                if (isMachO_) fprintf(out, "\n.section __DATA,__bss\n");
                else          fprintf(out, "\n.section .bss\n");
                hasBss = true;
            }
            std::string symName = g.name;
            if (isMachO_) symName = "_" + symName;
            fprintf(out, ".global %s\n%s:\n    .zero %u\n", symName.c_str(), symName.c_str(), g.size);
        }
    }

    // .rodata
    bool hasRodata = false;
    for (auto& g : globOutputs_) {
        if (g.isConst && !g.isZeroInit && (!g.initData.empty() || g.hasStringInit || !g.symbolInits.empty())) {
            if (!hasRodata) {
                if (isMachO_) fprintf(out, "\n.section __TEXT,__const\n");
                else          fprintf(out, "\n.section .rodata\n");
                hasRodata = true;
            }
            std::string symName = g.name;
            if (isMachO_) symName = "_" + symName;
            fprintf(out, ".global %s\n%s:\n", symName.c_str(), symName.c_str());
            if (g.hasStringInit) {
                std::string s = g.stringInit;
                if (s.size() >= 2 && s.front() == '"' && s.back() == '"') s = s.substr(1, s.size() - 2);
                fprintf(out, "    .ascii \"%s\"\n    .byte 0\n", s.c_str());
            } else if (!g.symbolInits.empty()) {
                for (const auto& sym : g.symbolInits) {
                    std::string s = sym;
                    if (isMachO_) s = "_" + s;
                    fprintf(out, "    .quad %s\n", s.c_str());
                }
            } else {
                fprintf(out, "    .byte");
                for (size_t i = 0; i < g.initData.size(); ++i)
                    fprintf(out, "%s%u", (i?",":" "), g.initData[i]);
                fprintf(out, "\n");
            }
        }
    }

    // .text
    if (isMachO_) fprintf(out, "\n.section __TEXT,__text\n\n");
    else          fprintf(out, "\n.section .text\n\n");

    for (auto& fo : fnOutputs_) {
        if (fo.isExtern) {
            continue;
        }
        std::string symName = fo.name;
        if (isMachO_) symName = "_" + symName;
        
        fprintf(out, ".global %s\n", symName.c_str());
        if (!isMachO_) fprintf(out, ".type %s, %%function\n", symName.c_str());
        
        for (auto& line : fo.lines) {
            std::string l = line;
            if (isMachO_ && !l.empty() && l.back() == ':') {
                // Label
                std::string lbl = l.substr(0, l.size() - 1);
                // If it's the function name, add underscore
                if (lbl == fo.name) lbl = "_" + lbl;
                fprintf(out, "%s:\n", lbl.c_str());
            } else if (!l.empty() && l.back() == ':') {
                fprintf(out, "%s\n", l.c_str());
            } else {
                fprintf(out, "    %s\n", l.c_str());
            }
        }
        if (!isMachO_) fprintf(out, ".size %s, .-%s\n\n", symName.c_str(), symName.c_str());
    }

    // .debug_line
    if (debugEnabled_) {
        DWARFLineProgram lineProg;
        std::unordered_map<std::string, u32> fileMap;
        std::unordered_map<std::string, u64> fnOffsets;
        std::unordered_map<std::string, u64> fnSizes;

        u64 currentTextOff = 0;
        for (auto& fo : fnOutputs_) {
            if (fo.isExtern) continue;
            fnOffsets[fo.name] = currentTextOff;
            u64 size = 0;
            for (auto& line : fo.lines) size += line.length() + 1;
            fnSizes[fo.name] = size;
            currentTextOff += size;
        }

        for (auto& fo : fnOutputs_) {
            if (fo.isExtern) continue;
            u64 fnOff = fnOffsets[fo.name];

            for (auto& li : fo.lineInfos) {
                if (!li.loc.file) continue;
                u32 fIdx;
                std::string filename = li.loc.file;
                if (fileMap.count(filename)) {
                    fIdx = fileMap[filename];
                } else {
                    fIdx = lineProg.addFile(filename, 0);
                    fileMap[filename] = fIdx;
                }

                DWARFLineEntry entry;
                entry.address = fnOff + li.offset;
                entry.line = li.loc.line;
                entry.column = li.loc.column;
                entry.fileIndex = fIdx;
                entry.isStmt = true;
                entry.basicBlock = false;
                entry.endSequence = false;
                lineProg.addEntry(entry);
            }

            DWARFLineEntry endEntry;
            endEntry.address = fnOff + fnSizes[fo.name];
            endEntry.endSequence = true;
            lineProg.addEntry(endEntry);
        }

        auto bytes = lineProg.emit();
        if (isMachO_) fprintf(out, "\n.section __DWARF,__debug_line,regular,debug\n");
        else          fprintf(out, "\n.section .debug_line\n");
        
        for (size_t i = 0; i < bytes.size(); ++i) {
            if (i % 16 == 0) fprintf(out, "\n    .byte ");
            fprintf(out, "%s%u", (i % 16 == 0 ? "" : ","), bytes[i]);
        }
        fprintf(out, "\n");

        // .debug_info
        DWARFDebugInfo info;
        std::string cuName = "test.c";
        for (auto& fo : fnOutputs_) {
            if (!fo.lineInfos.empty() && fo.lineInfos[0].loc.file) {
                cuName = fo.lineInfos[0].loc.file;
                break;
            }
        }
        info.addCU(cuName, "qc version 0.1.0", 0);
        auto infoBytes = info.emit();
        if (isMachO_) fprintf(out, "\n.section __DWARF,__debug_info,regular,debug\n");
        else          fprintf(out, "\n.section .debug_info\n");
        for (size_t i = 0; i < infoBytes.size(); ++i) {
            if (i % 16 == 0) fprintf(out, "\n    .byte ");
            fprintf(out, "%s%u", (i % 16 == 0 ? "" : ","), infoBytes[i]);
        }
        fprintf(out, "\n");

        // .debug_abbrev
        DWARFAbbrev abbrev;
        auto abbrevBytes = abbrev.emit();
        if (isMachO_) fprintf(out, "\n.section __DWARF,__debug_abbrev,regular,debug\n");
        else          fprintf(out, "\n.section .debug_abbrev\n");
        for (size_t i = 0; i < abbrevBytes.size(); ++i) {
            if (i % 16 == 0) fprintf(out, "\n    .byte ");
            fprintf(out, "%s%u", (i % 16 == 0 ? "" : ","), abbrevBytes[i]);
        }
        fprintf(out, "\n");
    }
}

// ---------------------------------------------------------------------------
// emitObject — ELF or PE/COFF
// ---------------------------------------------------------------------------
std::vector<u8> ARM64CodeGen::emitObject() {
    bool useELF = (target_.format == TargetFormat::ELF);

    if (useELF) {
        ELFWriter elf(TargetArch::ARM64);
        ELFSection& text   = elf.textSection();
        ELFSection& data   = elf.dataSection();
        ELFSection& bss    = elf.bssSection();
        ELFSection& rodata = elf.rodataSection();

        for (auto& fo : fnOutputs_) {
            if (fo.isExtern) {
                ELFSymbol sym;
                sym.name       = fo.name;
                sym.binding    = STB_GLOBAL;
                sym.type       = STT_FUNC;
                sym.isExternal = true;
                elf.addSymbol(sym);
                continue;
            }

            u64 fnOff = text.data.size();
            for (auto& line : fo.lines) {
                std::string l = line + "\n";
                text.data.insert(text.data.end(), l.begin(), l.end());
            }
            u64 fnSize = text.data.size() - fnOff;

            ELFSymbol sym;
            sym.name         = fo.name;
            sym.value        = fnOff;
            sym.size         = fnSize;
            sym.binding      = STB_GLOBAL;
            sym.type         = STT_FUNC;
            sym.sectionIndex = text.shIndex;
            sym.isExternal   = false;
            elf.addSymbol(sym);
        }

        // --- .debug_line ---
        if (debugEnabled_) {
            DWARFLineProgram lineProg;
            std::unordered_map<std::string, u32> fileMap;

            std::unordered_map<std::string, u64> fnOffsets;
            std::unordered_map<std::string, u64> fnSizes;

            u64 currentTextOff = 0;
            for (auto& fo : fnOutputs_) {
                if (fo.isExtern) continue;
                fnOffsets[fo.name] = currentTextOff;
                u64 size = 0;
                for (auto& line : fo.lines) size += line.length() + 1;
                fnSizes[fo.name] = size;
                currentTextOff += size;
            }

            for (auto& fo : fnOutputs_) {
                if (fo.isExtern) continue;
                u64 fnOff = fnOffsets[fo.name];

                for (auto& li : fo.lineInfos) {
                    if (!li.loc.file) continue;
                    u32 fIdx;
                    std::string filename = li.loc.file;
                    if (fileMap.count(filename)) {
                        fIdx = fileMap[filename];
                    } else {
                        fIdx = lineProg.addFile(filename, 0);
                        fileMap[filename] = fIdx;
                    }

                    DWARFLineEntry entry;
                    entry.address = fnOff + li.offset;
                    entry.line = li.loc.line;
                    entry.column = li.loc.column;
                    entry.fileIndex = fIdx;
                    entry.isStmt = true;
                    entry.basicBlock = false;
                    entry.endSequence = false;
                    lineProg.addEntry(entry);
                }

                DWARFLineEntry endEntry;
                endEntry.address = fnOff + fnSizes[fo.name];
                endEntry.endSequence = true;
                lineProg.addEntry(endEntry);
            }
            elf.debugLineSection().data = lineProg.emit();
        }

        for (auto& go : globOutputs_) {
            if (go.isExtern) {
                ELFSymbol sym;
                sym.name       = go.name;
                sym.binding    = STB_GLOBAL;
                sym.type       = STT_OBJECT;
                sym.isExternal = true;
                elf.addSymbol(sym);
                continue;
            }

            u64 off = 0;
            ELFSection* sec = nullptr;

            if (go.isZeroInit) sec = &bss;
            else if (go.isConst) sec = &rodata;
            else sec = &data;

            if (sec) {
                u32 align = go.align > 0 ? go.align : 1;
                while (sec->data.size() % align != 0) sec->data.push_back(0);
                off = sec->data.size();
            }

            if (go.isZeroInit) {
                bss.data.resize(bss.data.size() + go.size, 0);
            } else if (go.isConst) {
                if (go.hasStringInit) {
                    rodata.data.insert(rodata.data.end(), go.stringInit.begin(), go.stringInit.end());
                    rodata.data.push_back(0);
                } else {
                    rodata.data.insert(rodata.data.end(), go.initData.begin(), go.initData.end());
                }
            } else {
                if (go.hasStringInit) {
                    data.data.insert(data.data.end(), go.stringInit.begin(), go.stringInit.end());
                    data.data.push_back(0);
                } else if (!go.initData.empty()) {
                    data.data.insert(data.data.end(), go.initData.begin(), go.initData.end());
                } else {
                    data.data.resize(data.data.size() + go.size, 0);
                }
            }
            (void)sec;

            ELFSymbol sym;
            sym.name         = go.name;
            sym.value        = off;
            sym.size         = go.size;
            sym.binding      = STB_GLOBAL;
            sym.type         = STT_OBJECT;
            sym.sectionIndex = (go.isConst ? rodata.shIndex : (go.isZeroInit ? bss.shIndex : data.shIndex));
            sym.isExternal   = false;
            elf.addSymbol(sym);
        }

        return elf.emit();

    } else {
        // PE/COFF for Windows ARM64
        PEWriter pe(TargetArch::ARM64);
        PESection& text  = pe.textSection();
        PESection& data  = pe.dataSection();
        PESection& bss   = pe.bssSection();
        PESection& rdata = pe.rdataSection();

        for (auto& fo : fnOutputs_) {
            if (fo.isExtern) {
                PESymbol sym;
                sym.name          = fo.name;
                sym.sectionNumber = IMAGE_SYM_UNDEFINED;
                sym.storageClass  = IMAGE_SYM_CLASS_EXTERNAL;
                sym.isExternal    = true;
                pe.addSymbol(sym);
                continue;
            }

            u32 fnOff = (u32)text.data.size();
            for (auto& line : fo.lines) {
                std::string l = line + "\n";
                text.data.insert(text.data.end(), l.begin(), l.end());
            }

            PESymbol sym;
            sym.name          = fo.name;
            sym.value         = fnOff;
            sym.sectionNumber = 1; // .text
            sym.storageClass  = IMAGE_SYM_CLASS_EXTERNAL;
            sym.isExternal    = false;
            pe.addSymbol(sym);
        }

        for (auto& go : globOutputs_) {
            if (go.isExtern) {
                PESymbol sym;
                sym.name          = go.name;
                sym.sectionNumber = IMAGE_SYM_UNDEFINED;
                sym.storageClass  = IMAGE_SYM_CLASS_EXTERNAL;
                sym.isExternal    = true;
                pe.addSymbol(sym);
                continue;
            }

            u32 off = 0;
            i16 secNum = 0;

            if (go.isZeroInit) {
                secNum = 3; // .bss
                off = (u32)bss.data.size();
                bss.data.resize(bss.data.size() + go.size, 0);
            } else if (go.isConst) {
                secNum = 4; // .rdata
                off = (u32)rdata.data.size();
                if (go.hasStringInit) {
                    rdata.data.insert(rdata.data.end(), go.stringInit.begin(), go.stringInit.end());
                    rdata.data.push_back(0);
                } else {
                    rdata.data.insert(rdata.data.end(), go.initData.begin(), go.initData.end());
                }
            } else {
                secNum = 2; // .data
                off = (u32)data.data.size();
                if (go.hasStringInit) {
                    data.data.insert(data.data.end(), go.stringInit.begin(), go.stringInit.end());
                    data.data.push_back(0);
                } else if (!go.initData.empty()) {
                    data.data.insert(data.data.end(), go.initData.begin(), go.initData.end());
                } else {
                    data.data.resize(data.data.size() + go.size, 0);
                }
            }

            PESymbol sym;
            sym.name          = go.name;
            sym.value         = off;
            sym.sectionNumber = secNum;
            sym.storageClass  = IMAGE_SYM_CLASS_EXTERNAL;
            pe.addSymbol(sym);
        }

        return pe.emit();
    }
}

// ---------------------------------------------------------------------------
// CodeGen::create — unified factory for all backends
//
// This is the single definition of CodeGen::create(). x64_codegen.cpp does
// NOT define CodeGen::create() when both files are compiled together
// (its create() is guarded by QC_X64_STANDALONE). The X64 backend is
// instantiated via makeX64CodeGen(), defined in x64_codegen.cpp, which
// avoids requiring the full X64CodeGen class definition here.
// ---------------------------------------------------------------------------

// Declared in x64_codegen.cpp — creates an X64CodeGen without requiring
// the class definition to be visible here.
std::unique_ptr<CodeGen> makeX64CodeGen(const TargetInfo& target, DiagEngine& diag);

std::unique_ptr<CodeGen> CodeGen::create(const TargetInfo& target, DiagEngine& diag) {
    switch (target.arch) {
        case TargetArch::X64:
            return makeX64CodeGen(target, diag);
        case TargetArch::ARM64:
            return std::make_unique<ARM64CodeGen>(target, diag);
        default:
            return nullptr;
    }
}

} // namespace qc
