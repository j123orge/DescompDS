cmake_minimum_required(VERSION 3.20)
project(DescompDS VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Enable testing
enable_testing()

# Compiler options
if(MSVC)
    add_compile_options(/W4 /EHsc /utf-8)
else()
    add_compile_options(-Wall -Wextra -Wpedantic)
endif()

# Subdirectories
add_subdirectory(tools/recompiler)
add_subdirectory(tests)
