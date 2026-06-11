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
  src/format/dwarf.cpp
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
install(FILES LICENSE DESTINATION .)

# Standard Library
add_subdirectory(stdqc)
add_subdirectory(qbios)

# CPack Integration
set(CPACK_PACKAGE_NAME "qc-compiler")
set(CPACK_PACKAGE_VENDOR "COSQ Network Private Limited")
set(CPACK_PACKAGE_CONTACT "Benoy Bose <benoybose@cosq.net>")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "qc - A C11 and C++ Compiler Prototype")
set(CPACK_RESOURCE_FILE_LICENSE "${CMAKE_CURRENT_SOURCE_DIR}/LICENSE")
set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}")

# Binary generators
if(WIN32)
  set(CPACK_GENERATOR "ZIP;NSIS")
elseif(APPLE)
  set(CPACK_GENERATOR "TGZ;DragNDrop")
else()
  set(CPACK_GENERATOR "TGZ;DEB;RPM")
endif()

# Platform-specific settings
set(CPACK_DEBIAN_PACKAGE_MAINTAINER "${CPACK_PACKAGE_CONTACT}")
set(CPACK_RPM_PACKAGE_LICENSE "MIT")
set(CPACK_NSIS_DISPLAY_NAME "qc Compiler")
set(CPACK_NSIS_HELP_LINK "https://cosq.net")

include(CPack)


# Tests (optional)
option(QC_BUILD_TESTS "Build tests" OFF)
if(QC_BUILD_TESTS)
  enable_testing()
endif()
