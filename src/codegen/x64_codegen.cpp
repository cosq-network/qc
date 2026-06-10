// x64_codegen.cpp — x86-64 (System V AMD64 ABI) backend for qc
// Implements X64CodeGen : public CodeGen
// C++17

#include "qc/codegen.h"
#include "qc/ir.h"
#include "qc/elf_writer.h"
#include "qc/pe_writer.h"
#include "qc/dwarf.h"
#include "qc/type.h"

#include <cassert>
#include <cstdio>
#include <cstring>
#include <algorithm>
#include <sstream>
#include <string>
#include <vector>
#include <unordered_map>
#include <stdexcept>

namespace qc {

// ---------------------------------------------------------------------------
// Physical register encoding (Intel / System V AMD64)
//   rax=0, rcx=1, rdx=2, rbx=3, rsp=4, rbp=5, rsi=6, rdi=7
//   r8=8 … r15=15
//   xmm0=16 … xmm15=31
// ---------------------------------------------------------------------------
static const char* gprName64(u32 r) {
    static const char* names[] = {
        "rax","rcx","rdx","rbx","rsp","rbp","rsi","rdi",
        "r8","r9","r10","r11","r12","r13","r14","r15"
    };
    if (r < 16) return names[r];
    return "?gpr?";
}
static const char* gprName32(u32 r) {
    static const char* names[] = {
        "eax","ecx","edx","ebx","esp","ebp","esi","edi",
        "r8d","r9d","r10d","r11d","r12d","r13d","r14d","r15d"
    };
    if (r < 16) return names[r];
    return "?gpr32?";
}
static const char* gprName8(u32 r) {
    static const char* names[] = {
        "al","cl","dl","bl","spl","bpl","sil","dil",
        "r8b","r9b","r10b","r11b","r12b","r13b","r14b","r15b"
    };
    if (r < 16) return names[r];
    return "?gpr8?";
}
static const char* xmmName(u32 r) {
    static const char* names[] = {
        "xmm0","xmm1","xmm2","xmm3","xmm4","xmm5","xmm6","xmm7",
        "xmm8","xmm9","xmm10","xmm11","xmm12","xmm13","xmm14","xmm15"
    };
    if (r >= 16 && r < 32) return names[r - 16];
    return "?xmm?";
}
static bool isXmm(u32 r) { return r >= 16 && r < 32; }

// Physical register indices
static constexpr u32 REG_RAX = 0;
static constexpr u32 REG_RCX = 1;
static constexpr u32 REG_RDX = 2;
static constexpr u32 REG_RBX = 3;
[[maybe_unused]] static constexpr u32 REG_RSP = 4;
static constexpr u32 REG_RBP = 5;
static constexpr u32 REG_RSI = 6;
static constexpr u32 REG_RDI = 7;
static constexpr u32 REG_R8  = 8;
static constexpr u32 REG_R9  = 9;
static constexpr u32 REG_R10 = 10;
static constexpr u32 REG_R11 = 11;
static constexpr u32 REG_R12 = 12;
static constexpr u32 REG_R13 = 13;
static constexpr u32 REG_R14 = 14;
static constexpr u32 REG_R15 = 15;
[[maybe_unused]] static constexpr u32 REG_XMM0 = 16;

// System V AMD64 integer argument registers (in order)
static const u32 kIntArgRegs[] = { REG_RDI, REG_RSI, REG_RDX, REG_RCX, REG_R8, REG_R9 };
static const u32 kFltArgRegs[] = { 16,17,18,19,20,21,22,23 }; // xmm0-xmm7
static constexpr u32 kNumIntArgRegs = 6;
static constexpr u32 kNumFltArgRegs = 8;

// Caller-saved (volatile) GPRs: rax rcx rdx rsi rdi r8 r9 r10 r11
// (Retained for documentation; not iterated at runtime.)
[[maybe_unused]] static const u32 kCallerSaved[] = { REG_RAX,REG_RCX,REG_RDX,REG_RSI,REG_RDI,
                                                      REG_R8,REG_R9,REG_R10,REG_R11 };
// Callee-saved GPRs: rbx r12 r13 r14 r15  (rbp handled by frame, rsp fixed)
static const u32 kCalleeSaved[] = { REG_RBX,REG_R12,REG_R13,REG_R14,REG_R15 };
// Allocatable GPRs in priority order (caller-saved first to minimise push/pop)
static const u32 kAllocGPR[] = {
    REG_RAX, REG_RCX, REG_RDX, REG_RSI, REG_RDI,
    REG_R8,  REG_R9,  REG_R10, REG_R11,
    REG_RBX, REG_R12, REG_R13, REG_R14, REG_R15
};
static constexpr u32 kNumAllocGPR = 14;
// Allocatable XMM regs: xmm0-xmm15 (indices 16-31)
static const u32 kAllocXMM[] = {16,17,18,19,20,21,22,23,24,25,26,27,28,29,30,31};
static constexpr u32 kNumAllocXMM = 16;

// ---------------------------------------------------------------------------
// Helper: is a type a floating-point type?
// ---------------------------------------------------------------------------
static bool typeIsFloat(const Type* t) {
    if (!t) return false;
    switch (t->kind()) {
        case TypeKind::Float:
        case TypeKind::Double:
        case TypeKind::LongDouble:
            return true;
        default: return false;
    }
}

// ---------------------------------------------------------------------------
// Per-function code-generation context
// ---------------------------------------------------------------------------
struct X64LineInfo {
    u64 offset;
    SourceLocation loc;
};

struct X64FuncCtx {
    // register allocation
    std::unordered_map<u64, u32> virToPhys;  // virtual reg → physical reg
    std::unordered_map<u64, i32> spillSlots; // virtual reg → rbp-relative offset
    i32  nextSpillOff  = -8;   // next stack slot (grows down)
    i32  frameSize     = 0;    // total frame size (set after allocation)
    bool usedCallee[16] = {};  // which callee-saved regs we clobbered

    // assembly text lines for this function
    std::vector<std::string> lines;
    std::vector<X64LineInfo> lineInfos;
    u64                      currentOffset = 0;
    SourceLocation           lastLoc;

    // current label for temp use
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
// X64CodeGen class
// ---------------------------------------------------------------------------
class X64CodeGen : public CodeGen {
public:
    explicit X64CodeGen(const TargetInfo& target, DiagEngine& diag)
        : target_(target), diag_(diag) {}

    void compile(const IRModule& mod) override;
    void emitAssembly(FILE* out) override;
    std::vector<u8> emitObject() override;

private:
    // Per-function helpers
    void compileFunction(const IRFunction& fn);
    void compileGlobal(const IRGlobal& g);

    // Register allocator
    void allocateRegisters(const IRFunction& fn, X64FuncCtx& ctx);
    // Get the physical reg for a virtual reg, loading from spill if needed
    u32  resolveReg(u64 virt, X64FuncCtx& ctx, bool loadIfSpilled = true);
    // Emit a load from a spill slot into a scratch reg and return it
    u32  loadSpill(u64 virt, X64FuncCtx& ctx, u32 scratch = REG_RAX);
    // Emit a store to a spill slot
    void storeSpill(u64 virt, u32 physReg, X64FuncCtx& ctx);

    // Instruction emission
    void emitInstr(const IRInstr& ins, X64FuncCtx& ctx);
    void emitAlloca(const IRInstr& ins, X64FuncCtx& ctx);
    void emitLoad(const IRInstr& ins, X64FuncCtx& ctx);
    void emitStore(const IRInstr& ins, X64FuncCtx& ctx);
    void emitBinop(const IRInstr& ins, X64FuncCtx& ctx);
    void emitShift(const IRInstr& ins, X64FuncCtx& ctx);
    void emitDiv(const IRInstr& ins, X64FuncCtx& ctx);
    void emitUnop(const IRInstr& ins, X64FuncCtx& ctx);
    void emitCmp(const IRInstr& ins, X64FuncCtx& ctx);
    void emitBr(const IRInstr& ins, X64FuncCtx& ctx);
    void emitCondBr(const IRInstr& ins, X64FuncCtx& ctx);
    void emitRet(const IRInstr& ins, X64FuncCtx& ctx);
    void emitCall(const IRInstr& ins, X64FuncCtx& ctx);
    void emitGEP(const IRInstr& ins, X64FuncCtx& ctx);
    void emitCast(const IRInstr& ins, X64FuncCtx& ctx);
    void emitFpBinop(const IRInstr& ins, X64FuncCtx& ctx);
    void emitFpCmp(const IRInstr& ins, X64FuncCtx& ctx);
    void emitFpUnop(const IRInstr& ins, X64FuncCtx& ctx);

    // Operand helpers
    std::string opStr(const IRValue& v, X64FuncCtx& ctx, bool wantXmm = false);
    std::string memStr(u32 base, i32 disp);
    std::string spillStr(i32 off);

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

public:
    void setDebugEnabled(bool e) override { debugEnabled_ = e; }
};

// ---------------------------------------------------------------------------
// RegAlloc::get (defined here for the base struct)
// ---------------------------------------------------------------------------
u32 RegAlloc::get(u64 virt) const {
    auto it = virToPhys.find(virt);
    if (it != virToPhys.end()) return it->second;
    // If spilled, caller must handle; return sentinel
    return 0xFFFFFFFF;
}

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------
static std::string localLabel(const std::string& fn, const std::string& bb) {
    return ".L_" + fn + "_" + bb;
}

std::string X64CodeGen::memStr(u32 base, i32 disp) {
    if (disp == 0)
        return std::string("[") + gprName64(base) + "]";
    if (disp > 0)
        return std::string("[") + gprName64(base) + " + " + std::to_string(disp) + "]";
    return std::string("[") + gprName64(base) + " - " + std::to_string(-disp) + "]";
}

std::string X64CodeGen::spillStr(i32 off) {
    return memStr(REG_RBP, off);
}

// ---------------------------------------------------------------------------
// Register allocation: simple linear scan over SSA virtual registers
// ---------------------------------------------------------------------------
void X64CodeGen::allocateRegisters(const IRFunction& fn, X64FuncCtx& ctx) {
    // Collect all virtual register IDs referenced in the function
    std::vector<u64> virts;
    auto addVirt = [&](const IRValue& v) {
        if (v.isReg()) {
            // deduplicate
            if (std::find(virts.begin(), virts.end(), v.id) == virts.end())
                virts.push_back(v.id);
        }
    };

    // Parameters first (so they get priority in allocation)
    for (auto& p : fn.params) {
        u64 id = p.reg;
        if (std::find(virts.begin(), virts.end(), id) == virts.end())
            virts.push_back(id);
    }

    for (auto& bb : fn.blocks) {
        for (auto& ins : bb.instrs) {
            if (ins.hasDst()) addVirt(ins.dst);
            for (auto& s : ins.srcs) addVirt(s);
        }
    }

    // Separate float vs integer virtual regs by checking first def type
    // Build a map: virtual reg id → is float?
    std::unordered_map<u64, bool> isFloat;
    for (auto& p : fn.params)
        isFloat[p.reg] = typeIsFloat(p.type.get());
    for (auto& bb : fn.blocks) {
        for (auto& ins : bb.instrs) {
            if (ins.hasDst() && ins.dst.isReg()) {
                bool f = typeIsFloat(ins.dst.type.get());
                isFloat[ins.dst.id] = f;
            }
        }
    }

    u32 gprIdx = 0;
    u32 xmmIdx = 0;

    // Assign parameters first according to calling convention
    u32 intArgIdx = 0, fltArgIdx = 0;
    for (auto& p : fn.params) {
        bool f = typeIsFloat(p.type.get());
        if (f && fltArgIdx < kNumFltArgRegs) {
            ctx.virToPhys[p.reg] = kFltArgRegs[fltArgIdx++];
        } else if (!f && intArgIdx < kNumIntArgRegs) {
            ctx.virToPhys[p.reg] = kIntArgRegs[intArgIdx++];
        } else {
            // spill the param (passed on stack beyond 6/8 args — simplification: spill)
            ctx.nextSpillOff -= 8;
            ctx.spillSlots[p.reg] = ctx.nextSpillOff;
        }
    }

    // Assign remaining virtual regs
    for (u64 vid : virts) {
        if (ctx.virToPhys.count(vid) || ctx.spillSlots.count(vid)) continue;
        bool f = isFloat.count(vid) && isFloat[vid];
        if (f) {
            if (xmmIdx < kNumAllocXMM) {
                ctx.virToPhys[vid] = kAllocXMM[xmmIdx++];
            } else {
                ctx.nextSpillOff -= 8;
                ctx.spillSlots[vid] = ctx.nextSpillOff;
            }
        } else {
            if (gprIdx < kNumAllocGPR) {
                u32 phys = kAllocGPR[gprIdx++];
                ctx.virToPhys[vid] = phys;
                // Track callee-saved usage
                for (u32 cs : kCalleeSaved)
                    if (phys == cs) ctx.usedCallee[phys] = true;
            } else {
                ctx.nextSpillOff -= 8;
                ctx.spillSlots[vid] = ctx.nextSpillOff;
            }
        }
    }

    // Round frame size to 16-byte alignment (already aligned via -8 increments when needed)
    i32 localFrame = -ctx.nextSpillOff; // positive value
    // Add 8 bytes for Alloca slots computed later — we do a second pass
    // Count alloca instructions to add their sizes
    i32 allocaBytes = 0;
    for (auto& bb : fn.blocks)
        for (auto& ins : bb.instrs)
            if (ins.op == IROpcode::Alloca && ins.opType)
                allocaBytes += (i32)((ins.opType->size() + 7) & ~7u);

    localFrame += allocaBytes;
    // Align to 16 bytes: after push rbp we need rsp % 16 == 0 before call
    // push rbp makes offset 8, so we need localFrame % 16 == 0
    if (localFrame % 16 != 0)
        localFrame = (localFrame + 15) & ~15;
    if (localFrame == 0) localFrame = 0; // allow zero frame
    ctx.frameSize = localFrame;
}

// ---------------------------------------------------------------------------
// Resolve a virtual register to its physical reg, emitting a load if spilled.
// If spilled and loadIfSpilled, uses REG_RAX as scratch.
// ---------------------------------------------------------------------------
u32 X64CodeGen::resolveReg(u64 virt, X64FuncCtx& ctx, bool loadIfSpilled) {
    auto it = ctx.virToPhys.find(virt);
    if (it != ctx.virToPhys.end()) return it->second;
    auto sit = ctx.spillSlots.find(virt);
    if (sit != ctx.spillSlots.end()) {
        if (loadIfSpilled) {
            i32 off = sit->second;
            ctx.emitInst(std::string("mov ") + gprName64(REG_RAX) + ", " + spillStr(off));
            return REG_RAX;
        }
    }
    return REG_RAX; // fallback
}

u32 X64CodeGen::loadSpill(u64 virt, X64FuncCtx& ctx, u32 scratch) {
    auto sit = ctx.spillSlots.find(virt);
    if (sit != ctx.spillSlots.end()) {
        ctx.emitInst(std::string("mov ") + gprName64(scratch) + ", " + spillStr(sit->second));
        return scratch;
    }
    return resolveReg(virt, ctx, false);
}

void X64CodeGen::storeSpill(u64 virt, u32 physReg, X64FuncCtx& ctx) {
    auto sit = ctx.spillSlots.find(virt);
    if (sit != ctx.spillSlots.end()) {
        ctx.emitInst(std::string("mov ") + spillStr(sit->second) + ", " + gprName64(physReg));
    }
}

// ---------------------------------------------------------------------------
// Operand string for assembly output
// ---------------------------------------------------------------------------
std::string X64CodeGen::opStr(const IRValue& v, X64FuncCtx& ctx, bool wantXmm) {
    switch (v.kind) {
        case IRValueKind::Constant:
            return std::to_string((i64)v.id);
        case IRValueKind::FConst:
            // float constants need to go through memory; return hex encoding inline
            // (simplified: just cast to uint64 representation)
            {
                double d = v.fval;
                u64 bits;
                std::memcpy(&bits, &d, 8);
                return std::to_string(bits);
            }
        case IRValueKind::Register: {
            auto it = ctx.virToPhys.find(v.id);
            if (it != ctx.virToPhys.end()) {
                u32 r = it->second;
                return isXmm(r) ? xmmName(r) : gprName64(r);
            }
            auto sit = ctx.spillSlots.find(v.id);
            if (sit != ctx.spillSlots.end())
                return spillStr(sit->second);
            return "?vreg?";
        }
        case IRValueKind::Global:
            return v.name + "[rip]";
        case IRValueKind::Label:
            return localLabel(ctx.fnName, v.name);
        default:
            return "0";
    }
}

// ---------------------------------------------------------------------------
// compileFunction
// ---------------------------------------------------------------------------
void X64CodeGen::compileFunction(const IRFunction& fn) {
    if (fn.isExtern) {
        FnOutput fo;
        fo.name = fn.name;
        fo.isExtern = true;
        fnOutputs_.push_back(std::move(fo));
        return;
    }

    X64FuncCtx ctx;
    ctx.fnName = fn.name;
    allocateRegisters(fn, ctx);

    // --- Prologue ---
    ctx.emitLabel(fn.name);
    ctx.emitInst("push rbp");
    ctx.emitInst("mov rbp, rsp");
    if (ctx.frameSize > 0)
        ctx.emitInst("sub rsp, " + std::to_string(ctx.frameSize));

    // Save callee-saved registers we use
    for (u32 cs : kCalleeSaved) {
        if (ctx.usedCallee[cs])
            ctx.emitInst(std::string("push ") + gprName64(cs));
    }

    // Compute alloca offsets: we lay them out from rbp downwards
    // starting at nextSpillOff (which is already negative)
    // Re-compute: spill slots were assigned during allocateRegisters,
    // alloca slots start from nextSpillOff continuing downward.
    // We track the alloca offset counter separately.
    i32 allocaOff = ctx.nextSpillOff; // already accounts for spills

    // --- Emit basic blocks ---
    for (auto& bb : fn.blocks) {
        ctx.emitLabel(localLabel(fn.name, bb.name));
        for (auto& ins : bb.instrs) {
            // Handle alloca offset assignment here
            if (ins.op == IROpcode::Alloca && ins.hasDst()) {
                u32 sz = ins.opType ? ((ins.opType->size() + 7) & ~7u) : 8u;
                allocaOff -= (i32)sz;
                // If dst already has a phys reg (from register allocation),
                // load the address into it; otherwise store the pointer itself
                // into a new spill slot.
                if (ctx.virToPhys.count(ins.dst.id)) {
                    u32 destReg = ctx.virToPhys[ins.dst.id];
                    ctx.emitInst(std::string("lea ") + gprName64(destReg) + ", " + spillStr(allocaOff));
                } else {
                    // Allocate a new spill slot for the pointer value
                    i32 ptrSlot = allocaOff - 8;
                    allocaOff  = ptrSlot;
                    ctx.spillSlots[ins.dst.id] = ptrSlot;
                    ctx.emitInst(std::string("lea rax, ") + spillStr(allocaOff + 8));
                    ctx.emitInst(std::string("mov ") + spillStr(ptrSlot) + ", rax");
                }
                continue;
            }
            emitInstr(ins, ctx);
        }
    }

    // --- Epilogue ---
    ctx.emitLabel(".L_" + fn.name + "_epilogue");
    // Restore callee-saved in reverse
    for (int i = (int)(sizeof(kCalleeSaved)/sizeof(kCalleeSaved[0])) - 1; i >= 0; --i) {
        if (ctx.usedCallee[kCalleeSaved[i]])
            ctx.emitInst(std::string("pop ") + gprName64(kCalleeSaved[i]));
    }
    if (ctx.frameSize > 0)
        ctx.emitInst("add rsp, " + std::to_string(ctx.frameSize));
    ctx.emitInst("pop rbp");
    ctx.emitInst("ret");

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

// ---------------------------------------------------------------------------
// Instruction emission dispatch
// ---------------------------------------------------------------------------
void X64CodeGen::emitInstr(const IRInstr& ins, X64FuncCtx& ctx) {
    if (debugEnabled_) ctx.setLoc(ins.loc);
    switch (ins.op) {
        case IROpcode::Alloca:    emitAlloca(ins, ctx); break;
        case IROpcode::Load:      emitLoad(ins, ctx);   break;
        case IROpcode::Store:     emitStore(ins, ctx);  break;
        case IROpcode::Add:
        case IROpcode::Sub:
        case IROpcode::And:
        case IROpcode::Or:
        case IROpcode::Xor:
        case IROpcode::Mul:       emitBinop(ins, ctx);  break;
        case IROpcode::Shl:
        case IROpcode::Shr:
        case IROpcode::AShr:      emitShift(ins, ctx);  break;
        case IROpcode::Div:
        case IROpcode::Mod:       emitDiv(ins, ctx);    break;
        case IROpcode::Neg:
        case IROpcode::Not:
        case IROpcode::FNeg:      emitUnop(ins, ctx);   break;
        case IROpcode::IEq:  case IROpcode::INe:
        case IROpcode::ILt:  case IROpcode::ILe:
        case IROpcode::IGt:  case IROpcode::IGe:
        case IROpcode::IULt: case IROpcode::IULe:
        case IROpcode::IUGt: case IROpcode::IUGe: emitCmp(ins, ctx); break;
        case IROpcode::FEq:  case IROpcode::FNe:
        case IROpcode::FLt:  case IROpcode::FLe:
        case IROpcode::FGt:  case IROpcode::FGe:  emitFpCmp(ins, ctx); break;
        case IROpcode::FAdd:
        case IROpcode::FSub:
        case IROpcode::FMul:
        case IROpcode::FDiv:      emitFpBinop(ins, ctx); break;
        case IROpcode::Br:        emitBr(ins, ctx);    break;
        case IROpcode::CondBr:    emitCondBr(ins, ctx);break;
        case IROpcode::Ret:       emitRet(ins, ctx); break;
        case IROpcode::Call:      emitCall(ins, ctx);  break;
        case IROpcode::GEP:       emitGEP(ins, ctx);   break;
        case IROpcode::Trunc:
        case IROpcode::ZExt:
        case IROpcode::SExt:
        case IROpcode::FPTrunc:
        case IROpcode::FPExt:
        case IROpcode::FPToSI:
        case IROpcode::FPToUI:
        case IROpcode::SIToFP:
        case IROpcode::UIToFP:
        case IROpcode::PtrToInt:
        case IROpcode::IntToPtr:
        case IROpcode::Bitcast:   emitCast(ins, ctx);  break;
        case IROpcode::Unreachable:
            ctx.emitInst("ud2");
            break;
        case IROpcode::Phi:
            // Phi nodes are handled by predecessor blocks in a real compiler;
            // for simplicity, we emit a no-op comment here.
            ctx.emitInst("; phi " + (ins.hasDst() ? opStr(ins.dst, ctx) : ""));
            break;
        case IROpcode::MemCopy: {
            // memcpy(dst, src, size): use rep movsb
            if (ins.srcs.size() >= 3) {
                u32 r_di = REG_RDI, r_si = REG_RSI, r_cx = REG_RCX;
                ctx.emitInst("mov " + std::string(gprName64(r_di)) + ", " + opStr(ins.srcs[0], ctx));
                ctx.emitInst("mov " + std::string(gprName64(r_si)) + ", " + opStr(ins.srcs[1], ctx));
                ctx.emitInst("mov " + std::string(gprName64(r_cx)) + ", " + opStr(ins.srcs[2], ctx));
                ctx.emitInst("rep movsb");
            }
            break;
        }
        case IROpcode::MemSet: {
            // memset(dst, val, size)
            if (ins.srcs.size() >= 3) {
                ctx.emitInst("mov " + std::string(gprName64(REG_RDI)) + ", " + opStr(ins.srcs[0], ctx));
                ctx.emitInst("mov al, " + opStr(ins.srcs[1], ctx));
                ctx.emitInst("mov " + std::string(gprName64(REG_RCX)) + ", " + opStr(ins.srcs[2], ctx));
                ctx.emitInst("rep stosb");
            }
            break;
        }
        default:
            ctx.emitInst("; unhandled opcode");
            break;
    }
}

// ---------------------------------------------------------------------------
// Alloca — handled in compileFunction loop; this is a fallback
// ---------------------------------------------------------------------------
void X64CodeGen::emitAlloca(const IRInstr& ins, X64FuncCtx& ctx) {
    // Already handled in the main loop; skip duplicate
    (void)ins; (void)ctx;
}

// ---------------------------------------------------------------------------
// Load
// ---------------------------------------------------------------------------
void X64CodeGen::emitLoad(const IRInstr& ins, X64FuncCtx& ctx) {
    if (!ins.hasDst() || ins.srcs.empty()) return;
    const IRValue& ptr = ins.srcs[0];
    const IRValue& dst = ins.dst;

    bool dstIsFloat = typeIsFloat(dst.type.get());
    std::string ptrStr = opStr(ptr, ctx);

    if (dstIsFloat) {
        // determine destination xmm register
        std::string dstXmm;
        if (dst.isReg() && ctx.virToPhys.count(dst.id))
            dstXmm = xmmName(ctx.virToPhys[dst.id]);
        else
            dstXmm = "xmm0";
        ctx.emitInst("movsd " + dstXmm + ", qword ptr [" + ptrStr + "]");
    } else {
        std::string dstReg;
        if (dst.isReg() && ctx.virToPhys.count(dst.id))
            dstReg = gprName64(ctx.virToPhys[dst.id]);
        else
            dstReg = gprName64(REG_RAX);

        u32 sz = ins.opType ? ins.opType->size() : 8;
        if (sz == 1) {
            ctx.emitInst("movzx " + dstReg + ", byte ptr [" + ptrStr + "]");
        } else if (sz == 2) {
            ctx.emitInst("movzx " + dstReg + ", word ptr [" + ptrStr + "]");
        } else if (sz == 4) {
            ctx.emitInst(std::string("mov ") + gprName32(ctx.virToPhys.count(dst.id) ? ctx.virToPhys[dst.id] : 0u) + ", dword ptr [" + ptrStr + "]");
        } else {
            ctx.emitInst("mov " + dstReg + ", qword ptr [" + ptrStr + "]");
        }

        // Spill if needed
        if (dst.isReg() && ctx.spillSlots.count(dst.id) && !ctx.virToPhys.count(dst.id)) {
            ctx.emitInst("mov " + spillStr(ctx.spillSlots[dst.id]) + ", rax");
        }
    }
}

// ---------------------------------------------------------------------------
// Store
// ---------------------------------------------------------------------------
void X64CodeGen::emitStore(const IRInstr& ins, X64FuncCtx& ctx) {
    // srcs[0] = value, srcs[1] = pointer
    if (ins.srcs.size() < 2) return;
    const IRValue& val = ins.srcs[0];
    const IRValue& ptr = ins.srcs[1];

    bool valIsFloat = typeIsFloat(val.type.get());
    std::string ptrStr = opStr(ptr, ctx);

    if (valIsFloat) {
        std::string srcXmm;
        if (val.isReg() && ctx.virToPhys.count(val.id))
            srcXmm = xmmName(ctx.virToPhys[val.id]);
        else {
            // Load from spill
            srcXmm = "xmm15";
            if (val.isReg() && ctx.spillSlots.count(val.id))
                ctx.emitInst("movsd xmm15, " + spillStr(ctx.spillSlots[val.id]));
        }
        ctx.emitInst("movsd qword ptr [" + ptrStr + "], " + srcXmm);
    } else {
        std::string srcReg;
        if (val.kind == IRValueKind::Constant) {
            // Use immediate
            u32 sz = ins.opType ? ins.opType->size() : 8;
            std::string sizeStr = (sz==1?"byte ptr":(sz==2?"word ptr":(sz==4?"dword ptr":"qword ptr")));
            ctx.emitInst("mov " + sizeStr + " [" + ptrStr + "], " + std::to_string((i64)val.id));
            return;
        }
        if (val.isReg() && ctx.virToPhys.count(val.id))
            srcReg = gprName64(ctx.virToPhys[val.id]);
        else {
            srcReg = gprName64(REG_R10);
            if (val.isReg() && ctx.spillSlots.count(val.id))
                ctx.emitInst("mov " + std::string(gprName64(REG_R10)) + ", " + spillStr(ctx.spillSlots[val.id]));
        }
        u32 sz = ins.opType ? ins.opType->size() : 8;
        std::string sizeStr = (sz==1?"byte ptr":(sz==2?"word ptr":(sz==4?"dword ptr":"qword ptr")));
        std::string srcPart = (sz==1?gprName8(val.isReg()&&ctx.virToPhys.count(val.id)?ctx.virToPhys[val.id]:REG_R10):
                               (sz==4?gprName32(val.isReg()&&ctx.virToPhys.count(val.id)?ctx.virToPhys[val.id]:REG_R10):srcReg));
        ctx.emitInst("mov " + sizeStr + " [" + ptrStr + "], " + srcPart);
    }
}

// ---------------------------------------------------------------------------
// Binary operations: add, sub, and, or, xor, imul
// ---------------------------------------------------------------------------
void X64CodeGen::emitBinop(const IRInstr& ins, X64FuncCtx& ctx) {
    if (!ins.hasDst() || ins.srcs.size() < 2) return;
    const IRValue& lhs = ins.srcs[0];
    const IRValue& rhs = ins.srcs[1];

    auto getMnem = [&]() -> std::string {
        switch (ins.op) {
            case IROpcode::Add: return "add";
            case IROpcode::Sub: return "sub";
            case IROpcode::And: return "and";
            case IROpcode::Or:  return "or";
            case IROpcode::Xor: return "xor";
            case IROpcode::Mul: return "imul";
            default: return "add";
        }
    };

    // Determine destination phys reg
    u32 dstPhys = REG_RAX;
    bool dstIsSpill = false;
    if (ins.dst.isReg()) {
        if (ctx.virToPhys.count(ins.dst.id))
            dstPhys = ctx.virToPhys[ins.dst.id];
        else
            dstIsSpill = true;
    }

    // Load lhs into dstPhys
    if (lhs.kind == IRValueKind::Constant) {
        ctx.emitInst("mov " + std::string(gprName64(dstPhys)) + ", " + std::to_string((i64)lhs.id));
    } else if (lhs.isReg()) {
        if (ctx.virToPhys.count(lhs.id)) {
            u32 lp = ctx.virToPhys[lhs.id];
            if (lp != dstPhys)
                ctx.emitInst("mov " + std::string(gprName64(dstPhys)) + ", " + gprName64(lp));
        } else if (ctx.spillSlots.count(lhs.id)) {
            ctx.emitInst("mov " + std::string(gprName64(dstPhys)) + ", " + spillStr(ctx.spillSlots[lhs.id]));
        }
    }

    // Apply operation with rhs
    if (rhs.kind == IRValueKind::Constant) {
        if (ins.op == IROpcode::Mul)
            ctx.emitInst("imul " + std::string(gprName64(dstPhys)) + ", " + gprName64(dstPhys) + ", " + std::to_string((i64)rhs.id));
        else
            ctx.emitInst(getMnem() + " " + gprName64(dstPhys) + ", " + std::to_string((i64)rhs.id));
    } else if (rhs.isReg()) {
        std::string rhsStr;
        if (ctx.virToPhys.count(rhs.id))
            rhsStr = gprName64(ctx.virToPhys[rhs.id]);
        else if (ctx.spillSlots.count(rhs.id)) {
            // load into r11 (scratch)
            ctx.emitInst("mov " + std::string(gprName64(REG_R11)) + ", " + spillStr(ctx.spillSlots[rhs.id]));
            rhsStr = gprName64(REG_R11);
        } else {
            rhsStr = gprName64(REG_R11);
        }
        ctx.emitInst(getMnem() + " " + gprName64(dstPhys) + ", " + rhsStr);
    }

    if (dstIsSpill)
        ctx.emitInst("mov " + spillStr(ctx.spillSlots[ins.dst.id]) + ", " + gprName64(dstPhys));
}

// ---------------------------------------------------------------------------
// Shifts: shl, shr, sar
// ---------------------------------------------------------------------------
void X64CodeGen::emitShift(const IRInstr& ins, X64FuncCtx& ctx) {
    if (!ins.hasDst() || ins.srcs.size() < 2) return;
    const IRValue& lhs = ins.srcs[0];
    const IRValue& cnt = ins.srcs[1];

    std::string mnem;
    switch (ins.op) {
        case IROpcode::Shl:  mnem = "shl";  break;
        case IROpcode::Shr:  mnem = "shr";  break;
        case IROpcode::AShr: mnem = "sar";  break;
        default: mnem = "shl"; break;
    }

    u32 dstPhys = REG_RAX;
    bool dstSpill = false;
    if (ins.dst.isReg()) {
        if (ctx.virToPhys.count(ins.dst.id)) dstPhys = ctx.virToPhys[ins.dst.id];
        else dstSpill = true;
    }

    // Load lhs into dst
    if (lhs.kind == IRValueKind::Constant)
        ctx.emitInst("mov " + std::string(gprName64(dstPhys)) + ", " + std::to_string((i64)lhs.id));
    else if (lhs.isReg() && ctx.virToPhys.count(lhs.id)) {
        if (ctx.virToPhys[lhs.id] != dstPhys)
            ctx.emitInst("mov " + std::string(gprName64(dstPhys)) + ", " + gprName64(ctx.virToPhys[lhs.id]));
    } else if (lhs.isReg() && ctx.spillSlots.count(lhs.id)) {
        ctx.emitInst("mov " + std::string(gprName64(dstPhys)) + ", " + spillStr(ctx.spillSlots[lhs.id]));
    }

    // Count: immediate or load into cl
    if (cnt.kind == IRValueKind::Constant) {
        ctx.emitInst(mnem + " " + gprName64(dstPhys) + ", " + std::to_string((u32)(cnt.id & 63)));
    } else {
        // Move count to cl, but preserve if dstPhys == rcx
        if (dstPhys == REG_RCX) {
            // use r11 as temp for dst
            ctx.emitInst("mov " + std::string(gprName64(REG_R11)) + ", " + gprName64(dstPhys));
            if (cnt.isReg() && ctx.virToPhys.count(cnt.id))
                ctx.emitInst("mov cl, " + std::string(gprName8(ctx.virToPhys[cnt.id])));
            else if (cnt.isReg() && ctx.spillSlots.count(cnt.id)) {
                ctx.emitInst("mov " + std::string(gprName64(REG_R10)) + ", " + spillStr(ctx.spillSlots[cnt.id]));
                ctx.emitInst("mov cl, r10b");
            }
            ctx.emitInst(mnem + " " + std::string(gprName64(REG_R11)) + ", cl");
            ctx.emitInst("mov " + std::string(gprName64(dstPhys)) + ", " + gprName64(REG_R11));
        } else {
            if (cnt.isReg() && ctx.virToPhys.count(cnt.id))
                ctx.emitInst("mov cl, " + std::string(gprName8(ctx.virToPhys[cnt.id])));
            else if (cnt.isReg() && ctx.spillSlots.count(cnt.id)) {
                ctx.emitInst("mov " + std::string(gprName64(REG_R10)) + ", " + spillStr(ctx.spillSlots[cnt.id]));
                ctx.emitInst("mov cl, r10b");
            }
            ctx.emitInst(mnem + " " + gprName64(dstPhys) + ", cl");
        }
    }

    if (dstSpill)
        ctx.emitInst("mov " + spillStr(ctx.spillSlots[ins.dst.id]) + ", " + gprName64(dstPhys));
}

// ---------------------------------------------------------------------------
// Division / Modulo using idiv
// ---------------------------------------------------------------------------
void X64CodeGen::emitDiv(const IRInstr& ins, X64FuncCtx& ctx) {
    if (!ins.hasDst() || ins.srcs.size() < 2) return;
    const IRValue& lhs = ins.srcs[0];
    const IRValue& rhs = ins.srcs[1];

    // Determine if signed or unsigned (we treat IROpcode::Div as signed)
    bool isMod = (ins.op == IROpcode::Mod);

    // Load lhs into rax
    if (lhs.kind == IRValueKind::Constant)
        ctx.emitInst("mov rax, " + std::to_string((i64)lhs.id));
    else if (lhs.isReg() && ctx.virToPhys.count(lhs.id)) {
        if (ctx.virToPhys[lhs.id] != REG_RAX)
            ctx.emitInst("mov rax, " + std::string(gprName64(ctx.virToPhys[lhs.id])));
    } else if (lhs.isReg() && ctx.spillSlots.count(lhs.id))
        ctx.emitInst("mov rax, " + spillStr(ctx.spillSlots[lhs.id]));

    // Sign-extend rax into rdx
    ctx.emitInst("cqo");

    // Load divisor into r11 (to avoid clobbering rax/rdx)
    if (rhs.kind == IRValueKind::Constant) {
        ctx.emitInst("mov r11, " + std::to_string((i64)rhs.id));
        ctx.emitInst("idiv r11");
    } else if (rhs.isReg() && ctx.virToPhys.count(rhs.id)) {
        u32 rp = ctx.virToPhys[rhs.id];
        if (rp == REG_RAX || rp == REG_RDX) {
            ctx.emitInst("mov r11, " + std::string(gprName64(rp)));
            ctx.emitInst("idiv r11");
        } else {
            ctx.emitInst("idiv " + std::string(gprName64(rp)));
        }
    } else if (rhs.isReg() && ctx.spillSlots.count(rhs.id)) {
        ctx.emitInst("mov r11, " + spillStr(ctx.spillSlots[rhs.id]));
        ctx.emitInst("idiv r11");
    } else {
        ctx.emitInst("idiv r11");
    }

    // Result: rax = quotient, rdx = remainder
    u32 resultReg = isMod ? REG_RDX : REG_RAX;

    u32 dstPhys = REG_RAX;
    bool dstSpill = false;
    if (ins.dst.isReg()) {
        if (ctx.virToPhys.count(ins.dst.id)) dstPhys = ctx.virToPhys[ins.dst.id];
        else dstSpill = true;
    }

    if (dstPhys != resultReg)
        ctx.emitInst("mov " + std::string(gprName64(dstPhys)) + ", " + gprName64(resultReg));

    if (dstSpill)
        ctx.emitInst("mov " + spillStr(ctx.spillSlots[ins.dst.id]) + ", " + gprName64(resultReg));
}

// ---------------------------------------------------------------------------
// Unary operations
// ---------------------------------------------------------------------------
void X64CodeGen::emitUnop(const IRInstr& ins, X64FuncCtx& ctx) {
    if (!ins.hasDst() || ins.srcs.empty()) return;
    const IRValue& src = ins.srcs[0];

    bool isFloatOp = (ins.op == IROpcode::FNeg);

    if (isFloatOp) {
        // XOR high bit with sign mask
        std::string srcXmm = "xmm0", dstXmm = "xmm0";
        if (src.isReg() && ctx.virToPhys.count(src.id)) srcXmm = xmmName(ctx.virToPhys[src.id]);
        if (ins.dst.isReg() && ctx.virToPhys.count(ins.dst.id)) dstXmm = xmmName(ctx.virToPhys[ins.dst.id]);
        if (srcXmm != dstXmm) ctx.emitInst("movsd " + dstXmm + ", " + srcXmm);
        ctx.emitInst("; fneg via xorpd with sign mask");
        ctx.emitInst("pcmpeqd xmm15, xmm15");
        ctx.emitInst("psllq xmm15, 63");
        ctx.emitInst("xorpd " + dstXmm + ", xmm15");
        return;
    }

    u32 dstPhys = REG_RAX;
    bool dstSpill = false;
    if (ins.dst.isReg()) {
        if (ctx.virToPhys.count(ins.dst.id)) dstPhys = ctx.virToPhys[ins.dst.id];
        else dstSpill = true;
    }

    // Load src into dst
    if (src.kind == IRValueKind::Constant)
        ctx.emitInst("mov " + std::string(gprName64(dstPhys)) + ", " + std::to_string((i64)src.id));
    else if (src.isReg() && ctx.virToPhys.count(src.id)) {
        if (ctx.virToPhys[src.id] != dstPhys)
            ctx.emitInst("mov " + std::string(gprName64(dstPhys)) + ", " + gprName64(ctx.virToPhys[src.id]));
    } else if (src.isReg() && ctx.spillSlots.count(src.id))
        ctx.emitInst("mov " + std::string(gprName64(dstPhys)) + ", " + spillStr(ctx.spillSlots[src.id]));

    switch (ins.op) {
        case IROpcode::Neg: ctx.emitInst("neg " + std::string(gprName64(dstPhys))); break;
        case IROpcode::Not: ctx.emitInst("not " + std::string(gprName64(dstPhys))); break;
        default: break;
    }

    if (dstSpill)
        ctx.emitInst("mov " + spillStr(ctx.spillSlots[ins.dst.id]) + ", " + gprName64(dstPhys));
}

// ---------------------------------------------------------------------------
// Compare (integer)
// ---------------------------------------------------------------------------
void X64CodeGen::emitCmp(const IRInstr& ins, X64FuncCtx& ctx) {
    if (!ins.hasDst() || ins.srcs.size() < 2) return;
    const IRValue& lhs = ins.srcs[0];
    const IRValue& rhs = ins.srcs[1];

    auto loadOp = [&](const IRValue& v, u32 scratch) -> std::string {
        if (v.kind == IRValueKind::Constant) {
            ctx.emitInst("mov " + std::string(gprName64(scratch)) + ", " + std::to_string((i64)v.id));
            return gprName64(scratch);
        }
        if (v.isReg() && ctx.virToPhys.count(v.id)) return gprName64(ctx.virToPhys[v.id]);
        if (v.isReg() && ctx.spillSlots.count(v.id)) {
            ctx.emitInst("mov " + std::string(gprName64(scratch)) + ", " + spillStr(ctx.spillSlots[v.id]));
            return gprName64(scratch);
        }
        return gprName64(scratch);
    };

    std::string lhsStr = loadOp(lhs, REG_RAX);
    std::string rhsStr = loadOp(rhs, REG_R11);
    ctx.emitInst("cmp " + lhsStr + ", " + rhsStr);

    const char* setcc = "sete";
    switch (ins.op) {
        case IROpcode::IEq:  setcc = "sete";  break;
        case IROpcode::INe:  setcc = "setne"; break;
        case IROpcode::ILt:  setcc = "setl";  break;
        case IROpcode::ILe:  setcc = "setle"; break;
        case IROpcode::IGt:  setcc = "setg";  break;
        case IROpcode::IGe:  setcc = "setge"; break;
        case IROpcode::IULt: setcc = "setb";  break;
        case IROpcode::IULe: setcc = "setbe"; break;
        case IROpcode::IUGt: setcc = "seta";  break;
        case IROpcode::IUGe: setcc = "setae"; break;
        default: break;
    }

    u32 dstPhys = REG_RAX;
    bool dstSpill = false;
    if (ins.dst.isReg()) {
        if (ctx.virToPhys.count(ins.dst.id)) dstPhys = ctx.virToPhys[ins.dst.id];
        else dstSpill = true;
    }

    ctx.emitInst(std::string(setcc) + " " + gprName8(dstPhys));
    ctx.emitInst("movzx " + std::string(gprName64(dstPhys)) + ", " + gprName8(dstPhys));

    if (dstSpill)
        ctx.emitInst("mov " + spillStr(ctx.spillSlots[ins.dst.id]) + ", " + gprName64(dstPhys));
}

// ---------------------------------------------------------------------------
// Float compare
// ---------------------------------------------------------------------------
void X64CodeGen::emitFpCmp(const IRInstr& ins, X64FuncCtx& ctx) {
    if (!ins.hasDst() || ins.srcs.size() < 2) return;
    const IRValue& lhs = ins.srcs[0];
    const IRValue& rhs = ins.srcs[1];

    auto getXmm = [&](const IRValue& v, u32 scratch) -> std::string {
        if (v.isReg() && ctx.virToPhys.count(v.id)) return xmmName(ctx.virToPhys[v.id]);
        if (v.isReg() && ctx.spillSlots.count(v.id)) {
            ctx.emitInst("movsd " + std::string(xmmName(scratch)) + ", " + spillStr(ctx.spillSlots[v.id]));
            return xmmName(scratch);
        }
        return xmmName(scratch);
    };

    std::string lhsXmm = getXmm(lhs, 14);
    std::string rhsXmm = getXmm(rhs, 15);
    ctx.emitInst("ucomisd " + lhsXmm + ", " + rhsXmm);

    const char* setcc = "sete";
    switch (ins.op) {
        case IROpcode::FEq: setcc = "sete";  break;
        case IROpcode::FNe: setcc = "setne"; break;
        case IROpcode::FLt: setcc = "setb";  break;
        case IROpcode::FLe: setcc = "setbe"; break;
        case IROpcode::FGt: setcc = "seta";  break;
        case IROpcode::FGe: setcc = "setae"; break;
        default: break;
    }

    u32 dstPhys = REG_RAX;
    bool dstSpill = false;
    if (ins.dst.isReg()) {
        if (ctx.virToPhys.count(ins.dst.id)) dstPhys = ctx.virToPhys[ins.dst.id];
        else dstSpill = true;
    }

    ctx.emitInst(std::string(setcc) + " " + gprName8(dstPhys));
    ctx.emitInst("movzx " + std::string(gprName64(dstPhys)) + ", " + gprName8(dstPhys));

    if (dstSpill)
        ctx.emitInst("mov " + spillStr(ctx.spillSlots[ins.dst.id]) + ", " + gprName64(dstPhys));
}

// ---------------------------------------------------------------------------
// FP binary ops
// ---------------------------------------------------------------------------
void X64CodeGen::emitFpBinop(const IRInstr& ins, X64FuncCtx& ctx) {
    if (!ins.hasDst() || ins.srcs.size() < 2) return;
    const IRValue& lhs = ins.srcs[0];
    const IRValue& rhs = ins.srcs[1];

    std::string mnem;
    switch (ins.op) {
        case IROpcode::FAdd: mnem = "addsd"; break;
        case IROpcode::FSub: mnem = "subsd"; break;
        case IROpcode::FMul: mnem = "mulsd"; break;
        case IROpcode::FDiv: mnem = "divsd"; break;
        default: mnem = "addsd"; break;
    }

    auto getXmm = [&](const IRValue& v, u32 scratch) -> std::string {
        if (v.isReg() && ctx.virToPhys.count(v.id)) return xmmName(ctx.virToPhys[v.id]);
        if (v.isReg() && ctx.spillSlots.count(v.id)) {
            ctx.emitInst("movsd " + std::string(xmmName(scratch)) + ", " + spillStr(ctx.spillSlots[v.id]));
            return xmmName(scratch);
        }
        if (v.kind == IRValueKind::FConst) {
            // Store into temp slot (simplified: use movsd with literal)
            // In real code, use a .rodata constant
            return xmmName(scratch);
        }
        return xmmName(scratch);
    };

    u32 dstPhys = 16; // xmm0 as default
    bool dstSpill = false;
    if (ins.dst.isReg()) {
        if (ctx.virToPhys.count(ins.dst.id)) dstPhys = ctx.virToPhys[ins.dst.id];
        else dstSpill = true;
    }

    std::string lhsXmm = getXmm(lhs, 14);
    std::string rhsXmm = getXmm(rhs, 15);
    std::string dstXmm = xmmName(dstPhys);

    if (dstXmm != lhsXmm) ctx.emitInst("movsd " + dstXmm + ", " + lhsXmm);
    ctx.emitInst(mnem + " " + dstXmm + ", " + rhsXmm);

    if (dstSpill) {
        i32 off = ctx.spillSlots[ins.dst.id];
        ctx.emitInst("movsd " + spillStr(off) + ", " + dstXmm);
    }
}

// ---------------------------------------------------------------------------
// FP unop (FNeg handled in emitUnop above)
// ---------------------------------------------------------------------------
void X64CodeGen::emitFpUnop(const IRInstr& ins, X64FuncCtx& ctx) {
    emitUnop(ins, ctx);
}

// ---------------------------------------------------------------------------
// Branch
// ---------------------------------------------------------------------------
void X64CodeGen::emitBr(const IRInstr& ins, X64FuncCtx& ctx) {
    ctx.emitInst("jmp " + localLabel(ctx.fnName, ins.label));
}

// ---------------------------------------------------------------------------
// Conditional branch
// ---------------------------------------------------------------------------
void X64CodeGen::emitCondBr(const IRInstr& ins, X64FuncCtx& ctx) {
    if (ins.srcs.empty()) return;
    const IRValue& cond = ins.srcs[0];

    std::string condReg;
    if (cond.isReg() && ctx.virToPhys.count(cond.id))
        condReg = gprName64(ctx.virToPhys[cond.id]);
    else if (cond.isReg() && ctx.spillSlots.count(cond.id)) {
        ctx.emitInst("mov rax, " + spillStr(ctx.spillSlots[cond.id]));
        condReg = "rax";
    } else {
        condReg = "rax";
        ctx.emitInst("mov rax, " + std::to_string((i64)cond.id));
    }

    ctx.emitInst("cmp " + condReg + ", 0");
    ctx.emitInst("jne " + localLabel(ctx.fnName, ins.label));
    ctx.emitInst("jmp " + localLabel(ctx.fnName, ins.label2));
}

// ---------------------------------------------------------------------------
// Return
// ---------------------------------------------------------------------------
void X64CodeGen::emitRet(const IRInstr& ins, X64FuncCtx& ctx) {
    if (!ins.srcs.empty() && !ins.srcs[0].isVoid()) {
        const IRValue& val = ins.srcs[0];
        bool isFloat = typeIsFloat(val.type.get());

        if (isFloat) {
            if (val.isReg() && ctx.virToPhys.count(val.id)) {
                u32 pr = ctx.virToPhys[val.id];
                if (pr != REG_XMM0)
                    ctx.emitInst("movsd xmm0, " + std::string(xmmName(pr)));
            } else if (val.isReg() && ctx.spillSlots.count(val.id)) {
                ctx.emitInst("movsd xmm0, " + spillStr(ctx.spillSlots[val.id]));
            }
        } else {
            if (val.kind == IRValueKind::Constant) {
                ctx.emitInst("mov rax, " + std::to_string((i64)val.id));
            } else if (val.isReg() && ctx.virToPhys.count(val.id)) {
                u32 pr = ctx.virToPhys[val.id];
                if (pr != REG_RAX)
                    ctx.emitInst("mov rax, " + std::string(gprName64(pr)));
            } else if (val.isReg() && ctx.spillSlots.count(val.id)) {
                ctx.emitInst("mov rax, " + spillStr(ctx.spillSlots[val.id]));
            } else if (val.isGlobal()) {
                ctx.emitInst("lea rax, " + val.name + "[rip]");
            }
        }
    }
    ctx.emitInst("jmp .L_" + ctx.fnName + "_epilogue");
}

// ---------------------------------------------------------------------------
// Call
// ---------------------------------------------------------------------------
void X64CodeGen::emitCall(const IRInstr& ins, X64FuncCtx& ctx) {
    // srcs[0] = function (Global or Register), srcs[1..] = arguments
    if (ins.srcs.empty()) return;
    const IRValue& callee = ins.srcs[0];

    // Collect argument values
    std::vector<IRValue> args(ins.srcs.begin() + 1, ins.srcs.end());

    // Pass arguments according to SysV AMD64 ABI
    u32 intArgIdx = 0, fltArgIdx = 0;
    std::vector<std::pair<std::string,std::string>> argMovs; // "dst <- src" as asm
    u32 stackArgs = 0;

    for (auto& arg : args) {
        bool argIsFloat = typeIsFloat(arg.type.get());
        if (argIsFloat && fltArgIdx < kNumFltArgRegs) {
            std::string dst = xmmName(kFltArgRegs[fltArgIdx++]);
            std::string src;
            if (arg.isReg() && ctx.virToPhys.count(arg.id)) src = xmmName(ctx.virToPhys[arg.id]);
            else if (arg.isReg() && ctx.spillSlots.count(arg.id)) {
                ctx.emitInst("movsd " + dst + ", " + spillStr(ctx.spillSlots[arg.id]));
                continue;
            } else src = dst;
            if (src != dst) ctx.emitInst("movsd " + dst + ", " + src);
        } else if (!argIsFloat && intArgIdx < kNumIntArgRegs) {
            std::string dst = gprName64(kIntArgRegs[intArgIdx++]);
            if (arg.kind == IRValueKind::Constant) {
                ctx.emitInst("mov " + dst + ", " + std::to_string((i64)arg.id));
            } else if (arg.isReg() && ctx.virToPhys.count(arg.id)) {
                std::string src = gprName64(ctx.virToPhys[arg.id]);
                if (src != dst) ctx.emitInst("mov " + dst + ", " + src);
            } else if (arg.isReg() && ctx.spillSlots.count(arg.id)) {
                ctx.emitInst("mov " + dst + ", " + spillStr(ctx.spillSlots[arg.id]));
            } else if (arg.isGlobal()) {
                ctx.emitInst("lea " + dst + ", " + arg.name + "[rip]");
            }
        } else {
            // Stack arg: push in reverse, simplified — push in order here
            stackArgs++;
            if (arg.kind == IRValueKind::Constant) {
                ctx.emitInst("push " + std::to_string((i64)arg.id));
            } else if (arg.isReg() && ctx.virToPhys.count(arg.id)) {
                ctx.emitInst("push " + std::string(gprName64(ctx.virToPhys[arg.id])));
            } else if (arg.isReg() && ctx.spillSlots.count(arg.id)) {
                ctx.emitInst("mov rax, " + spillStr(ctx.spillSlots[arg.id]));
                ctx.emitInst("push rax");
            }
        }
    }

    // Align stack to 16 bytes before call (stack starts 8-off after push rbp)
    // We push stackArgs 8-byte values; if odd, add extra alignment
    if (stackArgs % 2 != 0)
        ctx.emitInst("sub rsp, 8   ; align stack");

    // Emit call
    if (callee.isGlobal()) {
        ctx.emitInst("call " + callee.name);
    } else if (callee.isReg() && ctx.virToPhys.count(callee.id)) {
        ctx.emitInst("call " + std::string(gprName64(ctx.virToPhys[callee.id])));
    } else {
        ctx.emitInst("call rax ; indirect");
    }

    // Clean up stack args
    if (stackArgs > 0) {
        u32 adj = stackArgs * 8;
        if (stackArgs % 2 != 0) adj += 8;
        ctx.emitInst("add rsp, " + std::to_string(adj));
    }

    // Store result
    if (ins.hasDst()) {
        bool retFloat = typeIsFloat(ins.dst.type.get());
        if (retFloat) {
            if (ins.dst.isReg() && ctx.virToPhys.count(ins.dst.id)) {
                u32 dp = ctx.virToPhys[ins.dst.id];
                if (dp != REG_XMM0) ctx.emitInst("movsd " + std::string(xmmName(dp)) + ", xmm0");
            } else if (ins.dst.isReg() && ctx.spillSlots.count(ins.dst.id)) {
                ctx.emitInst("movsd " + spillStr(ctx.spillSlots[ins.dst.id]) + ", xmm0");
            }
        } else {
            if (ins.dst.isReg() && ctx.virToPhys.count(ins.dst.id)) {
                u32 dp = ctx.virToPhys[ins.dst.id];
                if (dp != REG_RAX) ctx.emitInst("mov " + std::string(gprName64(dp)) + ", rax");
            } else if (ins.dst.isReg() && ctx.spillSlots.count(ins.dst.id)) {
                ctx.emitInst("mov " + spillStr(ctx.spillSlots[ins.dst.id]) + ", rax");
            }
        }
    }
}

// ---------------------------------------------------------------------------
// GEP — GetElementPointer
// ---------------------------------------------------------------------------
void X64CodeGen::emitGEP(const IRInstr& ins, X64FuncCtx& ctx) {
    if (!ins.hasDst() || ins.srcs.empty()) return;
    const IRValue& base = ins.srcs[0];

    u32 dstPhys = REG_RAX;
    bool dstSpill = false;
    if (ins.dst.isReg()) {
        if (ctx.virToPhys.count(ins.dst.id)) dstPhys = ctx.virToPhys[ins.dst.id];
        else dstSpill = true;
    }

    // Load base pointer
    if (base.isReg() && ctx.virToPhys.count(base.id)) {
        if (ctx.virToPhys[base.id] != dstPhys)
            ctx.emitInst("mov " + std::string(gprName64(dstPhys)) + ", " + gprName64(ctx.virToPhys[base.id]));
    } else if (base.isReg() && ctx.spillSlots.count(base.id)) {
        ctx.emitInst("mov " + std::string(gprName64(dstPhys)) + ", " + spillStr(ctx.spillSlots[base.id]));
    } else if (base.isGlobal()) {
        ctx.emitInst("lea " + std::string(gprName64(dstPhys)) + ", " + base.name + "[rip]");
    }

    // Apply each index (simplified: flat byte offset)
    u32 elemSz = ins.opType ? ins.opType->size() : 1;
    for (size_t i = 1; i < ins.srcs.size(); ++i) {
        const IRValue& idx = ins.srcs[i];
        if (idx.kind == IRValueKind::Constant) {
            i64 off = (i64)idx.id * (i64)elemSz;
            if (off != 0)
                ctx.emitInst("add " + std::string(gprName64(dstPhys)) + ", " + std::to_string(off));
        } else {
            // Load index into r11, scale, add
            if (idx.isReg() && ctx.virToPhys.count(idx.id))
                ctx.emitInst("mov r11, " + std::string(gprName64(ctx.virToPhys[idx.id])));
            else if (idx.isReg() && ctx.spillSlots.count(idx.id))
                ctx.emitInst("mov r11, " + spillStr(ctx.spillSlots[idx.id]));
            if (elemSz > 1)
                ctx.emitInst("imul r11, r11, " + std::to_string(elemSz));
            ctx.emitInst("add " + std::string(gprName64(dstPhys)) + ", r11");
        }
    }

    if (dstSpill)
        ctx.emitInst("mov " + spillStr(ctx.spillSlots[ins.dst.id]) + ", " + gprName64(dstPhys));
}

// ---------------------------------------------------------------------------
// Casts
// ---------------------------------------------------------------------------
void X64CodeGen::emitCast(const IRInstr& ins, X64FuncCtx& ctx) {
    if (!ins.hasDst() || ins.srcs.empty()) return;
    const IRValue& src = ins.srcs[0];

    u32 dstPhys = REG_RAX;
    bool dstSpill = false;
    if (ins.dst.isReg()) {
        if (ctx.virToPhys.count(ins.dst.id)) dstPhys = ctx.virToPhys[ins.dst.id];
        else dstSpill = true;
    }

    u32 srcPhys = REG_RAX;
    bool srcIsXmm = false;
    if (src.isReg() && ctx.virToPhys.count(src.id)) {
        srcPhys = ctx.virToPhys[src.id];
        srcIsXmm = isXmm(srcPhys);
    } else if (src.isReg() && ctx.spillSlots.count(src.id)) {
        // Load first
        ctx.emitInst("mov rax, " + spillStr(ctx.spillSlots[src.id]));
        srcPhys = REG_RAX;
    }

    switch (ins.op) {
        case IROpcode::SExt:
        case IROpcode::ZExt: {
            u32 srcSz = src.type ? src.type->size() : 4;
            u32 dstSz = ins.dst.type ? ins.dst.type->size() : 8;
            bool sign = (ins.op == IROpcode::SExt);
            if (srcSz == 1 && dstSz == 8) {
                ctx.emitInst(std::string(sign?"movsx ":"movzx ") + gprName64(dstPhys) + ", " + gprName8(srcPhys));
            } else if (srcSz == 2 && dstSz == 8) {
                ctx.emitInst(std::string(sign?"movsx ":"movzx ") + gprName64(dstPhys) + ", " + gprName64(srcPhys)); // cx etc
            } else if (srcSz == 4 && dstSz == 8) {
                ctx.emitInst(std::string(sign?"movsxd ":"mov ") + gprName64(dstPhys) + ", " + gprName32(srcPhys));
            } else {
                if (srcPhys != dstPhys)
                    ctx.emitInst("mov " + std::string(gprName64(dstPhys)) + ", " + gprName64(srcPhys));
            }
            break;
        }
        case IROpcode::Trunc:
            // Move then mask if needed
            if (srcPhys != dstPhys)
                ctx.emitInst("mov " + std::string(gprName64(dstPhys)) + ", " + gprName64(srcPhys));
            break;
        case IROpcode::SIToFP: {
            // convert integer to double
            u32 xmmDst = 16; // xmm0
            bool dstIsXmm2 = false;
            if (ins.dst.isReg() && ctx.virToPhys.count(ins.dst.id) && isXmm(ctx.virToPhys[ins.dst.id])) {
                xmmDst = ctx.virToPhys[ins.dst.id];
                dstIsXmm2 = true;
            }
            ctx.emitInst("cvtsi2sd " + std::string(xmmName(xmmDst)) + ", " + gprName64(srcPhys));
            if (!dstIsXmm2 && !dstSpill) {
                // move to gpr — unusual but handle
            } else if (dstSpill && ins.dst.isReg() && ctx.spillSlots.count(ins.dst.id)) {
                ctx.emitInst("movsd " + spillStr(ctx.spillSlots[ins.dst.id]) + ", " + xmmName(xmmDst));
            }
            return; // early return to avoid double-store below
        }
        case IROpcode::UIToFP: {
            u32 xmmDst = 16;
            if (ins.dst.isReg() && ctx.virToPhys.count(ins.dst.id) && isXmm(ctx.virToPhys[ins.dst.id])) {
                xmmDst = ctx.virToPhys[ins.dst.id];
            }
            ctx.emitInst("cvtsi2sd " + std::string(xmmName(xmmDst)) + ", " + gprName64(srcPhys));
            if (dstSpill && ins.dst.isReg() && ctx.spillSlots.count(ins.dst.id))
                ctx.emitInst("movsd " + spillStr(ctx.spillSlots[ins.dst.id]) + ", " + xmmName(xmmDst));
            return;
        }
        case IROpcode::FPToSI: {
            u32 xmmSrc = srcIsXmm ? srcPhys : 16u;
            if (!srcIsXmm) {
                // load from spill
                if (src.isReg() && ctx.spillSlots.count(src.id))
                    ctx.emitInst("movsd " + std::string(xmmName(xmmSrc)) + ", " + spillStr(ctx.spillSlots[src.id]));
            }
            ctx.emitInst("cvttsd2si " + std::string(gprName64(dstPhys)) + ", " + xmmName(xmmSrc));
            break;
        }
        case IROpcode::FPToUI: {
            u32 xmmSrc = srcIsXmm ? srcPhys : 16u;
            ctx.emitInst("cvttsd2si " + std::string(gprName64(dstPhys)) + ", " + xmmName(xmmSrc));
            break;
        }
        case IROpcode::FPTrunc:
        case IROpcode::FPExt: {
            // double <-> float conversion (use cvtsd2ss / cvtss2sd)
            u32 xmmSrc = srcIsXmm ? srcPhys : 14u;
            u32 xmmDst = (ins.dst.isReg() && ctx.virToPhys.count(ins.dst.id) && isXmm(ctx.virToPhys[ins.dst.id]))
                           ? ctx.virToPhys[ins.dst.id] : 15u;
            if (ins.op == IROpcode::FPTrunc)
                ctx.emitInst("cvtsd2ss " + std::string(xmmName(xmmDst)) + ", " + xmmName(xmmSrc));
            else
                ctx.emitInst("cvtss2sd " + std::string(xmmName(xmmDst)) + ", " + xmmName(xmmSrc));
            if (dstSpill && ins.dst.isReg() && ctx.spillSlots.count(ins.dst.id))
                ctx.emitInst("movsd " + spillStr(ctx.spillSlots[ins.dst.id]) + ", " + xmmName(xmmDst));
            return;
        }
        case IROpcode::PtrToInt:
        case IROpcode::IntToPtr:
        case IROpcode::Bitcast:
            if (srcPhys != dstPhys)
                ctx.emitInst("mov " + std::string(gprName64(dstPhys)) + ", " + gprName64(srcPhys));
            break;
        default:
            if (srcPhys != dstPhys)
                ctx.emitInst("mov " + std::string(gprName64(dstPhys)) + ", " + gprName64(srcPhys));
            break;
    }

    if (dstSpill && ins.dst.isReg() && ctx.spillSlots.count(ins.dst.id))
        ctx.emitInst("mov " + spillStr(ctx.spillSlots[ins.dst.id]) + ", " + gprName64(dstPhys));
}

// ---------------------------------------------------------------------------
// Global
// ---------------------------------------------------------------------------
void X64CodeGen::compileGlobal(const IRGlobal& g) {
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
// compile — top level
// ---------------------------------------------------------------------------
void X64CodeGen::compile(const IRModule& mod) {
    for (auto& fn : mod.functions) compileFunction(fn);
    for (auto& g  : mod.globals)   compileGlobal(g);
}

// ---------------------------------------------------------------------------
// emitAssembly — Intel syntax x64 assembly
// ---------------------------------------------------------------------------
void X64CodeGen::emitAssembly(FILE* out) {
    fprintf(out, "; qc x64 assembly (Intel syntax, System V AMD64)\n");
    fprintf(out, "bits 64\n");
    fprintf(out, "default rel\n\n");

    // .data section
    bool hasData = false;
    for (auto& g : globOutputs_) {
        if (!g.isZeroInit && (!g.initData.empty() || g.hasStringInit)) {
            if (!hasData) { fprintf(out, "section .data\n"); hasData = true; }
            if (g.isConst) {
                // put in rodata instead
            }
            fprintf(out, "global %s\n", g.name.c_str());
            fprintf(out, "%s:\n", g.name.c_str());
            if (g.hasStringInit) {
                fprintf(out, "    db ");
                for (char c : g.stringInit) fprintf(out, "%d,", (unsigned char)c);
                fprintf(out, "0\n");
            } else {
                fprintf(out, "    db ");
                for (size_t i = 0; i < g.initData.size(); ++i) {
                    fprintf(out, "%u%s", g.initData[i], (i+1<g.initData.size()?",":""));
                }
                fprintf(out, "\n");
            }
        }
    }

    // .bss section
    bool hasBss = false;
    for (auto& g : globOutputs_) {
        if (g.isZeroInit) {
            if (!hasBss) { fprintf(out, "\nsection .bss\n"); hasBss = true; }
            fprintf(out, "global %s\n", g.name.c_str());
            fprintf(out, "%s: resb %u\n", g.name.c_str(), g.size);
        }
    }

    // .rodata
    bool hasRodata = false;
    for (auto& g : globOutputs_) {
        if (g.isConst && !g.isZeroInit && (!g.initData.empty() || g.hasStringInit || !g.symbolInits.empty())) {
            if (!hasRodata) { fprintf(out, "\nsection .rodata\n"); hasRodata = true; }
            fprintf(out, "global %s\n", g.name.c_str());
            fprintf(out, "%s:\n", g.name.c_str());
            if (g.hasStringInit) {
                fprintf(out, "    db ");
                for (char c : g.stringInit) fprintf(out, "%d,", (unsigned char)c);
                fprintf(out, "0\n");
            } else if (!g.symbolInits.empty()) {
                for (const auto& sym : g.symbolInits) {
                    if (sym == "0") {
                        fprintf(out, "    dq 0\n");
                    } else {
                        fprintf(out, "    dq %s\n", sym.c_str());
                    }
                }
            } else {
                fprintf(out, "    db ");
                for (size_t i = 0; i < g.initData.size(); ++i)
                    fprintf(out, "%u%s", g.initData[i], (i+1<g.initData.size()?",":""));
                fprintf(out, "\n");
            }
        }
    }

    // .text section
    fprintf(out, "\nsection .text\n\n");

    // Declare external references
    for (auto& fo : fnOutputs_) {
        if (fo.isExtern) fprintf(out, "extern %s\n", fo.name.c_str());
    }
    for (auto& go : globOutputs_) {
        if (go.isExtern) fprintf(out, "extern %s\n", go.name.c_str());
    }
    fprintf(out, "\n");

    for (auto& fo : fnOutputs_) {
        if (fo.isExtern) continue;
        fprintf(out, "global %s\n", fo.name.c_str());
        for (auto& line : fo.lines) {
            if (!line.empty() && line.back() == ':')
                fprintf(out, "%s\n", line.c_str());
            else
                fprintf(out, "    %s\n", line.c_str());
        }
        fprintf(out, "\n");
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

        auto bytes = lineProg.emit();
        fprintf(out, "\nsection .debug_line\n");
        for (size_t i = 0; i < bytes.size(); ++i) {
            if (i % 16 == 0) fprintf(out, "\n    db ");
            fprintf(out, "0x%02x%s", bytes[i], (i % 16 == 15 || i == bytes.size() - 1 ? "" : ","));
        }
        fprintf(out, "\n");

        // --- .debug_info ---
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
        fprintf(out, "\nsection .debug_info\n");
        for (size_t i = 0; i < infoBytes.size(); ++i) {
            if (i % 16 == 0) fprintf(out, "\n    db ");
            fprintf(out, "0x%02x%s", infoBytes[i], (i % 16 == 15 || i == infoBytes.size() - 1 ? "" : ","));
        }
        fprintf(out, "\n");

        // --- .debug_abbrev ---
        DWARFAbbrev abbrev;
        auto abbrevBytes = abbrev.emit();
        fprintf(out, "\nsection .debug_abbrev\n");
        for (size_t i = 0; i < abbrevBytes.size(); ++i) {
            if (i % 16 == 0) fprintf(out, "\n    db ");
            fprintf(out, "0x%02x%s", abbrevBytes[i], (i % 16 == 15 || i == abbrevBytes.size() - 1 ? "" : ","));
        }
        fprintf(out, "\n");
    }
}

// ---------------------------------------------------------------------------
// Helper: append bytes to a vector
// ---------------------------------------------------------------------------
static void appendBytes(std::vector<u8>& v, const void* data, size_t n) {
    const u8* p = reinterpret_cast<const u8*>(data);
    v.insert(v.end(), p, p + n);
}
[[maybe_unused]] static void appendU32LE(std::vector<u8>& v, u32 x) { appendBytes(v, &x, 4); }
[[maybe_unused]] static void appendU64LE(std::vector<u8>& v, u64 x) { appendBytes(v, &x, 8); }

// ---------------------------------------------------------------------------
// emitObject — ELF or PE/COFF relocatable object
// ---------------------------------------------------------------------------
std::vector<u8> X64CodeGen::emitObject() {
    bool useELF = (target_.format == TargetFormat::ELF);

    if (useELF) {
        ELFWriter elf(TargetArch::X64);
        ELFSection& text   = elf.textSection();
        ELFSection& data   = elf.dataSection();
        ELFSection& bss    = elf.bssSection();
        ELFSection& rodata = elf.rodataSection();

        // Emit function code into .text as Intel-syntax text-encoded via fprintf→buffer.
        // For a real compiler this would be machine code; we represent it as assembler
        // directives in ASCII (i.e., a textual object). For object emission we build
        // a placeholder stub per function — a real compiler would have an MC layer.
        // Here we emit the text section data as the raw assembly string bytes so the
        // object is useful for inspection. In a production compiler you'd have a binary
        // encoder per instruction.

        // --- .text ---
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
            // Encode the assembly lines as UTF-8 text in .text
            // (In production: binary x86-64 machine code would go here)
            for (auto& line : fo.lines) {
                std::string l = line + "\n";
                text.data.insert(text.data.end(), l.begin(), l.end());
            }
            u64 fnSize = text.data.size() - fnOff;

            ELFSymbol sym;
            sym.name         = fo.name;
            sym.sectionIndex = text.shIndex; // will be resolved on emit()
            sym.value        = fnOff;
            sym.size         = fnSize;
            sym.binding      = STB_GLOBAL;
            sym.type         = STT_FUNC;
            sym.isExternal   = false;
            elf.addSymbol(sym);
        }

        // --- .debug_line ---
        if (debugEnabled_) {
            DWARFLineProgram lineProg;
            std::unordered_map<std::string, u32> fileMap;

            // Find the symbol index for the .text section so we can use it for relocations if needed
            // But here we are using absolute offsets within the section.
            
            // Map function names to their offsets in .text for line table
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

                // End sequence for this function
                DWARFLineEntry endEntry;
                endEntry.address = fnOff + fnSizes[fo.name];
                endEntry.endSequence = true;
                lineProg.addEntry(endEntry);
            }
            elf.debugLineSection().data = lineProg.emit();
        }

        // --- .data / .bss / .rodata for globals ---
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


            ELFSymbol sym;
            sym.name         = go.name;
            sym.sectionIndex = (go.isConst ? rodata.shIndex : (go.isZeroInit ? bss.shIndex : data.shIndex));
            sym.value        = off;
            sym.size         = go.size;
            sym.binding      = STB_GLOBAL;
            sym.type         = STT_OBJECT;
            sym.isExternal   = false;
            elf.addSymbol(sym);
        }

        return elf.emit();

    } else {
        // PE/COFF
        PEWriter pe(TargetArch::X64);
        PESection& text   = pe.textSection();
        PESection& data   = pe.dataSection();
        PESection& bss    = pe.bssSection();
        PESection& rdata  = pe.rdataSection();

        for (auto& fo : fnOutputs_) {
            if (fo.isExtern) {
                PESymbol sym;
                sym.name         = fo.name;
                sym.sectionNumber = IMAGE_SYM_UNDEFINED;
                sym.storageClass = IMAGE_SYM_CLASS_EXTERNAL;
                sym.isExternal   = true;
                pe.addSymbol(sym);
                continue;
            }

            u32 fnOff = (u32)text.data.size();
            for (auto& line : fo.lines) {
                std::string l = line + "\n";
                text.data.insert(text.data.end(), l.begin(), l.end());
            }

            PESymbol sym;
            sym.name         = fo.name;
            sym.sectionNumber = 1; // .text is section 1
            sym.value        = fnOff;
            sym.storageClass = IMAGE_SYM_CLASS_EXTERNAL;
            pe.addSymbol(sym);
        }

        for (auto& go : globOutputs_) {
            if (go.isExtern) {
                PESymbol sym;
                sym.name         = go.name;
                sym.sectionNumber = IMAGE_SYM_UNDEFINED;
                sym.storageClass = IMAGE_SYM_CLASS_EXTERNAL;
                sym.isExternal   = true;
                pe.addSymbol(sym);
                continue;
            }

            u32 off = 0;
            i16 secNum = 1;

            if (go.isZeroInit) {
                off    = (u32)bss.data.size();
                secNum = 3; // .bss
                bss.data.resize(bss.data.size() + go.size, 0);
            } else if (go.isConst) {
                off    = (u32)rdata.data.size();
                secNum = 4; // .rdata
                if (go.hasStringInit) {
                    rdata.data.insert(rdata.data.end(), go.stringInit.begin(), go.stringInit.end());
                    rdata.data.push_back(0);
                } else {
                    rdata.data.insert(rdata.data.end(), go.initData.begin(), go.initData.end());
                }
            } else {
                off    = (u32)data.data.size();
                secNum = 2; // .data
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
            sym.name         = go.name;
            sym.sectionNumber = secNum;
            sym.value        = off;
            sym.storageClass = IMAGE_SYM_CLASS_EXTERNAL;
            pe.addSymbol(sym);
        }

        return pe.emit();
    }
}

// ---------------------------------------------------------------------------
// Internal factory helper — creates an X64CodeGen instance.
// Called by the unified CodeGen::create() in arm64_codegen.cpp.
// Also provides a standalone create() when QC_X64_STANDALONE is defined.
// ---------------------------------------------------------------------------
std::unique_ptr<CodeGen> makeX64CodeGen(const TargetInfo& target, DiagEngine& diag) {
    return std::make_unique<X64CodeGen>(target, diag);
}

#ifdef QC_X64_STANDALONE
std::unique_ptr<CodeGen> CodeGen::create(const TargetInfo& target, DiagEngine& diag) {
    if (target.arch == TargetArch::X64)
        return makeX64CodeGen(target, diag);
    return nullptr;
}
#endif // QC_X64_STANDALONE

} // namespace qc
