#pragma once
#include "common.h"
#include <vector>
#include <string>

namespace qc {

struct DWARFFile {
    std::string name;
    u32         dirIndex;
};

struct DWARFLineEntry {
    u64            address;
    u32            line;
    u32            column;
    u32            fileIndex;
    bool           isStmt;
    bool           basicBlock;
    bool           endSequence;
};

class DWARFLineProgram {
public:
    DWARFLineProgram();
    
    void addDirectory(std::string dir);
    u32  addFile(std::string name, u32 dirIndex);
    void addEntry(const DWARFLineEntry& entry);
    
    std::vector<u8> emit();

private:
    std::vector<std::string> directories_;
    std::vector<DWARFFile>   files_;
    std::vector<DWARFLineEntry> entries_;
};

class DWARFDebugInfo {
public:
    DWARFDebugInfo();
    void addCU(const std::string& name, const std::string& producer, u32 lineOff);
    std::vector<u8> emit();
private:
    struct CU {
        std::string name;
        std::string producer;
        u32         lineOff;
    };
    std::vector<CU> cus_;
};

class DWARFAbbrev {
public:
    DWARFAbbrev();
    std::vector<u8> emit();
};

} // namespace qc
