#include "qc/ir.h"
#include "qc/type.h"
#include "qc/common.h"
#include <cstdio>
#include <cassert>
#include <string>

namespace qc {

// ============================================================
// IRFunction
// ============================================================

IRBlock* IRFunction::addBlock(std::string name) {
    blocks.push_back(IRBlock{});
    IRBlock& blk = blocks.back();
    blk.name = name.empty() ? ("bb" + std::to_string(blocks.size() - 1)) : std::move(name);
    return &blk;
}

// ============================================================
// IRModule
// ============================================================

IRFunction* IRModule::findFunction(std::string_view n) {
    for (auto& f : functions) {
        if (f.name == n) return &f;
    }
    return nullptr;
}

IRGlobal* IRModule::findGlobal(std::string_view n) {
    for (auto& g : globals) {
        if (g.name == n) return &g;
    }
    return nullptr;
}

// ============================================================
// IRBuilder helpers
// ============================================================

IRInstr& IRBuilder::emit(IRInstr instr) {
    assert(bb_ && "IRBuilder: no current block");
    if (!instr.loc.file) {
        instr.loc = loc_;
    }
    bb_->instrs.push_back(std::move(instr));
    return bb_->instrs.back();
}

IRValue IRBuilder::newReg(TypePtr ty) {
    assert(fn_ && "IRBuilder: no current function");
    return IRValue::reg(ty, fn_->newReg());
}

IRBlock* IRBuilder::newBlock(std::string name) {
    assert(fn_ && "IRBuilder: no current function");
    return fn_->addBlock(std::move(name));
}

// ============================================================
// IRBuilder – memory
// ============================================================

IRValue IRBuilder::makeAlloca(TypePtr ty, std::string /*name*/) {
    IRInstr instr;
    instr.op     = IROpcode::Alloca;
    instr.opType = ty;
    // The result of alloca is a pointer to the type
    TypePtr ptrTy = std::make_shared<PointerType>(ty);
    instr.dst    = newReg(ptrTy);
    IRValue dst  = instr.dst;
    emit(std::move(instr));
    return dst;
}

void IRBuilder::store(IRValue val, IRValue ptr) {
    IRInstr instr;
    instr.op  = IROpcode::Store;
    instr.dst = IRValue::voidVal();
    instr.srcs.push_back(std::move(val));
    instr.srcs.push_back(std::move(ptr));
    emit(std::move(instr));
}

IRValue IRBuilder::load(TypePtr ty, IRValue ptr) {
    IRInstr instr;
    instr.op     = IROpcode::Load;
    instr.opType = ty;
    instr.dst    = newReg(ty);
    IRValue dst  = instr.dst;
    instr.srcs.push_back(std::move(ptr));
    emit(std::move(instr));
    return dst;
}

// ============================================================
// IRBuilder – arithmetic
// ============================================================

IRValue IRBuilder::binop(IROpcode op, TypePtr ty, IRValue lhs, IRValue rhs) {
    IRInstr instr;
    instr.op     = op;
    instr.opType = ty;
    instr.dst    = newReg(ty);
    IRValue dst  = instr.dst;
    instr.srcs.push_back(std::move(lhs));
    instr.srcs.push_back(std::move(rhs));
    emit(std::move(instr));
    return dst;
}

IRValue IRBuilder::unop(IROpcode op, TypePtr ty, IRValue val) {
    IRInstr instr;
    instr.op     = op;
    instr.opType = ty;
    instr.dst    = newReg(ty);
    IRValue dst  = instr.dst;
    instr.srcs.push_back(std::move(val));
    emit(std::move(instr));
    return dst;
}

IRValue IRBuilder::cmp(IROpcode op, IRValue lhs, IRValue rhs) {
    // Result is always i1 (bool).  We create a module-lifetime Bool type here
    // since TypeContext is not accessible in the builder.
    static thread_local TypePtr i1Ty = std::make_shared<BuiltinType>(TypeKind::Bool);
    IRInstr instr;
    instr.op     = op;
    instr.opType = lhs.type;
    instr.dst    = newReg(i1Ty);
    IRValue dst  = instr.dst;
    instr.srcs.push_back(std::move(lhs));
    instr.srcs.push_back(std::move(rhs));
    emit(std::move(instr));
    return dst;
}

// ============================================================
// IRBuilder – cast / call / GEP
// ============================================================

IRValue IRBuilder::cast(IROpcode op, TypePtr dstTy, IRValue val) {
    IRInstr instr;
    instr.op     = op;
    instr.opType = dstTy;
    instr.dst    = newReg(dstTy);
    IRValue dst  = instr.dst;
    instr.srcs.push_back(std::move(val));
    emit(std::move(instr));
    return dst;
}

IRValue IRBuilder::call(TypePtr retTy, IRValue fn, std::vector<IRValue> args) {
    IRInstr instr;
    instr.op     = IROpcode::Call;
    instr.opType = retTy;
    bool hasResult = retTy && !retTy->isVoid();
    if (hasResult) {
        instr.dst = newReg(retTy);
    } else {
        instr.dst = IRValue::voidVal();
    }
    IRValue dst = instr.dst;
    instr.srcs.push_back(std::move(fn));
    for (auto& a : args) {
        instr.srcs.push_back(std::move(a));
    }
    emit(std::move(instr));
    return dst;
}

IRValue IRBuilder::gep(TypePtr elemTy, IRValue base, std::vector<IRValue> idx) {
    // Result is a pointer to elemTy.
    static thread_local std::unordered_map<Type*, TypePtr> ptrCache;
    TypePtr resTy;
    auto it = ptrCache.find(elemTy.get());
    if (it != ptrCache.end()) {
        resTy = it->second;
    } else {
        resTy = std::make_shared<PointerType>(elemTy);
        ptrCache[elemTy.get()] = resTy;
    }

    IRInstr instr;
    instr.op     = IROpcode::GEP;
    instr.opType = elemTy;
    instr.dst    = newReg(resTy);
    IRValue dst  = instr.dst;
    instr.srcs.push_back(std::move(base));
    for (auto& i : idx) {
        instr.srcs.push_back(std::move(i));
    }
    emit(std::move(instr));
    return dst;
}

// ============================================================
// IRBuilder – control flow
// ============================================================

void IRBuilder::br(IRBlock* target) {
    IRInstr instr;
    instr.op    = IROpcode::Br;
    instr.dst   = IRValue::voidVal();
    instr.label = target ? target->name : "";
    emit(std::move(instr));
}

void IRBuilder::condBr(IRValue cond, IRBlock* trueB, IRBlock* falseB) {
    IRInstr instr;
    instr.op     = IROpcode::CondBr;
    instr.dst    = IRValue::voidVal();
    instr.label  = trueB  ? trueB->name  : "";
    instr.label2 = falseB ? falseB->name : "";
    instr.srcs.push_back(std::move(cond));
    emit(std::move(instr));
}

void IRBuilder::ret(IRValue val) {
    IRInstr instr;
    instr.op  = IROpcode::Ret;
    instr.dst = IRValue::voidVal();
    if (!val.isVoid()) {
        instr.srcs.push_back(std::move(val));
    }
    emit(std::move(instr));
}

void IRBuilder::unreachable() {
    IRInstr instr;
    instr.op  = IROpcode::Unreachable;
    instr.dst = IRValue::voidVal();
    emit(std::move(instr));
}

// ============================================================
// IRBuilder – constants
// ============================================================

IRValue IRBuilder::constant(TypePtr ty, u64 v) {
    return IRValue::constant(ty, v);
}

IRValue IRBuilder::fconst(TypePtr ty, double v) {
    return IRValue::fconst(ty, v);
}

// ============================================================
// IRBuilder – hasTerminator
// ============================================================

bool IRBuilder::hasTerminator() const {
    if (!bb_ || bb_->instrs.empty()) return false;
    switch (bb_->instrs.back().op) {
        case IROpcode::Br:
        case IROpcode::CondBr:
        case IROpcode::Ret:
        case IROpcode::Unreachable:
            return true;
        default:
            return false;
    }
}

// ============================================================
// Pretty printer helpers
// ============================================================

static std::string typeStr(const Type* t) {
    if (!t) return "void";
    switch (t->kind()) {
        case TypeKind::Void:      return "void";
        case TypeKind::Bool:      return "i1";
        case TypeKind::Char:
        case TypeKind::SChar:     return "i8";
        case TypeKind::UChar:     return "u8";
        case TypeKind::Short:     return "i16";
        case TypeKind::UShort:    return "u16";
        case TypeKind::Int:       return "i32";
        case TypeKind::UInt:      return "u32";
        case TypeKind::Long:      return "i64";
        case TypeKind::ULong:     return "u64";
        case TypeKind::LongLong:  return "i64";
        case TypeKind::ULongLong: return "u64";
        case TypeKind::Float:     return "f32";
        case TypeKind::Double:    return "f64";
        case TypeKind::LongDouble:return "f80";
        case TypeKind::Pointer:
        case TypeKind::Reference:
        case TypeKind::RValueRef: return "ptr";
        case TypeKind::Array:
        case TypeKind::IncompleteArray: return "ptr";
        case TypeKind::Struct:
        case TypeKind::Union:
        case TypeKind::Class: {
            auto* rt = dynamic_cast<const RecordType*>(t);
            return rt ? ("%struct." + rt->name()) : "struct";
        }
        case TypeKind::Enum:      return "i32";
        case TypeKind::Function:  return "ptr";
        case TypeKind::NullptrT:  return "ptr";
        case TypeKind::Qualified: {
            auto* qt = dynamic_cast<const QualType*>(t);
            return qt ? typeStr(qt->base()) : "?";
        }
        case TypeKind::Typedef:   return t->toString();
        default:                  return "?";
    }
}

static std::string valStr(const IRValue& v) {
    switch (v.kind) {
        case IRValueKind::Void:     return "void";
        case IRValueKind::Constant: return std::to_string((i64)v.id);
        case IRValueKind::FConst: {
            char buf[64];
            std::snprintf(buf, sizeof(buf), "%g", v.fval);
            return buf;
        }
        case IRValueKind::Register: return "%" + std::to_string(v.id);
        case IRValueKind::Global:   return "@" + v.name;
        case IRValueKind::Label:    return v.name;
    }
    return "?";
}

static const char* opcodeName(IROpcode op) {
    switch (op) {
        case IROpcode::Alloca:    return "alloca";
        case IROpcode::Load:      return "load";
        case IROpcode::Store:     return "store";
        case IROpcode::Add:       return "add";
        case IROpcode::Sub:       return "sub";
        case IROpcode::Mul:       return "mul";
        case IROpcode::Div:       return "sdiv";
        case IROpcode::UDiv:      return "udiv";
        case IROpcode::Mod:       return "srem";
        case IROpcode::UMod:      return "urem";
        case IROpcode::And:       return "and";
        case IROpcode::Or:        return "or";
        case IROpcode::Xor:       return "xor";
        case IROpcode::Shl:       return "shl";
        case IROpcode::Shr:       return "lshr";
        case IROpcode::AShr:      return "ashr";
        case IROpcode::Neg:       return "neg";
        case IROpcode::Not:       return "not";
        case IROpcode::FAdd:      return "fadd";
        case IROpcode::FSub:      return "fsub";
        case IROpcode::FMul:      return "fmul";
        case IROpcode::FDiv:      return "fdiv";
        case IROpcode::FNeg:      return "fneg";
        case IROpcode::IEq:       return "icmp eq";
        case IROpcode::INe:       return "icmp ne";
        case IROpcode::ILt:       return "icmp slt";
        case IROpcode::ILe:       return "icmp sle";
        case IROpcode::IGt:       return "icmp sgt";
        case IROpcode::IGe:       return "icmp sge";
        case IROpcode::IULt:      return "icmp ult";
        case IROpcode::IULe:      return "icmp ule";
        case IROpcode::IUGt:      return "icmp ugt";
        case IROpcode::IUGe:      return "icmp uge";
        case IROpcode::FEq:       return "fcmp oeq";
        case IROpcode::FNe:       return "fcmp one";
        case IROpcode::FLt:       return "fcmp olt";
        case IROpcode::FLe:       return "fcmp ole";
        case IROpcode::FGt:       return "fcmp ogt";
        case IROpcode::FGe:       return "fcmp oge";
        case IROpcode::Trunc:     return "trunc";
        case IROpcode::ZExt:      return "zext";
        case IROpcode::SExt:      return "sext";
        case IROpcode::FPTrunc:   return "fptrunc";
        case IROpcode::FPExt:     return "fpext";
        case IROpcode::FPToSI:    return "fptosi";
        case IROpcode::FPToUI:    return "fptoui";
        case IROpcode::SIToFP:    return "sitofp";
        case IROpcode::UIToFP:    return "uitofp";
        case IROpcode::PtrToInt:  return "ptrtoint";
        case IROpcode::IntToPtr:  return "inttoptr";
        case IROpcode::Bitcast:   return "bitcast";
        case IROpcode::Br:        return "br";
        case IROpcode::CondBr:    return "br";
        case IROpcode::Ret:       return "ret";
        case IROpcode::Unreachable: return "unreachable";
        case IROpcode::Call:      return "call";
        case IROpcode::Phi:       return "phi";
        case IROpcode::GEP:       return "getelementptr";
        case IROpcode::MemCopy:   return "memcpy";
        case IROpcode::MemSet:    return "memset";
    }
    return "???";
}

static void dumpInstr(const IRInstr& ins, FILE* out) {
    std::string opName = opcodeName(ins.op);

    switch (ins.op) {
        case IROpcode::Alloca:
            std::fprintf(out, "  %s = alloca %s\n",
                valStr(ins.dst).c_str(),
                ins.opType ? typeStr(ins.opType.get()).c_str() : "?");
            break;

        case IROpcode::Load:
            std::fprintf(out, "  %s = load %s, ptr %s\n",
                valStr(ins.dst).c_str(),
                ins.opType ? typeStr(ins.opType.get()).c_str() : "?",
                ins.srcs.empty() ? "?" : valStr(ins.srcs[0]).c_str());
            break;

        case IROpcode::Store:
            std::fprintf(out, "  store %s %s, ptr %s\n",
                ins.srcs.empty() ? "?" : (ins.srcs[0].type ? typeStr(ins.srcs[0].type.get()).c_str() : "?"),
                ins.srcs.empty() ? "?" : valStr(ins.srcs[0]).c_str(),
                ins.srcs.size() < 2 ? "?" : valStr(ins.srcs[1]).c_str());
            break;

        case IROpcode::Br:
            std::fprintf(out, "  br label %%%s\n", ins.label.c_str());
            break;

        case IROpcode::CondBr:
            std::fprintf(out, "  br i1 %s, label %%%s, label %%%s\n",
                ins.srcs.empty() ? "?" : valStr(ins.srcs[0]).c_str(),
                ins.label.c_str(),
                ins.label2.c_str());
            break;

        case IROpcode::Ret:
            if (ins.srcs.empty()) {
                std::fprintf(out, "  ret void\n");
            } else {
                std::fprintf(out, "  ret %s %s\n",
                    ins.srcs[0].type ? typeStr(ins.srcs[0].type.get()).c_str() : "?",
                    valStr(ins.srcs[0]).c_str());
            }
            break;

        case IROpcode::Unreachable:
            std::fprintf(out, "  unreachable\n");
            break;

        case IROpcode::Call: {
            // dst = call retTy @fn(args...)
            std::string retTy = ins.opType ? typeStr(ins.opType.get()) : "void";
            std::string fnVal = ins.srcs.empty() ? "?" : valStr(ins.srcs[0]);
            std::string argStr;
            for (std::size_t i = 1; i < ins.srcs.size(); ++i) {
                if (i > 1) argStr += ", ";
                if (ins.srcs[i].type) {
                    argStr += typeStr(ins.srcs[i].type.get());
                    argStr += " ";
                }
                argStr += valStr(ins.srcs[i]);
            }
            if (ins.hasDst()) {
                std::fprintf(out, "  %s = call %s %s(%s)\n",
                    valStr(ins.dst).c_str(), retTy.c_str(), fnVal.c_str(), argStr.c_str());
            } else {
                std::fprintf(out, "  call %s %s(%s)\n",
                    retTy.c_str(), fnVal.c_str(), argStr.c_str());
            }
            break;
        }

        case IROpcode::GEP: {
            std::string idxStr;
            for (std::size_t i = 1; i < ins.srcs.size(); ++i) {
                if (!idxStr.empty()) idxStr += ", ";
                idxStr += (ins.srcs[i].type ? typeStr(ins.srcs[i].type.get()) : "i32");
                idxStr += " ";
                idxStr += valStr(ins.srcs[i]);
            }
            std::fprintf(out, "  %s = getelementptr %s, ptr %s, %s\n",
                valStr(ins.dst).c_str(),
                ins.opType ? typeStr(ins.opType.get()).c_str() : "?",
                ins.srcs.empty() ? "?" : valStr(ins.srcs[0]).c_str(),
                idxStr.c_str());
            break;
        }

        // Cast instructions: dst = op srcTy src to dstTy
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
        case IROpcode::Bitcast: {
            std::string srcTyStr = (ins.srcs.empty() || !ins.srcs[0].type)
                                   ? std::string("?")
                                   : typeStr(ins.srcs[0].type.get());
            std::string dstTyStr = ins.opType ? typeStr(ins.opType.get()) : std::string("?");
            std::string srcValStr = ins.srcs.empty() ? std::string("?") : valStr(ins.srcs[0]);
            std::fprintf(out, "  %s = %s %s %s to %s\n",
                valStr(ins.dst).c_str(),
                opName.c_str(),
                srcTyStr.c_str(),
                srcValStr.c_str(),
                dstTyStr.c_str());
            break;
        }

        // Compare instructions
        case IROpcode::IEq: case IROpcode::INe:
        case IROpcode::ILt: case IROpcode::ILe:
        case IROpcode::IGt: case IROpcode::IGe:
        case IROpcode::IULt: case IROpcode::IULe:
        case IROpcode::IUGt: case IROpcode::IUGe:
        case IROpcode::FEq: case IROpcode::FNe:
        case IROpcode::FLt: case IROpcode::FLe:
        case IROpcode::FGt: case IROpcode::FGe: {
            std::string opTyStr = ins.opType ? typeStr(ins.opType.get()) : std::string("?");
            std::string lhsStr  = ins.srcs.size() > 0 ? valStr(ins.srcs[0]) : std::string("?");
            std::string rhsStr  = ins.srcs.size() > 1 ? valStr(ins.srcs[1]) : std::string("?");
            std::fprintf(out, "  %s = %s %s %s, %s\n",
                valStr(ins.dst).c_str(),
                opName.c_str(),
                opTyStr.c_str(),
                lhsStr.c_str(),
                rhsStr.c_str());
            break;
        }

        default: {
            // Generic: dst = op ty operands
            if (ins.hasDst()) {
                std::fprintf(out, "  %s = %s",
                    valStr(ins.dst).c_str(), opName.c_str());
            } else {
                std::fprintf(out, "  %s", opName.c_str());
            }
            if (ins.opType) {
                std::fprintf(out, " %s", typeStr(ins.opType.get()).c_str());
            }
            for (auto& s : ins.srcs) {
                std::fprintf(out, " %s,", valStr(s).c_str());
            }
            std::fprintf(out, "\n");
            break;
        }
    }
}

void dumpFunction(const IRFunction& fn, FILE* out) {
    if (!out) out = stderr;

    // Build parameter string
    std::string params;
    for (std::size_t i = 0; i < fn.params.size(); ++i) {
        if (i) params += ", ";
        const auto& p = fn.params[i];
        if (p.type) params += typeStr(p.type.get()) + " ";
        params += "%" + std::to_string(p.reg);
    }
    if (fn.isVariadic) {
        if (!params.empty()) params += ", ";
        params += "...";
    }

    std::string retTyStr = fn.retType ? typeStr(fn.retType.get()) : "void";

    if (fn.isExtern) {
        std::fprintf(out, "declare %s @%s(%s)\n\n",
            retTyStr.c_str(), fn.name.c_str(), params.c_str());
        return;
    }

    std::fprintf(out, "define %s @%s(%s) {\n",
        retTyStr.c_str(), fn.name.c_str(), params.c_str());

    for (const auto& blk : fn.blocks) {
        std::fprintf(out, "%s:\n", blk.name.c_str());
        for (const auto& ins : blk.instrs) {
            dumpInstr(ins, out);
        }
    }

    std::fprintf(out, "}\n\n");
}

void dumpModule(const IRModule& mod, FILE* out) {
    if (!out) out = stderr;

    std::fprintf(out, "; Module: %s\n\n", mod.name.c_str());

    // Globals
    for (const auto& g : mod.globals) {
        if (g.isExtern) {
            std::fprintf(out, "@%s = external global %s\n",
                g.name.c_str(),
                g.type ? typeStr(g.type.get()).c_str() : "?");
        } else if (g.hasStringInit) {
            std::fprintf(out, "@%s = %sconstant [%zu x i8] c\"%s\\00\"\n",
                g.name.c_str(),
                g.isConst ? "" : "",
                g.stringInit.size() + 1,
                g.stringInit.c_str());
        } else if (g.isZeroInit || g.initData.empty()) {
            std::fprintf(out, "@%s = %sglobal %s zeroinitializer\n",
                g.name.c_str(),
                g.isConst ? "constant " : "",
                g.type ? typeStr(g.type.get()).c_str() : "?");
        } else {
            std::fprintf(out, "@%s = %sglobal %s <init>\n",
                g.name.c_str(),
                g.isConst ? "constant " : "",
                g.type ? typeStr(g.type.get()).c_str() : "?");
        }
    }
    if (!mod.globals.empty()) std::fprintf(out, "\n");

    // Functions
    for (const auto& fn : mod.functions) {
        dumpFunction(fn, out);
    }
}

} // namespace qc
