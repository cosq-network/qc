#include "qc/irgen.h"
#include "qc/ir.h"
#include "qc/ast.h"
#include "qc/type.h"
#include "qc/common.h"
#include <cassert>
#include <string>
#include <vector>

namespace qc {

// ============================================================
// Constructor
// ============================================================

IRGen::IRGen(TypeContext& types, DiagEngine& diag, const TargetInfo& target)
    : types_(types), diag_(diag), target_(target)
{
    mod_.target = target;
}

// ============================================================
// Top-level generate
// ============================================================

IRModule IRGen::generate(const TranslationUnit& tu) {
    for (auto& d : tu.decls) {
        genDecl(d.get());
    }
    return std::move(mod_);
}

// ============================================================
// Declarations
// ============================================================

void IRGen::genDecl(const Decl* d) {
    if (!d) return;
    switch (d->kind()) {
        case DeclKind::Var:
            genVarDecl(static_cast<const VarDecl*>(d));
            break;
        case DeclKind::Function:
            genFuncDecl(static_cast<const FuncDecl*>(d));
            break;
        case DeclKind::Struct:
        case DeclKind::Union:
        case DeclKind::Class:
            genRecordDecl(static_cast<const RecordDecl*>(d));
            break;
        default:
            // Typedef, Enum, Namespace etc. — nothing to emit.
            break;
    }
}

void IRGen::genRecordDecl(const RecordDecl* /*d*/) {
    // Record types are handled at the type level; no IR to emit.
}

void IRGen::genVarDecl(const VarDecl* d) {
    if (!d) return;

    if (d->isGlobal || d->isStatic) {
        // Emit a global variable.
        IRGlobal g;
        g.name      = d->name;
        g.type      = d->type;
        g.isConst   = d->isConstexpr || (d->type && d->type->quals().isConst);
        g.isExtern  = d->isExtern;
        g.isZeroInit = (!d->init);
        g.align     = d->alignment;
        mod_.globals.push_back(std::move(g));
        return;
    }

    // Local variable: alloca + optional store of init.
    assert(curFn_ && "genVarDecl for local called outside function");
    TypePtr ty = d->type ? d->type : types_.intTy();
    IRValue ptr = builder_.makeAlloca(ty, d->name);
    // Find the alloca instruction and set its alignment
    if (!builder_.block()->instrs.empty()) {
        auto& ins = builder_.block()->instrs.back();
        if (ins.op == IROpcode::Alloca) {
            ins.align = d->alignment;
        }
    }
    varMap_[d] = ptr;

    if (d->init) {
        IRValue val = genExpr(d->init.get());
        if (d->type) val = coerce(val, val.type ? val.type.get() : nullptr, d->type.get());
        builder_.store(val, ptr);
    }
}

void IRGen::genFuncDecl(const FuncDecl* d) {
    if (!d) return;

    // Create or find function record.
    IRFunction fn;
    fn.name    = d->name;
    fn.retType = types_.voidTy(); // default
    if (d->type && d->type->isFunction()) {
        auto* ft = static_cast<FunctionType*>(d->type.get());
        fn.retType    = ft->returnType()
                        ? std::shared_ptr<Type>(d->type, ft->returnType())
                        : types_.voidTy();
        fn.isVariadic = ft->isVariadic();
    }
    fn.isExtern = d->isExtern || !d->body;

    // Build param list.
    u64 pReg = 0;
    for (auto& p : d->params) {
        IRParam param;
        param.name = p->name;
        param.type = p->type;
        param.reg  = pReg++;
        fn.params.push_back(param);
        fn.nextReg = pReg; // keep nextReg ahead of param regs
    }
    fn.isVariadic = d->isVariadic;

    mod_.functions.push_back(std::move(fn));
    IRFunction* irfn = &mod_.functions.back();
    curFn_ = irfn;

    if (!d->body) {
        // Declaration only — no body to generate.
        curFn_ = nullptr;
        return;
    }

    builder_.setFunction(irfn);
    IRBlock* entry = irfn->addBlock("entry");
    builder_.setBlock(entry);

    // Make sure nextReg is past all param registers.
    if (irfn->nextReg < (u64)irfn->params.size()) {
        irfn->nextReg = (u64)irfn->params.size();
    }

    // For each parameter, create an alloca + store so the param is addressable.
    for (auto& p : d->params) {
        // Find corresponding IRParam.
        IRParam* irp = nullptr;
        for (auto& ip : irfn->params) {
            if (ip.name == p->name) { irp = &ip; break; }
        }
        if (!irp) continue;

        TypePtr ty = p->type ? p->type : types_.intTy();
        IRValue ptr = builder_.makeAlloca(ty, p->name);
        varMap_[p.get()] = ptr;

        // Store incoming param register into alloca.
        IRValue paramVal = IRValue::reg(ty, irp->reg);
        builder_.store(paramVal, ptr);
    }

    genStmt(d->body.get());

    // Add implicit ret void if no terminator.
    if (!builder_.hasTerminator()) {
        builder_.ret();
    }

    curFn_ = nullptr;
    varMap_.clear();
    labelMap_.clear();
    loopStack_.clear();
    switchBreaks_.clear();
}

// ============================================================
// Statements
// ============================================================

void IRGen::genStmt(const Stmt* s) {
    if (!s) return;
    switch (s->kind()) {
        case StmtKind::Compound:
            genCompoundStmt(static_cast<const CompoundStmt*>(s));
            break;
        case StmtKind::Expr: {
            auto* es = static_cast<const ExprStmt*>(s);
            if (es->expr) genExpr(es->expr.get());
            break;
        }
        case StmtKind::Decl: {
            auto* ds = static_cast<const DeclStmt*>(s);
            for (auto& d : ds->decls) {
                genDecl(d.get());
            }
            break;
        }
        case StmtKind::If:
            genIfStmt(static_cast<const IfStmt*>(s));
            break;
        case StmtKind::While:
            genWhileStmt(static_cast<const WhileStmt*>(s));
            break;
        case StmtKind::DoWhile:
            genDoWhileStmt(static_cast<const DoWhileStmt*>(s));
            break;
        case StmtKind::For:
            genForStmt(static_cast<const ForStmt*>(s));
            break;
        case StmtKind::Switch:
            genSwitchStmt(static_cast<const SwitchStmt*>(s));
            break;
        case StmtKind::Return:
            genReturnStmt(static_cast<const ReturnStmt*>(s));
            break;
        case StmtKind::Break:
            genBreakStmt();
            break;
        case StmtKind::Continue:
            genContinueStmt();
            break;
        case StmtKind::Goto:
            genGotoStmt(static_cast<const GotoStmt*>(s));
            break;
        case StmtKind::Label:
            genLabelStmt(static_cast<const LabelStmt*>(s));
            break;
        case StmtKind::Case:
        case StmtKind::Default: {
            // Handled inside genSwitchStmt; if encountered standalone just gen body.
            auto* cs = static_cast<const CaseStmt*>(s);
            if (cs->body) genStmt(cs->body.get());
            break;
        }
        case StmtKind::Null:
            break;
        default:
            break;
    }
}

void IRGen::genCompoundStmt(const CompoundStmt* s) {
    if (!s) return;
    for (auto& child : s->stmts) {
        genStmt(child.get());
        // If we've hit a terminator (e.g., return/break inside block),
        // subsequent unreachable code can be skipped by checking hasTerminator.
        // We still generate it so labels/decls are processed, but real codegen
        // would normally stop. For correctness we continue.
    }
}

void IRGen::genIfStmt(const IfStmt* s) {
    if (!s) return;

    IRValue cond = genExpr(s->cond.get());
    cond = boolify(cond);

    IRBlock* then_bb  = builder_.newBlock(makeLabel("then"));
    IRBlock* else_bb  = s->els ? builder_.newBlock(makeLabel("else")) : nullptr;
    IRBlock* merge_bb = builder_.newBlock(makeLabel("ifend"));

    builder_.condBr(cond, then_bb, else_bb ? else_bb : merge_bb);

    // Then branch.
    builder_.setBlock(then_bb);
    genStmt(s->then.get());
    if (!builder_.hasTerminator()) {
        builder_.br(merge_bb);
    }

    // Else branch.
    if (else_bb) {
        builder_.setBlock(else_bb);
        genStmt(s->els.get());
        if (!builder_.hasTerminator()) {
            builder_.br(merge_bb);
        }
    }

    builder_.setBlock(merge_bb);
}

void IRGen::genWhileStmt(const WhileStmt* s) {
    if (!s) return;

    IRBlock* header_bb = builder_.newBlock(makeLabel("while.cond"));
    IRBlock* body_bb   = builder_.newBlock(makeLabel("while.body"));
    IRBlock* exit_bb   = builder_.newBlock(makeLabel("while.end"));

    builder_.br(header_bb);

    builder_.setBlock(header_bb);
    IRValue cond = genExpr(s->cond.get());
    cond = boolify(cond);
    builder_.condBr(cond, body_bb, exit_bb);

    builder_.setBlock(body_bb);
    enterLoop(header_bb, exit_bb);
    genStmt(s->body.get());
    exitLoop();
    if (!builder_.hasTerminator()) {
        builder_.br(header_bb);
    }

    builder_.setBlock(exit_bb);
}

void IRGen::genDoWhileStmt(const DoWhileStmt* s) {
    if (!s) return;

    IRBlock* body_bb   = builder_.newBlock(makeLabel("do.body"));
    IRBlock* cond_bb   = builder_.newBlock(makeLabel("do.cond"));
    IRBlock* exit_bb   = builder_.newBlock(makeLabel("do.end"));

    builder_.br(body_bb);

    builder_.setBlock(body_bb);
    enterLoop(cond_bb, exit_bb);
    genStmt(s->body.get());
    exitLoop();
    if (!builder_.hasTerminator()) {
        builder_.br(cond_bb);
    }

    builder_.setBlock(cond_bb);
    IRValue cond = genExpr(s->cond.get());
    cond = boolify(cond);
    builder_.condBr(cond, body_bb, exit_bb);

    builder_.setBlock(exit_bb);
}

void IRGen::genForStmt(const ForStmt* s) {
    if (!s) return;

    // Init.
    if (s->init) genStmt(s->init.get());

    IRBlock* header_bb = builder_.newBlock(makeLabel("for.cond"));
    IRBlock* body_bb   = builder_.newBlock(makeLabel("for.body"));
    IRBlock* step_bb   = builder_.newBlock(makeLabel("for.inc"));
    IRBlock* exit_bb   = builder_.newBlock(makeLabel("for.end"));

    builder_.br(header_bb);

    // Condition.
    builder_.setBlock(header_bb);
    if (s->cond) {
        IRValue cond = genExpr(s->cond.get());
        cond = boolify(cond);
        builder_.condBr(cond, body_bb, exit_bb);
    } else {
        // Infinite loop (no condition).
        builder_.br(body_bb);
    }

    // Body.
    builder_.setBlock(body_bb);
    enterLoop(step_bb, exit_bb);
    genStmt(s->body.get());
    exitLoop();
    if (!builder_.hasTerminator()) {
        builder_.br(step_bb);
    }

    // Step.
    builder_.setBlock(step_bb);
    if (s->step) genExpr(s->step.get());
    if (!builder_.hasTerminator()) {
        builder_.br(header_bb);
    }

    builder_.setBlock(exit_bb);
}

// Helper: collect case/default statements from a compound body.
static void collectCases(const Stmt* s, std::vector<const CaseStmt*>& cases) {
    if (!s) return;
    if (s->kind() == StmtKind::Case || s->kind() == StmtKind::Default) {
        cases.push_back(static_cast<const CaseStmt*>(s));
        // Also check nested case body for fall-through sub-cases.
        if (static_cast<const CaseStmt*>(s)->body)
            collectCases(static_cast<const CaseStmt*>(s)->body.get(), cases);
        return;
    }
    if (s->kind() == StmtKind::Compound) {
        for (auto& child : static_cast<const CompoundStmt*>(s)->stmts) {
            if (child->kind() == StmtKind::Case || child->kind() == StmtKind::Default) {
                cases.push_back(static_cast<const CaseStmt*>(child.get()));
            }
        }
    }
}

void IRGen::genSwitchStmt(const SwitchStmt* s) {
    if (!s) return;

    IRValue cond = genExpr(s->cond.get());
    TypePtr condTy = cond.type ? cond.type : types_.intTy();

    IRBlock* exit_bb = builder_.newBlock(makeLabel("switch.end"));

    // Collect cases.
    std::vector<const CaseStmt*> cases;
    collectCases(s->body.get(), cases);

    // Create a block for each case.
    std::vector<IRBlock*> caseBlocks;
    IRBlock* defaultBlock = nullptr;
    for (auto* cs : cases) {
        IRBlock* cb = builder_.newBlock(makeLabel(cs->isDefault ? "switch.default" : "switch.case"));
        caseBlocks.push_back(cb);
        if (cs->isDefault) defaultBlock = cb;
    }
    if (!defaultBlock) defaultBlock = exit_bb;

    // Emit comparison chain.
    IRBlock* chainStart = builder_.newBlock(makeLabel("switch.chain"));
    builder_.br(chainStart);
    builder_.setBlock(chainStart);

    for (std::size_t i = 0; i < cases.size(); ++i) {
        const CaseStmt* cs = cases[i];
        if (cs->isDefault) continue; // default handled at the end of chain

        IRValue caseVal = genExpr(cs->value.get());
        if (caseVal.type && condTy) {
            caseVal = coerce(caseVal, caseVal.type.get(), condTy.get());
        }

        IRValue cmpResult = builder_.cmp(IROpcode::IEq, cond, caseVal);

        // Next test block: skip over default cases in the chain.
        std::size_t ni = i + 1;
        while (ni < cases.size() && cases[ni]->isDefault) ++ni;
        IRBlock* nextTest = (ni < cases.size())
            ? builder_.newBlock(makeLabel("switch.chain"))
            : defaultBlock;

        builder_.condBr(cmpResult, caseBlocks[i], nextTest);
        builder_.setBlock(nextTest);
    }
    // At the end of the chain, fall through to default/exit.
    if (!builder_.hasTerminator()) {
        builder_.br(defaultBlock);
    }

    // Emit case bodies.
    switchBreaks_.push_back(exit_bb);

    if (s->body && s->body->kind() == StmtKind::Compound) {
        auto* body = static_cast<const CompoundStmt*>(s->body.get());
        std::size_t caseIdx = 0;
        for (auto& child : body->stmts) {
            if (child->kind() == StmtKind::Case || child->kind() == StmtKind::Default) {
                if (caseIdx < caseBlocks.size()) {
                    if (!builder_.hasTerminator()) {
                        builder_.br(caseBlocks[caseIdx]);
                    }
                    builder_.setBlock(caseBlocks[caseIdx]);
                    ++caseIdx;
                }
                auto* cs = static_cast<const CaseStmt*>(child.get());
                if (cs->body) genStmt(cs->body.get());
            } else {
                genStmt(child.get());
            }
        }
    } else {
        genStmt(s->body.get());
    }

    switchBreaks_.pop_back();

    if (!builder_.hasTerminator()) {
        builder_.br(exit_bb);
    }
    builder_.setBlock(exit_bb);
}

void IRGen::genReturnStmt(const ReturnStmt* s) {
    if (!s) return;
    if (s->value) {
        IRValue val = genExpr(s->value.get());
        // Coerce to function return type if needed.
        if (curFn_ && curFn_->retType && val.type) {
            val = coerce(val, val.type.get(), curFn_->retType.get());
        }
        builder_.ret(val);
    } else {
        builder_.ret();
    }
}

void IRGen::genBreakStmt() {
    // Break exits the innermost loop or switch. When both are present,
    // the loop is innermost (it was entered after the switch).
    IRBlock* target = nullptr;
    if (!loopStack_.empty()) {
        target = loopStack_.back().brk;
    } else if (!switchBreaks_.empty()) {
        target = switchBreaks_.back();
    }
    if (target) builder_.br(target);
    else builder_.unreachable();
}

void IRGen::genContinueStmt() {
    IRBlock* target = nullptr;
    if (!loopStack_.empty()) {
        target = loopStack_.back().cont;
    }
    if (target) builder_.br(target);
    else builder_.unreachable();
}

void IRGen::genGotoStmt(const GotoStmt* s) {
    if (!s) return;
    // Look up or create a block for the label.
    auto it = labelMap_.find(s->label);
    if (it != labelMap_.end()) {
        builder_.br(it->second);
    } else {
        // Forward reference: create block now, it will be filled when label is encountered.
        IRBlock* target = builder_.newBlock(s->label);
        labelMap_[s->label] = target;
        builder_.br(target);
    }
}

void IRGen::genLabelStmt(const LabelStmt* s) {
    if (!s) return;
    IRBlock* labelBlock = nullptr;
    auto it = labelMap_.find(s->label);
    if (it != labelMap_.end()) {
        labelBlock = it->second;
    } else {
        labelBlock = builder_.newBlock(s->label);
        labelMap_[s->label] = labelBlock;
    }
    if (!builder_.hasTerminator()) {
        builder_.br(labelBlock);
    }
    builder_.setBlock(labelBlock);
    if (s->body) genStmt(s->body.get());
}

// ============================================================
// Expressions
// ============================================================

IRValue IRGen::genExpr(const Expr* e) {
    if (!e) return IRValue::voidVal();
    switch (e->kind()) {
        case ExprKind::IntLit:    return genIntLit(static_cast<const IntLitExpr*>(e));
        case ExprKind::FloatLit:  return genFloatLit(static_cast<const FloatLitExpr*>(e));
        case ExprKind::StringLit: return genStringLit(static_cast<const StringLitExpr*>(e));
        case ExprKind::CharLit: {
            auto* ce = static_cast<const CharLitExpr*>(e);
            return IRValue::constant(types_.intTy(), ce->value);
        }
        case ExprKind::BoolLit: {
            auto* be = static_cast<const BoolLitExpr*>(e);
            return IRValue::constant(types_.boolTy(), be->value ? 1 : 0);
        }
        case ExprKind::NullptrLit:
            return IRValue::constant(types_.ptrTo(types_.voidTy()), 0);
        case ExprKind::Ident:    return genIdent(static_cast<const IdentExpr*>(e));
        case ExprKind::Binary:   return genBinary(static_cast<const BinaryExpr*>(e));
        case ExprKind::Unary:    return genUnary(static_cast<const UnaryExpr*>(e));
        case ExprKind::Ternary:  return genTernary(static_cast<const TernaryExpr*>(e));
        case ExprKind::Call:     return genCall(static_cast<const CallExpr*>(e));
        case ExprKind::Index:    return genIndex(static_cast<const IndexExpr*>(e));
        case ExprKind::Member:
        case ExprKind::MemberPtr: return genMember(static_cast<const MemberExpr*>(e));
        case ExprKind::Cast:
        case ExprKind::CStyle_Cast: return genCast(static_cast<const CastExpr*>(e));
        case ExprKind::Sizeof:   return genSizeof(static_cast<const SizeofExpr*>(e));
        case ExprKind::Alignof:  return genAlignof(static_cast<const AlignofExpr*>(e));
        case ExprKind::Generic: {
            auto* ge = static_cast<const GenericExpr*>(e);
            if (ge->resultExpr) return genExpr(ge->resultExpr.get());
            return IRValue::voidVal();
        }
        case ExprKind::Assign:   return genAssign(static_cast<const AssignExpr*>(e));
        case ExprKind::InitList: return genInitList(static_cast<const InitListExpr*>(e),
                                                    IRValue::voidVal());
        default:
            return IRValue::voidVal();
    }
}

IRValue IRGen::genLValue(const Expr* e) {
    if (!e) return IRValue::voidVal();
    switch (e->kind()) {
        case ExprKind::Ident: {
            auto* ie = static_cast<const IdentExpr*>(e);
            if (ie->decl) {
                auto it = varMap_.find(ie->decl);
                if (it != varMap_.end()) return it->second;
                // Global: return global value.
                IRGlobal* g = mod_.findGlobal(ie->name);
                if (g) return IRValue::global(g->type ? g->type : types_.ptrTo(types_.voidTy()), g->name);
            }
            return IRValue::voidVal();
        }
        case ExprKind::Unary: {
            auto* ue = static_cast<const UnaryExpr*>(e);
            if (ue->op == UnaryOp::Deref) {
                // *ptr → the pointer itself is the lvalue address.
                return genExpr(ue->operand.get());
            }
            return IRValue::voidVal();
        }
        case ExprKind::Member:
        case ExprKind::MemberPtr: {
            auto* me = static_cast<const MemberExpr*>(e);
            IRValue basePtr;
            if (me->isArrow) {
                basePtr = genExpr(me->base.get());
            } else {
                basePtr = genLValue(me->base.get());
            }
            if (!me->field || basePtr.isVoid()) return IRValue::voidVal();

            TypePtr fieldTy = me->field->type ? me->field->type : types_.intTy();
            TypePtr i32Ty = types_.intTy();
            IRValue offset = IRValue::constant(i32Ty, (u64)me->field->offset);
            return builder_.gep(fieldTy, basePtr, { offset });
        }
        case ExprKind::Index: {
            auto* ie = static_cast<const IndexExpr*>(e);
            IRValue base = genExpr(ie->base.get());
            IRValue idx  = genExpr(ie->index.get());
            TypePtr elemTy = e->type ? e->type : types_.intTy();
            return builder_.gep(elemTy, base, { idx });
        }
        default:
            return IRValue::voidVal();
    }
}

IRValue IRGen::genIntLit(const IntLitExpr* e) {
    TypePtr ty = e->type ? e->type : (e->isSigned ? types_.intTy() : types_.uintTy());
    return IRValue::constant(ty, e->value);
}

IRValue IRGen::genFloatLit(const FloatLitExpr* e) {
    TypePtr ty = e->type ? e->type : types_.doubleTy();
    return IRValue::fconst(ty, e->value);
}

IRValue IRGen::genStringLit(const StringLitExpr* e) {
    // Deduplicate.
    auto it = stringMap_.find(e->value);
    std::string gname;
    if (it != stringMap_.end()) {
        gname = it->second;
    } else {
        gname = makeStringName();
        stringMap_[e->value] = gname;

        IRGlobal g;
        g.name          = gname;
        g.isConst       = true;
        g.hasStringInit = true;
        g.stringInit    = e->value;
        // Type: [N x i8]
        g.type = types_.arrayOf(types_.charTy(), (i64)(e->value.size() + 1));
        mod_.globals.push_back(std::move(g));
    }

    IRGlobal* g = mod_.findGlobal(gname);
    TypePtr ptrTy = types_.ptrTo(types_.charTy());
    return IRValue::global(ptrTy, gname);
}

IRValue IRGen::genIdent(const IdentExpr* e) {
    if (!e) return IRValue::voidVal();

    // Try local variable map.
    if (e->decl) {
        auto it = varMap_.find(e->decl);
        if (it != varMap_.end()) {
            // Load from alloca.
            TypePtr ty = e->decl->type ? e->decl->type : types_.intTy();
            return builder_.load(ty, it->second);
        }
    }

    // Try global.
    IRGlobal* g = mod_.findGlobal(e->name);
    if (g) {
        TypePtr ptrTy = types_.ptrTo(g->type ? g->type : types_.intTy());
        IRValue gVal = IRValue::global(ptrTy, g->name);
        if (e->type) {
            return builder_.load(e->type, gVal);
        }
        return gVal;
    }

    // Try function.
    IRFunction* fn = mod_.findFunction(e->name);
    if (fn) {
        TypePtr ptrTy = types_.ptrTo(types_.voidTy());
        return IRValue::global(ptrTy, fn->name);
    }

    return IRValue::voidVal();
}

IRValue IRGen::genBinary(const BinaryExpr* e) {
    if (!e) return IRValue::voidVal();

    // Short-circuit for logical AND / OR.
    // Strategy: alloca the result in entry block, store default, then store
    // the rhs value conditionally, load in merge block.
    if (e->op == BinaryOp::LAnd) {
        TypePtr i32 = types_.intTy();
        // Allocate result slot in the current block (before branches).
        IRValue resPtr = builder_.makeAlloca(i32, "land.res");
        builder_.store(IRValue::constant(i32, 0), resPtr); // default: false

        IRValue lhsVal = genExpr(e->lhs.get());
        lhsVal = boolify(lhsVal);

        IRBlock* rhs_bb   = builder_.newBlock(makeLabel("land.rhs"));
        IRBlock* merge_bb = builder_.newBlock(makeLabel("land.end"));
        builder_.condBr(lhsVal, rhs_bb, merge_bb);

        builder_.setBlock(rhs_bb);
        IRValue rhsVal = genExpr(e->rhs.get());
        rhsVal = boolify(rhsVal);
        // Extend bool to i32 if needed.
        if (rhsVal.type && rhsVal.type->kind() == TypeKind::Bool) {
            rhsVal = builder_.cast(IROpcode::ZExt, i32, rhsVal);
        }
        builder_.store(rhsVal, resPtr);
        builder_.br(merge_bb);

        builder_.setBlock(merge_bb);
        return builder_.load(i32, resPtr);
    }

    if (e->op == BinaryOp::LOr) {
        TypePtr i32 = types_.intTy();
        IRValue resPtr = builder_.makeAlloca(i32, "lor.res");
        builder_.store(IRValue::constant(i32, 1), resPtr); // default: true

        IRValue lhsVal = genExpr(e->lhs.get());
        lhsVal = boolify(lhsVal);

        IRBlock* rhs_bb   = builder_.newBlock(makeLabel("lor.rhs"));
        IRBlock* merge_bb = builder_.newBlock(makeLabel("lor.end"));
        builder_.condBr(lhsVal, merge_bb, rhs_bb);

        builder_.setBlock(rhs_bb);
        IRValue rhsVal = genExpr(e->rhs.get());
        rhsVal = boolify(rhsVal);
        if (rhsVal.type && rhsVal.type->kind() == TypeKind::Bool) {
            rhsVal = builder_.cast(IROpcode::ZExt, i32, rhsVal);
        }
        builder_.store(rhsVal, resPtr);
        builder_.br(merge_bb);

        builder_.setBlock(merge_bb);
        return builder_.load(i32, resPtr);
    }

    if (e->op == BinaryOp::Comma) {
        genExpr(e->lhs.get()); // discard
        return genExpr(e->rhs.get());
    }

    IRValue lhs = genExpr(e->lhs.get());
    IRValue rhs = genExpr(e->rhs.get());

    TypePtr ty = e->type ? e->type : (lhs.type ? lhs.type : types_.intTy());

    // Comparison operators.
    switch (e->op) {
        case BinaryOp::Eq: case BinaryOp::Ne:
        case BinaryOp::Lt: case BinaryOp::Le:
        case BinaryOp::Gt: case BinaryOp::Ge: {
            Type* rawTy = ty.get();
            IROpcode cmpOp;
            if (rawTy && rawTy->isFloat()) {
                cmpOp = selectFCmp(e->op);
            } else {
                // Determine signed/unsigned from lhs type.
                Type* lhsTy = lhs.type ? lhs.type.get() : rawTy;
                bool isSigned = true;
                if (lhsTy) {
                    auto k = lhsTy->kind();
                    if (k == TypeKind::UChar || k == TypeKind::UShort ||
                        k == TypeKind::UInt  || k == TypeKind::ULong  ||
                        k == TypeKind::ULongLong || k == TypeKind::Pointer ||
                        k == TypeKind::NullptrT) {
                        isSigned = false;
                    }
                }
                cmpOp = selectIntCmp(e->op, isSigned);
            }
            return builder_.cmp(cmpOp, lhs, rhs);
        }
        default:
            break;
    }

    IROpcode op = selectBinOp(e->op, ty.get());
    return builder_.binop(op, ty, lhs, rhs);
}

IRValue IRGen::genUnary(const UnaryExpr* e) {
    if (!e) return IRValue::voidVal();

    switch (e->op) {
        case UnaryOp::AddrOf:
            return genLValue(e->operand.get());

        case UnaryOp::Deref: {
            IRValue ptr = genExpr(e->operand.get());
            TypePtr elemTy = e->type ? e->type : types_.intTy();
            return builder_.load(elemTy, ptr);
        }

        case UnaryOp::Plus:
            return genExpr(e->operand.get());

        case UnaryOp::Neg: {
            IRValue val = genExpr(e->operand.get());
            TypePtr ty = e->type ? e->type : (val.type ? val.type : types_.intTy());
            if (ty->isFloat()) {
                return builder_.unop(IROpcode::FNeg, ty, val);
            }
            IRValue zero = IRValue::constant(ty, 0);
            return builder_.binop(IROpcode::Sub, ty, zero, val);
        }

        case UnaryOp::Not: {
            // Bitwise NOT: xor with -1 (all ones).
            IRValue val = genExpr(e->operand.get());
            TypePtr ty = e->type ? e->type : (val.type ? val.type : types_.intTy());
            IRValue allOnes = IRValue::constant(ty, ~(u64)0);
            return builder_.binop(IROpcode::Xor, ty, val, allOnes);
        }

        case UnaryOp::LNot: {
            // Logical NOT: cmp eq 0, zext to int.
            IRValue val = genExpr(e->operand.get());
            TypePtr ty = val.type ? val.type : types_.intTy();
            IRValue zero = IRValue::constant(ty, 0);
            IRValue cmpResult = builder_.cmp(IROpcode::IEq, val, zero);
            TypePtr retTy = e->type ? e->type : types_.intTy();
            return builder_.cast(IROpcode::ZExt, retTy, cmpResult);
        }

        case UnaryOp::PreInc:
        case UnaryOp::PreDec: {
            IRValue ptr = genLValue(e->operand.get());
            TypePtr ty  = e->type ? e->type : types_.intTy();
            IRValue old = builder_.load(ty, ptr);
            IRValue one = IRValue::constant(ty, 1);
            IROpcode addSub = (e->op == UnaryOp::PreInc) ? IROpcode::Add : IROpcode::Sub;
            IRValue newVal = builder_.binop(addSub, ty, old, one);
            builder_.store(newVal, ptr);
            return newVal;
        }

        case UnaryOp::PostInc:
        case UnaryOp::PostDec: {
            IRValue ptr = genLValue(e->operand.get());
            TypePtr ty  = e->type ? e->type : types_.intTy();
            IRValue old = builder_.load(ty, ptr);
            IRValue one = IRValue::constant(ty, 1);
            IROpcode addSub = (e->op == UnaryOp::PostInc) ? IROpcode::Add : IROpcode::Sub;
            IRValue newVal = builder_.binop(addSub, ty, old, one);
            builder_.store(newVal, ptr);
            return old; // return original value
        }
    }
    return IRValue::voidVal();
}

IRValue IRGen::genTernary(const TernaryExpr* e) {
    if (!e) return IRValue::voidVal();

    TypePtr ty = e->type ? e->type : types_.intTy();

    // Alloca for the result, placed before the condition evaluation.
    IRValue resPtr = builder_.makeAlloca(ty, "ternary.res");

    IRValue cond = genExpr(e->cond.get());
    cond = boolify(cond);

    IRBlock* then_bb  = builder_.newBlock(makeLabel("ternary.true"));
    IRBlock* else_bb  = builder_.newBlock(makeLabel("ternary.false"));
    IRBlock* merge_bb = builder_.newBlock(makeLabel("ternary.end"));

    builder_.condBr(cond, then_bb, else_bb);

    builder_.setBlock(then_bb);
    IRValue thenVal = genExpr(e->then.get());
    builder_.store(thenVal, resPtr);
    if (!builder_.hasTerminator()) builder_.br(merge_bb);

    builder_.setBlock(else_bb);
    IRValue elseVal = genExpr(e->els.get());
    builder_.store(elseVal, resPtr);
    if (!builder_.hasTerminator()) builder_.br(merge_bb);

    builder_.setBlock(merge_bb);
    return builder_.load(ty, resPtr);
}

IRValue IRGen::genCall(const CallExpr* e) {
    if (!e) return IRValue::voidVal();

    IRValue callee = genExpr(e->callee.get());
    TypePtr retTy = e->type ? e->type : types_.voidTy();

    // If callee is a function pointer via ident, get return type from function decl.
    if (e->callee && e->callee->kind() == ExprKind::Ident) {
        auto* ie = static_cast<const IdentExpr*>(e->callee.get());
        IRFunction* fn = mod_.findFunction(ie->name);
        if (fn && fn->retType) retTy = fn->retType;
    }

    std::vector<IRValue> args;
    for (auto& arg : e->args) {
        args.push_back(genExpr(arg.get()));
    }

    return builder_.call(retTy, callee, std::move(args));
}

IRValue IRGen::genMember(const MemberExpr* e) {
    if (!e) return IRValue::voidVal();

    IRValue basePtr;
    if (e->isArrow) {
        basePtr = genExpr(e->base.get()); // already a pointer
    } else {
        basePtr = genLValue(e->base.get());
    }

    if (!e->field || basePtr.isVoid()) return IRValue::voidVal();

    TypePtr fieldTy = e->field->type ? e->field->type : types_.intTy();
    TypePtr i32Ty   = types_.intTy();
    IRValue offset  = IRValue::constant(i32Ty, (u64)e->field->offset);
    IRValue ptr     = builder_.gep(fieldTy, basePtr, { offset });
    return builder_.load(fieldTy, ptr);
}

IRValue IRGen::genIndex(const IndexExpr* e) {
    if (!e) return IRValue::voidVal();

    IRValue base = genExpr(e->base.get());
    IRValue idx  = genExpr(e->index.get());
    TypePtr elemTy = e->type ? e->type : types_.intTy();
    IRValue ptr = builder_.gep(elemTy, base, { idx });
    return builder_.load(elemTy, ptr);
}

IRValue IRGen::genCast(const CastExpr* e) {
    if (!e) return IRValue::voidVal();
    IRValue val = genExpr(e->operand.get());
    TypePtr srcTy = val.type ? val.type : (e->operand->type ? e->operand->type : types_.intTy());
    TypePtr dstTy = e->targetType ? e->targetType : (e->type ? e->type : types_.intTy());
    if (!srcTy || !dstTy) return val;
    IROpcode op = selectCastOp(srcTy.get(), dstTy.get());
    if (op == IROpcode::Bitcast && srcTy.get() == dstTy.get()) return val; // no-op
    return builder_.cast(op, dstTy, val);
}

IRValue IRGen::genSizeof(const SizeofExpr* e) {
    if (!e) return IRValue::voidVal();
    u32 sz = 0;
    if (e->isType && e->targetType) {
        sz = e->targetType->size();
    } else if (e->operand && e->operand->type) {
        sz = e->operand->type->size();
    }
    TypePtr ty = e->type ? e->type : types_.ulonglongTy();
    return IRValue::constant(ty, (u64)sz);
}

IRValue IRGen::genAlignof(const AlignofExpr* e) {
    if (!e) return IRValue::voidVal();
    u32 al = 0;
    if (e->isType && e->targetType) {
        al = e->targetType->align();
    } else if (e->operand && e->operand->type) {
        al = e->operand->type->align();
    }
    TypePtr ty = e->type ? e->type : types_.ulonglongTy();
    return IRValue::constant(ty, (u64)al);
}

IRValue IRGen::genAssign(const AssignExpr* e) {
    if (!e) return IRValue::voidVal();

    IRValue lhsPtr = genLValue(e->lhs.get());
    IRValue rhs    = genExpr(e->rhs.get());

    TypePtr lhsTy = e->lhs->type ? e->lhs->type : types_.intTy();

    if (e->op == AssignOp::Assign) {
        rhs = coerce(rhs, rhs.type ? rhs.type.get() : nullptr, lhsTy.get());
        builder_.store(rhs, lhsPtr);
        return rhs;
    }

    // Compound assignment: load lhs, apply op, store back.
    IRValue lhsVal = builder_.load(lhsTy, lhsPtr);

    IROpcode binOp;
    switch (e->op) {
        case AssignOp::AddAssign: binOp = lhsTy->isFloat() ? IROpcode::FAdd : IROpcode::Add; break;
        case AssignOp::SubAssign: binOp = lhsTy->isFloat() ? IROpcode::FSub : IROpcode::Sub; break;
        case AssignOp::MulAssign: binOp = lhsTy->isFloat() ? IROpcode::FMul : IROpcode::Mul; break;
        case AssignOp::DivAssign: binOp = lhsTy->isFloat() ? IROpcode::FDiv : IROpcode::Div; break;
        case AssignOp::ModAssign: binOp = IROpcode::Mod; break;
        case AssignOp::AndAssign: binOp = IROpcode::And; break;
        case AssignOp::OrAssign:  binOp = IROpcode::Or;  break;
        case AssignOp::XorAssign: binOp = IROpcode::Xor; break;
        case AssignOp::ShlAssign: binOp = IROpcode::Shl; break;
        case AssignOp::ShrAssign: binOp = IROpcode::Shr; break;
        default: binOp = IROpcode::Add; break;
    }

    rhs = coerce(rhs, rhs.type ? rhs.type.get() : nullptr, lhsTy.get());
    IRValue result = builder_.binop(binOp, lhsTy, lhsVal, rhs);
    builder_.store(result, lhsPtr);
    return result;
}

IRValue IRGen::genInitList(const InitListExpr* e, IRValue dest) {
    if (!e) return IRValue::voidVal();
    // If dest is valid (an alloca), store each element.
    if (!dest.isVoid()) {
        TypePtr i32 = types_.intTy();
        for (std::size_t i = 0; i < e->inits.size(); ++i) {
            IRValue val = genExpr(e->inits[i].get());
            IRValue idx = IRValue::constant(i32, (u64)i);
            TypePtr elemTy = e->inits[i]->type ? e->inits[i]->type : types_.intTy();
            IRValue ptr = builder_.gep(elemTy, dest, { idx });
            builder_.store(val, ptr);
        }
        return dest;
    }
    // Otherwise just evaluate all initializers and return last.
    IRValue last = IRValue::voidVal();
    for (auto& init : e->inits) {
        last = genExpr(init.get());
    }
    return last;
}

// ============================================================
// Helpers
// ============================================================

IRValue IRGen::coerce(IRValue v, Type* from, Type* to) {
    if (!from || !to) return v;
    from = stripQuals(from);
    to   = stripQuals(to);
    if (typeEqual(from, to)) return v;
    IROpcode op = selectCastOp(from, to);
    TypePtr toTy;
    // Build a shared_ptr for 'to' — we need one from the module context.
    // Since we can't get it from TypeContext by raw pointer, create a wrapper.
    if (v.type && v.type.get() == to) toTy = v.type;
    else {
        // Build the appropriate type ptr via TypeContext.
        switch (to->kind()) {
            case TypeKind::Void:      toTy = types_.voidTy(); break;
            case TypeKind::Bool:      toTy = types_.boolTy(); break;
            case TypeKind::Char:      toTy = types_.charTy(); break;
            case TypeKind::SChar:     toTy = types_.scharTy(); break;
            case TypeKind::UChar:     toTy = types_.ucharTy(); break;
            case TypeKind::Short:     toTy = types_.shortTy(); break;
            case TypeKind::UShort:    toTy = types_.ushortTy(); break;
            case TypeKind::Int:       toTy = types_.intTy(); break;
            case TypeKind::UInt:      toTy = types_.uintTy(); break;
            case TypeKind::Long:      toTy = types_.longTy(); break;
            case TypeKind::ULong:     toTy = types_.ulongTy(); break;
            case TypeKind::LongLong:  toTy = types_.longlongTy(); break;
            case TypeKind::ULongLong: toTy = types_.ulonglongTy(); break;
            case TypeKind::Float:     toTy = types_.floatTy(); break;
            case TypeKind::Double:    toTy = types_.doubleTy(); break;
            case TypeKind::Pointer:
            case TypeKind::Reference:
            case TypeKind::RValueRef: {
                auto* pt = static_cast<PointerType*>(to);
                TypePtr pointee;
                // Try to get pointee via same trick.
                switch (pt->pointee()->kind()) {
                    case TypeKind::Void: pointee = types_.voidTy(); break;
                    case TypeKind::Char: pointee = types_.charTy(); break;
                    case TypeKind::Int:  pointee = types_.intTy();  break;
                    default: pointee = types_.voidTy(); break;
                }
                toTy = types_.ptrTo(pointee);
                break;
            }
            default:
                // Use a shared_ptr alias to the 'to' type if possible from v.
                toTy = v.type; // best effort
                break;
        }
    }
    if (!toTy) return v;
    // No-op cast check.
    if (op == IROpcode::Bitcast && from == to) return v;
    return builder_.cast(op, toTy, v);
}

IRValue IRGen::boolify(IRValue v) {
    if (v.type && v.type->kind() == TypeKind::Bool) return v;
    TypePtr ty = v.type ? v.type : types_.intTy();
    IRValue zero = IRValue::constant(ty, 0);
    return builder_.cmp(IROpcode::INe, v, zero);
}

IROpcode IRGen::selectIntCmp(BinaryOp op, bool isSigned) {
    switch (op) {
        case BinaryOp::Eq: return IROpcode::IEq;
        case BinaryOp::Ne: return IROpcode::INe;
        case BinaryOp::Lt: return isSigned ? IROpcode::ILt : IROpcode::IULt;
        case BinaryOp::Le: return isSigned ? IROpcode::ILe : IROpcode::IULe;
        case BinaryOp::Gt: return isSigned ? IROpcode::IGt : IROpcode::IUGt;
        case BinaryOp::Ge: return isSigned ? IROpcode::IGe : IROpcode::IUGe;
        default:           return IROpcode::IEq;
    }
}

IROpcode IRGen::selectFCmp(BinaryOp op) {
    switch (op) {
        case BinaryOp::Eq: return IROpcode::FEq;
        case BinaryOp::Ne: return IROpcode::FNe;
        case BinaryOp::Lt: return IROpcode::FLt;
        case BinaryOp::Le: return IROpcode::FLe;
        case BinaryOp::Gt: return IROpcode::FGt;
        case BinaryOp::Ge: return IROpcode::FGe;
        default:           return IROpcode::FEq;
    }
}

IROpcode IRGen::selectBinOp(BinaryOp op, Type* ty) {
    bool isFloat = ty && ty->isFloat();
    bool isUnsigned = false;
    if (ty) {
        auto k = ty->kind();
        isUnsigned = (k == TypeKind::UChar || k == TypeKind::UShort ||
                      k == TypeKind::UInt  || k == TypeKind::ULong  ||
                      k == TypeKind::ULongLong);
    }
    switch (op) {
        case BinaryOp::Add: return isFloat ? IROpcode::FAdd : IROpcode::Add;
        case BinaryOp::Sub: return isFloat ? IROpcode::FSub : IROpcode::Sub;
        case BinaryOp::Mul: return isFloat ? IROpcode::FMul : IROpcode::Mul;
        case BinaryOp::Div: return isFloat ? IROpcode::FDiv : IROpcode::Div;
        case BinaryOp::Mod: return IROpcode::Mod;
        case BinaryOp::And: return IROpcode::And;
        case BinaryOp::Or:  return IROpcode::Or;
        case BinaryOp::Xor: return IROpcode::Xor;
        case BinaryOp::Shl: return IROpcode::Shl;
        case BinaryOp::Shr: return isUnsigned ? IROpcode::Shr : IROpcode::AShr;
        default:            return IROpcode::Add;
    }
}

IROpcode IRGen::selectCastOp(Type* from, Type* to) {
    if (!from || !to) return IROpcode::Bitcast;
    from = stripQuals(from);
    to   = stripQuals(to);

    bool fromFloat = from->isFloat();
    bool toFloat   = to->isFloat();
    bool fromInt   = from->isInteger() || from->kind() == TypeKind::Bool || from->kind() == TypeKind::Enum;
    bool toInt     = to->isInteger()   || to->kind() == TypeKind::Bool   || to->kind() == TypeKind::Enum;
    bool fromPtr   = from->isPointer() || from->kind() == TypeKind::NullptrT;
    bool toPtr     = to->isPointer()   || to->kind() == TypeKind::NullptrT;

    if (fromFloat && toFloat) {
        return (from->size() > to->size()) ? IROpcode::FPTrunc : IROpcode::FPExt;
    }
    if (fromFloat && toInt) {
        auto k = to->kind();
        bool toUnsigned = (k == TypeKind::UChar || k == TypeKind::UShort ||
                           k == TypeKind::UInt  || k == TypeKind::ULong  ||
                           k == TypeKind::ULongLong);
        return toUnsigned ? IROpcode::FPToUI : IROpcode::FPToSI;
    }
    if (fromInt && toFloat) {
        auto k = from->kind();
        bool fromUnsigned = (k == TypeKind::UChar || k == TypeKind::UShort ||
                             k == TypeKind::UInt  || k == TypeKind::ULong  ||
                             k == TypeKind::ULongLong || k == TypeKind::Bool);
        return fromUnsigned ? IROpcode::UIToFP : IROpcode::SIToFP;
    }
    if (fromPtr && toInt) return IROpcode::PtrToInt;
    if (fromInt && toPtr) return IROpcode::IntToPtr;
    if (fromPtr && toPtr) return IROpcode::Bitcast;

    if (fromInt && toInt) {
        u32 fromSz = from->size();
        u32 toSz   = to->size();
        if (fromSz > toSz) return IROpcode::Trunc;
        if (fromSz < toSz) {
            // Sign extend vs zero extend.
            auto k = from->kind();
            bool fromUnsigned = (k == TypeKind::UChar || k == TypeKind::UShort ||
                                 k == TypeKind::UInt  || k == TypeKind::ULong  ||
                                 k == TypeKind::ULongLong || k == TypeKind::Bool);
            return fromUnsigned ? IROpcode::ZExt : IROpcode::SExt;
        }
        return IROpcode::Bitcast; // same size, different signedness
    }

    return IROpcode::Bitcast;
}

std::string IRGen::makeLabel(const char* prefix) {
    return std::string(prefix) + std::to_string(labelCounter_++);
}

std::string IRGen::makeStringName() {
    return ".str" + std::to_string(stringCounter_++);
}

void IRGen::enterLoop(IRBlock* cont, IRBlock* brk) {
    loopStack_.push_back({cont, brk});
}

void IRGen::exitLoop() {
    if (!loopStack_.empty()) loopStack_.pop_back();
}

} // namespace qc
