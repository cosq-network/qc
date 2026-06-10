# qc - A C/C++ Compiler Prototype

`qc` is an experimental C11 and C++ compiler written in C++17. It features a hand-written recursive descent parser, custom semantic analysis, and a lightweight IR. It includes built-in code generators capable of emitting relocatable ELF and PE object files for ARM64 and x86-64 architectures without relying on LLVM or other external backends.

## Current State

`qc` has evolved from a basic C subset to a capable freestanding C/C++ toolchain. It can now compile complex C11 code and significant C++ object-oriented code, including polymorphism and RAII.

**Key Features:**
*   **C11 Support**: Standard C constructs, `_Generic`, static assertions, and atomic-like patterns.
*   **Advanced C++ support**:
    *   Classes with RAII (Constructors/Destructors).
    *   Single and Multiple Inheritance (Field and VTable layout).
    *   Dynamic Dispatch via VTables (Virtual and Overridden functions).
    *   Pure Virtual Functions and Abstract Class validation.
    *   Member and Global `operator new`/`delete` overloads.
*   **Standard Library (stdqc)**:
    *   Freestanding C library with core `string.h` and `memory.h` routines.
    *   C++ Standard Library headers: `<vector>`, `<string>`, `<utility>`, `<algorithm>`, and `<new>`.
*   **Kernel Library (qbios)**: A portable, freestanding library for low-level kernel development.
*   **Full Preprocessor**: Supports conditional compilation (`#if`, `#elif`, `defined()`), macro expansion, and include path management.
*   **Debug Information**: Generates DWARF 4 debug information for source-level debugging.
*   **Native CodeGen**: Targets `aarch64` (ARM64) and `x86_64` (x64) with direct ELF (`.o`) and PE (`.obj`) emission.

**Limitations:**
*   **Templates**: Lacks support for C++ templates.
*   **Standard Library**: `stdqc` is freestanding and does not provide complex userspace APIs (e.g., file I/O, networking).
*   **Linker**: Still generates relocatable object files. A system linker (e.g., `ld.lld`, `gcc`) is required for the final executable.

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
