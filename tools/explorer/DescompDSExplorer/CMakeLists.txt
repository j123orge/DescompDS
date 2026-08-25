cmake_minimum_required(VERSION 3.16)
project(DescompDSExplorer LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Console Explorer — zero external dependencies
add_executable(DescompDSExplorer main.cpp)

target_compile_definitions(DescompDSExplorer PRIVATE
    _CRT_SECURE_NO_WARNINGS
)

if(WIN32)
    target_link_libraries(DescompDSExplorer PRIVATE kernel32)
endif()