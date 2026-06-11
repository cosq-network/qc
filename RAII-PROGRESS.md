# Phase 3: Constructors and Destructors (RAII) Progress

## Completed Work

### AST & Types
- Updated `VarDecl` in `include/qc/ast.h` to store:
    - `args`: List of expressions for constructor-style initialization.
    - `constructor`: Pointer to resolved `MethodInfo`.
    - `destructor`: Pointer to resolved `MethodInfo`.
- Updated `RecordType` in `include/qc/type.h` and `src/type.cpp`:
    - Added `findConstructor(argTypes)`: Simple matching by argument count.
    - Added `findDestructor()`: Finds the `~ClassName` method.

### Parser
- Updated `parseVarDecl` in `src/parser.cpp` to handle `Type name(arg1, arg2, ...);` syntax.
- *Issue Identified*: Ambiguity with function declarations is causing "expected ')'" errors in `tests/test_raii.cpp`.

### Semantic Analysis
- Updated `Sema::analyseVarDecl` in `src/sema.cpp`:
    - Resolves the constructor based on argument types.
    - Resolves the destructor for record types.
    - Emits errors if a required constructor is missing.

### IR Generation
- Updated `IRGen` in `include/qc/irgen.h` and `src/irgen.cpp`:
    - Added `Cleanup` structure and `cleanupStack_` (stack of vectors) for scope management.
    - Implemented `pushScope()`, `popScope()`, and `emitPopScope()`.
    - Updated `genFuncDecl`: Pushes initial scope and emits cleanups before `ret`.
    - Updated `genCompoundStmt`: Manages scope for `{}` blocks.
    - Updated `genReturnStmt`: Emits cleanups from *all* active scopes before returning.
    - Updated `genVarDecl`:
        - Generates call to the resolved constructor (passing `this`).
        - Registers the destructor in the current scope's cleanup list.

## Remaining Tasks
1. [x] **Fix Parser Ambiguity**: Resolved by implementing multi-token lookahead in `isStartOfParameterList` to distinguish between functional casts and parameter declarations.
2. [x] **Loop Cleanup**: Implemented `emitCleanupsToDepth` in `IRGen` to ensure `break` and `continue` correctly emit cleanups for scopes they exit.
3. [ ] **Array Initializers**: Support constructors/destructors for arrays of objects.
4. [ ] **Member Initializer Lists**: Support calling member constructors in class constructor definitions.

## Current Blockers
- None. All major parsing and basic RAII issues are resolved.
