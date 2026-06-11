cmake_minimum_required(VERSION 3.20)

set(QBIOS_SRCS 
    src/memory.c 
    src/string.c
)

set(QBIOS_OBJS "")

foreach(src IN LISTS QBIOS_SRCS)
    get_filename_component(src_name ${src} NAME_WE)
    set(obj_file "${CMAKE_CURRENT_BINARY_DIR}/${src_name}.o")
    list(APPEND QBIOS_OBJS ${obj_file})
    
    # Use the qc compiler to compile the source file
    add_custom_command(
        OUTPUT ${obj_file}
        COMMAND $<TARGET_FILE:qc> -c ${CMAKE_CURRENT_SOURCE_DIR}/${src} -I${CMAKE_CURRENT_SOURCE_DIR}/include -o ${obj_file}
        DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/${src} qc
        COMMENT "Compiling qbios/${src} with qc"
    )
endforeach()

set(QBIOS_LIB "${CMAKE_CURRENT_BINARY_DIR}/libqbios.a")

# Find llvm-ar if available, as Apple's ar might choke on ELF files
find_program(LLVM_AR NAMES llvm-ar)
if(LLVM_AR)
    set(ARCHIVE_CMD ${LLVM_AR})
else()
    set(ARCHIVE_CMD ${CMAKE_AR})
endif()

# Archive the object files
add_custom_command(
    OUTPUT ${QBIOS_LIB}
    COMMAND ${ARCHIVE_CMD} rcs ${QBIOS_LIB} ${QBIOS_OBJS}
    DEPENDS ${QBIOS_OBJS}
    COMMENT "Linking ${QBIOS_LIB}"
)

# Custom target to drive the compilation
add_custom_target(qbios_build ALL DEPENDS ${QBIOS_LIB})

# Expose qbios as an imported target so other CMake targets can depend on it
add_library(qbios STATIC IMPORTED GLOBAL)
set_target_properties(qbios PROPERTIES IMPORTED_LOCATION ${QBIOS_LIB})
set_target_properties(qbios PROPERTIES INTERFACE_INCLUDE_DIRECTORIES ${CMAKE_CURRENT_SOURCE_DIR}/include)
add_dependencies(qbios qbios_build)

# Install rules
install(FILES ${QBIOS_LIB} DESTINATION lib)
install(DIRECTORY include/qbios/ DESTINATION include/qbios)

# Tests (optional)
# NOTE: Disabled because 'qc' natively outputs ELF/PE objects. On macOS, the host
# linker expects Mach-O objects and will fail to link test_qbios natively.
# if(QC_BUILD_TESTS)
#     add_subdirectory(tests)
# endif()


