// common.cpp — DiagEngine, parseTarget, Token::spelling, tokenKindName
#include "qc/common.h"
#include "qc/token.h"

#include <cstdio>
#include <cstring>
#include <stdexcept>

namespace qc {

// ===========================================================================
// DiagEngine::emit
// ===========================================================================

void DiagEngine::emit(Diagnostic::Level level, SourceLocation loc, std::string msg) {
    // Store the diagnostic
    diags_.push_back(Diagnostic{level, loc, msg});

    // Print to stderr in the canonical format:
    //   file:line:col: level: message
    const char* levelStr = "";
    switch (level) {
    case Diagnostic::Level::Note:    levelStr = "note";    break;
    case Diagnostic::Level::Warning: levelStr = "warning"; break;
    case Diagnostic::Level::Error:   levelStr = "error";   break;
    case Diagnostic::Level::Fatal:   levelStr = "fatal";   break;
    }

    const char* file = (loc.file && loc.file[0] != '\0') ? loc.file : "<unknown>";

    if (loc.line > 0) {
        std::fprintf(stderr, "%s:%u:%u: %s: %s\n",
                     file, loc.line, loc.column, levelStr, msg.c_str());
    } else {
        // No location information — emit without position
        std::fprintf(stderr, "%s: %s: %s\n", file, levelStr, msg.c_str());
    }
}

// ===========================================================================
// parseTarget
// ===========================================================================

TargetInfo parseTarget(std::string_view arch, std::string_view format) {
    TargetInfo info;

    // Architecture
    if (arch == "x64" || arch == "x86_64") {
        info.arch          = TargetArch::X64;
        info.pointerSize   = 8;
        info.pointerAlign  = 8;
        info.isLittleEndian = true;
    } else if (arch == "arm64" || arch == "aarch64") {
        info.arch          = TargetArch::ARM64;
        info.pointerSize   = 8;
        info.pointerAlign  = 8;
        info.isLittleEndian = true;
    } else {
        // Default to x64 for unknown architectures
        info.arch          = TargetArch::X64;
        info.pointerSize   = 8;
        info.pointerAlign  = 8;
        info.isLittleEndian = true;
    }

    // Object file format / OS
    if (format == "elf") {
        info.format = TargetFormat::ELF;
        info.os     = TargetOS::Linux;
    } else if (format == "pe" || format == "coff") {
        info.format = TargetFormat::PE;
        info.os     = TargetOS::Windows;
    } else if (format == "macho" || format == "mach-o") {
        // Treat Mach-O targets as ELF internally for now (no PE writer)
        info.format = TargetFormat::ELF;
        info.os     = TargetOS::MacOS;
    } else {
        // Default to ELF/Linux for unknown formats
        info.format = TargetFormat::ELF;
        info.os     = TargetOS::Linux;
    }

    return info;
}

// ===========================================================================
// tokenKindName — returns a short human-readable name for a TokenKind
// ===========================================================================

std::string_view tokenKindName(TokenKind k) {
    switch (k) {
    // Literals
    case TokenKind::IntLit:      return "integer-literal";
    case TokenKind::FloatLit:    return "float-literal";
    case TokenKind::StringLit:   return "string-literal";
    case TokenKind::CharLit:     return "char-literal";
    case TokenKind::WStringLit:  return "wide-string-literal";
    case TokenKind::WCharLit:    return "wide-char-literal";

    // Identifiers
    case TokenKind::Identifier:  return "identifier";

    // C keywords
    case TokenKind::Kw_auto:            return "auto";
    case TokenKind::Kw_break:           return "break";
    case TokenKind::Kw_case:            return "case";
    case TokenKind::Kw_char:            return "char";
    case TokenKind::Kw_const:           return "const";
    case TokenKind::Kw_continue:        return "continue";
    case TokenKind::Kw_default:         return "default";
    case TokenKind::Kw_do:              return "do";
    case TokenKind::Kw_double:          return "double";
    case TokenKind::Kw_else:            return "else";
    case TokenKind::Kw_enum:            return "enum";
    case TokenKind::Kw_extern:          return "extern";
    case TokenKind::Kw_float:           return "float";
    case TokenKind::Kw_for:             return "for";
    case TokenKind::Kw_goto:            return "goto";
    case TokenKind::Kw_if:              return "if";
    case TokenKind::Kw_inline:          return "inline";
    case TokenKind::Kw_int:             return "int";
    case TokenKind::Kw_long:            return "long";
    case TokenKind::Kw_register:        return "register";
    case TokenKind::Kw_restrict:        return "restrict";
    case TokenKind::Kw_return:          return "return";
    case TokenKind::Kw_short:           return "short";
    case TokenKind::Kw_signed:          return "signed";
    case TokenKind::Kw_sizeof:          return "sizeof";
    case TokenKind::Kw_static:          return "static";
    case TokenKind::Kw_struct:          return "struct";
    case TokenKind::Kw_switch:          return "switch";
    case TokenKind::Kw_typedef:         return "typedef";
    case TokenKind::Kw_union:           return "union";
    case TokenKind::Kw_unsigned:        return "unsigned";
    case TokenKind::Kw_void:            return "void";
    case TokenKind::Kw_volatile:        return "volatile";
    case TokenKind::Kw_while:           return "while";
    case TokenKind::Kw__Bool:           return "_Bool";
    case TokenKind::Kw__Complex:        return "_Complex";
    case TokenKind::Kw__Alignas:        return "_Alignas";
    case TokenKind::Kw__Alignof:        return "_Alignof";
    case TokenKind::Kw__Atomic:         return "_Atomic";
    case TokenKind::Kw__Static_assert:  return "_Static_assert";
    case TokenKind::Kw__Noreturn:       return "_Noreturn";
    case TokenKind::Kw__Generic:        return "_Generic";

    // C++ keywords
    case TokenKind::Kw_alignas:         return "alignas";
    case TokenKind::Kw_alignof:         return "alignof";
    case TokenKind::Kw_and:             return "and";
    case TokenKind::Kw_and_eq:          return "and_eq";
    case TokenKind::Kw_asm:             return "asm";
    case TokenKind::Kw_bitand:          return "bitand";
    case TokenKind::Kw_bitor:           return "bitor";
    case TokenKind::Kw_bool:            return "bool";
    case TokenKind::Kw_catch:           return "catch";
    case TokenKind::Kw_class:           return "class";
    case TokenKind::Kw_compl:           return "compl";
    case TokenKind::Kw_concept:         return "concept";
    case TokenKind::Kw_constexpr:       return "constexpr";
    case TokenKind::Kw_consteval:       return "consteval";
    case TokenKind::Kw_constinit:       return "constinit";
    case TokenKind::Kw_co_await:        return "co_await";
    case TokenKind::Kw_co_return:       return "co_return";
    case TokenKind::Kw_co_yield:        return "co_yield";
    case TokenKind::Kw_decltype:        return "decltype";
    case TokenKind::Kw_delete:          return "delete";
    case TokenKind::Kw_dynamic_cast:    return "dynamic_cast";
    case TokenKind::Kw_explicit:        return "explicit";
    case TokenKind::Kw_export:          return "export";
    case TokenKind::Kw_false:           return "false";
    case TokenKind::Kw_final:           return "final";
    case TokenKind::Kw_friend:          return "friend";
    case TokenKind::Kw_mutable:         return "mutable";
    case TokenKind::Kw_namespace:       return "namespace";
    case TokenKind::Kw_new:             return "new";
    case TokenKind::Kw_noexcept:        return "noexcept";
    case TokenKind::Kw_not:             return "not";
    case TokenKind::Kw_not_eq:          return "not_eq";
    case TokenKind::Kw_nullptr:         return "nullptr";
    case TokenKind::Kw_operator:        return "operator";
    case TokenKind::Kw_or:              return "or";
    case TokenKind::Kw_or_eq:           return "or_eq";
    case TokenKind::Kw_override:        return "override";
    case TokenKind::Kw_private:         return "private";
    case TokenKind::Kw_protected:       return "protected";
    case TokenKind::Kw_public:          return "public";
    case TokenKind::Kw_reinterpret_cast:return "reinterpret_cast";
    case TokenKind::Kw_requires:        return "requires";
    case TokenKind::Kw_static_assert:   return "static_assert";
    case TokenKind::Kw_static_cast:     return "static_cast";
    case TokenKind::Kw_template:        return "template";
    case TokenKind::Kw_this:            return "this";
    case TokenKind::Kw_throw:           return "throw";
    case TokenKind::Kw_true:            return "true";
    case TokenKind::Kw_try:             return "try";
    case TokenKind::Kw_typename:        return "typename";
    case TokenKind::Kw_using:           return "using";
    case TokenKind::Kw_virtual:         return "virtual";
    case TokenKind::Kw_xor:             return "xor";
    case TokenKind::Kw_xor_eq:          return "xor_eq";

    // Punctuation
    case TokenKind::LParen:     return "(";
    case TokenKind::RParen:     return ")";
    case TokenKind::LBrace:     return "{";
    case TokenKind::RBrace:     return "}";
    case TokenKind::LBracket:   return "[";
    case TokenKind::RBracket:   return "]";
    case TokenKind::Semicolon:  return ";";
    case TokenKind::Colon:      return ":";
    case TokenKind::ColonColon: return "::";
    case TokenKind::Comma:      return ",";
    case TokenKind::Dot:        return ".";
    case TokenKind::Arrow:      return "->";
    case TokenKind::DotStar:    return ".*";
    case TokenKind::ArrowStar:  return "->*";
    case TokenKind::Ellipsis:   return "...";
    case TokenKind::Question:   return "?";

    // Operators
    case TokenKind::Plus:       return "+";
    case TokenKind::Minus:      return "-";
    case TokenKind::Star:       return "*";
    case TokenKind::Slash:      return "/";
    case TokenKind::Percent:    return "%";
    case TokenKind::Amp:        return "&";
    case TokenKind::Pipe:       return "|";
    case TokenKind::Caret:      return "^";
    case TokenKind::Tilde:      return "~";
    case TokenKind::Bang:       return "!";
    case TokenKind::PlusPlus:   return "++";
    case TokenKind::MinusMinus: return "--";
    case TokenKind::LShift:     return "<<";
    case TokenKind::RShift:     return ">>";
    case TokenKind::Eq:         return "=";
    case TokenKind::PlusEq:     return "+=";
    case TokenKind::MinusEq:    return "-=";
    case TokenKind::StarEq:     return "*=";
    case TokenKind::SlashEq:    return "/=";
    case TokenKind::PercentEq:  return "%=";
    case TokenKind::AmpEq:      return "&=";
    case TokenKind::PipeEq:     return "|=";
    case TokenKind::CaretEq:    return "^=";
    case TokenKind::LShiftEq:   return "<<=";
    case TokenKind::RShiftEq:   return ">>=";
    case TokenKind::EqEq:       return "==";
    case TokenKind::BangEq:     return "!=";
    case TokenKind::Lt:         return "<";
    case TokenKind::Gt:         return ">";
    case TokenKind::LtEq:       return "<=";
    case TokenKind::GtEq:       return ">=";
    case TokenKind::Spaceship:  return "<=>";
    case TokenKind::AmpAmp:     return "&&";
    case TokenKind::PipePipe:   return "||";
    case TokenKind::At:         return "@";

    // Preprocessor
    case TokenKind::Hash:       return "#";
    case TokenKind::HashHash:   return "##";

    // Special
    case TokenKind::Eof:        return "<eof>";
    case TokenKind::Error:      return "<error>";
    }
    return "<unknown>";
}

// ===========================================================================
// Token::spelling
// ===========================================================================

std::string_view Token::spelling() const {
    // For tokens with meaningful raw text (identifiers, literals) return it.
    if (!text.empty()) {
        return std::string_view(text);
    }
    // For punctuation / operators / keywords fall back to the kind name.
    return tokenKindName(kind);
}

// ===========================================================================
// LEB128 encoding
// ===========================================================================

void encodeULEB128(std::vector<u8>& buf, u64 value) {
    do {
        u8 byte = value & 0x7F;
        value >>= 7;
        if (value != 0) byte |= 0x80;
        buf.push_back(byte);
    } while (value != 0);
}

void encodeSLEB128(std::vector<u8>& buf, i64 value) {
    bool more = true;
    do {
        u8 byte = value & 0x7F;
        value >>= 7;
        if ((value == 0 && (byte & 0x40) == 0) || (value == -1 && (byte & 0x40) != 0)) {
            more = false;
        } else {
            byte |= 0x80;
        }
        buf.push_back(byte);
    } while (more);
}

} // namespace qc
