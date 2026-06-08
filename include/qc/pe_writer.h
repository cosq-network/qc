#pragma once
#include "common.h"
#include <vector>
#include <string>
#include <unordered_map>

namespace qc {

// PE/COFF constants
static constexpr u16 IMAGE_FILE_MACHINE_AMD64  = 0x8664;
static constexpr u16 IMAGE_FILE_MACHINE_ARM64  = 0xAA64;

static constexpr u32 IMAGE_SCN_CNT_CODE               = 0x00000020;
static constexpr u32 IMAGE_SCN_CNT_INITIALIZED_DATA   = 0x00000040;
static constexpr u32 IMAGE_SCN_CNT_UNINITIALIZED_DATA = 0x00000080;
static constexpr u32 IMAGE_SCN_MEM_EXECUTE            = 0x20000000;
static constexpr u32 IMAGE_SCN_MEM_READ               = 0x40000000;
static constexpr u32 IMAGE_SCN_MEM_WRITE              = 0x80000000;
static constexpr u32 IMAGE_SCN_ALIGN_16BYTES          = 0x00500000;
static constexpr u32 IMAGE_SCN_ALIGN_8BYTES           = 0x00400000;

static constexpr u16 IMAGE_SYM_CLASS_EXTERNAL  = 2;
static constexpr u16 IMAGE_SYM_CLASS_STATIC    = 3;
static constexpr u16 IMAGE_SYM_CLASS_LABEL     = 6;
static constexpr u16 IMAGE_SYM_CLASS_FILE      = 103;
static constexpr i16 IMAGE_SYM_UNDEFINED       = 0;
static constexpr i16 IMAGE_SYM_ABSOLUTE        = -1;

static constexpr u16 IMAGE_REL_AMD64_ADDR64   = 0x0001;
static constexpr u16 IMAGE_REL_AMD64_ADDR32   = 0x0002;
static constexpr u16 IMAGE_REL_AMD64_ADDR32NB = 0x0003;
static constexpr u16 IMAGE_REL_AMD64_REL32    = 0x0004;

static constexpr u16 IMAGE_REL_ARM64_ADDR32   = 0x0001;
static constexpr u16 IMAGE_REL_ARM64_ADDR64   = 0x000A;
static constexpr u16 IMAGE_REL_ARM64_BRANCH26 = 0x0003;

#pragma pack(push, 1)
struct COFFHeader {
    u16 Machine;
    u16 NumberOfSections;
    u32 TimeDateStamp;
    u32 PointerToSymbolTable;
    u32 NumberOfSymbols;
    u16 SizeOfOptionalHeader;
    u16 Characteristics;
};

struct COFFSectionHeader {
    char Name[8];
    u32  VirtualSize;
    u32  VirtualAddress;
    u32  SizeOfRawData;
    u32  PointerToRawData;
    u32  PointerToRelocations;
    u32  PointerToLinenumbers;
    u16  NumberOfRelocations;
    u16  NumberOfLinenumbers;
    u32  Characteristics;
};

struct COFFSymbol {
    union {
        char ShortName[8];
        struct { u32 Zeroes; u32 Offset; } LongName;
    } Name;
    u32 Value;
    i16 SectionNumber;
    u16 Type;
    u8  StorageClass;
    u8  NumberOfAuxSymbols;
};

struct COFFReloc {
    u32 VirtualAddress;
    u32 SymbolTableIndex;
    u16 Type;
};
#pragma pack(pop)

// High level section
struct PESection {
    std::string   name;       // up to 8 chars
    u32           characteristics = 0;
    u32           align = 16;
    std::vector<u8> data;
};

struct PESymbol {
    std::string name;
    i16         sectionNumber = IMAGE_SYM_UNDEFINED;
    u32         value  = 0;
    u8          storageClass  = IMAGE_SYM_CLASS_EXTERNAL;
    bool        isExternal    = false;
};

struct PEReloc {
    u32 offset;
    u32 symIndex;
    u16 type;
};

class PEWriter {
public:
    explicit PEWriter(TargetArch arch);

    PESection& textSection()   { return getSection(".text",  IMAGE_SCN_CNT_CODE | IMAGE_SCN_MEM_READ | IMAGE_SCN_MEM_EXECUTE | IMAGE_SCN_ALIGN_16BYTES); }
    PESection& dataSection()   { return getSection(".data",  IMAGE_SCN_CNT_INITIALIZED_DATA | IMAGE_SCN_MEM_READ | IMAGE_SCN_MEM_WRITE | IMAGE_SCN_ALIGN_8BYTES); }
    PESection& rdataSection()  { return getSection(".rdata", IMAGE_SCN_CNT_INITIALIZED_DATA | IMAGE_SCN_MEM_READ | IMAGE_SCN_ALIGN_8BYTES); }
    PESection& bssSection()    { return getSection(".bss",   IMAGE_SCN_CNT_UNINITIALIZED_DATA | IMAGE_SCN_MEM_READ | IMAGE_SCN_MEM_WRITE | IMAGE_SCN_ALIGN_8BYTES); }

    PESection& getSection(std::string name, u32 chars);

    u32  addSymbol(PESymbol sym);
    u32  findSymbol(std::string_view name) const;

    void addReloc(std::string_view section, PEReloc r);

    std::vector<u8> emit();

private:
    TargetArch   arch_;
    std::vector<PESection>  sections_;
    std::vector<PESymbol>   symbols_;
    std::unordered_map<std::string, std::vector<PEReloc>> relocs_;
    std::unordered_map<std::string, u32> sectionIndex_;
};

} // namespace qc
