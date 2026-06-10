// src/format/pe_writer.cpp
// Implements PEWriter — produces COFF .obj files for AMD64 / ARM64.

#include "qc/pe_writer.h"

#include <algorithm>
#include <cstring>
#include <ctime>

namespace qc {

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------
template<typename T>
static void appendBytes(std::vector<u8>& buf, const T& val) {
    const u8* p = reinterpret_cast<const u8*>(&val);
    buf.insert(buf.end(), p, p + sizeof(T));
}

[[maybe_unused]] static void padTo(std::vector<u8>& buf, size_t align) {
    if (align <= 1) return;
    while (buf.size() % align != 0)
        buf.push_back(0);
}

// ---------------------------------------------------------------------------
// PEWriter constructor
// ---------------------------------------------------------------------------
PEWriter::PEWriter(TargetArch arch) : arch_(arch) {}

// ---------------------------------------------------------------------------
// getSection
// ---------------------------------------------------------------------------
PESection& PEWriter::getSection(std::string name, u32 chars) {
    auto it = sectionIndex_.find(name);
    if (it != sectionIndex_.end()) {
        return sections_[it->second];
    }
    u32 idx = static_cast<u32>(sections_.size());
    PESection sec;
    sec.name            = name;
    sec.characteristics = chars;
    sections_.push_back(std::move(sec));
    sectionIndex_[sections_.back().name] = idx;
    return sections_.back();
}

// ---------------------------------------------------------------------------
// addSymbol
// ---------------------------------------------------------------------------
u32 PEWriter::addSymbol(PESymbol sym) {
    u32 idx = static_cast<u32>(symbols_.size());
    symbols_.push_back(std::move(sym));
    return idx;
}

// ---------------------------------------------------------------------------
// findSymbol
// ---------------------------------------------------------------------------
u32 PEWriter::findSymbol(std::string_view name) const {
    for (u32 i = 0; i < static_cast<u32>(symbols_.size()); ++i) {
        if (symbols_[i].name == name)
            return i;
    }
    return 0;
}

// ---------------------------------------------------------------------------
// addReloc
// ---------------------------------------------------------------------------
void PEWriter::addReloc(std::string_view section, PEReloc r) {
    relocs_[std::string(section)].push_back(r);
}

// ---------------------------------------------------------------------------
// emit
// ---------------------------------------------------------------------------
std::vector<u8> PEWriter::emit() {
    // -----------------------------------------------------------------------
    // 0. Determine machine type
    // -----------------------------------------------------------------------
    u16 machine = (arch_ == TargetArch::X64)
                  ? IMAGE_FILE_MACHINE_AMD64
                  : IMAGE_FILE_MACHINE_ARM64;

    u32 numSections = static_cast<u32>(sections_.size());

    // -----------------------------------------------------------------------
    // 1. Build the string table for long symbol names (> 8 chars).
    //    COFF string table format:
    //      [4-byte total size including the size field itself]
    //      [null-terminated strings...]
    //    We first collect all strings that need long names.
    // -----------------------------------------------------------------------
    std::vector<u8> stringTableData; // does NOT include the 4-byte size field yet
    std::unordered_map<std::string, u32> longNameOffsets; // name → offset past size field

    auto getStringOffset = [&](const std::string& name) -> u32 {
        if (longNameOffsets.count(name)) return longNameOffsets.at(name);
        // offset is relative to start of string table (after the 4-byte size)
        u32 off = static_cast<u32>(stringTableData.size()) + 4; // +4 for the size field
        longNameOffsets[name] = off;
        stringTableData.insert(stringTableData.end(), name.begin(), name.end());
        stringTableData.push_back(0);
        return off;
    };

    // Pre-scan symbols to populate string table
    for (auto& sym : symbols_) {
        if (sym.name.size() > 8) {
            getStringOffset(sym.name);
        }
    }

    // -----------------------------------------------------------------------
    // 2. Layout offsets
    //    [COFFHeader]  20 bytes
    //    [COFFSectionHeader × numSections]  40 bytes each
    //    [raw section data]
    //    [COFF relocations per section]
    //    [symbol table]
    //    [string table]
    // -----------------------------------------------------------------------
    u32 headerSize      = sizeof(COFFHeader);
    u32 secHdrSize      = sizeof(COFFSectionHeader) * numSections;
    u32 rawDataStart    = headerSize + secHdrSize;

    // Compute per-section raw data offsets and relocation offsets
    std::vector<u32> secRawOffset(numSections, 0);
    std::vector<u32> secRelocOffset(numSections, 0);

    u32 cursor = rawDataStart;

    // First pass: lay out raw data
    for (u32 i = 0; i < numSections; ++i) {
        PESection& sec = sections_[i];
        if (sec.data.empty()) {
            secRawOffset[i] = 0; // no raw data
        } else {
            secRawOffset[i] = cursor;
            cursor += static_cast<u32>(sec.data.size());
            // Sections in COFF .obj files are typically not aligned beyond their
            // natural alignment; we align to 2 bytes for robustness.
            if (cursor % 2 != 0) cursor++;
        }
    }

    // Second pass: lay out relocation tables
    for (u32 i = 0; i < numSections; ++i) {
        const std::string& name = sections_[i].name;
        auto rit = relocs_.find(name);
        u32 numRelocs = (rit != relocs_.end()) ? static_cast<u32>(rit->second.size()) : 0;
        if (numRelocs > 0) {
            secRelocOffset[i] = cursor;
            cursor += numRelocs * sizeof(COFFReloc);
        } else {
            secRelocOffset[i] = 0;
        }
    }

    // Symbol table
    u32 symTableOffset = cursor;
    u32 numSymbols     = static_cast<u32>(symbols_.size());
    cursor += numSymbols * sizeof(COFFSymbol);

    // String table
    u32 strTableOffset = cursor;
    (void)strTableOffset;
    u32 strTableSize   = static_cast<u32>(stringTableData.size()) + 4; // +4 for size field

    // -----------------------------------------------------------------------
    // 3. Assemble output
    // -----------------------------------------------------------------------
    std::vector<u8> out;
    out.reserve(cursor + strTableSize + 256);

    // --- COFFHeader ---
    {
        COFFHeader h{};
        h.Machine              = machine;
        h.NumberOfSections     = static_cast<u16>(numSections);
        h.TimeDateStamp        = 0; // deterministic
        h.PointerToSymbolTable = symTableOffset;
        h.NumberOfSymbols      = numSymbols;
        h.SizeOfOptionalHeader = 0;
        h.Characteristics      = 0; // relocatable .obj has 0 characteristics
        appendBytes(out, h);
    }

    // --- COFFSectionHeader array ---
    for (u32 i = 0; i < numSections; ++i) {
        PESection& sec = sections_[i];
        const std::string& name = sec.name;
        auto rit = relocs_.find(name);
        u16 numRelocs = (rit != relocs_.end()) ? static_cast<u16>(rit->second.size()) : 0;

        COFFSectionHeader sh{};
        // Name field: up to 8 chars, null-padded
        {
            size_t copyLen = std::min(name.size(), static_cast<size_t>(8));
            std::memcpy(sh.Name, name.data(), copyLen);
            // rest is already zero from value-initialization
        }
        sh.VirtualSize         = 0;
        sh.VirtualAddress      = 0;
        sh.SizeOfRawData       = static_cast<u32>(sec.data.size());
        sh.PointerToRawData    = sec.data.empty() ? 0 : secRawOffset[i];
        sh.PointerToRelocations = (numRelocs > 0) ? secRelocOffset[i] : 0;
        sh.PointerToLinenumbers = 0;
        sh.NumberOfRelocations  = numRelocs;
        sh.NumberOfLinenumbers  = 0;
        sh.Characteristics     = sec.characteristics;
        appendBytes(out, sh);
    }

    // --- Raw section data ---
    for (u32 i = 0; i < numSections; ++i) {
        PESection& sec = sections_[i];
        if (sec.data.empty()) continue;

        // Should already be at secRawOffset[i]
        assert(out.size() == secRawOffset[i]);
        out.insert(out.end(), sec.data.begin(), sec.data.end());
        if (out.size() % 2 != 0) out.push_back(0); // align to 2
    }

    // --- COFF relocations ---
    for (u32 i = 0; i < numSections; ++i) {
        const std::string& name = sections_[i].name;
        auto rit = relocs_.find(name);
        if (rit == relocs_.end() || rit->second.empty()) continue;

        assert(out.size() == secRelocOffset[i]);
        for (auto& r : rit->second) {
            COFFReloc cr{};
            cr.VirtualAddress   = r.offset;
            cr.SymbolTableIndex = r.symIndex;
            cr.Type             = r.type;
            appendBytes(out, cr);
        }
    }

    // --- Symbol table ---
    assert(out.size() == symTableOffset);
    for (auto& sym : symbols_) {
        COFFSymbol cs{};
        std::memset(&cs, 0, sizeof(cs));

        if (sym.name.size() <= 8) {
            // Short name: embed directly
            std::memcpy(cs.Name.ShortName, sym.name.data(), sym.name.size());
        } else {
            // Long name: Zeroes = 0, Offset = offset in string table
            cs.Name.LongName.Zeroes = 0;
            cs.Name.LongName.Offset = getStringOffset(sym.name);
        }

        cs.Value         = sym.value;
        cs.SectionNumber = sym.sectionNumber;
        cs.Type          = 0x20; // function type (0x20 = function, 0x00 = no type)
        cs.StorageClass  = sym.storageClass;
        cs.NumberOfAuxSymbols = 0;
        appendBytes(out, cs);
    }

    // --- String table ---
    assert(out.size() == strTableOffset);
    // Write 4-byte size (includes the size field itself)
    appendBytes(out, strTableSize);
    // Write string data
    out.insert(out.end(), stringTableData.begin(), stringTableData.end());

    return out;
}

} // namespace qc
