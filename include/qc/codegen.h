#pragma once
#include "common.h"
#include "ir.h"

namespace qc {

// Machine instruction operand
struct MachineOp {
    enum class Kind { Reg, Imm, FImm, Mem, Label, Global };
    Kind        kind;
    i64         imm     = 0;
    double      fimm    = 0;
    u32         reg     = 0;    // physical register id
    u32         base    = 0;    // for Mem
    i32         disp    = 0;    // for Mem
    std::string label;          // for Label/Global

    static MachineOp mkReg(u32 r)       { MachineOp o; o.kind=Kind::Reg; o.reg=r; return o; }
    static MachineOp mkImm(i64 v)       { MachineOp o; o.kind=Kind::Imm; o.imm=v; return o; }
    static MachineOp mkFImm(double v)   { MachineOp o; o.kind=Kind::FImm; o.fimm=v; return o; }
    static MachineOp mkMem(u32 b, i32 d){ MachineOp o; o.kind=Kind::Mem; o.base=b; o.disp=d; return o; }
    static MachineOp mkLbl(std::string s){ MachineOp o; o.kind=Kind::Label; o.label=std::move(s); return o; }
    static MachineOp mkGlb(std::string s){ MachineOp o; o.kind=Kind::Global; o.label=std::move(s); return o; }
};

// Machine instruction
struct MachineInstr {
    u32                     opcode;  // arch-specific
    std::string             mnemonic;
    std::vector<MachineOp>  ops;
    std::string             comment;
};

// Machine basic block
struct MachineBlock {
    std::string                  label;
    std::vector<MachineInstr>    instrs;
};

// Machine function
struct MachineFunction {
    std::string                  name;
    std::vector<MachineBlock>    blocks;
    i32                          frameSize  = 0;
    bool                         needsFrame = false;
};

// Abstract code generator interface
class CodeGen {
public:
    virtual ~CodeGen() = default;

    virtual void compile(const IRModule& mod)       = 0;
    virtual void emitAssembly(FILE* out)            = 0;
    virtual std::vector<u8> emitObject()            = 0;
    virtual void setDebugEnabled(bool enabled)      = 0;

    static std::unique_ptr<CodeGen> create(const TargetInfo& target,
                                           DiagEngine& diag);
};

// Register allocator state (arch-independent)
struct RegAlloc {
    std::unordered_map<u64, u32>  virToPhys;  // virtual reg → physical reg
    std::unordered_map<u64, i32>  spillSlots; // virtual reg → stack offset
    i32                           nextSpill = -8;

    u32  get(u64 virt) const;
    bool isSpilled(u64 virt) const { return spillSlots.count(virt) > 0; }
};

} // namespace qc
