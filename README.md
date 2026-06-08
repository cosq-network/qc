# qc - A C/C++ Compiler Prototype

`qc` is an experimental C (and partial C++) compiler written in C++17. It features a hand-written recursive descent parser, custom semantic analysis, and a lightweight IR. It includes built-in code generators capable of emitting relocatable ELF and PE object files for ARM64 and x86-64 architectures without relying on LLVM or other external backends.

## Current State

`qc` is a subset C compiler. It can compile basic C code (functions, arithmetic, pointers, structs).

**Features:**
*   Lexing and parsing of C11 and C++ keywords.
*   Basic semantic analysis and type checking.
*   Generation of a custom Intermediate Representation (IR).
*   Built-in CodeGen targeting `aarch64` (ARM64) and `x86_64` (x64).
*   Direct emission of ELF (`.o`) and PE (`.obj`) relocatable object files.

**Limitations:**
*   **No Linker**: It generates object files, not final executables. You must use a system linker (e.g., `ld`, `lld`, `gcc`, `clang`) to link the output.
*   **No Standard Library**: It does not provide `libc`.
*   **Partial C++ Support**: Can parse basic classes and namespaces but lacks inheritance, templates, and overloading resolution.

## Building

Requires CMake 3.20+ and a C++17 compliant compiler.

```bash
mkdir build
cd build
cmake ..
make -j$(sysctl -n hw.ncpu) # Or nproc on Linux
```

## Usage

```bash
Usage: ./qc [options] <input files>

Options:
  -o <file>        Write output to <file>
  -S               Output assembly instead of object code
  -emit-ir         Output IR text (.ll)
  -fsyntax-only    Only run syntax and semantic checks
  -arch=x64        Target x86-64 (default)
  -arch=arm64      Target AArch64
  -melf            Use ELF object format (default)
  -mpe             Use PE/COFF object format
  -v               Verbose output
  -dump-ir         Dump IR to stderr
  -dump-ast        Dump AST to stderr
  -x c             Force C mode
  -x c++           Force C++ mode
```

### Example

Create `hello.c`:
```c
int main() {
    return 42;
}
```

Compile to object file:
```bash
./qc hello.c -o hello.o -arch=arm64
```
