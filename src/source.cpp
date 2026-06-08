// source.cpp — SourceFile implementation for the qc compiler
#include "qc/source.h"

#include <fstream>
#include <algorithm>
#include <stdexcept>

namespace qc {

// ---------------------------------------------------------------------------
// Constructor
// ---------------------------------------------------------------------------

SourceFile::SourceFile(std::string path)
    : path_(std::move(path))
{}

// ---------------------------------------------------------------------------
// load() — read the file at path_ into content_
// ---------------------------------------------------------------------------

bool SourceFile::load() {
    std::ifstream ifs(path_, std::ios::binary | std::ios::ate);
    if (!ifs.is_open())
        return false;

    const auto size = static_cast<std::streamsize>(ifs.tellg());
    ifs.seekg(0, std::ios::beg);

    content_.resize(static_cast<size_t>(size));
    if (size > 0 && !ifs.read(content_.data(), size))
        return false;

    buildLineTable();
    return true;
}

// ---------------------------------------------------------------------------
// loadFromString() — use a caller-supplied string as the source text
// ---------------------------------------------------------------------------

bool SourceFile::loadFromString(std::string content, std::string name) {
    path_    = std::move(name);
    content_ = std::move(content);
    buildLineTable();
    return true;
}

// ---------------------------------------------------------------------------
// buildLineTable() — populate lineOffsets_
//
// lineOffsets_[i] is the byte offset of the first character on line i+1.
// Line 1 always starts at offset 0.
// ---------------------------------------------------------------------------

void SourceFile::buildLineTable() {
    lineOffsets_.clear();
    lineOffsets_.push_back(0u); // line 1 starts at offset 0

    const char* p   = content_.data();
    const char* end = p + content_.size();

    for (; p < end; ++p) {
        if (*p == '\n') {
            // The next character (if any) begins a new line.
            u32 nextOffset = static_cast<u32>((p + 1) - content_.data());
            lineOffsets_.push_back(nextOffset);
        }
    }
}

// ---------------------------------------------------------------------------
// locationOf() — binary-search lineOffsets_ to convert a byte offset to a
//                SourceLocation with 1-based line and column numbers.
// ---------------------------------------------------------------------------

SourceLocation SourceFile::locationOf(u32 offset) const {
    SourceLocation loc;
    loc.offset = offset;
    loc.file   = path_.data();

    if (lineOffsets_.empty()) {
        loc.line   = 1;
        loc.column = offset + 1;
        return loc;
    }

    // Find the last entry in lineOffsets_ that is <= offset.
    // upper_bound gives us the first entry > offset; step one back.
    auto it = std::upper_bound(lineOffsets_.begin(), lineOffsets_.end(), offset);
    if (it != lineOffsets_.begin())
        --it;

    const auto lineIndex = static_cast<u32>(it - lineOffsets_.begin());
    loc.line   = lineIndex + 1;                    // 1-based
    loc.column = (offset - *it) + 1;               // 1-based
    return loc;
}

} // namespace qc
