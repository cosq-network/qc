#pragma once
#include "common.h"
#include <string>
#include <vector>

namespace qc {

class SourceFile {
public:
    explicit SourceFile(std::string path);

    bool        load();
    bool        loadFromString(std::string content, std::string name = "<input>");

    std::string_view path()    const { return path_; }
    std::string_view content() const { return content_; }
    const char*      data()    const { return content_.data(); }
    size_t           size()    const { return content_.size(); }

    SourceLocation locationOf(u32 offset) const;

    const std::vector<u32>& lineOffsets() const { return lineOffsets_; }

private:
    void buildLineTable();

    std::string          path_;
    std::string          content_;
    std::vector<u32>     lineOffsets_; // offset of start of each line
};

} // namespace qc
