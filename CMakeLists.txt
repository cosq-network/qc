cmake_minimum_required(VERSION 3.20)
project(qc VERSION 0.1.0 LANGUAGES CXX C)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Build type
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

# Target architecture detection
if(NOT DEFINED QC_TARGET_ARCH)
  if(CMAKE_SYSTEM_PROCESSOR MATCHES "aarch64|arm64|ARM64")
    set(QC_TARGET_ARCH "arm64")
  else()
    set(QC_TARGET_ARCH "x64")
  endif()
endif()

# Output format detection
if(NOT DEFINED QC_TARGET_FORMAT)
  if(WIN32)
    set(QC_TARGET_FORMAT "pe")
  else()
    set(QC_TARGET_FORMAT "elf")
  endif()
endif()

message(STATUS "qc target arch:   ${QC_TARGET_ARCH}")
message(STATUS "qc target format: ${QC_TARGET_FORMAT}")

# Compiler flags
if(MSVC)
  add_compile_options(/W4 /WX- /permissive-)
  add_compile_definitions(_CRT_SECURE_NO_WARNINGS)
else()
  add_compile_options(-Wall -Wextra -Wno-unused-parameter)
endif()

# Sources
set(QC_SOURCES
  src/main.cpp
  src/common.cpp
  src/source.cpp
  src/lexer.cpp
  src/preprocessor.cpp
  src/parser.cpp
  src/type.cpp
  src/sema.cpp
  src/ir.cpp
  src/irgen.cpp
  src/driver.cpp
  src/codegen/x64_codegen.cpp
  src/codegen/arm64_codegen.cpp
  src/format/elf_writer.cpp
  src/format/pe_writer.cpp
)

add_executable(qc ${QC_SOURCES})

target_include_directories(qc PRIVATE include)

target_compile_definitions(qc PRIVATE
  QC_DEFAULT_ARCH="${QC_TARGET_ARCH}"
  QC_DEFAULT_FORMAT="${QC_TARGET_FORMAT}"
  QC_VERSION="${PROJECT_VERSION}"
)

# Install
install(TARGETS qc RUNTIME DESTINATION bin)

# Standard Library
add_subdirectory(stdqc)

# Tests (optional)
option(QC_BUILD_TESTS "Build tests" OFF)
if(QC_BUILD_TESTS)
  enable_testing()
  add_subdirectory(tests)
endif()
