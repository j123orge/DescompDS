cmake_minimum_required(VERSION 3.16)
project(DescompDSExplorer LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Win32 GUI Explorer — zero external dependencies
add_executable(DescompDSExplorer
    gui_main.cpp
    bmd.cpp
    romfile.cpp
    d3d_view.cpp
)

# Standalone BMD diagnostic/validation CLI (Stage 1-2)
add_executable(bmd_diag bmd_diag.cpp bmd.cpp romfile.cpp)

# Geometry sanity diagnostic
add_executable(bmd_geom bmd_geom.cpp bmd.cpp romfile.cpp)

# Comprehensive BMD validation (per-DL, OBJ export, topology)
add_executable(bmd_validate bmd_validate.cpp romfile.cpp)

target_compile_definitions(DescompDSExplorer PRIVATE
    _CRT_SECURE_NO_WARNINGS
    UNICODE
    _UNICODE
)

# Ensure the UTF-8 source file is interpreted as UTF-8 (so wide string literals
# with non-ASCII chars like the em-dash "—" render correctly).
if(MSVC)
    target_compile_options(DescompDSExplorer PRIVATE /utf-8)
    target_compile_options(bmd_diag PRIVATE /utf-8)
    target_compile_options(bmd_geom PRIVATE /utf-8)
    target_compile_options(bmd_validate PRIVATE /utf-8)
endif()

if(WIN32)
    set_target_properties(DescompDSExplorer PROPERTIES
        WIN32_EXECUTABLE ON
    )
endif()

if(WIN32)
    target_link_libraries(DescompDSExplorer PRIVATE kernel32)
    target_link_libraries(DescompDSExplorer PRIVATE d3d11 dxgi d3dcompiler)
endif()