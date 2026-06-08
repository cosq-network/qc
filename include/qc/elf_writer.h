#pragma once
#include "common.h"
#include <vector>
#include <string>
#include <unordered_map>

namespace qc {

// ELF64 types
using Elf64_Addr  = u64;
using Elf64_Off   = u64;
using Elf64_Half  = u16;
using Elf64_Word  = u32;
using Elf64_Sword = i32;
using Elf64_Xword = u64;
using Elf64_Sxword= i64;

static constexpr u32 ELF_MAGIC = 0x464C457F; // 0x7F 'E' 'L' 'F'
static constexpr u8  ELFCLASS64 = 2;
static constexpr u8  ELFDATA2LSB = 1; // little endian
static constexpr u8  ELFDATA2MSB = 2;
static constexpr u16 ET_REL  = 1; // relocatable
static constexpr u16 ET_EXEC = 2;
static constexpr u16 EM_X86_64  = 62;
static constexpr u16 EM_AARCH64 = 183;
static constexpr u32 EV_CURRENT = 1;
static constexpr u8  ELFOSABI_NONE = 0;

// Section header types
static constexpr u32 SHT_NULL     = 0;
static constexpr u32 SHT_PROGBITS = 1;
static constexpr u32 SHT_SYMTAB   = 2;
static constexpr u32 SHT_STRTAB   = 3;
static constexpr u32 SHT_RELA     = 4;
static constexpr u32 SHT_NOBITS   = 8;
static constexpr u32 SHT_REL      = 9;

// Section flags
static constexpr u64 SHF_WRITE     = 0x1;
static constexpr u64 SHF_ALLOC     = 0x2;
static constexpr u64 SHF_EXECINSTR = 0x4;
static constexpr u64 SHF_MERGE     = 0x10;
static constexpr u64 SHF_STRINGS   = 0x20;

// Symbol binding
static constexpr u8 STB_LOCAL  = 0;
static constexpr u8 STB_GLOBAL = 1;
static constexpr u8 STB_WEAK   = 2;
// Symbol type
static constexpr u8 STT_NOTYPE  = 0;
static constexpr u8 STT_OBJECT  = 1;
static constexpr u8 STT_FUNC    = 2;
static constexpr u8 STT_SECTION = 3;
static constexpr u8 STT_FILE    = 4;
// Symbol visibility
static constexpr u8 STV_DEFAULT  = 0;

// x86_64 relocation types
static constexpr u32 R_X86_64_NONE    = 0;
static constexpr u32 R_X86_64_64      = 1;
static constexpr u32 R_X86_64_PC32    = 2;
static constexpr u32 R_X86_64_PLT32   = 4;
static constexpr u32 R_X86_64_32S     = 11;

// aarch64 relocation types
static constexpr u32 R_AARCH64_ABS64     = 257;
static constexpr u32 R_AARCH64_CALL26    = 283;
static constexpr u32 R_AARCH64_ADR_PREL_PG_HI21 = 275;
static constexpr u32 R_AARCH64_ADD_ABS_LO12_NC  = 277;

#pragma pack(push, 1)
struct Elf64_Ehdr {
    u8        e_ident[16];
    Elf64_Half e_type;
    Elf64_Half e_machine;
    Elf64_Word e_version;
    Elf64_Addr e_entry;
    Elf64_Off  e_phoff;
    Elf64_Off  e_shoff;
    Elf64_Word e_flags;
    Elf64_Half e_ehsize;
    Elf64_Half e_phentsize;
    Elf64_Half e_phnum;
    Elf64_Half e_shentsize;
    Elf64_Half e_shnum;
    Elf64_Half e_shstrndx;
};

struct Elf64_Shdr {
    Elf64_Word  sh_name;
    Elf64_Word  sh_type;
    Elf64_Xword sh_flags;
    Elf64_Addr  sh_addr;
    Elf64_Off   sh_offset;
    Elf64_Xword sh_size;
    Elf64_Word  sh_link;
    Elf64_Word  sh_info;
    Elf64_Xword sh_addralign;
    Elf64_Xword sh_entsize;
};

struct Elf64_Sym {
    Elf64_Word  st_name;
    u8          st_info;
    u8          st_other;
    Elf64_Half  st_shndx;
    Elf64_Addr  st_value;
    Elf64_Xword st_size;
};

struct Elf64_Rela {
    Elf64_Addr   r_offset;
    Elf64_Xword  r_info;
    Elf64_Sxword r_addend;
};
#pragma pack(pop)

// High-level section
struct ELFSection {
    std::string   name;
    u32           type   = SHT_PROGBITS;
    u64           flags  = 0;
    u32           align  = 1;
    std::vector<u8> data;
    u32           shIndex = 0;
};

// Relocation entry
struct Reloc {
    u64         offset;   // offset within section
    u32         symIndex; // symbol table index
    u32         type;     // R_X86_64_* or R_AARCH64_*
    i64         addend;
};

// Symbol table entry (before layout)
struct ELFSymbol {
    std::string name;
    u32         sectionIndex = 0; // SHN_UNDEF = 0, SHN_ABS = 0xfff1
    u64         value        = 0;
    u64         size         = 0;
    u8          binding      = STB_GLOBAL;
    u8          type         = STT_NOTYPE;
    bool        isExternal   = false; // undefined reference
};

class ELFWriter {
public:
    explicit ELFWriter(TargetArch arch);

    // Create / get sections
    ELFSection& textSection()   { return getSection(".text",   SHT_PROGBITS, SHF_ALLOC | SHF_EXECINSTR, 16); }
    ELFSection& dataSection()   { return getSection(".data",   SHT_PROGBITS, SHF_ALLOC | SHF_WRITE, 8); }
    ELFSection& rodataSection() { return getSection(".rodata", SHT_PROGBITS, SHF_ALLOC | SHF_MERGE, 8); }
    ELFSection& bssSection()    { return getSection(".bss",    SHT_NOBITS,   SHF_ALLOC | SHF_WRITE, 8); }

    ELFSection& getSection(std::string name, u32 type, u64 flags, u32 align);

    // Symbol management
    u32 addSymbol(ELFSymbol sym);
    u32 findSymbol(std::string_view name) const;

    // Relocation
    void addReloc(std::string_view section, Reloc r);

    // Emit
    std::vector<u8> emit();

private:
    void buildStringTable(std::vector<u8>& strtab,
                          std::unordered_map<std::string,u32>& offsets,
                          const std::vector<std::string>& strs);

    TargetArch arch_;
    std::vector<ELFSection>  sections_;
    std::vector<ELFSymbol>   symbols_;
    std::unordered_map<std::string, std::vector<Reloc>> relocs_;
    std::unordered_map<std::string, u32> sectionIndex_;
};

} // namespace qc
