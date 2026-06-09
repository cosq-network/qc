# CMakeLists.txt for qbios tests

add_executable(test_qbios 
    test_qbios.c 
    host_stubs.c
)

target_compile_options(test_qbios PRIVATE -fno-builtin)

# Link against the qbios library
target_link_libraries(test_qbios PRIVATE qbios)

# Include the headers from qbios
target_include_directories(test_qbios PRIVATE ../include)

# Add it to the test suite
add_test(NAME test_qbios COMMAND test_qbios)
