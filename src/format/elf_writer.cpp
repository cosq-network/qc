// src/format/elf_writer.cpp
// Implements ELFWriter — produces ELF64 relocatable (.o) files.

#include "qc/elf_writer.h"

#include <algorithm>
#include <cstring>
#include <cassert>

namespace qc {

// ---------------------------------------------------------------------------
// Helper: append raw bytes of a POD value to a vector
// ---------------------------------------------------------------------------
template<typename T>
static void appendBytes(std::vector<u8>& buf, const T& val) {
    const u8* p = reinterpret_cast<const u8*>(&val);
    buf.insert(buf.end(), p, p + sizeof(T));
}

// Pad buf to the given alignment
static void padTo(std::vector<u8>& buf, size_t align) {
    if (align <= 1) return;
    while (buf.size() % align != 0)
        buf.push_back(0);
}

// ---------------------------------------------------------------------------
// ELFWriter constructor
// ---------------------------------------------------------------------------
ELFWriter::ELFWriter(TargetArch arch) : arch_(arch) {
    sections_.reserve(16); // Pre-allocate to prevent reference invalidation
    // Ensure there is always a null symbol at index 0
    ELFSymbol nullSym;
    nullSym.name        = "";
    nullSym.sectionIndex = 0;
    nullSym.value        = 0;
    nullSym.size         = 0;
    nullSym.binding      = STB_LOCAL;
    nullSym.type         = STT_NOTYPE;
    nullSym.isExternal   = false;
    symbols_.push_back(std::move(nullSym));
}

// ---------------------------------------------------------------------------
// getSection
// ---------------------------------------------------------------------------
ELFSection& ELFWriter::getSection(std::string name, u32 type, u64 flags, u32 align) {
    auto it = sectionIndex_.find(name);
    if (it != sectionIndex_.end()) {
        return sections_[it->second];
    }
    u32 idx = static_cast<u32>(sections_.size());
    ELFSection sec;
    sec.name    = name;
    sec.type    = type;
    sec.flags   = flags;
    sec.align   = align;
    sec.shIndex = idx; // will be updated properly during emit
    sections_.push_back(std::move(sec));
    sectionIndex_[sections_.back().name] = idx;
    return sections_.back();
}

// ---------------------------------------------------------------------------
// addSymbol
// ---------------------------------------------------------------------------
u32 ELFWriter::addSymbol(ELFSymbol sym) {
    u32 idx = static_cast<u32>(symbols_.size());
    symbols_.push_back(std::move(sym));
    return idx;
}

// ---------------------------------------------------------------------------
// findSymbol
// ---------------------------------------------------------------------------
u32 ELFWriter::findSymbol(std::string_view name) const {
    for (u32 i = 0; i < static_cast<u32>(symbols_.size()); ++i) {
        if (symbols_[i].name == name)
            return i;
    }
    return 0; // null symbol
}

// ---------------------------------------------------------------------------
// addReloc
// ---------------------------------------------------------------------------
void ELFWriter::addReloc(std::string_view section, Reloc r) {
    relocs_[std::string(section)].push_back(r);
}

// ---------------------------------------------------------------------------
// buildStringTable
// ---------------------------------------------------------------------------
void ELFWriter::buildStringTable(std::vector<u8>& strtab,
                                  std::unordered_map<std::string, u32>& offsets,
                                  const std::vector<std::string>& strs) {
    strtab.clear();
    strtab.push_back(0); // first byte is always the null entry

    for (const auto& s : strs) {
        if (offsets.count(s)) continue; // already inserted (dedup)
        u32 off = static_cast<u32>(strtab.size());
        offsets[s] = off;
        strtab.insert(strtab.end(), s.begin(), s.end());
        strtab.push_back(0);
    }
}

// ---------------------------------------------------------------------------
// emit
// ---------------------------------------------------------------------------
std::vector<u8> ELFWriter::emit() {
    // -----------------------------------------------------------------------
    // 0. Decide machine type
    // -----------------------------------------------------------------------
    u16 machine = (arch_ == TargetArch::X64) ? EM_X86_64 : EM_AARCH64;

    // -----------------------------------------------------------------------
    // 1. Separate symbols into locals and globals (keeping original indices
    //    for relocations).  We will reorder: locals first, then globals.
    //    We track a mapping from old index → new index.
    // -----------------------------------------------------------------------
    std::vector<u32> oldToNew(symbols_.size(), 0);
    std::vector<u32> newOrder; // new order: null(0) + locals + globals

    // Index 0 is always the null symbol (local)
    newOrder.push_back(0);
    oldToNew[0] = 0;

    // Collect non-null locals
    for (u32 i = 1; i < static_cast<u32>(symbols_.size()); ++i) {
        if (symbols_[i].binding == STB_LOCAL) {
            oldToNew[i] = static_cast<u32>(newOrder.size());
            newOrder.push_back(i);
        }
    }
    u32 numLocals = static_cast<u32>(newOrder.size()); // for sh_info of .symtab

    // Collect globals (and weaks)
    for (u32 i = 1; i < static_cast<u32>(symbols_.size()); ++i) {
        if (symbols_[i].binding != STB_LOCAL) {
            oldToNew[i] = static_cast<u32>(newOrder.size());
            newOrder.push_back(i);
        }
    }

    // -----------------------------------------------------------------------
    // 2. Build section name string table (.shstrtab)
    //    Names that will appear: "" (null) + each user section + ".symtab" +
    //    ".strtab" + ".shstrtab" + ".rela.<section>" for each reloc section.
    // -----------------------------------------------------------------------
    // We'll figure out exact names after we know which rela sections exist.
    // Collect all section names we'll write to the section header table.
    // Order: null section, user sections, .symtab, .strtab, .shstrtab, .rela.*

    // Determine which user sections have relocations
    std::vector<std::string> relaSectionNames; // names of .rela.* sections
    for (auto& kv : relocs_) {
        if (!kv.second.empty()) {
            relaSectionNames.push_back(".rela." + kv.first.substr(1)); // e.g. .rela.text
            // actually the RELA section name is ".rela" + section name
            // but conventional is ".rela.text" for ".text"
        }
    }
    // Sort for determinism
    std::sort(relaSectionNames.begin(), relaSectionNames.end());

    // -----------------------------------------------------------------------
    // 3. Build symbol string table (.strtab)
    // -----------------------------------------------------------------------
    std::vector<std::string> symNames;
    symNames.reserve(newOrder.size());
    for (u32 ni : newOrder) {
        symNames.push_back(symbols_[ni].name);
    }

    std::vector<u8> symStrtab;
    std::unordered_map<std::string, u32> symStrOffsets;
    buildStringTable(symStrtab, symStrOffsets, symNames);

    // -----------------------------------------------------------------------
    // 4. Assign section header indices
    //    Layout of sections in section header table:
    //      0: null
    //      1..N: user sections
    //      N+1: .symtab
    //      N+2: .strtab  (symbol strtab)
    //      N+3: .shstrtab
    //      N+4...: .rela.* sections
    // -----------------------------------------------------------------------
    u32 numUserSections = static_cast<u32>(sections_.size());
    u32 symtabIdx   = numUserSections + 1;
    u32 strtabIdx   = numUserSections + 2;
    u32 shstrtabIdx = numUserSections + 3;
    u32 firstRelaIdx = numUserSections + 4;
    u32 totalSections = firstRelaIdx + static_cast<u32>(relaSectionNames.size());

    // Update shIndex for user sections (1-based because 0 is null section)
    for (u32 i = 0; i < numUserSections; ++i) {
        sections_[i].shIndex = i + 1;
        sectionIndex_[sections_[i].name] = i; // keep 0-based index into sections_ vec
    }

    // -----------------------------------------------------------------------
    // 5. Build shstrtab — collect all section names
    // -----------------------------------------------------------------------
    std::vector<std::string> allShNames;
    allShNames.push_back(""); // null section
    for (auto& s : sections_) allShNames.push_back(s.name);
    allShNames.push_back(".symtab");
    allShNames.push_back(".strtab");
    allShNames.push_back(".shstrtab");
    for (auto& rn : relaSectionNames) allShNames.push_back(rn);

    std::vector<u8> shstrtab;
    std::unordered_map<std::string, u32> shStrOffsets;
    buildStringTable(shstrtab, shStrOffsets, allShNames);

    // -----------------------------------------------------------------------
    // 6. Build .symtab data
    // -----------------------------------------------------------------------
    std::vector<u8> symtabData;
    // We need to know section shIndices for symbol shndx
    // For a symbol referencing a user section: sections_[idx].shIndex = idx+1
    // For undefined: SHN_UNDEF = 0
    // For absolute:  SHN_ABS   = 0xfff1

    for (u32 ni : newOrder) {
        const ELFSymbol& sym = symbols_[ni];
        Elf64_Sym esym{};
        // st_name
        esym.st_name = symStrOffsets.count(sym.name) ? symStrOffsets.at(sym.name) : 0;
        // st_info
        esym.st_info = static_cast<u8>((sym.binding << 4) | (sym.type & 0xf));
        // st_other
        esym.st_other = STV_DEFAULT;
        // st_shndx
        if (sym.isExternal) {
            esym.st_shndx = 0; // SHN_UNDEF
        } else if (sym.sectionIndex == 0xfff1u) {
            esym.st_shndx = 0xfff1; // SHN_ABS
        } else {
            // sectionIndex is 0-based index into sections_, shIndex = that + 1
            u32 sidx = sym.sectionIndex;
            esym.st_shndx = static_cast<u16>(sidx + 1);
        }
        esym.st_value = sym.value;
        esym.st_size  = sym.size;
        appendBytes(symtabData, esym);
    }

    // -----------------------------------------------------------------------
    // 7. Start assembling the output buffer
    //    Layout:
    //      [Elf64_Ehdr]
    //      [section data for each user section]
    //      [.strtab data]
    //      [.symtab data]
    //      [.shstrtab data]
    //      [.rela.* data]
    //      [Elf64_Shdr table]
    // -----------------------------------------------------------------------
    std::vector<u8> out;
    out.reserve(65536);

    // Write a placeholder header (64 bytes) — we'll fill it in at the end
    Elf64_Ehdr hdr{};
    hdr.e_ident[0] = 0x7f; hdr.e_ident[1] = 'E';
    hdr.e_ident[2] = 'L';  hdr.e_ident[3] = 'F';
    hdr.e_ident[4] = ELFCLASS64;
    hdr.e_ident[5] = ELFDATA2LSB;
    hdr.e_ident[6] = EV_CURRENT;
    hdr.e_ident[7] = ELFOSABI_NONE;
    // rest of e_ident is zero
    hdr.e_type      = ET_REL;
    hdr.e_machine   = machine;
    hdr.e_version   = EV_CURRENT;
    hdr.e_entry     = 0;
    hdr.e_phoff     = 0;
    hdr.e_shoff     = 0; // filled later
    hdr.e_flags     = 0;
    hdr.e_ehsize    = sizeof(Elf64_Ehdr);
    hdr.e_phentsize = 0;
    hdr.e_phnum     = 0;
    hdr.e_shentsize = sizeof(Elf64_Shdr);
    hdr.e_shnum     = 0; // filled later
    hdr.e_shstrndx  = 0; // filled later

    appendBytes(out, hdr); // placeholder, will be overwritten

    // -----------------------------------------------------------------------
    // 8. Write user section data, record offsets
    // -----------------------------------------------------------------------
    std::vector<u64> secOffset(numUserSections, 0);
    for (u32 i = 0; i < numUserSections; ++i) {
        ELFSection& sec = sections_[i];
        if (sec.type == SHT_NOBITS || sec.data.empty()) {
            secOffset[i] = out.size(); // offset still recorded
            continue;
        }
        padTo(out, sec.align > 0 ? sec.align : 1);
        secOffset[i] = out.size();
        out.insert(out.end(), sec.data.begin(), sec.data.end());
    }

    // -----------------------------------------------------------------------
    // 9. Write .symtab
    // -----------------------------------------------------------------------
    padTo(out, 8);
    u64 symtabOffset = out.size();
    out.insert(out.end(), symtabData.begin(), symtabData.end());

    // -----------------------------------------------------------------------
    // 10. Write .strtab (symbol names)
    // -----------------------------------------------------------------------
    u64 strtabOffset = out.size();
    out.insert(out.end(), symStrtab.begin(), symStrtab.end());

    // -----------------------------------------------------------------------
    // 11. Write .shstrtab
    // -----------------------------------------------------------------------
    padTo(out, 1);
    u64 shstrtabOffset = out.size();
    out.insert(out.end(), shstrtab.begin(), shstrtab.end());

    // -----------------------------------------------------------------------
    // 12. Write .rela.* sections — collect their offsets and sizes
    // -----------------------------------------------------------------------
    // Build a map: source section name → (relaOffset, relaSize, srcSecIdx)
    struct RelaInfo {
        u64 offset;
        u64 size;
        u32 srcSecShIdx; // sh_info: section being relocated (1-based shIndex)
    };
    std::vector<RelaInfo> relaInfos;

    for (auto& rn : relaSectionNames) {
        // rn is ".rela.text" → source section is ".text"
        std::string srcName = rn.substr(5); // strip ".rela"
        // srcName is e.g. ".text"
        auto srcIt = sectionIndex_.find(srcName);
        u32 srcShIdx = 0;
        if (srcIt != sectionIndex_.end()) {
            srcShIdx = sections_[srcIt->second].shIndex; // 1-based
        }

        auto& relocs = relocs_[srcName];

        padTo(out, 8);
        u64 relaOff = out.size();

        for (auto& r : relocs) {
            Elf64_Rela rela{};
            rela.r_offset = r.offset;
            // r_info = (newSymIndex << 32) | type
            u32 newSym = (r.symIndex < static_cast<u32>(oldToNew.size()))
                         ? oldToNew[r.symIndex] : 0;
            rela.r_info   = (static_cast<u64>(newSym) << 32) | static_cast<u64>(r.type);
            rela.r_addend = r.addend;
            appendBytes(out, rela);
        }

        u64 relaSize = out.size() - relaOff;
        relaInfos.push_back({relaOff, relaSize, srcShIdx});
    }

    // -----------------------------------------------------------------------
    // 13. Pad and write section header table
    // -----------------------------------------------------------------------
    padTo(out, 8);
    u64 shoff = out.size();

    // Section header 0: null
    {
        Elf64_Shdr sh{};
        appendBytes(out, sh);
    }

    // User section headers
    for (u32 i = 0; i < numUserSections; ++i) {
        ELFSection& sec = sections_[i];
        Elf64_Shdr sh{};
        sh.sh_name      = shStrOffsets.count(sec.name) ? shStrOffsets.at(sec.name) : 0;
        sh.sh_type      = sec.type;
        sh.sh_flags     = sec.flags;
        sh.sh_addr      = 0;
        sh.sh_offset    = (sec.type == SHT_NOBITS) ? 0 : secOffset[i];
        sh.sh_size      = (sec.type == SHT_NOBITS) ? static_cast<u64>(sec.data.size()) : static_cast<u64>(sec.data.size());
        sh.sh_link      = 0;
        sh.sh_info      = 0;
        sh.sh_addralign = (sec.align > 0) ? sec.align : 1;
        sh.sh_entsize   = 0;
        appendBytes(out, sh);
    }

    // .symtab
    {
        Elf64_Shdr sh{};
        sh.sh_name      = shStrOffsets.count(".symtab") ? shStrOffsets.at(".symtab") : 0;
        sh.sh_type      = SHT_SYMTAB;
        sh.sh_flags     = 0;
        sh.sh_addr      = 0;
        sh.sh_offset    = symtabOffset;
        sh.sh_size      = static_cast<u64>(symtabData.size());
        sh.sh_link      = strtabIdx;   // index of .strtab
        sh.sh_info      = numLocals;   // number of local symbols
        sh.sh_addralign = 8;
        sh.sh_entsize   = sizeof(Elf64_Sym);
        appendBytes(out, sh);
    }

    // .strtab
    {
        Elf64_Shdr sh{};
        sh.sh_name      = shStrOffsets.count(".strtab") ? shStrOffsets.at(".strtab") : 0;
        sh.sh_type      = SHT_STRTAB;
        sh.sh_flags     = 0;
        sh.sh_addr      = 0;
        sh.sh_offset    = strtabOffset;
        sh.sh_size      = static_cast<u64>(symStrtab.size());
        sh.sh_link      = 0;
        sh.sh_info      = 0;
        sh.sh_addralign = 1;
        sh.sh_entsize   = 0;
        appendBytes(out, sh);
    }

    // .shstrtab
    {
        Elf64_Shdr sh{};
        sh.sh_name      = shStrOffsets.count(".shstrtab") ? shStrOffsets.at(".shstrtab") : 0;
        sh.sh_type      = SHT_STRTAB;
        sh.sh_flags     = 0;
        sh.sh_addr      = 0;
        sh.sh_offset    = shstrtabOffset;
        sh.sh_size      = static_cast<u64>(shstrtab.size());
        sh.sh_link      = 0;
        sh.sh_info      = 0;
        sh.sh_addralign = 1;
        sh.sh_entsize   = 0;
        appendBytes(out, sh);
    }

    // .rela.* section headers
    for (u32 ri = 0; ri < static_cast<u32>(relaSectionNames.size()); ++ri) {
        const std::string& rn = relaSectionNames[ri];
        const RelaInfo& ri_info = relaInfos[ri];
        Elf64_Shdr sh{};
        sh.sh_name      = shStrOffsets.count(rn) ? shStrOffsets.at(rn) : 0;
        sh.sh_type      = SHT_RELA;
        sh.sh_flags     = SHF_ALLOC; // conventionally 0 for .o files; use 0 to match linkers
        // Actually for relocatable objects, .rela sections typically have sh_flags = 0
        sh.sh_flags     = 0;
        sh.sh_addr      = 0;
        sh.sh_offset    = ri_info.offset;
        sh.sh_size      = ri_info.size;
        sh.sh_link      = symtabIdx;         // .symtab index
        sh.sh_info      = ri_info.srcSecShIdx; // section being relocated
        sh.sh_addralign = 8;
        sh.sh_entsize   = sizeof(Elf64_Rela);
        appendBytes(out, sh);
    }

    // -----------------------------------------------------------------------
    // 14. Patch the header
    // -----------------------------------------------------------------------
    hdr.e_shoff    = shoff;
    hdr.e_shnum    = static_cast<u16>(totalSections);
    hdr.e_shstrndx = static_cast<u16>(shstrtabIdx);

    std::memcpy(out.data(), &hdr, sizeof(hdr));

    return out;
}

} // namespace qc
