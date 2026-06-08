# stdqc/CMakeLists.txt

add_library(stdqc STATIC
    src/c/stdio.cpp
    src/c/stdlib.cpp
    src/cxx/new.cpp
    src/arch/syscall.cpp
)

target_include_directories(stdqc PUBLIC
    include/c
    include/cxx
)

# Standard Library
option(STDQC_HOST_TEST "Build stdqc for host testing" OFF)
if(STDQC_HOST_TEST)
    target_compile_definitions(stdqc PRIVATE STDQC_HOST_TEST)
endif()

# Standard C/C++ flags for the library itself
target_compile_features(stdqc PRIVATE cxx_std_17)
if(NOT MSVC)
    target_compile_options(stdqc PRIVATE -ffreestanding -fno-exceptions -fno-rtti)
endif()
