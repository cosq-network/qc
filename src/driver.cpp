// src/driver.cpp
// Implements Driver and driverMain for the qc compiler.

#include "qc/driver.h"
#include "qc/source.h"
#include "qc/lexer.h"
#include "qc/parser.h"
#include "qc/ast.h"
#include "qc/type.h"
#include "qc/sema.h"
#include "qc/irgen.h"
#include "qc/ir.h"
#include "qc/codegen.h"
#include "qc/common.h"

#include <cstdio>
#include <cstring>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>
#include <algorithm>
#include <cstdlib>
#include <sstream>

namespace qc {

// ---------------------------------------------------------------------------
// Driver
// ---------------------------------------------------------------------------

Driver::Driver(CompileOptions opts) : opts_(std::move(opts)) {}

int Driver::run() {
    if (opts_.inputs.empty()) {
        std::fprintf(stderr, "qc: error: no input files\n");
        return 1;
    }

    if (opts_.link) {
        // When linking, we'll emit assembly to temp files and let 'cc' handle assembly and linking.
        // This ensures we get real machine code even though our ELFWriter is currently a prototype.
        std::vector<std::string> assemblyFiles;
        int result = 0;
        for (const auto& path : opts_.inputs) {
            // Force assembly output kind for the internal compilation
            OutputKind oldKind = opts_.outKind;
            opts_.outKind = OutputKind::Assembly;
            
            std::string out;
            int r = compileFile(path, out);
            opts_.outKind = oldKind; // restore

            if (r != 0) {
                result = r;
            } else {
                assemblyFiles.push_back(out);
            }
        }

        if (result == 0 && !assemblyFiles.empty()) {
            tempFiles_ = std::move(assemblyFiles);
            result = invokeLinker();
        }
        return result;
    }

    // Standard compile-only path
    std::vector<std::string> objectFiles;
    int result = 0;
    for (const auto& path : opts_.inputs) {
        std::string out;
        int r = compileFile(path, out);
        if (r != 0) {
            result = r;
        } else if (!out.empty() && opts_.outKind == OutputKind::Object) {
            objectFiles.push_back(out);
        }
    }
    return result;
}

int Driver::invokeLinker() {
    std::stringstream ss;
    ss << "cc"; // Use cc as the linker driver

    // Output file
    std::string outPath = opts_.output.empty() ? "a.out" : opts_.output;
    ss << " -o " << outPath;

    // Object files
    for (const auto& obj : tempFiles_) {
        ss << " " << obj;
    }

    // Library paths
    for (const auto& path : opts_.libPaths) {
        ss << " -L" << path;
    }

    // Libraries
    for (const auto& lib : opts_.libs) {
        ss << " -l" << lib;
    }

    // Linker inputs (objects, libraries)
    for (const auto& in : opts_.linkerInputs) {
        ss << " " << in;
    }

    // Linker args
    for (const auto& arg : opts_.linkerArgs) {
        ss << " -Wl," << arg;
    }

    // Freestanding / No standard libs if requested? 
    // For now we assume we WANT system libs unless specified.
    // But we definitely want stdqc if it's there.
    
    // Attempt to link against libstdqc if we can find it
    // ss << " -lstdqc"; 

    // Debug info
    if (opts_.debug) {
        ss << " -g";
    }

    std::string cmd = ss.str();
    if (opts_.verbose) {
        std::fprintf(stderr, "qc: linking: %s\n", cmd.c_str());
    }

    int exitCode = std::system(cmd.c_str());
    if (exitCode != 0) {
        std::fprintf(stderr, "qc: error: linker failed with exit code %d\n", exitCode);
        return 1;
    }

    return 0;
}
// ---------------------------------------------------------------------------
static std::string deriveOutputPath(const std::string& input, OutputKind kind) {
    // Strip trailing extension, then append the appropriate one
    std::string base = input;
    auto dot = base.rfind('.');
    auto slash = base.rfind('/');
    if (dot != std::string::npos && (slash == std::string::npos || dot > slash)) {
        base = base.substr(0, dot);
    }
    switch (kind) {
        case OutputKind::Object:   return base + ".o";
        case OutputKind::Assembly: return base + ".s";
        case OutputKind::IR:       return base + ".ll";
        default:                   return base + ".o";
    }
}

// ---------------------------------------------------------------------------
// Detect language from file extension
// ---------------------------------------------------------------------------
static InputLang detectLang(const std::string& path) {
    auto dot = path.rfind('.');
    if (dot == std::string::npos) return InputLang::CXX; // default to C++
    std::string ext = path.substr(dot + 1);
    if (ext == "c") return InputLang::C;
    if (ext == "cpp" || ext == "cc" || ext == "cxx" || ext == "C") return InputLang::CXX;
    return InputLang::CXX;
}

// ---------------------------------------------------------------------------
// Simple AST dump (decl names to stderr)
// ---------------------------------------------------------------------------
static void dumpAST(const TranslationUnit& tu) {
    std::fprintf(stderr, "=== AST Dump ===\n");
    for (const auto& d : tu.decls) {
        if (!d) continue;
        std::fprintf(stderr, "  %s\n", d->name.c_str());
    }
    std::fprintf(stderr, "================\n");
}

// ---------------------------------------------------------------------------
// Write bytes to a file
// ---------------------------------------------------------------------------
static bool writeFile(const std::string& path, const std::vector<u8>& bytes) {
    std::ofstream ofs(path, std::ios::binary);
    if (!ofs.is_open()) {
        std::fprintf(stderr, "qc: error: cannot open output file '%s'\n", path.c_str());
        return false;
    }
    ofs.write(reinterpret_cast<const char*>(bytes.data()),
              static_cast<std::streamsize>(bytes.size()));
    return ofs.good();
}

// ---------------------------------------------------------------------------
// compileFile
// ---------------------------------------------------------------------------
int Driver::compileFile(const std::string& path, std::string& outPath) {
    if (opts_.verbose) {
        std::fprintf(stderr, "qc: compiling '%s'\n", path.c_str());
    }

    // 1. Load source file
    SourceFile src(path);
    if (!src.load()) {
        std::fprintf(stderr, "qc: error: cannot read '%s'\n", path.c_str());
        return 1;
    }

    // 2. Determine language
    InputLang lang = opts_.lang;
    if (lang == InputLang::Auto) {
        lang = detectLang(path);
    }
    bool cxxMode = (lang == InputLang::CXX);

    // 3. Create type context and diagnostic engine
    TypeContext types;
    DiagEngine  diag;

    // 4. Lex
    Lexer lex(src, diag, cxxMode);

    // 4.5. Preprocess
    Preprocessor pp(lex, diag);
    for (const auto& path : opts_.includePaths) {
        pp.addIncludePath(path);
    }

    // 5. Parse
    Parser parser(pp, types, diag);
    auto tu = parser.parse();

    if (diag.hasError()) {
        // Print diagnostics
        for (const auto& d : diag.diags()) {
            const char* lvl = "note";
            if (d.level == Diagnostic::Level::Warning) lvl = "warning";
            else if (d.level == Diagnostic::Level::Error) lvl = "error";
            else if (d.level == Diagnostic::Level::Fatal) lvl = "fatal";
            std::fprintf(stderr, "%s:%u:%u: %s: %s\n",
                         d.loc.file ? d.loc.file : path.c_str(),
                         d.loc.line, d.loc.column,
                         lvl, d.message.c_str());
        }
        return 1;
    }

    // 6. Semantic analysis
    TargetInfo target;
    target.arch   = opts_.arch;
    target.format = opts_.format;
    target.os     = opts_.os;
    target.isLittleEndian = true;
    target.pointerSize    = 8;
    target.pointerAlign   = 8;

    Sema sema(types, diag, target);
    sema.analyse(*tu);

    // 7. Optional AST dump
    if (opts_.dumpAST) {
        qc::dumpAST(*tu);
    }

    if (diag.hasError()) {
        for (const auto& d : diag.diags()) {
            const char* lvl = "note";
            if (d.level == Diagnostic::Level::Warning) lvl = "warning";
            else if (d.level == Diagnostic::Level::Error) lvl = "error";
            else if (d.level == Diagnostic::Level::Fatal) lvl = "fatal";
            std::fprintf(stderr, "%s:%u:%u: %s: %s\n",
                         d.loc.file ? d.loc.file : path.c_str(),
                         d.loc.line, d.loc.column,
                         lvl, d.message.c_str());
        }
        return 1;
    }

    // 8. Check-only mode
    if (opts_.outKind == OutputKind::CheckOnly) {
        return 0;
    }

    // 9. IR generation
    IRGen irgen(types, diag, target);
    IRModule mod = irgen.generate(*tu);

    // 10. Optional IR dump
    if (opts_.dumpIR) {
        dumpModule(mod, stderr);
    }

    // 11. IR output mode
    if (opts_.outKind == OutputKind::IR) {
        outPath = opts_.output.empty()
                              ? deriveOutputPath(path, OutputKind::IR)
                              : opts_.output;
        // dumpModule writes to a FILE*; open the file and write IR text
        FILE* f = std::fopen(outPath.c_str(), "w");
        if (!f) {
            std::fprintf(stderr, "qc: error: cannot open '%s'\n", outPath.c_str());
            return 1;
        }
        dumpModule(mod, f);
        std::fclose(f);
        return diag.hasError() ? 1 : 0;
    }

    // 12. Code generation
    auto codegen = CodeGen::create(target, diag);
    if (!codegen) {
        std::fprintf(stderr, "qc: error: unsupported target\n");
        return 1;
    }
    if (opts_.debug) {
        codegen->setDebugEnabled(true);
    }
    codegen->compile(mod);

    if (diag.hasError()) {
        return 1;
    }

    // 13. Assembly output mode
    if (opts_.outKind == OutputKind::Assembly) {
        outPath = (opts_.output.empty() || opts_.link)
                              ? deriveOutputPath(path, OutputKind::Assembly)
                              : opts_.output;
        FILE* f = std::fopen(outPath.c_str(), "w");
        if (!f) {
            std::fprintf(stderr, "qc: error: cannot open '%s'\n", outPath.c_str());
            return 1;
        }
        codegen->emitAssembly(f);
        std::fclose(f);
        return diag.hasError() ? 1 : 0;
    }

    // 14. Object file output
    auto bytes = codegen->emitObject();

    outPath = (opts_.output.empty() || opts_.link)
                          ? deriveOutputPath(path, OutputKind::Object)
                          : opts_.output;

    if (!writeFile(outPath, bytes)) {
        return 1;
    }

    return diag.hasError() ? 1 : 0;
}

// ---------------------------------------------------------------------------
// driverMain — parse command-line arguments
// ---------------------------------------------------------------------------
static void printUsage(const char* prog) {
    std::fprintf(stderr,
        "Usage: %s [options] <input files>\n"
        "\n"
        "Options:\n"
        "  -o <file>        Write output to <file>\n"
        "  -I <dir>         Add <dir> to include search path\n"
        "  -L <dir>         Add <dir> to library search path\n"
        "  -l <lib>         Link with <lib>\n"
        "  -Wl,<arg>        Pass <arg> to the linker\n"
        "  -c               Compile only, do not link\n"
        "  -g               Generate debug information\n"
        "  -S               Output assembly instead of object code\n"
        "  -emit-ir         Output IR text (.ll)\n"
        "  -fsyntax-only    Only run syntax and semantic checks\n"
        "  -arch=x64        Target x86-64 (default)\n"
        "  -arch=arm64      Target AArch64\n"
        "  -melf            Use ELF object format (default)\n"
        "  -mpe             Use PE/COFF object format\n"
        "  -v               Verbose output\n"
        "  -dump-ir         Dump IR to stderr\n"
        "  -dump-ast        Dump AST to stderr\n"
        "  -x c             Force C mode\n"
        "  -x c++           Force C++ mode\n"
        "  --help           Show this help\n",
        prog);
}

int driverMain(int argc, char** argv) {
    CompileOptions opts;
#ifdef QC_DEFAULT_ARCH
    std::string defArch = QC_DEFAULT_ARCH;
    if (defArch == "arm64") opts.arch = TargetArch::ARM64;
    else opts.arch = TargetArch::X64;
#endif
#ifdef QC_DEFAULT_FORMAT
    std::string defFmt = QC_DEFAULT_FORMAT;
    if (defFmt == "pe") {
        opts.format = TargetFormat::PE;
        opts.os     = TargetOS::Windows;
    } else {
        opts.format = TargetFormat::ELF;
#ifdef __APPLE__
        opts.os     = TargetOS::MacOS;
#else
        opts.os     = TargetOS::Linux;
#endif
    }
#endif

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];

        if (arg == "--help" || arg == "-help") {
            printUsage(argv[0]);
            return 0;
        } else if (arg == "-o") {
            if (i + 1 >= argc) {
                std::fprintf(stderr, "qc: error: -o requires an argument\n");
                return 1;
            }
            opts.output = argv[++i];
        } else if (arg == "-I") {
            if (i + 1 >= argc) {
                std::fprintf(stderr, "qc: error: -I requires an argument\n");
                return 1;
            }
            opts.includePaths.push_back(argv[++i]);
        } else if (arg.size() > 2 && arg.substr(0, 2) == "-I") {
            opts.includePaths.push_back(arg.substr(2));
        } else if (arg == "-L") {
            if (i + 1 >= argc) {
                std::fprintf(stderr, "qc: error: -L requires an argument\n");
                return 1;
            }
            opts.libPaths.push_back(argv[++i]);
        } else if (arg.size() > 2 && arg.substr(0, 2) == "-L") {
            opts.libPaths.push_back(arg.substr(2));
        } else if (arg == "-l") {
            if (i + 1 >= argc) {
                std::fprintf(stderr, "qc: error: -l requires an argument\n");
                return 1;
            }
            opts.libs.push_back(argv[++i]);
        } else if (arg.size() > 2 && arg.substr(0, 2) == "-l") {
            opts.libs.push_back(arg.substr(2));
        } else if (arg.size() > 4 && arg.substr(0, 4) == "-Wl,") {
            opts.linkerArgs.push_back(arg.substr(4));
        } else if (arg == "-c") {
            opts.link = false;
        } else if (arg == "-g") {
            opts.debug = true;
        } else if (arg == "-S") {
            opts.outKind = OutputKind::Assembly;
            opts.link = false;
        } else if (arg == "-emit-ir" || arg == "--emit-ir") {
            opts.outKind = OutputKind::IR;
            opts.link = false;
        } else if (arg == "-fsyntax-only") {
            opts.outKind = OutputKind::CheckOnly;
            opts.link = false;
        } else if (arg == "-arch=x64" || arg == "-march=x86-64") {
            opts.arch = TargetArch::X64;
        } else if (arg == "-arch=arm64" || arg == "-march=aarch64") {
            opts.arch = TargetArch::ARM64;
        } else if (arg == "-melf") {
            opts.format = TargetFormat::ELF;
            if (opts.os == TargetOS::Windows) opts.os = TargetOS::Linux;
        } else if (arg == "-mpe") {
            opts.format = TargetFormat::PE;
            opts.os     = TargetOS::Windows;
        } else if (arg == "-v") {
            opts.verbose = true;
        } else if (arg == "-dump-ir") {
            opts.dumpIR = true;
        } else if (arg == "-dump-ast") {
            opts.dumpAST = true;
        } else if (arg == "-x") {
            if (i + 1 >= argc) {
                std::fprintf(stderr, "qc: error: -x requires an argument\n");
                return 1;
            }
            std::string lang = argv[++i];
            if (lang == "c") {
                opts.lang = InputLang::C;
            } else if (lang == "c++" || lang == "cxx" || lang == "cpp") {
                opts.lang = InputLang::CXX;
            } else {
                std::fprintf(stderr, "qc: warning: unknown language '%s'\n", lang.c_str());
            }
        } else if (arg.size() > 3 && arg.substr(0, 3) == "-x=") {
            // Support -x=c or -x=c++
            std::string lang = arg.substr(3);
            if (lang == "c") {
                opts.lang = InputLang::C;
            } else if (lang == "c++" || lang == "cxx" || lang == "cpp") {
                opts.lang = InputLang::CXX;
            } else {
                std::fprintf(stderr, "qc: warning: unknown language '%s'\n", lang.c_str());
            }
        } else if (arg.size() > 2 && arg[0] == '-' && arg[1] == 'O') {
            // Optimization level: -O0, -O1, -O2, -O3
            if (arg.size() == 3 && arg[2] >= '0' && arg[2] <= '3') {
                opts.optLevel = arg[2] - '0';
            }
        } else if (!arg.empty() && arg[0] != '-') {
            // Input file: check extension to see if it's a source file or a linker input
            auto dot = arg.rfind('.');
            bool isSource = false;
            if (dot != std::string::npos) {
                std::string ext = arg.substr(dot + 1);
                if (ext == "c" || ext == "cpp" || ext == "cxx" || ext == "cc" || ext == "i" || ext == "ii") {
                    isSource = true;
                }
            }
            if (isSource) {
                opts.inputs.push_back(arg);
            } else {
                opts.linkerInputs.push_back(arg);
            }
        } else {
            std::fprintf(stderr, "qc: warning: unrecognized option '%s'\n", arg.c_str());
        }
    }

    Driver driver(std::move(opts));
    return driver.run();
}

} // namespace qc
