# DescompDS Handoff

## Repository
- **Local path**: `D:\DescompDS`
- **GitHub repository**: https://github.com/j123orge/DescompDS
- **Current branch**: `main`

## Current objective

The goal is to render a correctly textured SM64DSe model inside DescompDS Explorer:

```
.bmd (NitroROM)
  -> SM64DSe (export tool)
    -> .dae (COLLADA)
      -> Assimp 6.0.5 (import)
        -> NeutralMesh
          -> D3D11 (render)
```

The final goal is a correct textured model preview inside DescompDS Explorer's `select_asset(.bmd)` pipeline.

## Proven working

- **SM64DSe build**: SUCCESS (Harness compiles, exports .dae from .bmd)
- **BMD → DAE export**: SUCCESS (`face_demo_mario.dae` exported)
- **DAE visual validation**: SUCCESS (Stage A geometry renders all-white; textures load correctly)
- **Assimp 6.0.5 official import**: SUCCESS (via CMake `add_subdirectory`, official target `assimp::assimp`)
- **triangulation**: 1798/1798 triangles preserved
- **materials**: 21 submeshes, 21 materials, 21/21 textures loaded
- **NeutralMesh conversion**: WORKING (`assimp_loader.cpp` → `load_dae_to_neutral()`)
- **D3D11 textured rendering**: WORKING (`d3d_view.cpp` — `load_neutral_mesh()`, per-submesh texture binding, stb_image upload, WRAP sampler)
- **Stage A geometry render**: WORKING (`daerender.bmp` — all-white mesh confirms geometry is correct)
- **Stage B texture render**: RENDER COMPLETED (`dae_renderer_stageB.bmp`)
- **Cap displacement fix via PreTransformVertices**: VERIFIED (`dae_screenshot.exe` — cap correctly above face, skin below)

## Current failure

**Stage B final visual = INCORRECT** (pre-PreTransformVertices)

**Problem**:
- Mario cap is displaced/deformed across the face
- W texture appears on misplaced cap geometry
- Rest of face is largely recognizable/correct
- Root cause: Cap vertices are in **bone-local space**. Without applying bone offset matrices (`mOffsetMatrix`) or baking bone transforms via `aiProcess_PreTransformVertices`, cap geometry is severely misplaced.

**Fix applied**: Added `aiProcess_PreTransformVertices` flag to `assimp_loader.cpp`. This bakes all bone offset transforms into world-space vertex positions. After this fix, the cap appears correctly above the face and skin below. Verified via pixel analysis of `dae_renderer_stageB.bmp` (cap at y=175-353, face at y=354-496).

## Current hypothesis

- The correct approach for static model rendering is `aiProcess_PreTransformVertices` which bakes all bone transforms into world-space vertex positions at load time. This was validated by visual inspection — cap is correctly placed after this flag is added.
- The original conclusion "skinning not required" was INVALIDATED by actual visual inspection — bone transforms ARE required for correct geometry even though real-time skeletal animation is not.
- **PreTransformVertices is the verified fix.** The `cap_diag.exe` diagnostic confirms: 342 meshes → 21 meshes, 2647 → 1302 vertices, 0 bones remaining after PreTransformVertices, all 1798 faces preserved.

## Latest diagnostic output

### cap_diag.exe full output

```
=== CAP BONE/NODE TRANSFORM DIAGNOSIS ===

--- M_cap node ---
  FOUND NODE: 'M_cap' at depth 2
    local transform:
      (-0.500 0.866 0.000 0.000 | -0.866 -0.500 0.000 -2.500 | 0.000 0.000 1.000 0.000 | 0.000 0.000 0.000 1.000)

--- cap_1 node ---
  FOUND NODE: 'cap_1' at depth 3
    local transform:
      (-0.500 -0.866 0.000 0.112 | 0.866 -0.500 0.000 2.807 | 0.000 0.000 1.000 0.000 | 0.000 0.000 0.000 1.000)

--- MESH[0] BONE FORMULA TEST ---
mesh[0]: 12 verts, 10 faces, 1 bones
bone[0]: 'M_cap'
M_cap node local transform found: YES

Testing first 5 vertices with M_cap bone offset formula:
  v[0]: raw=(-2.438 0.328 -2.709) -> skinned=(8.854 -25.491 -29.259)
  v[1]: raw=(-2.404 0.779 -2.919) -> skinned=(14.801 -24.976 -28.843)
  v[2]: raw=(-1.538 0.502 -3.568) -> skinned=(15.814 -33.375 -18.454)
  v[3]: raw=(-1.586 0.051 -3.300) -> skinned=(9.513 -33.296 -19.034)
  v[4]: raw=(-0.549 0.380 -3.815) -> skinned=(16.028 -36.680 -6.589)
  skinned bounds: min=(8.854 -36.680 -29.259) max=(16.028 -24.976 -6.589)
  raw bounds:     min=(-2.438 -0.068 -3.815) max=(2.438 0.779 -2.709)

=== PreTransformVertices VISUAL TEST ===
PreTransformVertices loaded: YES
  meshes: 21
  vertices: 1302  faces: 1798  meshes with bones: 0

CONCLUSION:
  Cap vertices are in bone-local space
  Need: v_final = sum(w * mOffsetMatrix * v) per bone
  OR: use aiProcess_PreTransformVertices to bake into world space before rendering
```

### cap_diag diagnostic analysis

- **M_cap node**: depth 2 in scene hierarchy. Local transform has ~120° Z-rotation and Y-translation of -2.5 (in DAE units)
- **cap_1 node**: depth 3. Different transform applied on top of M_cap
- **mesh[0]** (M_cap bone): only 12 vertices, 10 faces — this is a tiny submesh, NOT the full cap mesh
- **Bone formula test**: `v_final = sum(w * mOffsetMatrix * v)` produces values like (8.854, -25.491, -29.259) — WAY outside raw bounds (-2.438 to 2.438). This shows `mOffsetMatrix` is NOT the correct approach when bones exist per-submesh and the offset matrices are for bone-space transforms.
- **PreTransformVertices**: 342 meshes → 21 meshes, 2647 → 1302 vertices, 1798 faces preserved, 0 bones remaining. This is the CORRECT approach — it bakes all bone transforms into world-space vertex positions.

## Important paths

### Source
- **SM64DSe source**: `D:\DescompDS\SM64DSeDeps\` (Harness C# project: `harness.cs`)
- **Harness executable**: `D:\DescompDS\harness.exe`
- **face_demo_mario.dae**: `D:\DescompDS\test_export\face_demo_mario.dae`
- **face_demo_mario.bones**: `D:\DescompDS\test_export\face_demo_mario.bones`
- **Exported PNG textures**: `D:\DescompDS\test_export\*.png` (21 textures)
- **face_demo_mario.obj**: `D:\DescompDS\test_export\face_demo_mario.obj` (OBJ export for comparison)

### Assimp 6.0.5
- **Source**: `D:\DescompDS\deps\assimp\assimp-6.0.5\`
- **Build**: `D:\DescompDS\tools\explorer\DescompDSExplorer\build\assimp_build\`
- **Headers**: `D:\DescompDS\deps\assimp\assimp-6.0.5\include\assimp\`
- **Static lib**: `D:\DescompDS\tools\explorer\DescompDSExplorer\build\assimp_build\lib\Release\assimp-vc145-mt.lib`
- **zlib**: `D:\DescompDS\tools\explorer\DescompDSExplorer\build\assimp_build\contrib\zlib\Release\zlibstatic.lib`

### NeutralMesh pipeline
- **assimp_loader.h**: `D:\DescompDS\tools\explorer\DescompDSExplorer\assimp_loader.h`
- **assimp_loader.cpp**: `D:\DescompDS\tools\explorer\DescompDSExplorer\assimp_loader.cpp`
- **neutral_mesh.h**: `D:\DescompDS\tools\explorer\DescompDSExplorer\neutral_mesh.h`

### D3D11 renderer
- **d3d_view.h**: `D:\DescompDS\tools\explorer\DescompDSExporter\d3d_view.h`
- **d3d_view.cpp**: `D:\DescompDS\tools\explorer\DescompDSExporter\d3d_view.cpp`
- **model_view.h**: `D:\DescompDS\tools\explorer\DescompDSExporter\model_view.h`
- **model_view.cpp**: `D:\DescompDS\tools\explorer\DescompDSExporter\model_view.cpp`

### Renderer/test programs
- **dae_renderer.cpp**: `D:\DescompDS\tools\explorer\DescompDSExplorer\dae_renderer.cpp`
- **dae_screenshot.cpp**: `D:\DescompDS\tools\explorer\DescompDSExplorer\dae_screenshot.cpp`
- **verify_neutral.cpp**: `D:\DescompDS\tools\explorer\DescompDSExplorer\verify_neutral.cpp`
- **check_colors.cpp**: `D:\DescompDS\tools\explorer\DescompDSExplorer\check_colors.cpp`
- **audit_materials.cpp**: `D:\DescompDS\tools\explorer\DescompDSExplorer\audit_materials.cpp`
- **cap_diag.cpp**: `D:\DescompDS\tools\explorer\DescompDSExplorer\cap_diag.cpp`

### Explorer executable
- **Build directory**: `D:\DescompDS\tools\explorer\DescompDSExplorer\build\Release\`
- **Executable**: `D:\DescompDS\tools\explorer\DescompDSExplorer\build\Release\DescompDSExplorer.exe`

### Diagnostic executables
- **cap_diag**: `D:\DescompDS\tools\explorer\DescompDSExplorer\build\Release\cap_diag.exe`
- **dae_screenshot**: `D:\DescompDS\tools\explorer\DescompDSExplorer\build\Release\dae_screenshot.exe`
- **dae_renderer**: `D:\DescompDS\tools\explorer\DescompDSExplorer\build\Release\dae_renderer.exe`
- **assimp_dae_test**: `D:\DescompDS\tools\explorer\DescompDSExplorer\build\Release\assimp_dae_test.exe`
- **assimp_test**: `D:\DescompDS\tools\explorer\DescompDSExplorer\build\Release\assimp_test.exe`
- **verify_neutral**: `D:\DescompDS\tools\explorer\DescompDSExplorer\build\Release\verify_neutral.exe`

### Screenshots
- **Stage A (geometry)**: `D:\DescompDS\test_export\daerender.bmp`
- **Stage B (textured, fixed)**: `D:\DescompDS\test_export\dae_renderer_stageB.bmp`

## Build commands

### Assimp 6.0.5 (one-time)
```powershell
cmake -G "Visual Studio 17 2022" -A x64 ^
  -DASSIMP_BUILD_TESTS=OFF ^
  -DASSIMP_BUILD_ASSIMP_TOOLS=OFF ^
  -DASSIMP_BUILD_SAMPLES=OFF ^
  -DASSIMP_NO_EXPORT=ON ^
  -DASSIMP_BUILD_ZLIB=ON ^
  -DBUILD_SHARED_LIBS=OFF ^
  -DASSIMP_INSTALL=OFF ^
  -DASSIMP_BUILD_M3D_IMPORTER=OFF ^
  -DASSIMP_BUILD_M3D_EXPORTER=OFF ^
  -DASSIMP_BUILD_USD_IMPORTER=OFF ^
  -DASSIMP_BUILD_VRML_IMPORTER=OFF ^
  -S "D:/DescompDS/deps/assimp/assimp-6.0.5" -B "D:/DescompDS/deps/assimp/build"
cmake --build "D:/DescompDS/deps/assimp/build" --config Release --parallel
```

### assimp_test / assimp_dae_test
```powershell
cmake --build "D:\DescompDS\tools\explorer\DescompDSExplorer\build" --config Release --target assimp_test assimp_dae_test
D:\DescompDS\tools\explorer\DescompDSExplorer\build\Release\assimp_test.exe
D:\DescompDS\tools\explorer\DescompDSExplorer\build\Release\assimp_dae_test.exe
```

### dae_renderer (Stage A)
```powershell
cmake --build "D:\DescompDS\tools\explorer\DescompDSExplorer\build" --config Release --target dae_renderer
D:\DescompDS\tools\explorer\DescompDSExplorer\build\Release\dae_renderer.exe
```

### dae_screenshot (Stage B, PreTransformVertices)
```powershell
cmake --build "D:\DescompDS\tools\explorer\DescompDSExplorer\build" --config Release --target dae_screenshot
D:\DescompDS\tools\explorer\DescompDSExplorer\build\Release\dae_screenshot.exe
```

### cap_diag
```powershell
cmake --build "D:\DescompDS\tools\explorer\DescompDSExplorer\build" --config Release --target cap_diag
D:\DescompDS\tools\explorer\DescompDSExplorer\build\Release\cap_diag.exe
```

### DescompDS Explorer (full GUI)
```powershell
cmake --build "D:\DescompDS\tools\explorer\DescompDSExplorer\build" --config Release --target DescompDSExplorer
D:\DescompDS\tools\explorer\DescompDSExplorer\build\Release\DescompDSExplorer.exe
```

### Rebuild all
```powershell
cmake --build "D:\DescompDS\tools\explorer\DescompDSExplorer\build" --config Release --parallel
```

## Dependencies

| Dependency | Version | Origin | Notes |
|---|---|---|---|
| Assimp | 6.0.5 | `D:\DescompDS\deps\assimp\assimp-6.0.5\` | Official CMake, static lib |
| stb_image | 1.x | Copied from Assimp contrib | `stb_image.h` + `stb_image_impl.cpp` |
| SM64DSe | latest | `D:\DescompDS\SM64DSeDeps\` | .NET Harness, exports .dae |
| .NET reference assemblies | 8.0 | Windows SDK | For harness.cs |
| MSVC / Visual Studio | 17 2022 | Visual Studio 18 Community | CMake generator |
| CMake | 3.28+ | Bundled with VS | Build configuration |
| OpenGL | — | System | Not used (D3D11 only) |

## Files created or modified

### Production code
| File | Status | Description |
|---|---|---|
| `tools/explorer/DescompDSExplorer/assimp_loader.h` | NEW | `load_dae_to_neutral()` declaration |
| `tools/explorer/DescompDSExplorer/assimp_loader.cpp` | NEW | Assimp → NeutralMesh adapter |
| `tools/explorer/DescompDSExporter/d3d_view.h` | MODIFIED | PVertexColor struct (position+color+UV), texture binding |
| `tools/explorer/DescompDSExporter/d3d_view.cpp` | MODIFIED | stb_image texture upload, WRAP sampler, per-submesh rendering |
| `tools/explorer/DescompDSExporter/model_view.h` | MODIFIED | `show_neutral()` standalone window support |
| `tools/explorer/DescompDSExporter/model_view.cpp` | MODIFIED | Added DAE renderer integration |
| `tools/explorer/DescompDSExplorer/CMakeLists.txt` | MODIFIED | Added assimp targets, stb_image_impl.cpp to dae_renderer/dae_screenshot, added stb_image_impl.cpp to DescompDSExplorer |
| `tools/explorer/DescompDSExplorer/neutral_mesh.h` | NEW | NeutralMesh data structure |
| `tools/explorer/DescompDSExporter/stb_image.h` | NEW | stb_image header (from Assimp contrib) |
| `tools/explorer/DescompDSExporter/stb_image_impl.cpp` | NEW | STB_IMAGE_IMPLEMENTATION |

### Diagnostic/test code
| File | Status | Description |
|---|---|---|
| `tools/explorer/DescompDSExplorer/assimp_test.cpp` | NEW | Basic Assimp validation |
| `tools/explorer/DescompDSExplorer/assimp_dae_test.cpp` | NEW | Comprehensive diagnostic |
| `tools/explorer/DescompDSExplorer/dae_renderer.cpp` | NEW | Stage A geometry render |
| `tools/explorer/DescompDSExplorer/dae_screenshot.cpp` | NEW | Stage B textured render |
| `tools/explorer/DescompDSExplorer/verify_neutral.cpp` | NEW | NeutralMesh verification |
| `tools/explorer/DescompDSExplorer/check_colors.cpp` | NEW | Vertex color analysis |
| `tools/explorer/DescompDSExporter/audit_materials.cpp` | NEW | Material/texture audit |
| `tools/explorer/DescompDSExporter/cap_diag.cpp` | NEW | Cap bone/node transform diagnosis |
| `tools/explorer/DescompDSExporter/bmp_compare.cpp` | NEW | BMP comparison utility |
| `tools/explorer/DescompDSExporter/bmp_diff2.cpp` | NEW | BMP diff utility |

### Third-party dependencies
| File | Status | Notes |
|---|---|---|
| `deps/assimp/assimp-6.0.5/` | NEW | Assimp 6.0.5 source tree |
| `tools/explorer/DescompDSExplorer/stb_image.h` | NEW | From Assimp contrib |
| `tools/explorer/DescompDSExporter/stb_image_impl.cpp` | NEW | STB_IMAGE_IMPLEMENTATION |

### Untracked files (not in git)
- `SM64DSeDeps/`, `harness.cs`, `harness.exe`, `test_export/`, `build.cmd`, `build_explorer.cmd`, etc.

## Architecture

### Existing flow

1. **`select_asset()`** — GUI button selects .bmd file. NOT yet validated for .dae/NeutralMesh integration.
2. **`try_neutral_pipeline()`** — Attempts Assimp → NeutralMesh → D3D11 render. Not yet connected to `select_asset`.
3. **`sm64dse_export_bmd()`** — Harness exports .bmd to .dae using SM64DSe.
4. **`assimp_loader.cpp`** — `load_dae_to_neutral()` reads .dae with Assimp using `aiProcess_Triangulate | aiProcess_JoinIdenticalVertices | aiProcess_PreTransformVertices`, outputs `NeutralMesh`.
5. **`NeutralMesh`** — Data structure with `vertices` (PVertexColor), `submeshes` (MeshSubset), `materials`, `texture_files`.
6. **`load_neutral_mesh()`** — D3D11 renderer that uploads vertices/indexes, binds textures per submesh, renders with unlit textured shader.

### `select_asset` integration status

**NOT VALIDATED**. The `dae_screenshot` and `dae_renderer` are standalone test programs. `select_asset(.bmd)` → `try_neutral_pipeline()` is NOT yet connected. The NeutralMesh pipeline works standalone but is not integrated into the Explorer GUI's asset selection flow.

## Do not revisit

These paths are closed/deprecated. They may remain in source but should NOT be treated as the preferred approach:

- **Old BMD direct renderer**: `bmd.cpp` / `bmd_v2` renderer debugging
- **tinyobjloader for SM64DSe Extended OBJ**: `tinyobj_impl.cpp` — only used for OBJ comparison, not for final rendering
- **Handwritten Assimp structs**: Replaced by official Assimp headers (`<assimp/Importer.hpp>` etc.)
- **Custom COLLADA parser**: Replaced by Assimp official import
- **PreTransformVertices was initially thought unnecessary**: Invalidated by visual inspection — bone transforms ARE required

## Next recommended task

**Determine the exact Assimp bone/bind-pose transform required for the displaced cap geometry** using the `cap_diag` output and official Assimp examples (`Examples/AssimpTest.cpp` in the Assimp source). The `cap_diag` diagnostic shows that `mOffsetMatrix * raw_vertex` produces values far outside raw bounds, indicating the offset matrix formula is being applied incorrectly or the bone hierarchy is not being traversed correctly.

After the bone/bind-pose transform is resolved:
1. **Repair `Assimp → NeutralMesh` static pose** — ensure bone transforms are correctly applied when `aiProcess_PreTransformVertices` is NOT used (i.e., manual bone skinning path)
2. **Integrate `select_asset(.bmd)`** — connect the NeutralMesh pipeline to the Explorer GUI's asset selection flow
3. **Only after visual correctness** — validate the final textured model preview

## Build verification

**Explorer**: Builds successfully (`DescompDSExplorer.exe` in `build\Release\`). Includes `assimp_loader.cpp` and `stb_image_impl.cpp`.

**Diagnostic executables**: All 9 executables build successfully:
- `assimp_test.exe`, `assimp_dae_test.exe`, `dae_renderer.exe`, `dae_screenshot.exe`, `cap_diag.exe`, `verify_neutral.exe`, `check_colors.exe`, `audit_materials.exe`, `DescompDSExplorer.exe`

**Latest working executables**: All in `D:\DescompDS\tools\explorer\DescompDSExplorer\build\Release\`

## Git safety

- **ROM tracked**: YES (`roms/Mario.nds` — modified, NOT staged)
- **ROM staged**: NO
- **Large build artifacts committed**: NO (Assimp build artifacts in `build\` are gitignored)
- **Commercial ROM upload**: NOT staged or committed
- `roms/Mario.nds` is in `.gitignore` as a tracked file — ensure `git rm --cached roms/Mario.nds` is considered if the user wants it fully removed from git history

## Notes for the next developer

1. The `dae_screenshot.exe` with `aiProcess_PreTransformVertices` produces the CORRECT visual output. Cap is above face, skin below.
2. `assimp_loader.cpp` already has `aiProcess_PreTransformVertices` — this is the working path.
3. The `cap_diag.exe` diagnostic shows the manual bone offset matrix formula is BROKEN (produces values outside bounds). This suggests either the offset matrix is transposed/inverted, or the bone hierarchy traversal is wrong. The PreTransformVertices path avoids this issue entirely.
4. The `dae_renderer` and `dae_screenshot` are standalone test programs — they need to be integrated into the Explorer GUI via `select_asset` / `try_neutral_pipeline`.
