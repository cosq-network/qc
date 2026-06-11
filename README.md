# qc - A Production-Ready C/C++ Compiler Toolchain

`qc` is a robust, lightweight C11 and C++ compiler designed for production use, written in C++17. Built from the ground up without relying on heavy external dependencies like LLVM or GCC, it features a hand-written recursive descent parser, rigorous custom semantic analysis, and a highly efficient lightweight IR. 

Our goal is to provide a fully independent, fast, and secure toolchain for modern C and C++ development. `qc` includes built-in code generators capable of natively emitting relocatable ELF and PE object files for ARM64 and x86-64 architectures, making it uniquely suited for environments that demand rapid compilation, custom backend deployment, and strict control over the build process.

## Vision and Production Readiness

While `qc` began as an exploratory prototype, it is actively being developed into a production-grade compiler capable of handling complex, mission-critical codebases. Our core tenets are:

*   **Zero-Dependency Toolchain**: Eliminating reliance on enormous legacy compiler infrastructures ensures `qc` remains exceptionally fast, auditable, and easy to integrate into specialized CI pipelines.
*   **Predictable Performance**: By utilizing a lightweight IR and streamlined code generation, compilation speeds are kept remarkably low, drastically reducing iteration time for large projects.
*   **Freestanding First**: Designed with low-level systems programming in mind, `qc` excels at compiling kernel code, embedded systems, and freestanding environments without assuming POSIX availability.
*   **Correctness and Conformance**: Continuous expansion of rigorous test suites to ensure standards-compliant C11 and modern C++ compilation, particularly around advanced RAII, object lifetimes, and memory models.

## Current State

`qc` has evolved into a highly capable C/C++ toolchain. It can securely compile complex C11 software and significant C++ object-oriented systems, including advanced polymorphism, exception handling primitives, and RAII. The toolchain is verified for stability on Linux and macOS (including native ARM64 syscall support), and is backed by an automated CI/CD pipeline ensuring production reliability.

**Key Features:**
*   **C11 Support**: Standard C constructs, `_Generic`, static assertions, and atomic-like patterns.
*   **Advanced C++ support**:
    *   Classes with RAII (Constructors/Destructors).
    *   Virtual Destructors and correct destruction order in hierarchies.
    *   Temporary Object Lifetimes (cleanups at full-expression boundaries).
    *   Copy and Move Semantics (Resource transfers).
    *   Single and Multiple Inheritance (Field and VTable layout).
    *   Dynamic Dispatch via VTables (Virtual and Overridden functions).
    *   Foundational Exception Handling (`try`/`catch` syntax and `invoke`/`landingpad` IR).
    *   Pure Virtual Functions and Abstract Class validation.
    *   Member and Global `operator new`/`delete` overloads.
*   **Toolchain & CI/CD**:
    *   **Multi-platform CodeGen**: Targets `aarch64` (ARM64) and `x86_64` (x64) with direct ELF (`.o`) and PE (`.obj`) emission.
    *   **Cross-platform Support**: Native assembly and syscall support for Linux and macOS.
    *   **Automated Packaging**: CPack integration for `.deb`, `.rpm`, `.dmg`, `.zip`, and `.exe` installers.
    *   **GitHub Actions**: Automated build, test, and release pipeline with semantic versioning.
*   **Standard Library (stdqc)**:
    *   Freestanding C library with core `string.h` and `memory.h` routines.
    *   C++ Standard Library headers: `<vector>`, `<string>`, `<utility>`, `<algorithm>`, and `<new>`.
*   **Kernel Library (qbios)**: A portable, freestanding library for low-level kernel development.
*   **Full Preprocessor**: Supports conditional compilation (`#if`, `#elif`, `defined()`), macro expansion, and include path management.
*   **Debug Information**: Generates DWARF 4 debug information for robust source-level debugging in GDB/LLDB.

**Current Limitations:**
*   **Templates**: Native support for C++ templates is currently under development.
*   **Standard Library**: `stdqc` is freestanding and does not provide complex userspace APIs (e.g., file I/O, networking).
*   **Linker Integration**: `qc` generates relocatable object files. A system linker (e.g., `ld.lld`, `gcc`, `link.exe`) is currently required for final executable generation, though native linking is planned for a future release.

## Building

Requires CMake 3.20+ and a C++17 compliant compiler.

### Build Commands (macOS/Linux)
```bash
mkdir build
cd build
cmake ..
make -j$(nproc) # or sysctl -n hw.ncpu on macOS
```

## Usage

```bash
Usage: ./qc [options] <input files>

Options:
  -o <file>        Write output to <file>
  -S               Output assembly instead of object code
  -emit-ir         Output IR text (.ll)
  -fsyntax-only    Only run syntax and semantic checks
  -arch=[x64|arm64] Target architecture (default: host)
  -melf            Use ELF object format (default on non-Windows)
  -mpe             Use PE/COFF object format (default on Windows)
  -I <dir>         Add directory to include search path
  -L <dir>         Add directory to library search path
  -l <lib>         Link against library
  -v               Verbose output
  -dump-ir         Dump IR to stderr
  -dump-ast        Dump AST to stderr
```

### Example: Compiling a C++ Polymorphic Class

Create `test.cpp`:
```cpp
extern "C" int puts(const char*);

struct Base {
    virtual void greet() = 0;
    virtual ~Base() {}
};

struct Derived : public Base {
    void greet() override { puts("Hello from qc!"); }
};

int main() {
    Base* b = new Derived();
    b->greet();
    delete b;
    return 0;
}
```

Compile and link using the `stdqc` library:
```bash
./qc test.cpp -I../stdqc/include/c -o test.o
ld.lld test.o ../build/stdqc/libstdqc.a -o test.exe
./test.exe
```