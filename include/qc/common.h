#pragma once
#include <cstdint>
#include <cstddef>
#include <string>
#include <string_view>
#include <vector>
#include <memory>
#include <variant>
#include <optional>
#include <functional>
#include <cassert>
#include <unordered_map>
#include <map>

namespace qc {

using u8  = uint8_t;
using u16 = uint16_t;
using u32 = uint32_t;
using u64 = uint64_t;
using i8  = int8_t;
using i16 = int16_t;
using i32 = int32_t;
using i64 = int64_t;

template<typename T>
using Ptr = std::unique_ptr<T>;

template<typename T>
using Ref = std::shared_ptr<T>;

template<typename T, typename... Args>
Ptr<T> make(Args&&... args) {
    return std::make_unique<T>(std::forward<Args>(args)...);
}

struct SourceLocation {
    u32 line   = 0;
    u32 column = 0;
    u32 offset = 0;
    const char* file = nullptr;
};

struct Diagnostic {
    enum class Level { Note, Warning, Error, Fatal };
    Level       level;
    SourceLocation loc;
    std::string message;
};

class DiagEngine {
public:
    void note   (SourceLocation loc, std::string msg) { emit(Diagnostic::Level::Note,    loc, std::move(msg)); }
    void warning(SourceLocation loc, std::string msg) { emit(Diagnostic::Level::Warning, loc, std::move(msg)); }
    void error  (SourceLocation loc, std::string msg) { emit(Diagnostic::Level::Error,   loc, std::move(msg)); hasError_ = true; }
    void fatal  (SourceLocation loc, std::string msg) { emit(Diagnostic::Level::Fatal,   loc, std::move(msg)); hasError_ = true; }

    bool hasError() const { return hasError_; }
    const std::vector<Diagnostic>& diags() const { return diags_; }

private:
    void emit(Diagnostic::Level level, SourceLocation loc, std::string msg);
    std::vector<Diagnostic> diags_;
    bool hasError_ = false;
};

enum class TargetArch { X64, ARM64 };
enum class TargetFormat { ELF, PE };
enum class TargetOS { Linux, Windows, MacOS };

struct TargetInfo {
    TargetArch   arch;
    TargetFormat format;
    TargetOS     os;
    bool         isLittleEndian = true;
    u32          pointerSize    = 8;
    u32          pointerAlign   = 8;
};

TargetInfo parseTarget(std::string_view arch, std::string_view format);

} // namespace qc
