#pragma once
#include "common.h"

namespace qc {

enum class TokenKind : u16 {
    // Literals
    IntLit, FloatLit, StringLit, CharLit, WStringLit, WCharLit,

    // Identifiers
    Identifier,

    // Keywords — C
    Kw_auto, Kw_break, Kw_case, Kw_char, Kw_const, Kw_continue,
    Kw_default, Kw_do, Kw_double, Kw_else, Kw_enum, Kw_extern,
    Kw_float, Kw_for, Kw_goto, Kw_if, Kw_inline, Kw_int, Kw_long,
    Kw_register, Kw_restrict, Kw_return, Kw_short, Kw_signed,
    Kw_sizeof, Kw_static, Kw_struct, Kw_switch, Kw_typedef,
    Kw_union, Kw_unsigned, Kw_void, Kw_volatile, Kw_while,
    Kw__Bool, Kw__Complex, Kw__Alignas, Kw__Alignof, Kw__Atomic,
    Kw__Static_assert, Kw__Noreturn, Kw__Generic,

    // Keywords — C++
    Kw_alignas, Kw_alignof, Kw_and, Kw_and_eq, Kw_asm,
    Kw_bitand, Kw_bitor, Kw_bool, Kw_catch, Kw_class,
    Kw_compl, Kw_concept, Kw_constexpr, Kw_consteval, Kw_constinit,
    Kw_co_await, Kw_co_return, Kw_co_yield,
    Kw_decltype, Kw_delete, Kw_dynamic_cast, Kw_explicit,
    Kw_export, Kw_false, Kw_final, Kw_friend,
    Kw_mutable, Kw_namespace, Kw_new, Kw_noexcept, Kw_not,
    Kw_not_eq, Kw_nullptr, Kw_operator, Kw_or, Kw_or_eq,
    Kw_override, Kw_private, Kw_protected, Kw_public,
    Kw_const_cast, Kw_reinterpret_cast, Kw_requires, Kw_static_assert,
    Kw_static_cast, Kw_template, Kw_this, Kw_throw,
    Kw_true, Kw_try, Kw_typename, Kw_using, Kw_virtual,
    Kw_xor, Kw_xor_eq,

    // Punctuation
    LParen, RParen, LBrace, RBrace, LBracket, RBracket,
    Semicolon, Colon, ColonColon, Comma, Dot, Arrow,
    DotStar, ArrowStar, Ellipsis, Question,

    // Operators
    Plus, Minus, Star, Slash, Percent,
    Amp, Pipe, Caret, Tilde, Bang,
    PlusPlus, MinusMinus,
    LShift, RShift,
    Eq, PlusEq, MinusEq, StarEq, SlashEq, PercentEq,
    AmpEq, PipeEq, CaretEq, LShiftEq, RShiftEq,
    EqEq, BangEq, Lt, Gt, LtEq, GtEq, Spaceship,
    AmpAmp, PipePipe, At,

    // Preprocessor (after preprocessing these become others)
    Hash, HashHash,

    // Special
    Eof, Error,
};

struct Token {
    TokenKind      kind;
    SourceLocation loc;
    std::string    text;   // raw text (identifiers, literals)
    union {
        u64    intVal;
        double floatVal;
    } value = {};
    bool isUnsigned = false;
    bool isLong     = false;
    bool isLongLong = false;

    bool is(TokenKind k) const { return kind == k; }
    bool isNot(TokenKind k) const { return kind != k; }
    bool isKeyword() const {
        return kind >= TokenKind::Kw_auto && kind <= TokenKind::Kw_xor_eq;
    }
    bool isLiteral() const {
        return kind >= TokenKind::IntLit && kind <= TokenKind::WCharLit;
    }

    std::string_view spelling() const;
};

std::string_view tokenKindName(TokenKind k);

} // namespace qc
