#include "qc/parser.h"
#include "qc/ast.h"
#include "qc/lexer.h"
#include "qc/token.h"
#include "qc/type.h"
#include "qc/common.h"
#include <cassert>
#include <stdexcept>

namespace qc {

// ============================================================
// Constructor
// ============================================================
Parser::Parser(Preprocessor& pp, TypeContext& types, DiagEngine& diag)
    : pp_(pp), types_(types), diag_(diag)
{
    // Start with a global scope
    pushScope();
    // Register common C++ type aliases
    defineTypedef("__builtin_va_list", types_.ptrTo(types_.voidTy()));
}

// ============================================================
// Top-level entry
// ============================================================
Ptr<TranslationUnit> Parser::parse() {
    auto tu = make<TranslationUnit>();
    while (!atEnd()) {
        if (auto d = parseTopLevelDecl()) {
            tu->decls.push_back(std::move(d));
        }
    }
    return tu;
}

// ============================================================
// Helpers
// ============================================================
Token Parser::consume(TokenKind k, const char* msg) {
    Token t = cur();
    if (!t.is(k)) {
        diag_.error(t.loc, msg ? msg : ("expected token"));
    }
    next();
    return t;
}

bool Parser::match(TokenKind k) {
    if (cur().is(k)) {
        next();
        return true;
    }
    return false;
}

TypePtr Parser::lookupTypeName(std::string_view name) {
    for (int i = (int)scopes_.size() - 1; i >= 0; --i) {
        auto it = scopes_[i].typedefs.find(std::string(name));
        if (it != scopes_[i].typedefs.end()) {
            return it->second;
        }
    }
    return nullptr;
}

bool Parser::isTypeName(const Token& tok) {
    if (!tok.is(TokenKind::Identifier)) return false;
    return lookupTypeName(tok.text) != nullptr;
}

bool Parser::isStartOfDeclaration() {
    Token t = cur();
    switch (t.kind) {
    case TokenKind::Kw__Static_assert:
    case TokenKind::Kw_auto:
    case TokenKind::Kw_char:
    case TokenKind::Kw_const:
    case TokenKind::Kw_double:
    case TokenKind::Kw_enum:
    case TokenKind::Kw_extern:
    case TokenKind::Kw_float:
    case TokenKind::Kw_inline:
    case TokenKind::Kw_int:
    case TokenKind::Kw_long:
    case TokenKind::Kw_register:
    case TokenKind::Kw_restrict:
    case TokenKind::Kw_short:
    case TokenKind::Kw_signed:
    case TokenKind::Kw_static:
    case TokenKind::Kw_struct:
    case TokenKind::Kw_typedef:
    case TokenKind::Kw_union:
    case TokenKind::Kw_unsigned:
    case TokenKind::Kw_void:
    case TokenKind::Kw_volatile:
    case TokenKind::Kw_while:
    case TokenKind::Kw__Bool:
    case TokenKind::Kw_bool:
    case TokenKind::Kw_class:
    case TokenKind::Kw_constexpr:
    case TokenKind::Kw_explicit:
    case TokenKind::Kw_friend:
    case TokenKind::Kw_mutable:
    case TokenKind::Kw_namespace:
    case TokenKind::Kw_typename:
    case TokenKind::Kw_virtual:
        return true;
    case TokenKind::Identifier:
        return isTypeName(t);
    case TokenKind::ColonColon:
        return true;
    default:
        return false;
    }
}

// ============================================================
// Top-level declarations
// ============================================================
DeclPtr Parser::parseTopLevelDecl() {
    Token t = cur();

    // Handle namespace
    if (t.is(TokenKind::Kw_namespace)) {
        return parseNamespaceDecl();
    }

    // Skip access specifiers at top level (shouldn't happen, but be safe)
    if (t.is(TokenKind::Kw_public) || t.is(TokenKind::Kw_protected) || t.is(TokenKind::Kw_private)) {
        next();
        consume(TokenKind::Colon, "expected ':' after access specifier");
        return parseTopLevelDecl();
    }

    // Skip stray semicolons
    if (t.is(TokenKind::Semicolon)) {
        next();
        return nullptr;
    }

    return parseDeclaration();
}

DeclPtr Parser::parseNamespaceDecl() {
    SourceLocation loc = cur().loc;
    consume(TokenKind::Kw_namespace, "expected 'namespace'");

    std::string nsName;
    if (cur().is(TokenKind::Identifier)) {
        nsName = cur().text;
        next();
    }

    consume(TokenKind::LBrace, "expected '{' after namespace name");

    auto ns = make<NamespaceDecl>();
    ns->loc  = loc;
    ns->name = nsName;

    pushScope();
    while (!atEnd() && !cur().is(TokenKind::RBrace)) {
        if (cur().is(TokenKind::Semicolon)) { next(); continue; }
        if (auto d = parseTopLevelDecl()) {
            ns->members.push_back(std::move(d));
        }
    }
    popScope();

    consume(TokenKind::RBrace, "expected '}' at end of namespace");
    match(TokenKind::Semicolon); // optional trailing semicolon

    return ns;
}

// ============================================================
// DeclSpec parsing
// ============================================================
Parser::DeclSpec Parser::parseDeclSpec() {
    DeclSpec ds;
    bool seenType = false;

    bool running = true;
    while (running && !atEnd()) {
        Token t = cur();
        switch (t.kind) {
        // Storage class
        case TokenKind::Kw_extern:    next(); ds.isExtern    = true; break;
        case TokenKind::Kw_static:    next(); ds.isStatic    = true; break;
        case TokenKind::Kw_inline:    next(); ds.isInline    = true; break;
        case TokenKind::Kw_constexpr: next(); ds.isConstexpr = true; break;
        case TokenKind::Kw_typedef:   next(); ds.isTypedef   = true; break;
        case TokenKind::Kw_virtual:   next(); ds.isVirtual   = true; break;
        case TokenKind::Kw_explicit:  next(); ds.isExplicit  = true; break;
        case TokenKind::Kw_mutable:   next(); ds.isMutable   = true; break;
        case TokenKind::Kw_friend:    next(); ds.isFriend    = true; break;
        case TokenKind::Kw__Noreturn: next(); ds.isNoreturn  = true; break;
        case TokenKind::Kw_register:  next(); break; // ignored
        case TokenKind::Kw_auto:      next(); break; // C++ auto (type deduced)

        case TokenKind::Kw__Alignas:
        case TokenKind::Kw_alignas: {
            next();
            consume(TokenKind::LParen, "expected '(' after _Alignas");
            if (isTypeName(cur())) {
                auto ae = make<AlignofExpr>();
                ae->isType = true;
                ae->targetType = parseTypeName();
                ds.alignasExpr = std::move(ae);
            } else {
                ds.alignasExpr = parseAssignExpr();
            }
            consume(TokenKind::RParen, "expected ')' after _Alignas");
            break;
        }

        // Qualifiers and type specifiers — handled together
        case TokenKind::Kw_const:
        case TokenKind::Kw_volatile:
        case TokenKind::Kw_restrict:
        case TokenKind::Kw__Atomic:
        case TokenKind::Kw_char:
        case TokenKind::Kw_short:
        case TokenKind::Kw_int:
        case TokenKind::Kw_long:
        case TokenKind::Kw_signed:
        case TokenKind::Kw_unsigned:
        case TokenKind::Kw_float:
        case TokenKind::Kw_double:
        case TokenKind::Kw_void:
        case TokenKind::Kw__Bool:
        case TokenKind::Kw_bool:
        case TokenKind::Kw_struct:
        case TokenKind::Kw_union:
        case TokenKind::Kw_class:
        case TokenKind::Kw_enum:
        case TokenKind::Kw_typename:
            if (!seenType) {
                ds.baseType = parseTypeSpecifier(ds);
                seenType = true;
            } else {
                running = false;
            }
            break;

        case TokenKind::Identifier:
            if (!seenType && isTypeName(t)) {
                ds.baseType = parseTypeSpecifier(ds);
                seenType = true;
            } else {
                running = false;
            }
            break;

        case TokenKind::ColonColon:
            // Qualified name — could be a type
            if (!seenType) {
                ds.baseType = parseTypeSpecifier(ds);
                seenType = true;
            } else {
                running = false;
            }
            break;

        default:
            running = false;
            break;
        }
    }

    if (!ds.baseType) {
        ds.baseType = types_.intTy(); // default int rule
    }
    return ds;
}

TypePtr Parser::parseTypeName() {
    DeclSpec ds = parseDeclSpec();
    return parseAbstractDeclarator(ds.baseType);
}

// ============================================================
// Type specifier parsing
// ============================================================
TypePtr Parser::parseTypeSpecifier(DeclSpec& ds) {
    // Collect qualifiers and type keywords
    bool isConst    = false;
    bool isVolatile = false;
    bool isRestrict = false;
    bool isSigned   = false;
    bool isUnsigned = false;
    bool isShort    = false;
    int  longCount  = 0;
    bool hasChar    = false;
    bool hasInt     = false;
    bool hasFloat   = false;
    bool hasDouble  = false;
    bool hasVoid    = false;
    bool hasBool    = false;
    TypePtr baseType;

    bool running = true;
    while (running && !atEnd()) {
        Token t = cur();
        switch (t.kind) {
        case TokenKind::Kw_const:    next(); isConst    = true; break;
        case TokenKind::Kw_volatile: next(); isVolatile = true; break;
        case TokenKind::Kw_restrict: next(); isRestrict = true; break;
        case TokenKind::Kw__Atomic:
            next();
            if (cur().kind == TokenKind::LParen) {
                next(); // consume '('
                parseTypeName(); // parse and ignore for now
                consume(TokenKind::RParen, "expected ')' after _Atomic");
            }
            break; // ignore for now
        case TokenKind::Kw_signed:   next(); isSigned   = true; break;
        case TokenKind::Kw_unsigned: next(); isUnsigned = true; break;
        case TokenKind::Kw_short:    next(); isShort    = true; break;
        case TokenKind::Kw_long:     next(); ++longCount; break;
        case TokenKind::Kw_char:     next(); hasChar     = true; break;
        case TokenKind::Kw_int:      next(); hasInt      = true; break;
        case TokenKind::Kw_float:    next(); hasFloat    = true; break;
        case TokenKind::Kw_double:   next(); hasDouble   = true; break;
        case TokenKind::Kw_void:     next(); hasVoid     = true; break;
        case TokenKind::Kw__Bool:    next(); hasBool     = true; break;
        case TokenKind::Kw_bool:     next(); hasBool     = true; break;

        case TokenKind::Kw_struct:
        case TokenKind::Kw_union:
        case TokenKind::Kw_class: {
            TypeKind rk = t.is(TokenKind::Kw_union) ? TypeKind::Union
                        : t.is(TokenKind::Kw_class) ? TypeKind::Class
                        : TypeKind::Struct;
            next(); // consume struct/union/class
            // Optional tag name
            std::string tagName;
            if (cur().is(TokenKind::Identifier)) {
                tagName = cur().text;
                next();
            }
            // Optional body
            if (cur().is(TokenKind::LBrace)) {
                // Full definition
                // Re-position: we'll call parseRecordDecl helper with already-consumed kind
                // Build inline record
                auto rec = types_.makeRecord(rk, tagName);
                // Store tag
                if (!tagName.empty()) {
                    scopes_.back().tags[tagName] = rec;
                }
                consume(TokenKind::LBrace, "expected '{'");
                pushScope();
                while (!atEnd() && !cur().is(TokenKind::RBrace)) {
                    if (cur().is(TokenKind::Semicolon)) { next(); continue; }
                    // Access specifiers inside class
                    if (cur().is(TokenKind::Kw_public) || cur().is(TokenKind::Kw_protected) || cur().is(TokenKind::Kw_private)) {
                        next();
                        consume(TokenKind::Colon, "expected ':' after access specifier");
                        continue;
                    }
                    DeclSpec fds = parseDeclSpec();
                    if (cur().is(TokenKind::Semicolon)) {
                        next();
                        continue;
                    }
                    // Parse multiple declarators for this field
                    bool firstField = true;
                    while (!atEnd() && !cur().is(TokenKind::Semicolon)) {
                        if (!firstField) consume(TokenKind::Comma, "expected ','");
                        firstField = false;
                        std::string fname;
                        TypePtr ftype = parseDeclarator(fds.baseType, fname);
                        i32 bitWidth = -1;
                        if (cur().is(TokenKind::Colon)) {
                            next();
                            // bit width: constant expression
                            auto bwExpr = parseUnaryExpr();
                            if (bwExpr && bwExpr->kind() == ExprKind::IntLit) {
                                bitWidth = (i32)static_cast<IntLitExpr*>(bwExpr.get())->value;
                            }
                        }
                        FieldInfo fi;
                        fi.name     = fname;
                        fi.type     = ftype;
                        fi.bitWidth = bitWidth;
                        rec->addField(fi);
                    }
                    consume(TokenKind::Semicolon, "expected ';' after field declaration");
                }
                popScope();
                consume(TokenKind::RBrace, "expected '}' to close record");
                rec->finalize();
                baseType = rec;
            } else {
                // Forward reference or use of existing tag
                if (!tagName.empty()) {
                    // Look up in tag scopes
                    for (int i = (int)scopes_.size() - 1; i >= 0; --i) {
                        auto it = scopes_[i].tags.find(tagName);
                        if (it != scopes_[i].tags.end()) {
                            baseType = it->second;
                            break;
                        }
                    }
                    if (!baseType) {
                        // Create incomplete forward declaration
                        auto rec = types_.makeRecord(rk, tagName);
                        scopes_.back().tags[tagName] = rec;
                        baseType = rec;
                    }
                } else {
                    diag_.error(t.loc, "expected tag name or '{' after struct/union/class");
                    baseType = types_.intTy();
                }
            }
            running = false;
            break;
        }

        case TokenKind::Kw_enum: {
            next(); // consume 'enum'
            auto enumDecl = parseEnumDecl();
            if (enumDecl && enumDecl->enumType) {
                baseType = enumDecl->enumType;
            } else {
                baseType = types_.intTy();
            }
            running = false;
            break;
        }

        case TokenKind::Kw_typename: {
            // typename T — just skip typename and parse the following type name
            next();
            if (cur().is(TokenKind::Identifier) || cur().is(TokenKind::ColonColon)) {
                std::string name;
                // Handle :: prefix
                if (cur().is(TokenKind::ColonColon)) next();
                while (cur().is(TokenKind::Identifier)) {
                    name = cur().text;
                    next();
                    if (cur().is(TokenKind::ColonColon)) {
                        next();
                    } else {
                        break;
                    }
                }
                TypePtr ty = lookupTypeName(name);
                if (ty) {
                    baseType = ty;
                } else {
                    baseType = types_.intTy();
                }
            }
            running = false;
            break;
        }

        case TokenKind::Identifier: {
            TypePtr ty = lookupTypeName(t.text);
            if (ty) {
                next();
                baseType = ty;
            }
            running = false;
            break;
        }

        case TokenKind::ColonColon: {
            // Global scope qualified name
            next();
            std::string name;
            while (cur().is(TokenKind::Identifier)) {
                name = cur().text;
                next();
                if (cur().is(TokenKind::ColonColon)) {
                    next();
                } else {
                    break;
                }
            }
            TypePtr ty = lookupTypeName(name);
            if (ty) baseType = ty;
            else     baseType = types_.intTy();
            running = false;
            break;
        }

        default:
            running = false;
            break;
        }
    }

    // Resolve composite type from flags
    if (!baseType) {
        if (hasVoid) {
            baseType = types_.voidTy();
        } else if (hasBool) {
            baseType = types_.boolTy();
        } else if (hasChar) {
            if (isUnsigned)   baseType = types_.ucharTy();
            else if (isSigned) baseType = types_.scharTy();
            else               baseType = types_.charTy();
        } else if (hasFloat) {
            baseType = types_.floatTy();
        } else if (hasDouble) {
            if (longCount >= 1) { baseType = std::make_shared<BuiltinType>(TypeKind::LongDouble); }
            else                baseType = types_.doubleTy();
        } else if (isShort) {
            baseType = isUnsigned ? types_.ushortTy() : types_.shortTy();
        } else if (longCount >= 2) {
            baseType = isUnsigned ? types_.ulonglongTy() : types_.longlongTy();
        } else if (longCount == 1) {
            baseType = isUnsigned ? types_.ulongTy() : types_.longTy();
        } else if (hasInt || isSigned || isUnsigned) {
            baseType = isUnsigned ? types_.uintTy() : types_.intTy();
        } else {
            baseType = types_.intTy(); // default
        }
    }

    // Apply qualifiers
    QualFlags qf;
    qf.isConst    = isConst;
    qf.isVolatile = isVolatile;
    qf.isRestrict = isRestrict;
    if (qf.any()) {
        baseType = types_.qualOf(baseType, qf);
    }

    return baseType;
}

// ============================================================
// Declarator parsing
// ============================================================
TypePtr Parser::parseDeclarator(TypePtr base, std::string& nameOut) {
    // Handle leading *, &, &&
    TypePtr type = parsePointerSuffix(base);

    // Handle name and function/array suffixes
    type = parseFunctionSuffix(type, nameOut);

    return type;
}

TypePtr Parser::parseAbstractDeclarator(TypePtr base) {
    std::string ignored;
    TypePtr type = parsePointerSuffix(base);
    // Abstract declarator might have () and [] but no name
    if (cur().is(TokenKind::LParen)) {
        // Could be function type or grouped declarator
        next();
        if (!cur().is(TokenKind::RParen)) {
            type = parseDeclarator(type, ignored);
            consume(TokenKind::RParen, "expected ')'");
        } else {
            consume(TokenKind::RParen, "expected ')'");
        }
    }
    // Array suffixes
    type = parseArraySuffix(type);
    return type;
}

TypePtr Parser::parsePointerSuffix(TypePtr base) {
    TypePtr type = base;
    while (!atEnd()) {
        if (cur().is(TokenKind::Star)) {
            next();
            // Consume qualifiers on pointer
            QualFlags qf;
            while (cur().is(TokenKind::Kw_const) || cur().is(TokenKind::Kw_volatile) || cur().is(TokenKind::Kw_restrict)) {
                if (cur().is(TokenKind::Kw_const))    { qf.isConst    = true; next(); }
                if (cur().is(TokenKind::Kw_volatile)) { qf.isVolatile = true; next(); }
                if (cur().is(TokenKind::Kw_restrict)) { qf.isRestrict = true; next(); }
            }
            type = types_.ptrTo(type);
            if (qf.any()) type = types_.qualOf(type, qf);
        } else if (cur().is(TokenKind::Amp)) {
            next();
            type = types_.refTo(type);
        } else if (cur().is(TokenKind::AmpAmp)) {
            next();
            // rvalue reference — store as shared_ptr; lifetime managed by AST
            auto t = std::make_shared<PointerType>(type, TypeKind::RValueRef);
            type = t;
        } else {
            break;
        }
    }
    return type;
}

TypePtr Parser::parseFunctionSuffix(TypePtr base, std::string& nameOut) {
    TypePtr type = base;

    // Optional grouped declarator: (*name) or (&name)
    if (cur().is(TokenKind::LParen)) {
        // Check if this is a grouped declarator (contains *, &, name)
        // We do a lookahead heuristic: if after '(' we see *, &, identifier
        Token after = pp_.peek(); // peek again? Actually cur() is already peek
        // We need to look one more ahead
        // Simple heuristic: if next after '(' is *, &, identifier that's not a type, it's grouped
        next(); // consume '('
        if (cur().is(TokenKind::Star) || cur().is(TokenKind::Amp) || cur().is(TokenKind::AmpAmp)) {
            // Grouped declarator like (*name)
            TypePtr innerType = parsePointerSuffix(type);
            if (cur().is(TokenKind::Identifier)) {
                nameOut = cur().text;
                next();
            }
            consume(TokenKind::RParen, "expected ')'");
            type = innerType;
            // Now check for array/function suffixes
            type = parseArraySuffix(type);
            if (cur().is(TokenKind::LParen)) {
                type = parseFunctionSuffix(type, nameOut);
            }
            return type;
        } else {
            // This was a function call suffix or parameter list — put it back conceptually
            // Actually we consumed '(', now we need to parse parameter list
            // This is a function type: base(params)
            std::vector<ParamInfo> params;
            bool variadic = false;
            if (!cur().is(TokenKind::RParen)) {
                // Parse parameters
                while (!atEnd() && !cur().is(TokenKind::RParen)) {
                    if (cur().is(TokenKind::Ellipsis)) {
                        variadic = true;
                        next();
                        break;
                    }
                    if (!params.empty()) {
                        if (!match(TokenKind::Comma)) break;
                        if (cur().is(TokenKind::Ellipsis)) {
                            variadic = true;
                            next();
                            break;
                        }
                    }
                    // Check for void parameter list: (void)
                    if (cur().is(TokenKind::Kw_void) && (pp_.peek().is(TokenKind::RParen))) {
                        next(); // consume void
                        break;
                    }
                    DeclSpec pds = parseDeclSpec();
                    std::string pname;
                    TypePtr ptype = parseDeclarator(pds.baseType, pname);
                    ParamInfo pi;
                    pi.name = pname;
                    pi.type = ptype;
                    params.push_back(std::move(pi));
                }
            }
            consume(TokenKind::RParen, "expected ')'");
            // Skip cv-qualifiers on member function (const, volatile, noexcept, override, final)
            while (cur().is(TokenKind::Kw_const) || cur().is(TokenKind::Kw_volatile)
                || cur().is(TokenKind::Kw_noexcept) || cur().is(TokenKind::Kw_override)
                || cur().is(TokenKind::Kw_final)) {
                next();
            }
            // Skip -> trailing return type
            if (cur().is(TokenKind::Arrow)) {
                next();
                DeclSpec rds = parseDeclSpec();
                std::string ignored2;
                parseDeclarator(rds.baseType, ignored2);
            }
            type = types_.makeFn(type, std::move(params), variadic);
            return type;
        }
    }

    // Name
    if (cur().is(TokenKind::Identifier)) {
        nameOut = cur().text;
        next();
        // Handle :: scope resolution
        while (cur().is(TokenKind::ColonColon)) {
            next();
            if (cur().is(TokenKind::Identifier)) {
                nameOut = cur().text;
                next();
            }
        }
        // Template argument list
        if (cur().is(TokenKind::Lt)) {
            next();
            int depth = 1;
            while (!atEnd() && depth > 0) {
                if (cur().is(TokenKind::Lt))       { ++depth; next(); }
                else if (cur().is(TokenKind::Gt))  { --depth; next(); }
                else if (cur().is(TokenKind::RShift)) {
                    // >> treated as >>
                    --depth;
                    if (depth == 0) { next(); break; }
                    --depth;
                    next();
                }
                else next();
            }
        }
    } else if (cur().is(TokenKind::ColonColon)) {
        // ::name
        next();
        if (cur().is(TokenKind::Identifier)) {
            nameOut = cur().text;
            next();
        }
    }

    // Array suffix
    type = parseArraySuffix(type);

    // Function parameter list suffix
    if (cur().is(TokenKind::LParen)) {
        next(); // consume '('
        std::vector<ParamInfo> params;
        bool variadic = false;
        if (!cur().is(TokenKind::RParen)) {
            while (!atEnd() && !cur().is(TokenKind::RParen)) {
                if (cur().is(TokenKind::Ellipsis)) {
                    variadic = true;
                    next();
                    break;
                }
                if (!params.empty()) {
                    if (!match(TokenKind::Comma)) break;
                    if (cur().is(TokenKind::Ellipsis)) {
                        variadic = true;
                        next();
                        break;
                    }
                }
                // Check for void parameter list: (void)
                if (cur().is(TokenKind::Kw_void) && (pp_.peek().is(TokenKind::RParen))) {
                    next();
                    break;
                }
                DeclSpec pds = parseDeclSpec();
                std::string pname;
                TypePtr ptype = parseDeclarator(pds.baseType, pname);
                // Optional default value
                if (cur().is(TokenKind::Eq)) {
                    next();
                    parseAssignExpr(); // discard default value for now
                }
                ParamInfo pi;
                pi.name = pname;
                pi.type = ptype;
                params.push_back(std::move(pi));
            }
        }
        consume(TokenKind::RParen, "expected ')'");
        // Skip trailing qualifiers
        while (cur().is(TokenKind::Kw_const) || cur().is(TokenKind::Kw_volatile)
            || cur().is(TokenKind::Kw_noexcept) || cur().is(TokenKind::Kw_override)
            || cur().is(TokenKind::Kw_final)) {
            next();
        }
        // Trailing return type
        if (cur().is(TokenKind::Arrow)) {
            next();
            DeclSpec rds = parseDeclSpec();
            std::string ignored2;
            parseDeclarator(rds.baseType, ignored2);
        }
        type = types_.makeFn(type, std::move(params), variadic);
    }

    return type;
}

TypePtr Parser::parseArraySuffix(TypePtr base) {
    TypePtr type = base;
    while (cur().is(TokenKind::LBracket)) {
        next(); // consume '['
        if (cur().is(TokenKind::RBracket)) {
            next();
            type = types_.arrayOf(type, -1); // incomplete
        } else if (cur().is(TokenKind::Star)) {
            next();
            consume(TokenKind::RBracket, "expected ']'");
            type = types_.arrayOf(type, -1); // VLA
        } else {
            auto sizeExpr = parseAssignExpr();
            i64 count = -1;
            if (sizeExpr && sizeExpr->kind() == ExprKind::IntLit) {
                count = (i64)static_cast<IntLitExpr*>(sizeExpr.get())->value;
            }
            consume(TokenKind::RBracket, "expected ']'");
            type = types_.arrayOf(type, count);
        }
    }
    return type;
}

// ============================================================
// Record declaration
// ============================================================
Ptr<RecordDecl> Parser::parseRecordDecl(TypeKind k) {
    SourceLocation loc = cur().loc;
    // 'struct'/'union'/'class' already consumed by caller

    auto rd = make<RecordDecl>();
    rd->loc = loc;

    // Tag name
    if (cur().is(TokenKind::Identifier)) {
        rd->name = cur().text;
        next();
    }

    // Base class list (C++): class Foo : public Bar
    if (cur().is(TokenKind::Colon)) {
        next();
        while (!atEnd() && !cur().is(TokenKind::LBrace)) {
            // Skip access specifier
            if (cur().is(TokenKind::Kw_public) || cur().is(TokenKind::Kw_protected) || cur().is(TokenKind::Kw_private)) {
                next();
            }
            // Skip 'virtual'
            if (cur().is(TokenKind::Kw_virtual)) next();
            // Skip base name (possibly qualified)
            while (cur().is(TokenKind::Identifier) || cur().is(TokenKind::ColonColon)) {
                next();
            }
            // Template args
            if (cur().is(TokenKind::Lt)) {
                next();
                int depth = 1;
                while (!atEnd() && depth > 0) {
                    if (cur().is(TokenKind::Lt))      { ++depth; next(); }
                    else if (cur().is(TokenKind::Gt)) { --depth; next(); }
                    else next();
                }
            }
            if (!match(TokenKind::Comma)) break;
        }
    }

    // Create the RecordType
    auto recType = types_.makeRecord(k, rd->name);
    rd->recordType = recType;
    rd->type = recType;

    // Register tag
    if (!rd->name.empty()) {
        scopes_.back().tags[rd->name] = recType;
    }

    if (!cur().is(TokenKind::LBrace)) {
        // Forward declaration
        return rd;
    }

    consume(TokenKind::LBrace, "expected '{'");
    pushScope();

    while (!atEnd() && !cur().is(TokenKind::RBrace)) {
        if (cur().is(TokenKind::Semicolon)) { next(); continue; }
        // Access specifiers
        if (cur().is(TokenKind::Kw_public) || cur().is(TokenKind::Kw_protected) || cur().is(TokenKind::Kw_private)) {
            next();
            consume(TokenKind::Colon, "expected ':'");
            continue;
        }
        // Friend declaration
        if (cur().is(TokenKind::Kw_friend)) {
            next();
            // Skip until semicolon
            while (!atEnd() && !cur().is(TokenKind::Semicolon)) next();
            consume(TokenKind::Semicolon, "expected ';'");
            continue;
        }
        // Nested type or member
        auto decl = parseDeclaration();
        if (decl) {
            if (decl->kind() == DeclKind::Var || decl->kind() == DeclKind::Field) {
                FieldInfo fi;
                fi.name = decl->name;
                fi.type = decl->type;
                recType->addField(fi);
            }
            rd->members.push_back(std::move(decl));
        }
    }

    popScope();
    consume(TokenKind::RBrace, "expected '}' to close record");
    recType->finalize();

    return rd;
}

// ============================================================
// Enum declaration
// ============================================================
Ptr<EnumDecl> Parser::parseEnumDecl() {
    SourceLocation loc = cur().loc;
    // 'enum' already consumed

    auto ed = make<EnumDecl>();
    ed->loc = loc;

    // Scoped enum: enum class / enum struct
    bool isScoped = false;
    if (cur().is(TokenKind::Kw_class) || cur().is(TokenKind::Kw_struct)) {
        isScoped = true;
        next();
    }

    // Tag name
    if (cur().is(TokenKind::Identifier)) {
        ed->name = cur().text;
        next();
    }

    // Optional underlying type: enum Foo : int
    if (cur().is(TokenKind::Colon)) {
        next();
        DeclSpec uds = parseDeclSpec();
        (void)uds; // underlying type stored in DeclSpec, ignored for now
    }

    auto enumType = types_.makeEnum(ed->name);
    ed->enumType = enumType;
    ed->type     = enumType;

    // Register in scope
    if (!ed->name.empty()) {
        scopes_.back().enums[ed->name] = enumType;
    }

    if (!cur().is(TokenKind::LBrace)) {
        // Forward declaration
        return ed;
    }

    consume(TokenKind::LBrace, "expected '{'");

    i64 nextVal = 0;
    while (!atEnd() && !cur().is(TokenKind::RBrace)) {
        if (cur().is(TokenKind::Comma)) { next(); continue; }
        if (!cur().is(TokenKind::Identifier)) break;

        std::string eName = cur().text;
        next();

        i64 val = nextVal;
        if (match(TokenKind::Eq)) {
            auto valExpr = parseAssignExpr();
            if (valExpr && valExpr->kind() == ExprKind::IntLit) {
                val = (i64)static_cast<IntLitExpr*>(valExpr.get())->value;
            }
        }
        nextVal = val + 1;
        enumType->addEnumerator(eName, val);

        if (!cur().is(TokenKind::Comma)) break;
    }

    consume(TokenKind::RBrace, "expected '}' to close enum");

    return ed;
}

// ============================================================
// Declaration parsing
// ============================================================
DeclPtr Parser::parseDeclaration() {
    SourceLocation loc = cur().loc;

    if (cur().is(TokenKind::Kw__Static_assert)) {
        next();
        consume(TokenKind::LParen, "expected '(' after _Static_assert");
        auto expr = parseAssignExpr();
        consume(TokenKind::Comma, "expected ',' in _Static_assert");
        Token msg = cur();
        consume(TokenKind::StringLit, "expected string literal in _Static_assert");
        consume(TokenKind::RParen, "expected ')' after _Static_assert");
        consume(TokenKind::Semicolon, "expected ';' after _Static_assert");

        auto sa = make<StaticAssertDecl>();
        sa->loc = loc;
        sa->expr = std::move(expr);
        sa->message = msg.text;
        return sa;
    }

    DeclSpec ds = parseDeclSpec();

    // If just a semicolon (bare struct/union/class declaration)
    if (cur().is(TokenKind::Semicolon)) {
        next();
        // May have produced a forward decl via side-effect of parseTypeSpecifier
        return nullptr;
    }

    std::string name;
    TypePtr type = parseDeclarator(ds.baseType, name);

    // Determine if function or variable
    if (type && type->isFunction()) {
        return parseFunctionDecl(ds, type, name, loc);
    }

    // Could still be a function if the type is a pointer-to-function
    // Check for '(' immediately after declarator — already handled in parseFunctionSuffix

    // Variable declaration(s)
    auto varDecl = parseVarDecl(ds, type, name, loc);

    // Handle multiple declarators: int a, b, c;
    while (match(TokenKind::Comma)) {
        std::string extraName;
        TypePtr extraType = parseDeclarator(ds.baseType, extraName);
        // For simplicity, we only return the first; extras would need a DeclStmt
        // In top-level context, we emit them as separate decls. For now store first.
        auto extra = parseVarDecl(ds, extraType, extraName, loc);
        // We lose the extra decls here at top level — acceptable for now
        (void)extra;
    }

    consume(TokenKind::Semicolon, "expected ';' after declaration");
    return varDecl;
}

Ptr<FuncDecl> Parser::parseFunctionDecl(DeclSpec& ds, TypePtr fnType, std::string name, SourceLocation loc) {
    auto fd = make<FuncDecl>();
    fd->loc       = loc;
    fd->name      = name;
    fd->type      = fnType;
    fd->isExtern  = ds.isExtern;
    fd->isStatic  = ds.isStatic;
    fd->isInline  = ds.isInline;
    fd->isConstexpr = ds.isConstexpr;
    fd->isVirtual = ds.isVirtual;
    fd->isNoreturn = ds.isNoreturn;
    fd->alignasExpr = std::move(ds.alignasExpr);

    // Build param decls from function type
    if (fnType && fnType->kind() == TypeKind::Function) {
        auto* fty = static_cast<FunctionType*>(fnType.get());
        fd->isVariadic = fty->isVariadic();
        u32 idx = 0;
        for (const auto& pi : fty->params()) {
            auto pd = make<ParamDecl>();
            pd->name  = pi.name;
            pd->type  = pi.type;
            pd->index = idx++;
            fd->params.push_back(std::move(pd));
        }
    }

    if (ds.isTypedef) {
        // typedef of function type
        defineTypedef(name, fnType);
        auto td = make<TypedefDecl>();
        td->loc = loc;
        td->name = name;
        td->type = fnType;
        td->aliasedType = fnType;
        consume(TokenKind::Semicolon, "expected ';'");
        return nullptr; // return nothing in this path — typedef is handled
    }

    // Function body or declaration
    if (cur().is(TokenKind::LBrace)) {
        pushScope();
        // Add params to scope
        for (auto& pd : fd->params) {
            // They'll be added by sema; just push scope
        }
        fd->body = parseCompoundStmt();
        popScope();
    } else if (cur().is(TokenKind::Colon)) {
        // Constructor initializer list — skip it
        next();
        while (!atEnd() && !cur().is(TokenKind::LBrace)) {
            next();
        }
        pushScope();
        fd->body = parseCompoundStmt();
        popScope();
    } else {
        // Declaration only
        consume(TokenKind::Semicolon, "expected ';' after function declaration");
    }

    return fd;
}

Ptr<VarDecl> Parser::parseVarDecl(DeclSpec& ds, TypePtr type, std::string name, SourceLocation loc) {
    if (ds.isTypedef) {
        // Register typedef
        defineTypedef(name, type);
        auto td = make<TypedefDecl>();
        td->loc         = loc;
        td->name        = name;
        td->type        = type;
        td->aliasedType = type;
        // Don't consume semicolon here — caller does
        return nullptr;
    }

    auto vd = make<VarDecl>();
    vd->loc       = loc;
    vd->name      = name;
    vd->type      = type;
    vd->isExtern  = ds.isExtern;
    vd->isStatic  = ds.isStatic;
    vd->isConstexpr = ds.isConstexpr;
    vd->isMutable = ds.isMutable;
    vd->alignasExpr = std::move(ds.alignasExpr);

    // Optional initializer
    if (match(TokenKind::Eq)) {
        if (cur().is(TokenKind::LBrace)) {
            vd->init = parseInitListExpr();
        } else {
            vd->init = parseAssignExpr();
        }
    } else if (cur().is(TokenKind::LParen)) {
        // Constructor-style: int x(5);
        next();
        vd->init = parseAssignExpr();
        consume(TokenKind::RParen, "expected ')'");
    } else if (cur().is(TokenKind::LBrace)) {
        // Brace-init: int x{5};
        vd->init = parseInitListExpr();
    }

    return vd;
}

// ============================================================
// Statements
// ============================================================
StmtPtr Parser::parseStmt() {
    Token t = cur();
    switch (t.kind) {
    case TokenKind::LBrace:
        return parseCompoundStmt();
    case TokenKind::Kw_if:
        return parseIfStmt();
    case TokenKind::Kw_while:
        return parseWhileStmt();
    case TokenKind::Kw_do:
        return parseDoWhileStmt();
    case TokenKind::Kw_for:
        return parseForStmt();
    case TokenKind::Kw_switch:
        return parseSwitchStmt();
    case TokenKind::Kw_return:
        return parseReturnStmt();
    case TokenKind::Kw_break:
        return parseBreakStmt();
    case TokenKind::Kw_continue:
        return parseContinueStmt();
    case TokenKind::Kw_goto:
        return parseGotoStmt();
    case TokenKind::Kw_try:
        return parseTryStmt();
    case TokenKind::Kw_throw: {
        SourceLocation loc = cur().loc;
        next();
        auto s = make<ThrowStmt>();
        s->loc = loc;
        if (!cur().is(TokenKind::Semicolon)) {
            s->value = parseExpr();
        }
        consume(TokenKind::Semicolon, "expected ';' after throw");
        return s;
    }
    case TokenKind::Kw_case: {
        SourceLocation loc = cur().loc;
        next();
        auto cs = make<CaseStmt>();
        cs->loc       = loc;
        cs->isDefault = false;
        cs->value     = parseExpr();
        consume(TokenKind::Colon, "expected ':' after case");
        cs->body = parseStmt();
        return cs;
    }
    case TokenKind::Kw_default: {
        SourceLocation loc = cur().loc;
        next();
        consume(TokenKind::Colon, "expected ':' after default");
        auto cs = make<CaseStmt>();
        cs->loc       = loc;
        cs->isDefault = true;
        cs->body      = parseStmt();
        return cs;
    }
    case TokenKind::Semicolon: {
        SourceLocation loc = cur().loc;
        next();
        auto s = make<NullStmt>();
        s->loc = loc;
        return s;
    }
    default:
        // Label?
        if (t.is(TokenKind::Identifier)) {
            Token peek2 = pp_.peek();
            // Peek 2 tokens ahead is tricky; we check next() approach
            // Actually pp_.peek() gives the NEXT token after current.
            // Since cur() == t (identifier), pp_.peek() gives what comes after.
            if (peek2.is(TokenKind::Colon)) {
                SourceLocation loc = t.loc;
                std::string label = t.text;
                next(); // consume identifier
                next(); // consume ':'
                auto ls = make<LabelStmt>();
                ls->loc   = loc;
                ls->label = label;
                ls->body  = parseStmt();
                return ls;
            }
        }
        // Declaration or expression statement
        if (isStartOfDeclaration()) {
            SourceLocation loc = cur().loc;
            auto ds = make<DeclStmt>();
            ds->loc = loc;
            
            if (cur().is(TokenKind::Kw__Static_assert)) {
                auto decl = parseDeclaration();
                if (decl) ds->decls.push_back(std::move(decl));
                return ds;
            }

            DeclSpec spec = parseDeclSpec();
            bool first = true;
            while (!atEnd() && !cur().is(TokenKind::Semicolon)) {
                if (!first) {
                    if (!match(TokenKind::Comma)) break;
                }
                first = false;
                std::string name;
                TypePtr type = parseDeclarator(spec.baseType, name);
                if (type && type->isFunction()) {
                    auto fd = parseFunctionDecl(spec, type, name, loc);
                    if (fd) ds->decls.push_back(std::move(fd));
                    break; // function decl ends here
                } else {
                    auto vd = parseVarDecl(spec, type, name, loc);
                    if (vd) ds->decls.push_back(std::move(vd));
                }
            }
            consume(TokenKind::Semicolon, "expected ';'");
            return ds;
        } else {
            // Expression statement
            SourceLocation loc = cur().loc;
            auto es = make<ExprStmt>();
            es->loc  = loc;
            es->expr = parseExpr();
            consume(TokenKind::Semicolon, "expected ';' after expression");
            return es;
        }
    }
}

StmtPtr Parser::parseCompoundStmt() {
    SourceLocation loc = cur().loc;
    consume(TokenKind::LBrace, "expected '{'");
    pushScope();
    auto cs = make<CompoundStmt>();
    cs->loc = loc;
    while (!atEnd() && !cur().is(TokenKind::RBrace)) {
        if (auto s = parseStmt()) {
            cs->stmts.push_back(std::move(s));
        }
    }
    popScope();
    consume(TokenKind::RBrace, "expected '}'");
    return cs;
}

StmtPtr Parser::parseIfStmt() {
    SourceLocation loc = cur().loc;
    consume(TokenKind::Kw_if, "expected 'if'");
    consume(TokenKind::LParen, "expected '('");
    auto ifs = make<IfStmt>();
    ifs->loc  = loc;
    // Optional: if (init; cond) — C++17 init statement
    if (isStartOfDeclaration()) {
        // might be init statement; parse and embed
        auto initS = parseStmt();
        if (!cur().is(TokenKind::RParen)) {
            ifs->cond = parseExpr();
        } else {
            // The "decl" was the condition
            (void)initS;
            ifs->cond = nullptr;
        }
    } else {
        ifs->cond = parseExpr();
    }
    consume(TokenKind::RParen, "expected ')'");
    ifs->then = parseStmt();
    if (match(TokenKind::Kw_else)) {
        ifs->els = parseStmt();
    }
    return ifs;
}

StmtPtr Parser::parseWhileStmt() {
    SourceLocation loc = cur().loc;
    consume(TokenKind::Kw_while, "expected 'while'");
    consume(TokenKind::LParen, "expected '('");
    auto ws = make<WhileStmt>();
    ws->loc  = loc;
    ws->cond = parseExpr();
    consume(TokenKind::RParen, "expected ')'");
    ws->body = parseStmt();
    return ws;
}

StmtPtr Parser::parseDoWhileStmt() {
    SourceLocation loc = cur().loc;
    consume(TokenKind::Kw_do, "expected 'do'");
    auto dws = make<DoWhileStmt>();
    dws->loc  = loc;
    dws->body = parseStmt();
    consume(TokenKind::Kw_while, "expected 'while'");
    consume(TokenKind::LParen, "expected '('");
    dws->cond = parseExpr();
    consume(TokenKind::RParen, "expected ')'");
    consume(TokenKind::Semicolon, "expected ';' after do-while");
    return dws;
}

StmtPtr Parser::parseForStmt() {
    SourceLocation loc = cur().loc;
    consume(TokenKind::Kw_for, "expected 'for'");
    consume(TokenKind::LParen, "expected '('");

    auto fs = make<ForStmt>();
    fs->loc = loc;

    pushScope();

    // C++ range-for detection: for (decl : range)
    // Heuristic: if we see a type specifier followed by a declarator followed by ':'
    // We do speculative parse with a simple lookahead
    bool isRangeFor = false;
    if (isStartOfDeclaration()) {
        // Could be range-for or init-decl
        DeclSpec spec = parseDeclSpec();
        std::string varName;
        TypePtr varType = parseDeclarator(spec.baseType, varName);
        if (cur().is(TokenKind::Colon)) {
            // Range-for: for (T x : range)
            next(); // consume ':'
            isRangeFor = true;
            auto rangeExpr = parseExpr();
            consume(TokenKind::RParen, "expected ')'");

            // Build equivalent: init = nullptr, cond = rangeExpr (we store range in cond for now)
            auto vd = make<VarDecl>();
            vd->loc  = loc;
            vd->name = varName;
            vd->type = varType;
            auto ds = make<DeclStmt>();
            ds->decls.push_back(std::move(vd));
            fs->init = std::move(ds);
            fs->cond = std::move(rangeExpr);
            fs->step = nullptr;
        } else {
            // Normal for init-decl: for (int i = 0; ...)
            auto vd = parseVarDecl(spec, varType, varName, loc);
            // Handle multiple declarators
            while (match(TokenKind::Comma)) {
                std::string en;
                TypePtr et = parseDeclarator(spec.baseType, en);
                auto ev = parseVarDecl(spec, et, en, loc);
                (void)ev;
            }
            auto ds = make<DeclStmt>();
            if (vd) ds->decls.push_back(std::move(vd));
            fs->init = std::move(ds);
            consume(TokenKind::Semicolon, "expected ';' in for");
        }
    } else {
        // Expression init or empty
        if (!cur().is(TokenKind::Semicolon)) {
            auto es = make<ExprStmt>();
            es->expr = parseExpr();
            fs->init = std::move(es);
        }
        consume(TokenKind::Semicolon, "expected ';' in for");
    }

    if (!isRangeFor) {
        // Condition
        if (!cur().is(TokenKind::Semicolon)) {
            fs->cond = parseExpr();
        }
        consume(TokenKind::Semicolon, "expected ';' in for");
        // Step
        if (!cur().is(TokenKind::RParen)) {
            fs->step = parseExpr();
        }
        consume(TokenKind::RParen, "expected ')'");
    }

    fs->body = parseStmt();
    popScope();
    return fs;
}

StmtPtr Parser::parseSwitchStmt() {
    SourceLocation loc = cur().loc;
    consume(TokenKind::Kw_switch, "expected 'switch'");
    consume(TokenKind::LParen, "expected '('");
    auto ss = make<SwitchStmt>();
    ss->loc  = loc;
    ss->cond = parseExpr();
    consume(TokenKind::RParen, "expected ')'");
    ss->body = parseStmt();
    return ss;
}

StmtPtr Parser::parseReturnStmt() {
    SourceLocation loc = cur().loc;
    consume(TokenKind::Kw_return, "expected 'return'");
    auto rs = make<ReturnStmt>();
    rs->loc = loc;
    if (!cur().is(TokenKind::Semicolon)) {
        rs->value = parseExpr();
    }
    consume(TokenKind::Semicolon, "expected ';' after return");
    return rs;
}

StmtPtr Parser::parseBreakStmt() {
    SourceLocation loc = cur().loc;
    consume(TokenKind::Kw_break, "expected 'break'");
    consume(TokenKind::Semicolon, "expected ';'");
    auto s = make<BreakStmt>();
    s->loc = loc;
    return s;
}

StmtPtr Parser::parseContinueStmt() {
    SourceLocation loc = cur().loc;
    consume(TokenKind::Kw_continue, "expected 'continue'");
    consume(TokenKind::Semicolon, "expected ';'");
    auto s = make<ContinueStmt>();
    s->loc = loc;
    return s;
}

StmtPtr Parser::parseGotoStmt() {
    SourceLocation loc = cur().loc;
    consume(TokenKind::Kw_goto, "expected 'goto'");
    auto gs = make<GotoStmt>();
    gs->loc   = loc;
    gs->label = cur().text;
    consume(TokenKind::Identifier, "expected label name");
    consume(TokenKind::Semicolon, "expected ';'");
    return gs;
}

StmtPtr Parser::parseTryStmt() {
    SourceLocation loc = cur().loc;
    consume(TokenKind::Kw_try, "expected 'try'");
    auto ts = make<TryStmt>();
    ts->loc  = loc;
    ts->body = parseCompoundStmt();

    while (cur().is(TokenKind::Kw_catch)) {
        next(); // consume 'catch'
        consume(TokenKind::LParen, "expected '('");

        TryStmt::CatchClause clause;
        if (cur().is(TokenKind::Ellipsis)) {
            next(); // catch(...)
            clause.type = nullptr;
        } else {
            DeclSpec cds = parseDeclSpec();
            std::string cname;
            clause.type = parseDeclarator(cds.baseType, cname);
            clause.name = cname;
        }
        consume(TokenKind::RParen, "expected ')'");
        clause.body = parseCompoundStmt();
        ts->catches.push_back(std::move(clause));
    }

    return ts;
}

// ============================================================
// Expressions
// ============================================================
ExprPtr Parser::parseExpr() {
    auto e = parseAssignExpr();
    while (match(TokenKind::Comma)) {
        SourceLocation loc = cur().loc;
        auto rhs = parseAssignExpr();
        auto be = make<BinaryExpr>();
        be->loc = loc;
        be->op  = BinaryOp::Comma;
        be->lhs = std::move(e);
        be->rhs = std::move(rhs);
        e = std::move(be);
    }
    return e;
}

ExprPtr Parser::parseAssignExpr() {
    ExprPtr lhs = parseTernaryExpr();

    AssignOp aop;
    bool isAssign = false;
    switch (cur().kind) {
    case TokenKind::Eq:         aop = AssignOp::Assign;    isAssign = true; break;
    case TokenKind::PlusEq:     aop = AssignOp::AddAssign; isAssign = true; break;
    case TokenKind::MinusEq:    aop = AssignOp::SubAssign; isAssign = true; break;
    case TokenKind::StarEq:     aop = AssignOp::MulAssign; isAssign = true; break;
    case TokenKind::SlashEq:    aop = AssignOp::DivAssign; isAssign = true; break;
    case TokenKind::PercentEq:  aop = AssignOp::ModAssign; isAssign = true; break;
    case TokenKind::AmpEq:      aop = AssignOp::AndAssign; isAssign = true; break;
    case TokenKind::PipeEq:     aop = AssignOp::OrAssign;  isAssign = true; break;
    case TokenKind::CaretEq:    aop = AssignOp::XorAssign; isAssign = true; break;
    case TokenKind::LShiftEq:   aop = AssignOp::ShlAssign; isAssign = true; break;
    case TokenKind::RShiftEq:   aop = AssignOp::ShrAssign; isAssign = true; break;
    default: break;
    }

    if (isAssign) {
        SourceLocation loc = cur().loc;
        next();
        auto rhs = parseAssignExpr();
        auto ae = make<AssignExpr>();
        ae->loc = loc;
        ae->op  = aop;
        ae->lhs = std::move(lhs);
        ae->rhs = std::move(rhs);
        return ae;
    }

    return lhs;
}

ExprPtr Parser::parseTernaryExpr() {
    auto cond = parseLogOrExpr();
    if (match(TokenKind::Question)) {
        SourceLocation loc = cur().loc;
        auto then = parseExpr();
        consume(TokenKind::Colon, "expected ':' in ternary");
        auto els = parseTernaryExpr();
        auto te = make<TernaryExpr>();
        te->loc  = loc;
        te->cond = std::move(cond);
        te->then = std::move(then);
        te->els  = std::move(els);
        return te;
    }
    return cond;
}

ExprPtr Parser::parseLogOrExpr() {
    auto lhs = parseLogAndExpr();
    while (cur().is(TokenKind::PipePipe) || cur().is(TokenKind::Kw_or)) {
        SourceLocation loc = cur().loc;
        next();
        auto rhs = parseLogAndExpr();
        auto be = make<BinaryExpr>();
        be->loc = loc;
        be->op  = BinaryOp::LOr;
        be->lhs = std::move(lhs);
        be->rhs = std::move(rhs);
        lhs = std::move(be);
    }
    return lhs;
}

ExprPtr Parser::parseLogAndExpr() {
    auto lhs = parseBitorExpr();
    while (cur().is(TokenKind::AmpAmp) || cur().is(TokenKind::Kw_and)) {
        SourceLocation loc = cur().loc;
        next();
        auto rhs = parseBitorExpr();
        auto be = make<BinaryExpr>();
        be->loc = loc;
        be->op  = BinaryOp::LAnd;
        be->lhs = std::move(lhs);
        be->rhs = std::move(rhs);
        lhs = std::move(be);
    }
    return lhs;
}

ExprPtr Parser::parseBitorExpr() {
    auto lhs = parseXorExpr();
    while (cur().is(TokenKind::Pipe) || cur().is(TokenKind::Kw_bitor)) {
        SourceLocation loc = cur().loc;
        next();
        auto rhs = parseXorExpr();
        auto be = make<BinaryExpr>();
        be->loc = loc;
        be->op  = BinaryOp::Or;
        be->lhs = std::move(lhs);
        be->rhs = std::move(rhs);
        lhs = std::move(be);
    }
    return lhs;
}

ExprPtr Parser::parseXorExpr() {
    auto lhs = parseBitandExpr();
    while (cur().is(TokenKind::Caret) || cur().is(TokenKind::Kw_xor)) {
        SourceLocation loc = cur().loc;
        next();
        auto rhs = parseBitandExpr();
        auto be = make<BinaryExpr>();
        be->loc = loc;
        be->op  = BinaryOp::Xor;
        be->lhs = std::move(lhs);
        be->rhs = std::move(rhs);
        lhs = std::move(be);
    }
    return lhs;
}

ExprPtr Parser::parseBitandExpr() {
    auto lhs = parseEqExpr();
    while (cur().is(TokenKind::Amp) || cur().is(TokenKind::Kw_bitand)) {
        SourceLocation loc = cur().loc;
        next();
        auto rhs = parseEqExpr();
        auto be = make<BinaryExpr>();
        be->loc = loc;
        be->op  = BinaryOp::And;
        be->lhs = std::move(lhs);
        be->rhs = std::move(rhs);
        lhs = std::move(be);
    }
    return lhs;
}

ExprPtr Parser::parseEqExpr() {
    auto lhs = parseRelExpr();
    while (cur().is(TokenKind::EqEq) || cur().is(TokenKind::BangEq)
        || cur().is(TokenKind::Kw_not_eq)) {
        SourceLocation loc = cur().loc;
        BinaryOp op = cur().is(TokenKind::EqEq) ? BinaryOp::Eq : BinaryOp::Ne;
        next();
        auto rhs = parseRelExpr();
        auto be = make<BinaryExpr>();
        be->loc = loc;
        be->op  = op;
        be->lhs = std::move(lhs);
        be->rhs = std::move(rhs);
        lhs = std::move(be);
    }
    return lhs;
}

ExprPtr Parser::parseRelExpr() {
    auto lhs = parseShiftExpr();
    for (;;) {
        BinaryOp op;
        bool matched = true;
        switch (cur().kind) {
        case TokenKind::Lt:   op = BinaryOp::Lt; break;
        case TokenKind::Gt:   op = BinaryOp::Gt; break;
        case TokenKind::LtEq: op = BinaryOp::Le; break;
        case TokenKind::GtEq: op = BinaryOp::Ge; break;
        default: matched = false; break;
        }
        if (!matched) break;
        SourceLocation loc = cur().loc;
        next();
        auto rhs = parseShiftExpr();
        auto be = make<BinaryExpr>();
        be->loc = loc;
        be->op  = op;
        be->lhs = std::move(lhs);
        be->rhs = std::move(rhs);
        lhs = std::move(be);
    }
    return lhs;
}

ExprPtr Parser::parseShiftExpr() {
    auto lhs = parseAddExpr();
    while (cur().is(TokenKind::LShift) || cur().is(TokenKind::RShift)) {
        SourceLocation loc = cur().loc;
        BinaryOp op = cur().is(TokenKind::LShift) ? BinaryOp::Shl : BinaryOp::Shr;
        next();
        auto rhs = parseAddExpr();
        auto be = make<BinaryExpr>();
        be->loc = loc;
        be->op  = op;
        be->lhs = std::move(lhs);
        be->rhs = std::move(rhs);
        lhs = std::move(be);
    }
    return lhs;
}

ExprPtr Parser::parseAddExpr() {
    auto lhs = parseMulExpr();
    while (cur().is(TokenKind::Plus) || cur().is(TokenKind::Minus)) {
        SourceLocation loc = cur().loc;
        BinaryOp op = cur().is(TokenKind::Plus) ? BinaryOp::Add : BinaryOp::Sub;
        next();
        auto rhs = parseMulExpr();
        auto be = make<BinaryExpr>();
        be->loc = loc;
        be->op  = op;
        be->lhs = std::move(lhs);
        be->rhs = std::move(rhs);
        lhs = std::move(be);
    }
    return lhs;
}

ExprPtr Parser::parseMulExpr() {
    auto lhs = parseCastExpr();
    for (;;) {
        BinaryOp op;
        bool matched = true;
        switch (cur().kind) {
        case TokenKind::Star:    op = BinaryOp::Mul; break;
        case TokenKind::Slash:   op = BinaryOp::Div; break;
        case TokenKind::Percent: op = BinaryOp::Mod; break;
        default: matched = false; break;
        }
        if (!matched) break;
        SourceLocation loc = cur().loc;
        next();
        auto rhs = parseCastExpr();
        auto be = make<BinaryExpr>();
        be->loc = loc;
        be->op  = op;
        be->lhs = std::move(lhs);
        be->rhs = std::move(rhs);
        lhs = std::move(be);
    }
    return lhs;
}

ExprPtr Parser::parseCastExpr() {
    // C-style cast: (type) expr
    if (cur().is(TokenKind::LParen)) {
        // Lookahead: is this a cast?
        // We peek if inside are type specifiers
        // Simple heuristic: if the next token is a type keyword or typedef name, it's a cast
        Token inner = pp_.peek(); // token after '('
        bool isCast = false;
        switch (inner.kind) {
        case TokenKind::Kw_char: case TokenKind::Kw_short: case TokenKind::Kw_int:
        case TokenKind::Kw_long: case TokenKind::Kw_float: case TokenKind::Kw_double:
        case TokenKind::Kw_void: case TokenKind::Kw_unsigned: case TokenKind::Kw_signed:
        case TokenKind::Kw__Bool: case TokenKind::Kw_bool:
        case TokenKind::Kw_const: case TokenKind::Kw_volatile:
        case TokenKind::Kw_struct: case TokenKind::Kw_union: case TokenKind::Kw_class:
        case TokenKind::Kw_enum:
            isCast = true;
            break;
        case TokenKind::Identifier:
            isCast = isTypeName(inner);
            break;
        default:
            break;
        }

        if (isCast) {
            SourceLocation loc = cur().loc;
            next(); // consume '('
            DeclSpec cds = parseDeclSpec();
            std::string ignored;
            TypePtr targetType = parseAbstractDeclarator(cds.baseType);
            consume(TokenKind::RParen, "expected ')'");
            auto operand = parseCastExpr();
            auto ce = make<CastExpr>();
            ce->loc        = loc;
            ce->castKind   = CastExpr::CastKind::CStyle;
            ce->targetType = targetType;
            ce->operand    = std::move(operand);
            return ce;
        }
    }
    return parseUnaryExpr();
}

ExprPtr Parser::parseUnaryExpr() {
    Token t = cur();
    SourceLocation loc = t.loc;

    switch (t.kind) {
    case TokenKind::PlusPlus: {
        next();
        auto e = make<UnaryExpr>();
        e->loc = loc;
        e->op  = UnaryOp::PreInc;
        e->operand = parseCastExpr();
        return e;
    }
    case TokenKind::MinusMinus: {
        next();
        auto e = make<UnaryExpr>();
        e->loc = loc;
        e->op  = UnaryOp::PreDec;
        e->operand = parseCastExpr();
        return e;
    }
    case TokenKind::Plus: {
        next();
        auto e = make<UnaryExpr>();
        e->loc = loc;
        e->op  = UnaryOp::Plus;
        e->operand = parseCastExpr();
        return e;
    }
    case TokenKind::Minus: {
        next();
        auto e = make<UnaryExpr>();
        e->loc = loc;
        e->op  = UnaryOp::Neg;
        e->operand = parseCastExpr();
        return e;
    }
    case TokenKind::Tilde:
    case TokenKind::Kw_compl: {
        next();
        auto e = make<UnaryExpr>();
        e->loc = loc;
        e->op  = UnaryOp::Not;
        e->operand = parseCastExpr();
        return e;
    }
    case TokenKind::Bang:
    case TokenKind::Kw_not: {
        next();
        auto e = make<UnaryExpr>();
        e->loc = loc;
        e->op  = UnaryOp::LNot;
        e->operand = parseCastExpr();
        return e;
    }
    case TokenKind::Star: {
        next();
        auto e = make<UnaryExpr>();
        e->loc = loc;
        e->op  = UnaryOp::Deref;
        e->operand = parseCastExpr();
        return e;
    }
    case TokenKind::Amp:
    case TokenKind::Kw_bitand: {
        next();
        auto e = make<UnaryExpr>();
        e->loc = loc;
        e->op  = UnaryOp::AddrOf;
        e->operand = parseCastExpr();
        return e;
    }
    case TokenKind::Kw_sizeof: {
        next();
        auto se = make<SizeofExpr>();
        se->loc = loc;
        if (cur().is(TokenKind::LParen)) {
            // Could be sizeof(type) or sizeof(expr)
            Token inner = pp_.peek();
            bool isType = false;
            switch (inner.kind) {
            case TokenKind::Kw_char: case TokenKind::Kw_short: case TokenKind::Kw_int:
            case TokenKind::Kw_long: case TokenKind::Kw_float: case TokenKind::Kw_double:
            case TokenKind::Kw_void: case TokenKind::Kw_unsigned: case TokenKind::Kw_signed:
            case TokenKind::Kw__Bool: case TokenKind::Kw_bool: case TokenKind::Kw_struct:
            case TokenKind::Kw_union: case TokenKind::Kw_class: case TokenKind::Kw_enum:
            case TokenKind::Kw_const: case TokenKind::Kw_volatile:
                isType = true; break;
            case TokenKind::Identifier:
                isType = isTypeName(inner); break;
            default: break;
            }
            if (isType) {
                next(); // consume '('
                DeclSpec tds = parseDeclSpec();
                std::string ignored;
                se->targetType = parseAbstractDeclarator(tds.baseType);
                se->isType = true;
                consume(TokenKind::RParen, "expected ')'");
            } else {
                se->isType = false;
                se->operand = parseUnaryExpr();
            }
        } else {
            se->isType = false;
            se->operand = parseUnaryExpr();
        }
        se->type = types_.ulonglongTy();
        return se;
    }
    case TokenKind::Kw__Alignof:
    case TokenKind::Kw_alignof: {
        next();
        auto ae = make<AlignofExpr>();
        ae->loc = loc;
        consume(TokenKind::LParen, "expected '('");
        DeclSpec tds = parseDeclSpec();
        std::string ignored;
        ae->targetType = parseAbstractDeclarator(tds.baseType);
        consume(TokenKind::RParen, "expected ')'");
        ae->type = types_.ulonglongTy();
        return ae;
    }
    case TokenKind::Kw_new: {
        next();
        auto ne = make<NewExpr>();
        ne->loc = loc;
        ne->isArray = false;
        // Placement args: new (args) Type
        if (cur().is(TokenKind::LParen)) {
            next();
            if (!cur().is(TokenKind::RParen)) {
                // Could be placement or type — check for type
                Token inner = pp_.peek();
                // If it starts with a type keyword, it's a type in parens (grouped)
                // Otherwise it's placement args
                ne->placement = parseArgList();
            }
            consume(TokenKind::RParen, "expected ')'");
        }
        // Parse type
        DeclSpec nds = parseDeclSpec();
        std::string ignored;
        ne->allocType = parseDeclarator(nds.baseType, ignored);
        // Array new
        if (cur().is(TokenKind::LBracket)) {
            ne->isArray = true;
            next();
            if (!cur().is(TokenKind::RBracket)) {
                ne->initializer = parseExpr();
            }
            consume(TokenKind::RBracket, "expected ']'");
        }
        // Initializer
        if (cur().is(TokenKind::LParen)) {
            next();
            if (!cur().is(TokenKind::RParen)) {
                ne->initializer = parseExpr();
            }
            consume(TokenKind::RParen, "expected ')'");
        } else if (cur().is(TokenKind::LBrace)) {
            ne->initializer = parseInitListExpr();
        }
        if (ne->allocType) {
            ne->type = types_.ptrTo(ne->allocType);
        }
        return ne;
    }
    case TokenKind::Kw_delete: {
        next();
        auto de = make<DeleteExpr>();
        de->loc = loc;
        de->isArray = false;
        if (cur().is(TokenKind::LBracket)) {
            next();
            consume(TokenKind::RBracket, "expected ']'");
            de->isArray = true;
        }
        de->operand = parseCastExpr();
        de->type = types_.voidTy();
        return de;
    }
    case TokenKind::Kw_static_cast:
    case TokenKind::Kw_reinterpret_cast:
    case TokenKind::Kw_const_cast:
    case TokenKind::Kw_dynamic_cast: {
        CastExpr::CastKind ck;
        if (t.is(TokenKind::Kw_static_cast))      ck = CastExpr::CastKind::Static;
        else if (t.is(TokenKind::Kw_reinterpret_cast)) ck = CastExpr::CastKind::Reinterpret;
        else if (t.is(TokenKind::Kw_dynamic_cast))    ck = CastExpr::CastKind::Dynamic;
        else                                           ck = CastExpr::CastKind::Const;
        next();
        // <Type>
        consume(TokenKind::Lt, "expected '<' after cast keyword");
        DeclSpec cds = parseDeclSpec();
        std::string ignored;
        TypePtr targetType = parseAbstractDeclarator(cds.baseType);
        consume(TokenKind::Gt, "expected '>'");
        consume(TokenKind::LParen, "expected '('");
        auto operand = parseExpr();
        consume(TokenKind::RParen, "expected ')'");
        auto ce = make<CastExpr>();
        ce->loc        = loc;
        ce->castKind   = ck;
        ce->targetType = targetType;
        ce->operand    = std::move(operand);
        ce->type       = targetType;
        return ce;
    }
    default:
        return parsePostfixExpr();
    }
}

ExprPtr Parser::parsePostfixExpr() {
    auto e = parsePrimaryExpr();

    for (;;) {
        SourceLocation loc = cur().loc;
        if (cur().is(TokenKind::LBracket)) {
            next();
            auto idx = parseExpr();
            consume(TokenKind::RBracket, "expected ']'");
            auto ie = make<IndexExpr>();
            ie->loc   = loc;
            ie->base  = std::move(e);
            ie->index = std::move(idx);
            e = std::move(ie);
        } else if (cur().is(TokenKind::LParen)) {
            next();
            auto ce = make<CallExpr>();
            ce->loc    = loc;
            ce->callee = std::move(e);
            if (!cur().is(TokenKind::RParen)) {
                ce->args = parseArgList();
            }
            consume(TokenKind::RParen, "expected ')'");
            e = std::move(ce);
        } else if (cur().is(TokenKind::Dot) || cur().is(TokenKind::Arrow)) {
            bool isArrow = cur().is(TokenKind::Arrow);
            next();
            std::string member;
            if (cur().is(TokenKind::Identifier)) {
                member = cur().text;
                next();
            } else if (cur().is(TokenKind::Kw_template)) {
                next();
                member = cur().text;
                next();
            }
            auto me = make<MemberExpr>();
            me->loc     = loc;
            me->base    = std::move(e);
            me->member  = member;
            me->isArrow = isArrow;
            e = std::move(me);
        } else if (cur().is(TokenKind::PlusPlus)) {
            next();
            auto ue = make<UnaryExpr>();
            ue->loc = loc;
            ue->op  = UnaryOp::PostInc;
            ue->operand = std::move(e);
            e = std::move(ue);
        } else if (cur().is(TokenKind::MinusMinus)) {
            next();
            auto ue = make<UnaryExpr>();
            ue->loc = loc;
            ue->op  = UnaryOp::PostDec;
            ue->operand = std::move(e);
            e = std::move(ue);
        } else {
            break;
        }
    }

    return e;
}

ExprPtr Parser::parsePrimaryExpr() {
    Token t = cur();
    SourceLocation loc = t.loc;

    switch (t.kind) {
    case TokenKind::Kw__Generic: {
        next();
        auto ge = make<GenericExpr>();
        ge->loc = loc;
        consume(TokenKind::LParen, "expected '(' after _Generic");
        ge->controlExpr = parseAssignExpr();
        consume(TokenKind::Comma, "expected ',' in _Generic");
        while (true) {
            GenericExpr::Association assoc;
            if (cur().is(TokenKind::Kw_default)) {
                next();
                assoc.type = nullptr;
            } else {
                DeclSpec spec = parseDeclSpec();
                assoc.type = parseAbstractDeclarator(spec.baseType);
            }
            consume(TokenKind::Colon, "expected ':' in _Generic association");
            assoc.expr = parseAssignExpr();
            ge->associations.push_back(std::move(assoc));
            if (match(TokenKind::Comma)) continue;
            break;
        }
        consume(TokenKind::RParen, "expected ')' to close _Generic");
        return ge;
    }
    case TokenKind::IntLit: {
        next();
        auto e = make<IntLitExpr>();
        e->loc      = loc;
        e->value    = t.value.intVal;
        e->isSigned = !t.isUnsigned;
        if (t.isLongLong || t.isLong) {
            e->type = t.isUnsigned ? types_.ulonglongTy() : types_.longlongTy();
        } else if (t.isUnsigned) {
            e->type = types_.uintTy();
        } else {
            e->type = types_.intTy();
        }
        return e;
    }
    case TokenKind::FloatLit: {
        next();
        auto e = make<FloatLitExpr>();
        e->loc   = loc;
        e->value = t.value.floatVal;
        e->type  = types_.doubleTy();
        return e;
    }
    case TokenKind::StringLit:
    case TokenKind::WStringLit: {
        next();
        auto e = make<StringLitExpr>();
        e->loc   = loc;
        e->value = t.text;
        e->wide  = t.is(TokenKind::WStringLit);
        // type = char* or wchar_t*
        e->type = types_.ptrTo(e->wide ? types_.intTy() : types_.charTy());
        return e;
    }
    case TokenKind::CharLit:
    case TokenKind::WCharLit: {
        next();
        auto e = make<CharLitExpr>();
        e->loc   = loc;
        e->value = (u32)t.value.intVal;
        e->wide  = t.is(TokenKind::WCharLit);
        e->type  = e->wide ? types_.intTy() : types_.charTy();
        return e;
    }
    case TokenKind::Kw_true: {
        next();
        auto e = make<BoolLitExpr>();
        e->loc   = loc;
        e->value = true;
        e->type  = types_.boolTy();
        return e;
    }
    case TokenKind::Kw_false: {
        next();
        auto e = make<BoolLitExpr>();
        e->loc   = loc;
        e->value = false;
        e->type  = types_.boolTy();
        return e;
    }
    case TokenKind::Kw_nullptr: {
        next();
        auto e = make<NullptrLitExpr>();
        e->loc  = loc;
        e->type = types_.nullptrTTy();
        return e;
    }
    case TokenKind::Kw_this: {
        next();
        auto e = make<IdentExpr>();
        e->loc  = loc;
        e->name = "this";
        return e;
    }
    case TokenKind::LParen: {
        next();
        auto e = parseExpr();
        consume(TokenKind::RParen, "expected ')'");
        return e;
    }
    case TokenKind::LBrace: {
        return parseInitListExpr();
    }
    case TokenKind::Identifier: {
        std::string name = t.text;
        next();
        // Handle :: scope resolution
        while (cur().is(TokenKind::ColonColon)) {
            next();
            if (cur().is(TokenKind::Identifier)) {
                name = cur().text;
                next();
            } else if (cur().is(TokenKind::Tilde)) {
                // Destructor: ClassName::~ClassName
                next();
                if (cur().is(TokenKind::Identifier)) {
                    name = "~" + cur().text;
                    next();
                }
            } else if (cur().is(TokenKind::Kw_operator)) {
                name = "operator";
                next();
                // Skip operator token
                next();
            }
        }
        // Handle template argument list
        if (cur().is(TokenKind::Lt)) {
            Token saved = cur();
            // Only consume as template args if followed by types or >>
            // Simple approach: consume if we see type or identifier after <
            Token inner = pp_.peek();
            bool likelyTemplate = false;
            switch (inner.kind) {
            case TokenKind::Kw_int: case TokenKind::Kw_char: case TokenKind::Kw_float:
            case TokenKind::Kw_double: case TokenKind::Kw_bool: case TokenKind::Kw_void:
            case TokenKind::Kw_unsigned: case TokenKind::Kw_signed: case TokenKind::Kw_long:
            case TokenKind::Kw_const: case TokenKind::Kw_struct: case TokenKind::Kw_class:
            case TokenKind::Identifier:
                likelyTemplate = true;
                break;
            default:
                break;
            }
            if (likelyTemplate) {
                next(); // consume '<'
                int depth = 1;
                while (!atEnd() && depth > 0) {
                    if (cur().is(TokenKind::Lt))         { ++depth; next(); }
                    else if (cur().is(TokenKind::Gt))    { --depth; next(); }
                    else if (cur().is(TokenKind::RShift)){ depth -= 2; if (depth < 0) depth = 0; next(); }
                    else next();
                }
            }
        }
        auto e = make<IdentExpr>();
        e->loc  = loc;
        e->name = name;
        return e;
    }
    case TokenKind::ColonColon: {
        // Global scope: ::name
        next();
        std::string name;
        if (cur().is(TokenKind::Identifier)) {
            name = cur().text;
            next();
        }
        auto e = make<IdentExpr>();
        e->loc  = loc;
        e->name = name;
        return e;
    }
    default:
        diag_.error(loc, "unexpected token in expression");
        next(); // skip to avoid infinite loop
        auto e = make<IntLitExpr>();
        e->loc      = loc;
        e->value    = 0;
        e->isSigned = true;
        e->type     = types_.intTy();
        return e;
    }
}

ExprPtr Parser::parseInitListExpr() {
    SourceLocation loc = cur().loc;
    consume(TokenKind::LBrace, "expected '{'");
    auto ile = make<InitListExpr>();
    ile->loc = loc;
    while (!atEnd() && !cur().is(TokenKind::RBrace)) {
        if (cur().is(TokenKind::Dot)) {
            // Designated initializer: .field = value
            next();
            next(); // skip field name
            consume(TokenKind::Eq, "expected '='");
        } else if (cur().is(TokenKind::LBracket)) {
            // Array designated initializer: [0] = value
            next();
            parseAssignExpr(); // index
            consume(TokenKind::RBracket, "expected ']'");
            consume(TokenKind::Eq, "expected '='");
        }
        if (cur().is(TokenKind::LBrace)) {
            ile->inits.push_back(parseInitListExpr());
        } else {
            ile->inits.push_back(parseAssignExpr());
        }
        if (!match(TokenKind::Comma)) break;
        // Allow trailing comma
    }
    consume(TokenKind::RBrace, "expected '}'");
    return ile;
}

std::vector<ExprPtr> Parser::parseArgList() {
    std::vector<ExprPtr> args;
    while (!atEnd() && !cur().is(TokenKind::RParen)) {
        if (!args.empty()) {
            if (!match(TokenKind::Comma)) break;
        }
        if (cur().is(TokenKind::LBrace)) {
            args.push_back(parseInitListExpr());
        } else {
            args.push_back(parseAssignExpr());
        }
    }
    return args;
}

} // namespace qc

