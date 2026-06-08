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
    std::string              output;          // -o
    InputLang                lang     = InputLang::Auto;
    OutputKind               outKind  = OutputKind::Object;
    TargetArch               arch     = TargetArch::X64;
    TargetFormat             format   = TargetFormat::ELF;
    TargetOS                 os       = TargetOS::Linux;
    bool                     verbose  = false;
    bool                     dumpIR   = false;
    bool                     dumpAST  = false;
    int                      optLevel = 0;
};

class Driver {
public:
    explicit Driver(CompileOptions opts);

    int run();

private:
    int compileFile(const std::string& path);

    CompileOptions opts_;
    DiagEngine     diag_;
};

int driverMain(int argc, char** argv);

} // namespace qc
