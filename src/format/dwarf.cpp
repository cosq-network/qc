#include "qc/dwarf.h"
#include <cstring>
#include <algorithm>

namespace qc {

// DWARF Line Program Standard Opcodes
enum {
    DW_LNS_copy = 1,
    DW_LNS_advance_pc = 2,
    DW_LNS_advance_line = 3,
    DW_LNS_set_file = 4,
    DW_LNS_set_column = 5,
    DW_LNS_negate_stmt = 6,
    DW_LNS_set_basic_block = 7,
    DW_LNS_const_add_pc = 8,
    DW_LNS_fixed_advance_pc = 9,
    DW_LNS_set_prologue_end = 10,
    DW_LNS_set_epilogue_begin = 11,
    DW_LNS_set_isa = 12,
};

// DWARF Line Program Extended Opcodes
enum {
    DW_LNE_end_sequence = 1,
    DW_LNE_set_address = 2,
    DW_LNE_define_file = 3,
    DW_LNE_set_discriminator = 4,
};

DWARFLineProgram::DWARFLineProgram() {
    // DWARF directories/files are 1-indexed (0 is reserved for current dir/file in DWARF 4, 
    // but the header expects a list starting with index 1).
}

void DWARFLineProgram::addDirectory(std::string dir) {
    directories_.push_back(std::move(dir));
}

u32 DWARFLineProgram::addFile(std::string name, u32 dirIndex) {
    files_.push_back({std::move(name), dirIndex});
    return (u32)files_.size(); // 1-indexed
}

void DWARFLineProgram::addEntry(const DWARFLineEntry& entry) {
    entries_.push_back(entry);
}

static void writeU16(std::vector<u8>& buf, u16 val) {
    buf.push_back(val & 0xFF);
    buf.push_back((val >> 8) & 0xFF);
}

static void writeU32(std::vector<u8>& buf, u32 val) {
    buf.push_back(val & 0xFF);
    buf.push_back((val >> 8) & 0xFF);
    buf.push_back((val >> 16) & 0xFF);
    buf.push_back((val >> 24) & 0xFF);
}

static void writeU64(std::vector<u8>& buf, u64 val) {
    for (int i = 0; i < 8; ++i) {
        buf.push_back((val >> (i * 8)) & 0xFF);
    }
}

std::vector<u8> DWARFLineProgram::emit() {
    std::vector<u8> program;
    
    // Line program state machine defaults
    u64 address = 0;
    u32 file = 1;
    u32 line = 1;
    u32 column = 0;
    bool isStmt = true;

    const i8 lineBase = -5;
    const u8 lineRange = 14;
    const u8 opcodeBase = 13;

    for (const auto& entry : entries_) {
        // 1. File change
        if (entry.fileIndex != file) {
            file = entry.fileIndex;
            program.push_back(DW_LNS_set_file);
            encodeULEB128(program, file);
        }
        
        // 2. Column change
        if (entry.column != column) {
            column = entry.column;
            program.push_back(DW_LNS_set_column);
            encodeULEB128(program, column);
        }
        
        // 3. Stmt change
        if (entry.isStmt != isStmt) {
            isStmt = entry.isStmt;
            program.push_back(DW_LNS_negate_stmt);
        }

        // 4. Basic block
        if (entry.basicBlock) {
            program.push_back(DW_LNS_set_basic_block);
        }

        // 5. Address / Line increment
        if (entry.endSequence) {
            // Extended opcode: end sequence
            u64 addrDiff = entry.address - address;
            if (addrDiff > 0) {
                program.push_back(DW_LNS_advance_pc);
                encodeULEB128(program, addrDiff);
            }
            program.push_back(0); // extended opcode prefix
            encodeULEB128(program, 1); // length
            program.push_back(DW_LNE_end_sequence);
            
            // Reset state machine for next sequence if any
            address = 0;
            file = 1;
            line = 1;
            column = 0;
            isStmt = true;
            continue;
        }

        if (address == 0) {
            // First entry in sequence, set absolute address
            program.push_back(0); // extended
            encodeULEB128(program, 9); // length (1 for opcode + 8 for addr)
            program.push_back(DW_LNE_set_address);
            writeU64(program, entry.address);
            address = entry.address;
        }

        i64 lineDiff = (i64)entry.line - (i64)line;
        u64 addrDiff = entry.address - address;

        // Try special opcode
        bool usedSpecial = false;
        if (lineDiff >= lineBase && lineDiff < (lineBase + (i64)lineRange)) {
            u8 specialOp = (u8)((lineDiff - lineBase) + (lineRange * addrDiff) + opcodeBase);
            if (specialOp >= opcodeBase && specialOp <= 255) {
                program.push_back(specialOp);
                usedSpecial = true;
            }
        }

        if (!usedSpecial) {
            if (addrDiff > 0) {
                program.push_back(DW_LNS_advance_pc);
                encodeULEB128(program, addrDiff);
            }
            if (lineDiff != 0) {
                program.push_back(DW_LNS_advance_line);
                encodeSLEB128(program, lineDiff);
            }
            program.push_back(DW_LNS_copy);
        }

        address = entry.address;
        line = entry.line;
    }

    // Header
    std::vector<u8> header;
    // unit_length (u32) - will fill at end
    writeU32(header, 0); 
    writeU16(header, 4); // version
    // header_length (u32) - will fill at end
    writeU32(header, 0);
    header.push_back(1); // min_inst_len
    header.push_back(1); // max_ops_per_inst
    header.push_back(1); // default_is_stmt
    header.push_back((u8)lineBase);
    header.push_back(lineRange);
    header.push_back(opcodeBase);
    // standard_opcode_lengths
    header.push_back(0); // DW_LNS_copy
    header.push_back(1); // DW_LNS_advance_pc
    header.push_back(1); // DW_LNS_advance_line
    header.push_back(1); // DW_LNS_set_file
    header.push_back(1); // DW_LNS_set_column
    header.push_back(0); // DW_LNS_negate_stmt
    header.push_back(0); // DW_LNS_set_basic_block
    header.push_back(0); // DW_LNS_const_add_pc
    header.push_back(1); // DW_LNS_fixed_advance_pc
    header.push_back(0); // DW_LNS_set_prologue_end
    header.push_back(0); // DW_LNS_set_epilogue_begin
    header.push_back(1); // DW_LNS_set_isa

    // include_directories
    for (const auto& d : directories_) {
        header.insert(header.end(), d.begin(), d.end());
        header.push_back(0);
    }
    header.push_back(0); // end of directories

    // file_names
    for (const auto& f : files_) {
        header.insert(header.end(), f.name.begin(), f.name.end());
        header.push_back(0);
        encodeULEB128(header, f.dirIndex);
        encodeULEB128(header, 0); // mod time
        encodeULEB128(header, 0); // length
    }
    header.push_back(0); // end of files

    // Patch header_length
    u32 headerLen = (u32)(header.size() - 10);
    std::memcpy(&header[6], &headerLen, 4);

    // Patch unit_length
    u32 totalLen = (u32)(header.size() + program.size() - 4);
    std::memcpy(&header[0], &totalLen, 4);

    // Combine
    header.insert(header.end(), program.begin(), program.end());
    return header;
}

DWARFDebugInfo::DWARFDebugInfo() {}

void DWARFDebugInfo::addCU(const std::string& name, const std::string& producer, u32 lineOff) {
    cus_.push_back({name, producer, lineOff});
}

std::vector<u8> DWARFDebugInfo::emit() {
    std::vector<u8> buf;
    for (const auto& cu : cus_) {
        u32 start = (u32)buf.size();
        writeU32(buf, 0); // unit_length
        writeU16(buf, 4); // version
        writeU32(buf, 0); // debug_abbrev_offset (always 0 for now)
        buf.push_back(8); // address_size

        encodeULEB128(buf, 1); // abbreviation code 1 (DW_TAG_compile_unit)
        buf.insert(buf.end(), cu.producer.begin(), cu.producer.end());
        buf.push_back(0);
        writeU16(buf, 0x0c); // DW_LANG_C99
        buf.insert(buf.end(), cu.name.begin(), cu.name.end());
        buf.push_back(0);
        writeU32(buf, cu.lineOff);

        // Patch unit_length
        u32 unitLen = (u32)(buf.size() - start - 4);
        std::memcpy(&buf[start], &unitLen, 4);
    }
    return buf;
}

DWARFAbbrev::DWARFAbbrev() {}

std::vector<u8> DWARFAbbrev::emit() {
    std::vector<u8> buf;
    // Abbreviation 1: DW_TAG_compile_unit
    encodeULEB128(buf, 1);
    encodeULEB128(buf, 0x11); // DW_TAG_compile_unit
    buf.push_back(0); // DW_CHILDREN_no

    // Attributes
    encodeULEB128(buf, 0x25); // DW_AT_producer
    encodeULEB128(buf, 0x08); // DW_FORM_string

    encodeULEB128(buf, 0x13); // DW_AT_language
    encodeULEB128(buf, 0x0b); // DW_FORM_data2

    encodeULEB128(buf, 0x03); // DW_AT_name
    encodeULEB128(buf, 0x08); // DW_FORM_string

    encodeULEB128(buf, 0x10); // DW_AT_stmt_list
    encodeULEB128(buf, 0x17); // DW_FORM_sec_offset

    // End of attributes
    encodeULEB128(buf, 0);
    encodeULEB128(buf, 0);

    // End of abbreviations
    encodeULEB128(buf, 0);

    return buf;
}

} // namespace qc
