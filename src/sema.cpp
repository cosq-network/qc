#include "qc/sema.h"
#include "qc/ast.h"
#include "qc/type.h"
#include "qc/common.h"
#include <cassert>

namespace qc {

// ============================================================
// Constructor
// ============================================================
Sema::Sema(TypeContext& types, DiagEngine& diag, const TargetInfo& target)
    : types_(types), diag_(diag), target_(target)
{
    pushScope(); // global scope
}

// ============================================================
// Scope management
// ============================================================
void Sema::define(Symbol sym) {
    if (scopes_.empty()) pushScope();
    scopes_.back().syms[sym.name] = std::move(sym);
}

Symbol* Sema::lookup(std::string_view name) {
    for (int i = (int)scopes_.size() - 1; i >= 0; --i) {
        auto it = scopes_[i].syms.find(std::string(name));
        if (it != scopes_[i].syms.end()) {
            return &it->second;
        }
    }
    return nullptr;
}

// ============================================================
// Top-level analysis
// ============================================================
void Sema::analyse(TranslationUnit& tu) {
    for (auto& decl : tu.decls) {
        if (decl) analyseDecl(decl.get());
    }
}

void Sema::analyseDecl(Decl* d) {
    if (!d) return;

    if (d->alignasExpr) {
        analyseExpr(d->alignasExpr.get());
        if (d->alignasExpr->kind() == ExprKind::IntLit) {
            d->alignment = (u32)static_cast<IntLitExpr*>(d->alignasExpr.get())->value;
        } else if (d->alignasExpr->kind() == ExprKind::Alignof) {
            auto* ae = static_cast<AlignofExpr*>(d->alignasExpr.get());
            if (ae->isType && ae->targetType) {
                d->alignment = ae->targetType->align();
            } else if (ae->operand && ae->operand->type) {
                d->alignment = ae->operand->type->align();
            }
        }
    }

    switch (d->kind()) {
    case DeclKind::Var:
    case DeclKind::Field:
        analyseVarDecl(static_cast<VarDecl*>(d));
        break;
    case DeclKind::Function:
        analyseFuncDecl(static_cast<FuncDecl*>(d));
        break;
    case DeclKind::Struct:
    case DeclKind::Union:
    case DeclKind::Class:
        analyseRecordDecl(static_cast<RecordDecl*>(d));
        break;
    case DeclKind::Enum:
        analyseEnumDecl(static_cast<EnumDecl*>(d));
        break;
    case DeclKind::Namespace:
        analyseNamespaceDecl(static_cast<NamespaceDecl*>(d));
        break;
    case DeclKind::StaticAssert:
        analyseStaticAssertDecl(static_cast<StaticAssertDecl*>(d));
        break;
    case DeclKind::Typedef:
        // Nothing to analyse for typedef
        break;
    default:
        break;
    }
}

// ============================================================
// Declaration analysis
// ============================================================
void Sema::analyseVarDecl(VarDecl* d) {
    if (!d) return;
    d->isGlobal = (scopes_.size() == 1 && !d->isField);
    // If no type yet, assign int
    if (!d->type) {
        d->type = types_.intTy();
    }

    Type* rawTy = d->type.get();
    if (rawTy->kind() == TypeKind::Class || rawTy->kind() == TypeKind::Struct) {
        auto* rt = static_cast<RecordType*>(rawTy);
        
        // Resolve constructor
        std::vector<TypePtr> argTypes;
        for (auto& arg : d->args) {
            analyseExpr(arg.get());
            argTypes.push_back(arg->type);
        }
        
        if (!d->args.empty() || rt->findConstructor({})) {
            d->constructor = rt->findConstructor(argTypes);
            if (!d->constructor && (!d->args.empty() || !d->init)) {
                diag_.error(d->loc, "no matching constructor for " + rt->toString());
            }
        }
        
        // Resolve destructor
        d->destructor = rt->findDestructor();
    }

    // Analyse initializer
    if (d->init) {
        analyseExpr(d->init.get());
        // Check assignability
        if (d->init->type && d->type) {
            if (!implicitlyConvertible(d->init->type.get(), d->type.get())) {
                diag_.warning(d->loc, "incompatible types in initialization of '" + d->name + "'");
            }
        }
    }
    // Add to scope if not a field
    if (!d->isField) {
        Symbol sym;
        sym.name = d->name;
        sym.decl = d;
        sym.type = d->type;
        define(std::move(sym));
    }
}

void Sema::analyseFuncDecl(FuncDecl* d) {
    if (!d) return;
    // Register function in outer scope
    Symbol sym;
    sym.name = d->name;
    sym.decl = d;
    sym.type = d->type;
    define(sym);

    if (!d->body) return; // declaration only

    FuncDecl* prevFunc = currentFunc_;
    currentFunc_ = d;

    pushScope();

    // Inject 'this' pointer if non-static member function
    if (d->parentRecordType && !d->isStatic) {
        bool alreadyHasThis = !d->params.empty() && d->params[0]->name == "this";
        if (!alreadyHasThis) {
            auto thisParam = make<ParamDecl>();
            thisParam->name = "this";
            thisParam->type = types_.ptrTo(d->parentRecordType);
            d->params.insert(d->params.begin(), std::move(thisParam));
        }
    }

    // Add parameters to scope
    for (auto& param : d->params) {
        if (!param->type) param->type = types_.intTy();
        Symbol psym;
        psym.name = param->name;
        psym.decl = param.get();
        psym.type = param->type;
        define(std::move(psym));
    }

    // Analyse body
    analyseStmt(d->body.get());

    popScope();
    currentFunc_ = prevFunc;
}

void Sema::analyseRecordDecl(RecordDecl* d) {
    if (!d) return;
    if (d->recordType) {
        // Register in scope as a type (handled by parser's typedef mechanism)
        // Analyse member declarations
        for (auto& mem : d->members) {
            if (mem) analyseDecl(mem.get());
        }
    }
}

void Sema::analyseEnumDecl(EnumDecl* d) {
    if (!d) return;
    if (!d->enumType) return;

    // Register enumerator names as integer constants in scope
    for (const auto& e : d->enumType->enumerators()) {
        // Create a synthetic VarDecl-like symbol for each enumerator
        Symbol sym;
        sym.name = e.name;
        sym.decl = d;
        sym.type = types_.intTy();
        define(std::move(sym));
    }
}

void Sema::analyseNamespaceDecl(NamespaceDecl* d) {
    if (!d) return;
    pushScope();
    for (auto& mem : d->members) {
        if (mem) analyseDecl(mem.get());
    }
    popScope();
}

void Sema::analyseStaticAssertDecl(StaticAssertDecl* d) {
    if (!d) return;
    if (d->expr) {
        analyseExpr(d->expr.get());
        if (d->expr->kind() == ExprKind::IntLit) {
            auto* il = static_cast<IntLitExpr*>(d->expr.get());
            if (il->value == 0) {
                diag_.error(d->loc, "static assertion failed: " + d->message);
            }
        } else if (d->expr->kind() == ExprKind::BoolLit) {
            auto* bl = static_cast<BoolLitExpr*>(d->expr.get());
            if (!bl->value) {
                diag_.error(d->loc, "static assertion failed: " + d->message);
            }
        }
    }
}

// ============================================================
// Statement analysis
// ============================================================
void Sema::analyseStmt(Stmt* s) {
    if (!s) return;
    switch (s->kind()) {
    case StmtKind::Compound:
        analyseCompoundStmt(static_cast<CompoundStmt*>(s));
        break;
    case StmtKind::If:
        analyseIfStmt(static_cast<IfStmt*>(s));
        break;
    case StmtKind::While:
        analyseWhileStmt(static_cast<WhileStmt*>(s));
        break;
    case StmtKind::DoWhile:
        analyseDoWhileStmt(static_cast<DoWhileStmt*>(s));
        break;
    case StmtKind::For:
        analyseForStmt(static_cast<ForStmt*>(s));
        break;
    case StmtKind::Switch:
        analyseSwitchStmt(static_cast<SwitchStmt*>(s));
        break;
    case StmtKind::Return:
        analyseReturnStmt(static_cast<ReturnStmt*>(s));
        break;
    case StmtKind::Expr: {
        auto* es = static_cast<ExprStmt*>(s);
        if (es->expr) analyseExpr(es->expr.get());
        break;
    }
    case StmtKind::Decl: {
        auto* ds = static_cast<DeclStmt*>(s);
        for (auto& decl : ds->decls) {
            if (decl) analyseDecl(decl.get());
        }
        break;
    }
    case StmtKind::Break:
        if (!inLoop_ && !inSwitch_) {
            diag_.error(s->loc, "'break' not in loop or switch");
        }
        break;
    case StmtKind::Continue:
        if (!inLoop_) {
            diag_.error(s->loc, "'continue' not in loop");
        }
        break;
    case StmtKind::Case:
    case StmtKind::Default: {
        auto* cs = static_cast<CaseStmt*>(s);
        if (!cs->isDefault && cs->value) analyseExpr(cs->value.get());
        if (cs->body) analyseStmt(cs->body.get());
        break;
    }
    case StmtKind::Label: {
        auto* ls = static_cast<LabelStmt*>(s);
        if (ls->body) analyseStmt(ls->body.get());
        break;
    }
    case StmtKind::Goto:
        // Label resolution would require a second pass; skip for now
        break;
    case StmtKind::Try: {
        auto* ts = static_cast<TryStmt*>(s);
        analyseStmt(ts->body.get());
        for (auto& c : ts->catches) {
            pushScope();
            if (!c.name.empty() && c.type) {
                Symbol sym;
                sym.name = c.name;
                sym.decl = nullptr;
                sym.type = c.type;
                define(std::move(sym));
            }
            analyseStmt(c.body.get());
            popScope();
        }
        break;
    }
    case StmtKind::Throw: {
        auto* ts = static_cast<ThrowStmt*>(s);
        if (ts->value) analyseExpr(ts->value.get());
        break;
    }
    case StmtKind::Null:
        break;
    default:
        break;
    }
}

void Sema::analyseCompoundStmt(CompoundStmt* s) {
    if (!s) return;
    pushScope();
    for (auto& st : s->stmts) {
        if (st) analyseStmt(st.get());
    }
    popScope();
}

void Sema::analyseIfStmt(IfStmt* s) {
    if (!s) return;
    if (s->cond) {
        analyseExpr(s->cond.get());
        if (s->cond->type && !s->cond->type->isScalar()) {
            diag_.error(s->loc, "if condition must be scalar type");
        }
    }
    analyseStmt(s->then.get());
    if (s->els) analyseStmt(s->els.get());
}

void Sema::analyseWhileStmt(WhileStmt* s) {
    if (!s) return;
    if (s->cond) {
        analyseExpr(s->cond.get());
        if (s->cond->type && !s->cond->type->isScalar()) {
            diag_.error(s->loc, "while condition must be scalar type");
        }
    }
    bool prevLoop = inLoop_;
    inLoop_ = true;
    analyseStmt(s->body.get());
    inLoop_ = prevLoop;
}

void Sema::analyseDoWhileStmt(DoWhileStmt* s) {
    if (!s) return;
    bool prevLoop = inLoop_;
    inLoop_ = true;
    analyseStmt(s->body.get());
    inLoop_ = prevLoop;
    if (s->cond) {
        analyseExpr(s->cond.get());
        if (s->cond->type && !s->cond->type->isScalar()) {
            diag_.error(s->loc, "do-while condition must be scalar type");
        }
    }
}

void Sema::analyseForStmt(ForStmt* s) {
    if (!s) return;
    pushScope();
    if (s->init) analyseStmt(s->init.get());
    if (s->cond) {
        analyseExpr(s->cond.get());
        if (s->cond->type && !s->cond->type->isScalar()) {
            diag_.error(s->loc, "for condition must be scalar type");
        }
    }
    if (s->step) analyseExpr(s->step.get());
    bool prevLoop = inLoop_;
    inLoop_ = true;
    analyseStmt(s->body.get());
    inLoop_ = prevLoop;
    popScope();
}

void Sema::analyseSwitchStmt(SwitchStmt* s) {
    if (!s) return;
    if (s->cond) {
        analyseExpr(s->cond.get());
        if (s->cond->type && !s->cond->type->isInteger()) {
            diag_.error(s->loc, "switch condition must be integer type");
        }
    }
    bool prevSwitch = inSwitch_;
    inSwitch_ = true;
    analyseStmt(s->body.get());
    inSwitch_ = prevSwitch;
}

void Sema::analyseReturnStmt(ReturnStmt* s) {
    if (!s) return;
    if (s->value) {
        analyseExpr(s->value.get());
    }
    if (currentFunc_) {
        TypePtr retType;
        if (currentFunc_->type && currentFunc_->type->kind() == TypeKind::Function) {
            auto* fty = static_cast<FunctionType*>(currentFunc_->type.get());
            retType = TypePtr(fty->returnType(), [](Type*){});  // non-owning, just for type check
        }
        if (retType) {
            bool returnsVoid = retType->isVoid();
            if (returnsVoid && s->value) {
                diag_.error(s->loc, "void function should not return a value");
            } else if (!returnsVoid && !s->value) {
                diag_.warning(s->loc, "non-void function missing return value");
            } else if (!returnsVoid && s->value && s->value->type) {
                if (!implicitlyConvertible(s->value->type.get(), retType.get())) {
                    diag_.warning(s->loc, "incompatible return type");
                }
            }
        }
    }
}

// ============================================================
// Expression analysis
// ============================================================
void Sema::analyseExpr(Expr* e) {
    if (!e) return;
    switch (e->kind()) {
    case ExprKind::IntLit:
        analyseIntLit(static_cast<IntLitExpr*>(e));
        break;
    case ExprKind::FloatLit:
        analyseFloatLit(static_cast<FloatLitExpr*>(e));
        break;
    case ExprKind::StringLit:
        analyseStringLit(static_cast<StringLitExpr*>(e));
        break;
    case ExprKind::CharLit: {
        auto* cl = static_cast<CharLitExpr*>(e);
        if (!cl->type) cl->type = cl->wide ? types_.intTy() : types_.charTy();
        break;
    }
    case ExprKind::BoolLit: {
        auto* bl = static_cast<BoolLitExpr*>(e);
        if (!bl->type) bl->type = types_.boolTy();
        break;
    }
    case ExprKind::NullptrLit: {
        auto* nl = static_cast<NullptrLitExpr*>(e);
        if (!nl->type) nl->type = types_.nullptrTTy();
        break;
    }
    case ExprKind::Ident:
        analyseIdent(static_cast<IdentExpr*>(e));
        break;
    case ExprKind::Binary:
        analyseBinary(static_cast<BinaryExpr*>(e));
        break;
    case ExprKind::Unary:
        analyseUnary(static_cast<UnaryExpr*>(e));
        break;
    case ExprKind::Ternary:
        analyseTernary(static_cast<TernaryExpr*>(e));
        break;
    case ExprKind::Call:
        analyseCall(static_cast<CallExpr*>(e));
        break;
    case ExprKind::Index:
        analyseIndex(static_cast<IndexExpr*>(e));
        break;
    case ExprKind::Member:
    case ExprKind::MemberPtr:
        analyseMember(static_cast<MemberExpr*>(e));
        break;
    case ExprKind::Cast:
    case ExprKind::CStyle_Cast:
        analyseCast(static_cast<CastExpr*>(e));
        break;
    case ExprKind::Sizeof:
        analyseSizeof(static_cast<SizeofExpr*>(e));
        break;
    case ExprKind::Alignof: {
        auto* ae = static_cast<AlignofExpr*>(e);
        if (!ae->type) ae->type = types_.ulonglongTy();
        break;
    }
    case ExprKind::Assign:
        analyseAssign(static_cast<AssignExpr*>(e));
        break;
    case ExprKind::InitList:
        analyseInitList(static_cast<InitListExpr*>(e));
        break;
    case ExprKind::New: {
        auto* ne = static_cast<NewExpr*>(e);
        if (ne->allocType && !ne->type) {
            ne->type = types_.ptrTo(ne->allocType);
        }
        if (ne->initializer) analyseExpr(ne->initializer.get());
        for (auto& p : ne->placement) analyseExpr(p.get());
        break;
    }
    case ExprKind::Delete: {
        auto* de = static_cast<DeleteExpr*>(e);
        if (de->operand) analyseExpr(de->operand.get());
        if (de->operand && de->operand->type) {
            Type* bt = stripQuals(de->operand->type.get());
            if (!bt || bt->kind() != TypeKind::Pointer) {
                diag_.error(e->loc, "delete requires pointer operand");
            }
        }
        if (!de->type) de->type = types_.voidTy();
        break;
    }
    case ExprKind::Generic:
        analyseGeneric(static_cast<GenericExpr*>(e));
        break;
    default:
        break;
    }
}

void Sema::analyseIntLit(IntLitExpr* e) {
    if (!e->type) {
        e->type = e->isSigned ? types_.intTy() : types_.uintTy();
    }
}

void Sema::analyseFloatLit(FloatLitExpr* e) {
    if (!e->type) {
        e->type = types_.doubleTy();
    }
}

void Sema::analyseStringLit(StringLitExpr* e) {
    if (!e->type) {
        TypePtr charTy = e->wide ? types_.intTy() : types_.charTy();
        QualFlags qf;
        qf.isConst = true;
        e->type = types_.ptrTo(types_.qualOf(charTy, qf));
    }
}

void Sema::analyseGeneric(GenericExpr* e) {
    if (!e) return;
    analyseExpr(e->controlExpr.get());
    
    Type* controlType = e->controlExpr->type.get();
    ExprPtr matchedExpr;
    ExprPtr defaultExpr;

    for (auto& assoc : e->associations) {
        if (assoc.type) {
            // For a real C11 compiler, we would check for type compatibility (ignoring qualifiers).
            // Here we just do a strict pointer equality or kind check.
            if (controlType && implicitlyConvertible(controlType, assoc.type.get())) {
                matchedExpr = std::move(assoc.expr);
            }
        } else {
            defaultExpr = std::move(assoc.expr);
        }
    }

    if (matchedExpr) {
        e->resultExpr = std::move(matchedExpr);
    } else if (defaultExpr) {
        e->resultExpr = std::move(defaultExpr);
    } else {
        diag_.error(e->loc, "no matching generic association");
    }

    if (e->resultExpr) {
        analyseExpr(e->resultExpr.get());
        e->type = e->resultExpr->type;
    }
}

void Sema::analyseIdent(IdentExpr* e) {
    if (e->name == "this") {
        if (currentFunc_ && currentFunc_->parentRecordType) {
            e->type = types_.ptrTo(currentFunc_->parentRecordType);
        } else {
            diag_.error(e->loc, "'this' outside of member function");
            e->type = types_.intTy();
        }
        return;
    }

    Symbol* sym = lookup(e->name);
    if (!sym) {
        // Try implicit member access if in a member function
        if (currentFunc_ && currentFunc_->parentRecordType) {
            auto* rt = currentFunc_->parentRecordType.get();
            int fIdx = rt->fieldIndex(e->name);
            if (fIdx >= 0) {
                e->field = const_cast<FieldInfo*>(&rt->fields()[fIdx]);
                e->type = e->field->type;
                e->isLValue = true;
                e->isImplicitThis = true;
                return;
            }
            int mIdx = rt->methodIndex(e->name);
            if (mIdx >= 0) {
                e->method = const_cast<MethodInfo*>(&rt->methods()[mIdx]);
                e->type = e->method->type;
                e->isImplicitThis = true;
                return;
            }
        }

        // Don't error on common builtins
        if (e->name == "__func__" || e->name == "__FUNCTION__" || e->name == "__PRETTY_FUNCTION__") {
            QualFlags qf; qf.isConst = true;
            e->type = types_.ptrTo(types_.qualOf(types_.charTy(), qf));
            return;
        }
        diag_.error(e->loc, "undeclared identifier '" + e->name + "'");
        e->type = types_.intTy();
        return;
    }

    e->decl = sym->decl;
    e->type  = sym->type;

    // Identifiers that refer to variables/params are lvalues
    if (sym->decl) {
        DeclKind dk = sym->decl->kind();
        if (dk == DeclKind::Var || dk == DeclKind::Param || dk == DeclKind::Field) {
            e->isLValue = true;
        }
    }
}

void Sema::analyseBinary(BinaryExpr* e) {
    if (!e->lhs || !e->rhs) return;
    analyseExpr(e->lhs.get());
    analyseExpr(e->rhs.get());

    TypePtr lt = e->lhs->type ? decayType(e->lhs->type) : types_.intTy();
    TypePtr rt = e->rhs->type ? decayType(e->rhs->type) : types_.intTy();

    switch (e->op) {
    case BinaryOp::Add:
    case BinaryOp::Sub: {
        // Pointer arithmetic
        Type* lts = stripQuals(lt.get());
        Type* rts = stripQuals(rt.get());
        if (lts && lts->kind() == TypeKind::Pointer) {
            if (rts && rts->isInteger()) {
                e->type = lt; // ptr + int = ptr
            } else if (e->op == BinaryOp::Sub && rts && rts->kind() == TypeKind::Pointer) {
                e->type = types_.longTy(); // ptr - ptr = ptrdiff_t
            } else {
                e->type = lt;
            }
        } else if (rts && rts->kind() == TypeKind::Pointer && e->op == BinaryOp::Add) {
            if (lts && lts->isInteger()) {
                e->type = rt; // int + ptr = ptr
            } else {
                e->type = rt;
            }
        } else {
            e->type = usualArithmeticConv(lt, rt);
        }
        break;
    }
    case BinaryOp::Mul:
    case BinaryOp::Div:
    case BinaryOp::Mod:
        e->type = usualArithmeticConv(lt, rt);
        break;
    case BinaryOp::And:
    case BinaryOp::Or:
    case BinaryOp::Xor:
    case BinaryOp::Shl:
    case BinaryOp::Shr:
        e->type = usualArithmeticConv(lt, rt);
        break;
    case BinaryOp::LAnd:
    case BinaryOp::LOr:
        e->type = types_.intTy();
        break;
    case BinaryOp::Eq:
    case BinaryOp::Ne:
    case BinaryOp::Lt:
    case BinaryOp::Le:
    case BinaryOp::Gt:
    case BinaryOp::Ge:
        e->type = types_.intTy();
        break;
    case BinaryOp::Comma:
        e->type = rt;
        break;
    default:
        e->type = lt;
        break;
    }
}

void Sema::analyseUnary(UnaryExpr* e) {
    if (!e->operand) return;
    analyseExpr(e->operand.get());
    TypePtr ot = e->operand->type ? decayType(e->operand->type) : types_.intTy();

    switch (e->op) {
    case UnaryOp::PreInc:
    case UnaryOp::PreDec:
        if (!e->operand->isLValue) {
            diag_.error(e->loc, "increment/decrement requires lvalue");
        }
        e->type = ot;
        break;
    case UnaryOp::PostInc:
    case UnaryOp::PostDec:
        if (!e->operand->isLValue) {
            diag_.error(e->loc, "increment/decrement requires lvalue");
        }
        e->type = ot; // value before modification
        break;
    case UnaryOp::Plus:
        e->type = integerPromotion(ot);
        break;
    case UnaryOp::Neg:
        e->type = integerPromotion(ot);
        break;
    case UnaryOp::Not:
        // Bitwise not — must be integer
        if (ot && !stripQuals(ot.get())->isInteger()) {
            diag_.error(e->loc, "bitwise not requires integer operand");
        }
        e->type = integerPromotion(ot);
        break;
    case UnaryOp::LNot:
        // Logical not
        if (ot && !stripQuals(ot.get())->isScalar()) {
            diag_.error(e->loc, "logical not requires scalar operand");
        }
        e->type = types_.intTy();
        break;
    case UnaryOp::Deref: {
        // Must be pointer type
        Type* bt = stripQuals(ot.get());
        if (!bt || bt->kind() != TypeKind::Pointer) {
            diag_.error(e->loc, "dereference of non-pointer type");
            e->type = types_.intTy();
        } else {
            auto* pt = static_cast<PointerType*>(bt);
            // Return the shared_ptr to pointee — create a shared alias
            TypePtr pointeeTy(pt->pointee(), [](Type*){});
            e->type = pointeeTy;
            e->isLValue = true;
        }
        break;
    }
    case UnaryOp::AddrOf: {
        if (!e->operand->isLValue) {
            diag_.error(e->loc, "cannot take address of non-lvalue");
        }
        e->type = types_.ptrTo(ot);
        break;
    }
    default:
        e->type = ot;
        break;
    }
}

void Sema::analyseCall(CallExpr* e) {
    if (!e->callee) return;
    analyseExpr(e->callee.get());

    for (auto& arg : e->args) {
        if (arg) analyseExpr(arg.get());
    }

    TypePtr calleeType = e->callee->type ? decayType(e->callee->type) : nullptr;
    if (!calleeType) {
        e->type = types_.intTy();
        return;
    }

    Type* ct = stripQuals(calleeType.get());
    // Function pointer or function type
    FunctionType* fty = nullptr;
    if (ct && ct->kind() == TypeKind::Pointer) {
        Type* pt = stripQuals(static_cast<PointerType*>(ct)->pointee());
        if (pt && pt->kind() == TypeKind::Function) {
            fty = static_cast<FunctionType*>(pt);
        }
    } else if (ct && ct->kind() == TypeKind::Function) {
        fty = static_cast<FunctionType*>(ct);
    }

    if (!fty) {
        // Not a function type — error but continue
        diag_.error(e->loc, "called object is not a function");
        e->type = types_.intTy();
        return;
    }

    // Check arg count
    const auto& params = fty->params();
    if (!fty->isVariadic()) {
        if (e->args.size() != params.size()) {
            diag_.error(e->loc, "wrong number of arguments to function call");
        }
    } else {
        if (e->args.size() < params.size()) {
            diag_.error(e->loc, "too few arguments to variadic function");
        }
    }

    // Check arg types
    size_t ncheck = std::min(e->args.size(), params.size());
    for (size_t i = 0; i < ncheck; ++i) {
        if (!e->args[i] || !e->args[i]->type) continue;
        if (!implicitlyConvertible(e->args[i]->type.get(), params[i].type.get())) {
            diag_.warning(e->args[i]->loc, "incompatible argument type");
        }
    }

    // Set return type
    TypePtr retTy(fty->returnType(), [](Type*){});
    e->type = retTy;
}

void Sema::analyseIndex(IndexExpr* e) {
    if (!e->base || !e->index) return;
    analyseExpr(e->base.get());
    analyseExpr(e->index.get());

    TypePtr baseType = e->base->type ? decayType(e->base->type) : nullptr;
    TypePtr idxType  = e->index->type ? decayType(e->index->type) : nullptr;

    if (!baseType) {
        e->type = types_.intTy();
        return;
    }

    Type* bt = stripQuals(baseType.get());

    // Allow subscript of both pointer[index] and index[pointer]
    if (bt && bt->kind() == TypeKind::Pointer) {
        auto* pt = static_cast<PointerType*>(bt);
        if (idxType) {
            Type* it = stripQuals(idxType.get());
            if (!it || !it->isInteger()) {
                diag_.error(e->loc, "array index must be integer type");
            }
        }
        TypePtr elemTy(pt->pointee(), [](Type*){});
        e->type = elemTy;
        e->isLValue = true;
    } else if (bt && (bt->kind() == TypeKind::Array || bt->kind() == TypeKind::IncompleteArray)) {
        auto* at = static_cast<ArrayType*>(bt);
        TypePtr elemTy(at->element(), [](Type*){});
        e->type = elemTy;
        e->isLValue = true;
    } else {
        // Check if index is pointer (int[ptr] form)
        if (idxType) {
            Type* it = stripQuals(idxType.get());
            if (it && it->kind() == TypeKind::Pointer) {
                auto* pt = static_cast<PointerType*>(it);
                TypePtr elemTy(pt->pointee(), [](Type*){});
                e->type = elemTy;
                e->isLValue = true;
                return;
            }
        }
        diag_.error(e->loc, "subscript of non-pointer/array type");
        e->type = types_.intTy();
    }
}

void Sema::analyseMember(MemberExpr* e) {
    if (!e->base) return;
    analyseExpr(e->base.get());

    TypePtr baseType = e->base->type;
    if (!baseType) {
        e->type = types_.intTy();
        return;
    }

    Type* bt = stripQuals(baseType.get());

    if (e->isArrow) {
        // -> : base must be pointer to struct/union/class
        if (bt && bt->kind() == TypeKind::Pointer) {
            bt = stripQuals(static_cast<PointerType*>(bt)->pointee());
        } else {
            diag_.error(e->loc, "'->' requires pointer to struct/union/class");
            e->type = types_.intTy();
            return;
        }
    }

    // bt should now be struct/union/class
    if (!bt) {
        e->type = types_.intTy();
        return;
    }

    if (bt->kind() != TypeKind::Struct && bt->kind() != TypeKind::Union && bt->kind() != TypeKind::Class) {
        diag_.error(e->loc, "member access on non-struct/union/class type");
        e->type = types_.intTy();
        return;
    }

    auto* rt = static_cast<RecordType*>(bt);
    int idx = rt->fieldIndex(e->member);
    if (idx >= 0) {
        const FieldInfo& fi = rt->fields()[idx];
        e->field    = const_cast<FieldInfo*>(&fi);
        e->type     = fi.type;
        e->isLValue = e->isArrow || (e->base && e->base->isLValue);
        return;
    }

    int mIdx = rt->methodIndex(e->member);
    if (mIdx >= 0) {
        const MethodInfo& mi = rt->methods()[mIdx];
        e->method = const_cast<MethodInfo*>(&mi);
        e->type   = mi.type;
        return;
    }

    diag_.error(e->loc, "no member '" + e->member + "' in " + rt->toString());
    e->type = types_.intTy();
}

void Sema::analyseCast(CastExpr* e) {
    if (e->operand) analyseExpr(e->operand.get());

    if (!e->targetType) {
        e->type = e->operand ? e->operand->type : types_.intTy();
        return;
    }

    e->type = e->targetType;

    // Validate cast legality
    if (!e->operand || !e->operand->type) return;

    Type* from = stripQuals(e->operand->type.get());
    Type* to   = stripQuals(e->targetType.get());

    if (!from || !to) return;

    switch (e->castKind) {
    case CastExpr::CastKind::CStyle:
        // C-style cast: allow most conversions
        // Disallow: cast between unrelated struct/class types (just warn)
        if (from->isArithmetic() || from->isPointer() || to->isArithmetic() || to->isPointer()) {
            // OK
        } else if (from->isStruct() || to->isStruct()) {
            diag_.warning(e->loc, "casting between struct/class types");
        }
        break;
    case CastExpr::CastKind::Static:
        // static_cast: allow arithmetic, pointer to related types, void*
        if (!from->isArithmetic() && !from->isPointer() && !to->isArithmetic() && !to->isPointer()) {
            if (!typeCompatible(from, to)) {
                diag_.error(e->loc, "invalid static_cast");
            }
        }
        break;
    case CastExpr::CastKind::Reinterpret:
        // reinterpret_cast: allow pointer <-> integer, pointer <-> pointer
        if (!from->isPointer() && !from->isInteger()) {
            diag_.error(e->loc, "reinterpret_cast requires pointer or integer");
        }
        break;
    case CastExpr::CastKind::Const:
        // const_cast: only changes cv-qualifiers
        {
            Type* fb = stripQuals(const_cast<Type*>(from));
            Type* tb = stripQuals(const_cast<Type*>(to));
            if (!typeEqual(fb, tb)) {
                diag_.error(e->loc, "const_cast between unrelated types");
            }
        }
        break;
    case CastExpr::CastKind::Dynamic:
        // dynamic_cast: only for polymorphic types (runtime check)
        if (!from->isPointer() && from->kind() != TypeKind::Reference) {
            diag_.error(e->loc, "dynamic_cast requires pointer or reference");
        }
        break;
    }
}

void Sema::analyseSizeof(SizeofExpr* e) {
    if (!e->type) {
        e->type = types_.ulonglongTy();
    }
    if (e->isType) {
        // targetType already set
    } else if (e->operand) {
        analyseExpr(e->operand.get());
    }
}

void Sema::analyseAssign(AssignExpr* e) {
    if (!e->lhs || !e->rhs) return;
    analyseExpr(e->lhs.get());
    analyseExpr(e->rhs.get());

    if (!e->lhs->isLValue) {
        diag_.error(e->loc, "assignment to non-lvalue");
    }

    // Check const
    if (e->lhs->type) {
        if (e->lhs->type->quals().isConst) {
            diag_.error(e->loc, "assignment to const-qualified lvalue");
        }
    }

    // Check assignability
    if (e->lhs->type && e->rhs->type) {
        if (!implicitlyConvertible(e->rhs->type.get(), e->lhs->type.get())) {
            diag_.warning(e->loc, "incompatible types in assignment");
        }
    }

    e->type     = e->lhs->type;
    e->isLValue = false;
}

void Sema::analyseTernary(TernaryExpr* e) {
    if (e->cond)  analyseExpr(e->cond.get());
    if (e->then)  analyseExpr(e->then.get());
    if (e->els)   analyseExpr(e->els.get());

    if (e->cond && e->cond->type && !stripQuals(e->cond->type.get())->isScalar()) {
        diag_.error(e->loc, "ternary condition must be scalar");
    }

    // Result type: usual arithmetic conversion of then/else types
    if (e->then && e->els && e->then->type && e->els->type) {
        TypePtr lt = decayType(e->then->type);
        TypePtr rt = decayType(e->els->type);
        e->type = usualArithmeticConv(lt, rt);
    } else if (e->then && e->then->type) {
        e->type = e->then->type;
    } else if (e->els && e->els->type) {
        e->type = e->els->type;
    } else {
        e->type = types_.intTy();
    }
}

void Sema::analyseInitList(InitListExpr* e) {
    for (auto& init : e->inits) {
        if (init) analyseExpr(init.get());
    }
    // Type is inferred by context; leave null if not set
    if (!e->type) {
        if (!e->inits.empty() && e->inits.front() && e->inits.front()->type) {
            e->type = e->inits.front()->type;
        }
    }
}

// ============================================================
// Type helpers
// ============================================================

// Implement integer rank comparison inline
static int intRank(TypeKind k) {
    switch (k) {
    case TypeKind::Bool:      return 0;
    case TypeKind::Char:
    case TypeKind::SChar:
    case TypeKind::UChar:     return 1;
    case TypeKind::Short:
    case TypeKind::UShort:    return 2;
    case TypeKind::Int:
    case TypeKind::UInt:      return 3;
    case TypeKind::Long:
    case TypeKind::ULong:     return 4;
    case TypeKind::LongLong:
    case TypeKind::ULongLong: return 5;
    default:                  return 3;
    }
}

static bool isUnsignedKind(TypeKind k) {
    switch (k) {
    case TypeKind::Bool:
    case TypeKind::UChar:
    case TypeKind::UShort:
    case TypeKind::UInt:
    case TypeKind::ULong:
    case TypeKind::ULongLong:
        return true;
    default:
        return false;
    }
}

static TypeKind toUnsigned(TypeKind k) {
    switch (k) {
    case TypeKind::Char:    return TypeKind::UChar;
    case TypeKind::SChar:   return TypeKind::UChar;
    case TypeKind::Short:   return TypeKind::UShort;
    case TypeKind::Int:     return TypeKind::UInt;
    case TypeKind::Long:    return TypeKind::ULong;
    case TypeKind::LongLong:return TypeKind::ULongLong;
    default:                return k;
    }
}

TypePtr Sema::integerPromotion(TypePtr t) {
    Type* bt = stripQuals(t.get());
    if (!bt) return types_.intTy();

    TypeKind k = bt->kind();
    switch (k) {
    case TypeKind::Bool:
    case TypeKind::Char:
    case TypeKind::SChar:
    case TypeKind::UChar:
    case TypeKind::Short:
    case TypeKind::UShort:
        return types_.intTy();
    default:
        return t;
    }
}

TypePtr Sema::usualArithmeticConv(TypePtr a, TypePtr b) {
    Type* sa = stripQuals(a.get());
    Type* sb = stripQuals(b.get());

    if (!sa) return b;
    if (!sb) return a;

    // If either is floating point, use the higher-ranked float
    bool af = sa->isFloat();
    bool bf = sb->isFloat();

    if (af || bf) {
        // LongDouble > Double > Float
        auto floatRank = [](TypeKind k) -> int {
            switch (k) {
            case TypeKind::Float:      return 0;
            case TypeKind::Double:     return 1;
            case TypeKind::LongDouble: return 2;
            default:                   return 1;
            }
        };

        if (af && bf) {
            int ra = floatRank(sa->kind());
            int rb = floatRank(sb->kind());
            return ra >= rb ? a : b;
        }
        return af ? a : b;
    }

    // Both integer: apply promotions first
    TypePtr pa = integerPromotion(a);
    TypePtr pb = integerPromotion(b);
    Type* spa = stripQuals(pa.get());
    Type* spb = stripQuals(pb.get());

    if (!spa || !spb) return pa;

    TypeKind ka = spa->kind();
    TypeKind kb = spb->kind();

    if (ka == kb) return pa;

    int ra = intRank(ka);
    int rb = intRank(kb);
    bool ua = isUnsignedKind(ka);
    bool ub = isUnsignedKind(kb);

    // C99 6.3.1.8 usual arithmetic conversions
    if (ua == ub) {
        // Same signedness: pick higher rank
        return ra >= rb ? pa : pb;
    }

    // Different signedness
    TypeKind sKind  = ua ? kb : ka;
    int uRank = ua ? ra : rb;
    int sRank = ua ? rb : ra;

    if (uRank >= sRank) {
        // Unsigned has higher or equal rank: result is unsigned type
        if (ua) return pa; else return pb;
    } else {
        // Signed has higher rank: if it can represent all values of unsigned, use signed
        // For LP64 this means long can represent uint, longlong can represent ulong, etc.
        // Simplified: use the unsigned version of the signed type
        TypeKind result = toUnsigned(sKind);
        switch (result) {
        case TypeKind::UChar:     return types_.ucharTy();
        case TypeKind::UShort:    return types_.ushortTy();
        case TypeKind::UInt:      return types_.uintTy();
        case TypeKind::ULong:     return types_.ulongTy();
        case TypeKind::ULongLong: return types_.ulonglongTy();
        default:                  return types_.uintTy();
        }
    }
}

bool Sema::isAssignable(const Expr* lhs, const Expr* rhs) {
    if (!lhs->isLValue) return false;
    if (!lhs->type || !rhs->type) return false;
    if (lhs->type->quals().isConst) return false;
    return implicitlyConvertible(rhs->type.get(), lhs->type.get());
}

bool Sema::implicitlyConvertible(Type* from, Type* to) {
    if (!from || !to) return true; // unknown type, optimistic

    if (typeEqual(from, to)) return true;

    Type* sf = stripQuals(from);
    Type* st = stripQuals(to);

    if (!sf || !st) return true;

    // Arithmetic conversions
    if (sf->isArithmetic() && st->isArithmetic()) return true;

    // Pointer to void*
    if (sf->kind() == TypeKind::Pointer && st->kind() == TypeKind::Pointer) {
        Type* pf = stripQuals(static_cast<PointerType*>(sf)->pointee());
        Type* pt = stripQuals(static_cast<PointerType*>(st)->pointee());
        if (!pf || !pt) return true;
        if (pt->isVoid() || pf->isVoid()) return true;
        return typeCompatible(pf, pt);
    }

    // nullptr to pointer
    if (sf->kind() == TypeKind::NullptrT && st->kind() == TypeKind::Pointer) return true;

    // Integer 0 or nullptr_t to any pointer
    if (sf->isInteger() && st->kind() == TypeKind::Pointer) return true; // relaxed

    // Array to pointer decay
    if ((sf->kind() == TypeKind::Array || sf->kind() == TypeKind::IncompleteArray)
        && st->kind() == TypeKind::Pointer) {
        const ArrayType* at = static_cast<const ArrayType*>(sf);
        const PointerType* pt = static_cast<const PointerType*>(st);
        return typeCompatible(at->element(), pt->pointee());
    }

    // Function to function pointer
    if (sf->kind() == TypeKind::Function && st->kind() == TypeKind::Pointer) {
        Type* pp = stripQuals(static_cast<PointerType*>(st)->pointee());
        return typeEqual(sf, pp);
    }

    // Enum to integer
    if (sf->kind() == TypeKind::Enum && st->isInteger()) return true;

    // Integer to enum
    if (sf->isInteger() && st->kind() == TypeKind::Enum) return true;

    return false;
}

TypePtr Sema::decayType(TypePtr t) {
    Type* bt = stripQuals(t.get());
    if (!bt) return t;

    // Array decays to pointer to element
    if (bt->kind() == TypeKind::Array || bt->kind() == TypeKind::IncompleteArray) {
        auto* at = static_cast<ArrayType*>(bt);
        TypePtr elemTy(at->element(), [](Type*){});
        return types_.ptrTo(elemTy);
    }

    // Function decays to pointer to function
    if (bt->kind() == TypeKind::Function) {
        return types_.ptrTo(t);
    }

    return t;
}

} // namespace qc
