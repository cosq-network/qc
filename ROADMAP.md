# qc Compiler Roadmap

This document outlines the planned development for the `qc` compiler and its associated libraries (`stdqc`, `qbios`).

## Phase 5: Advanced C++ Object Model (In Progress)
*   [x] Single Inheritance & VTable Layout
*   [x] Base Constructor Calls
*   [x] Pure Virtual Functions & Abstract Classes
*   [x] Basic Multiple Inheritance (VTable offsets)
*   [ ] **Virtual Destructors**: Implement correct destruction order and VTable entries for destructors.
*   [ ] **Thunks**: Implement `this` pointer adjustment thunks for multiple inheritance overrides.
*   [ ] **Explicit Base Initialization**: Support `: Base(args)` syntax in constructors.
*   [ ] **Dynamic Cast & RTTI**: Implement basic `typeid` and `dynamic_cast` support.

## Phase 6: Templates & Meta-programming
*   [ ] **Function Templates**: Basic instantiation and type deduction.
*   [ ] **Class Templates**: Support for template classes (essential for expanding `stdqc`).
*   [ ] **Template Specialization**: Partial and full specialization support.
*   [ ] **SFINAE**: Implement basic substitution failure patterns.

## Phase 7: Toolchain & Infrastructure
*   [ ] **Linker Integration**: Built-in support for invoking `ld.lld` or `link.exe` directly from `qc`.
*   [ ] **Standard Library Expansion (stdqc)**:
    *   Add `<map>` and `<unordered_map>`.
    *   Implement basic exception handling (freestanding-safe).
    *   Expand `string.h` and `stdio.h` implementations.
*   [ ] **Optimization Passes**:
    *   Constant Folding (IR level).
    *   Dead Code Elimination.
    *   Instruction Selection improvements.
*   [ ] **Language Conformance**: Track C++17/C++20 features (e.g., `auto`, lambdas, concepts).

## Phase 8: Kernel Development (qbios focus)
*   [ ] **Interrupt Handling**: Attributes for interrupt service routines.
*   [ ] **Architecture Support**: Expand x86-64 and ARM64 specific intrinsics.
*   [ ] **Memory Management**: Basic slab allocator or similar for kernel-space use.

## Completed Milestones
*   **Phase 1**: Basic C compiler (arithmetic, functions, pointers).
*   **Phase 2**: C11 features (`_Generic`, `static_assert`) and DWARF 4 debug info.
*   **Phase 3**: RAII (Constructors/Destructors) and scope management.
*   **Phase 4**: Basic Containers (`vector`, `string`) for `stdqc`.
*   **Phase 5 (Initial)**: Polymorphism, VTables, and Multiple Inheritance.
