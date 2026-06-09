#pragma once
#include "common.h"
#include <string>
#include <vector>

namespace qc {

enum class InputLang { C, CXX, Auto };
enum class OutputKind { Object, Assembly, IR, CheckOnly };

struct CompileOptions {
    std::vector<std::string> inputs;
    std::vector<std::string> includePaths;
    std::vector<std::string> libPaths;       // -L
    std::vector<std::string> libs;           // -l
    std::vector<std::string> linkerArgs;     // -Wl,...
    std::vector<std::string> linkerInputs;   // .o, .a files
    std::string              output;          // -o
    InputLang                lang     = InputLang::Auto;
    OutputKind               outKind  = OutputKind::Object;
    TargetArch               arch     = TargetArch::X64;
    TargetFormat             format   = TargetFormat::ELF;
    TargetOS                 os       = TargetOS::Linux;
    bool                     verbose  = false;
    bool                     dumpIR   = false;
    bool                     dumpAST  = false;
    bool                     debug    = false; // -g
    bool                     link     = true;  // whether to run linker
    int                      optLevel = 0;
};

class Driver {
public:
    explicit Driver(CompileOptions opts);

    int run();

private:
    int compileFile(const std::string& path, std::string& outPath);
    int invokeLinker();

    CompileOptions opts_;
    DiagEngine     diag_;
    std::vector<std::string> tempFiles_;
};

int driverMain(int argc, char** argv);

} // namespace qc
