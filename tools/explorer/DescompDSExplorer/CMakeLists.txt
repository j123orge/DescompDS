cmake_minimum_required(VERSION 3.16)
project(DescompDSExplorer LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ---- Assimp (import only, static, no tools/tests) ----
set(ASSIMP_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(ASSIMP_BUILD_ASSIMP_TOOLS OFF CACHE BOOL "" FORCE)
set(ASSIMP_BUILD_SAMPLES OFF CACHE BOOL "" FORCE)
set(ASSIMP_NO_EXPORT ON CACHE BOOL "" FORCE)
set(ASSIMP_BUILD_ZLIB ON CACHE BOOL "" FORCE)
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
set(ASSIMP_INSTALL OFF CACHE BOOL "" FORCE)
set(ASSIMP_BUILD_M3D_IMPORTER OFF CACHE BOOL "" FORCE)
set(ASSIMP_BUILD_M3D_EXPORTER OFF CACHE BOOL "" FORCE)
set(ASSIMP_BUILD_USD_IMPORTER OFF CACHE BOOL "" FORCE)
set(ASSIMP_BUILD_VRML_IMPORTER OFF CACHE BOOL "" FORCE)

add_subdirectory("D:/DescompDS/deps/assimp/assimp-6.0.5" "${CMAKE_BINARY_DIR}/assimp_build")

# ---- Win32 GUI Explorer ----
add_executable(DescompDSExplorer
    gui_main.cpp
    bmd.cpp
    romfile.cpp
    d3d_view.cpp
    model_view.cpp
    obj_loader.cpp
    tinyobj_impl.cpp
    stb_image_impl.cpp
    core/nitro/nitro_rom.cpp
    core/nitro/compression.cpp
    core/formats/bmd_v2.cpp
    core/formats/nitro_texture.cpp
)

# ---- Standalone tools ----
add_executable(bmd_diag bmd_diag.cpp bmd.cpp romfile.cpp)
add_executable(bmd_geom bmd_geom.cpp bmd.cpp romfile.cpp)
add_executable(bmd_validate bmd_validate.cpp romfile.cpp)

# ---- tinyobjloader validation ----
add_executable(tinyobj_test tinyobj_test.cpp)
target_compile_definitions(tinyobj_test PRIVATE TINYOBJLOADER_DISABLE_FAST_FLOAT)

# ---- OBJ filter utilities ----
add_executable(strip_vc strip_vc.cpp)
add_executable(filter_obj filter_obj.cpp)

# ---- Assimp DAE validation test (official CMake target) ----
add_executable(assimp_dae_test assimp_dae_test.cpp)
target_link_libraries(assimp_dae_test PRIVATE assimp::assimp)

add_executable(assimp_test assimp_test.cpp)
target_link_libraries(assimp_test PRIVATE assimp::assimp)

# ---- Hardcoded DAE renderer (Assimp → NeutralMesh → D3D11) ----
add_executable(dae_renderer
    dae_renderer.cpp
    assimp_loader.cpp
    model_view.cpp
    d3d_view.cpp
    stb_image_impl.cpp
)
target_compile_definitions(dae_renderer PRIVATE _CRT_SECURE_NO_WARNINGS)
target_include_directories(dae_renderer PRIVATE "D:/DescompDS/tools/explorer/DescompDSExplorer")
target_link_libraries(dae_renderer PRIVATE assimp::assimp kernel32 d3d11 dxgi d3dcompiler user32 gdi32)

# ---- DAE screenshot capture ----
add_executable(dae_screenshot
    dae_screenshot.cpp
    assimp_loader.cpp
    model_view.cpp
    d3d_view.cpp
    stb_image_impl.cpp
)
target_compile_definitions(dae_screenshot PRIVATE _CRT_SECURE_NO_WARNINGS)
target_include_directories(dae_screenshot PRIVATE "D:/DescompDS/tools/explorer/DescompDSExplorer")
target_link_libraries(dae_screenshot PRIVATE assimp::assimp kernel32 d3d11 dxgi d3dcompiler user32 gdi32)

# ---- NeutralMesh verification ----
add_executable(verify_neutral verify_neutral.cpp assimp_loader.cpp)
target_compile_definitions(verify_neutral PRIVATE _CRT_SECURE_NO_WARNINGS)
target_link_libraries(verify_neutral PRIVATE assimp::assimp)

add_executable(check_colors check_colors.cpp)
target_link_libraries(check_colors PRIVATE assimp::assimp)

add_executable(audit_materials audit_materials.cpp assimp_loader.cpp)
target_compile_definitions(audit_materials PRIVATE _CRT_SECURE_NO_WARNINGS)
target_link_libraries(audit_materials PRIVATE assimp::assimp)

add_executable(cap_diag cap_diag.cpp assimp_loader.cpp)
target_compile_definitions(cap_diag PRIVATE _CRT_SECURE_NO_WARNINGS)
target_include_directories(cap_diag PRIVATE "D:/DescompDS/tools/explorer/DescompDSExplorer")
target_link_libraries(cap_diag PRIVATE assimp::assimp)

# Add assimp_loader to Explorer for future integration
target_sources(DescompDSExplorer PRIVATE assimp_loader.cpp)
target_link_libraries(DescompDSExplorer PRIVATE assimp::assimp)

target_compile_definitions(DescompDSExplorer PRIVATE
    _CRT_SECURE_NO_WARNINGS
    UNICODE
    _UNICODE
    TINYOBJLOADER_DISABLE_FAST_FLOAT
)

if(MSVC)
    target_compile_options(DescompDSExplorer PRIVATE /utf-8)
    target_compile_options(bmd_diag PRIVATE /utf-8)
    target_compile_options(bmd_geom PRIVATE /utf-8)
    target_compile_options(bmd_validate PRIVATE /utf-8)
endif()

if(WIN32)
    set_target_properties(DescompDSExplorer PROPERTIES WIN32_EXECUTABLE ON)
    target_link_libraries(DescompDSExplorer PRIVATE kernel32 d3d11 dxgi d3dcompiler)
endif()
