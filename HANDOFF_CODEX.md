# DescompDS Codex Handoff

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
              -> Explorer preview
```

The final goal is a correct textured model preview inside DescompDS Explorer's `select_asset(.bmd)` pipeline.

## Proven

Explicitly proven:
- **SM64DSe build**: SUCCESS (Harness compiles, exports .dae from .bmd)
- **BMD → DAE export**: SUCCESS (`face_demo_mario.dae` exported)
- **exported DAE visual reference**: SUCCESS (OBJ and validation PNGs generated)
- **Assimp 6.0.5 official import**: SUCCESS (via CMake `add_subdirectory`, official target `assimp::assimp`)
- **triangulation**: 1798 faces → 1798 triangles preserved
- **materials**: 21 submeshes, 21 materials
- **textures**: 21/21 textures load correctly
- **D3D11 neutral geometry renders**: WORKING
- **textured D3D11 rendering works technically**: stb_image upload, per-submesh texture binding, WRAP sampler all functional
- **UV/material binding works**: UV range u=[-0.1172, 2.1172] v=[-0.1875, 2.0000], all 21 textures mapped to correct submeshes
- **Stage A geometry render**: WORKING (`daerender.bmp` — all-white mesh confirms geometry is correct)

## Not proven / current failure

**final textured visual = FAILED**

- **Mario cap displaced/deformed**
- **W texture appears on displaced cap geometry**
- **final Explorer integration not validated**
- `select_asset(.bmd)` → `try_neutral_pipeline()` is NOT connected

**Current cap visual bug is ALLOWED TO REMAIN.** Do not attempt to fix it.

## Current experiment

**PreTransformVertices enabled in `assimp_loader.cpp`**: YES (experimental workaround)
**visually validated**: NO — this is NOT yet fully visually validated
**status**: Experimental workaround applied. The `aiProcess_PreTransformVertices` flag was added to `load_dae_to_neutral()`. Pixel analysis of `dae_renderer_stageB.bmp` showed cap above face in one test, but formal visual validation of the current build is NOT confirmed. The cap visual bug is allowed to remain per handoff instructions.

**Do NOT claim MODEL VIEWER FOUNDATION = DONE.**
**Do NOT claim FINAL VISUAL = PROVEN.**

## Root cause investigation

All current evidence:
- Assimp reports bones for the model (342 meshes with bones)
- Cap meshes are in bone-local/bind space
- M_cap offset matrix observed: local transform with ~120° Z-rotation and Y-translation -2.5
- Previous "skinning not required" conclusion was INVALIDATED by actual visual inspection — bone transforms ARE required for correct geometry even though real-time skeletal animation is not
- `aiProcess_PreTransformVertices` diagnostic: 342 meshes → 21 meshes, 2647 vertices → 1302 vertices, 1798 faces preserved, bones → 0, bounds unchanged

**State clearly**:
- `assimp_loader.cpp` currently uses `aiProcess_PreTransformVertices` flag
- This is an experimental/current workaround, NOT a fully visually validated final architecture
- Visual validation of this change is NOT yet completed

## Diagnostic status

### cap_diag.cpp
- **Builds**: YES (`D:\DescompDS\tools\explorer\DescompDSExplorer\build\Release\cap_diag.exe`)
- **Runs**: YES

### cap_diag output (actual)
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

### Bone/node findings
- **M_cap node**: depth 2 in scene hierarchy. Local transform has ~120° Z-rotation and Y-translation -2.5 (DAE units)
- **cap_1 node**: depth 3. Different transform applied on top of M_cap
- **mesh[0]** (M_cap bone): only 12 vertices, 10 faces — tiny submesh, NOT the full cap mesh
- **Bone formula test**: `v_final = sum(w * mOffsetMatrix * v)` produces values like (8.854, -25.491, -29.259) — WAY outside raw bounds (-2.438 to 2.438). The `mOffsetMatrix` formula is NOT correct for per-submesh bones.
- **PreTransformVertices**: 342 meshes → 21 meshes, 2647 → 1302 vertices, 1798 faces preserved, 0 bones remaining. This bakes all bone transforms into world-space vertex positions.

## Build status

| Target | Builds | Executable path |
|--------|--------|-----------------|
| DescompDSExplorer | YES | `D:\DescompDS\tools\explorer\DescompDSExplorer\build\Release\DescompDSExplorer.exe` |
| cap_diag | YES | `D:\DescompDS\tools\explorer\DescompDSExplorer\build\Release\cap_diag.exe` |
| dae_screenshot | YES | `D:\DescompDS\tools\explorer\DescompDSExplorer\build\Release\dae_screenshot.exe` |
| dae_renderer | YES | `D:\DescompDS\tools\explorer\DescompDSExplorer\build\Release\dae_renderer.exe` |
| assimp_test | YES | `D:\DescompDS\tools\explorer\DescompDSExplorer\build\Release\assimp_test.exe` |
| assimp_dae_test | YES | `D:\DescompDS\tools\explorer\DescompDSExplorer\build\Release\assimp_dae_test.exe` |
| verify_neutral | YES | `D:\DescompDS\tools\explorer\DescompDSExplorer\build\Release\verify_neutral.exe` |
| check_colors | YES | `D:\DescompDS\tools\explorer\DescompDSExplorer\build\Release\check_colors.exe` |
| audit_materials | YES | `D:\DescompDS\tools\explorer\DescompDSExplorer\build\Release\audit_materials.exe` |

## Exact build commands

Configure (one-time):
```powershell
cmake -G "Visual Studio 17 2022" -A x64 ^
  -S "D:/DescompDS/tools/explorer/DescompDSExplorer" ^
  -B "D:/DescompDS/tools/explorer/DescompDSExplorer/build" ^
  -DCMAKE_BUILD_TYPE=Release
```

Build all:
```powershell
cmake --build "D:\DescompDS\tools\explorer\DescompDSExplorer\build" --config Release --parallel
```

Build specific target:
```powershell
cmake --build "D:\DescompDS\tools\explorer\DescompDSExplorer\build" --config Release --target <target_name>
```

Rebuild after source changes:
```powershell
cmake --build "D:\DescompDS\tools\explorer\DescompDSExplorer\build" --config Release --target DescompDSExplorer cap_diag
```

## Dependencies

| Dependency | Version | Origin | Notes |
|---|---|---|---|
| Assimp | 6.0.5 | `D:\DescompDS\deps\assimp\assimp-6.0.5\` | Official CMake, static lib, `add_subdirectory` |
| stb_image | 1.x | Copied from Assimp contrib (`stb_image.h` + `stb_image_impl.cpp`) | Explicit project dependency |
| SM64DSe | latest | `D:\DescompDS\SM64DSeDeps\` | .NET Harness, exports .dae from .bmd |
| .NET reference assemblies | 8.0 | Windows SDK | For harness.cs |
| MSVC / Visual Studio | 17 2022 | Visual Studio 18 Community | CMake generator |
| CMake | 3.16+ | Bundled with VS | Build configuration |
| OpenGL | — | System | Not used (D3D11 only) |

## Relevant files

### Production code (all in DescompDSExplorer)
| File | Description |
|---|---|
| `tools/explorer/DescompDSExplorer/assimp_loader.h` | `load_dae_to_neutral()` declaration |
| `tools/explorer/DescompDSExplorer/assimp_loader.cpp` | Assimp → NeutralMesh adapter (uses `aiProcess_PreTransformVertices`) |
| `tools/explorer/DescompDSExplorer/neutral_mesh.h` | NeutralMesh data structure |
| `tools/explorer/DescompDSExplorer/d3d_view.h` | D3D11 renderer header (PVertexColor struct) |
| `tools/explorer/DescompDSExplorer/d3d_view.cpp` | D3D11 renderer (stb_image, WRAP sampler, per-submesh) |
| `tools/explorer/DescompDSExplorer/model_view.h` | `show_neutral()` standalone window |
| `tools/explorer/DescompDSExplorer/model_view.cpp` | DAE renderer integration |
| `tools/explorer/DescompDSExplorer/gui_main.cpp` | Explorer GUI main |
| `tools/explorer/DescompDSExplorer/CMakeLists.txt` | CMake configuration (stb_image_impl.cpp added to DescompDSExplorer target) |

### Diagnostic/test code (all in DescompDSExplorer)
| File | Description |
|---|---|
| `tools/explorer/DescompDSExplorer/assimp_test.cpp` | Basic Assimp validation |
| `tools/explorer/DescompDSExplorer/assimp_dae_test.cpp` | Comprehensive diagnostic |
| `tools/explorer/DescompDSExplorer/dae_renderer.cpp` | Stage A geometry render |
| `tools/explorer/DescompDSExplorer/dae_screenshot.cpp` | Stage B textured render (uses load_dae_to_neutral) |
| `tools/explorer/DescompDSExplorer/verify_neutral.cpp` | NeutralMesh verification |
| `tools/explorer/DescompDSExplorer/check_colors.cpp` | Vertex color analysis |
| `tools/explorer/DescompDSExplorer/audit_materials.cpp` | Material/texture audit |
| `tools/explorer/DescompDSExplorer/cap_diag.cpp` | Cap bone/node transform diagnosis |
| `tools/explorer/DescompDSExporter/stb_image.h` | stb_image header (from Assimp contrib) |
| `tools/explorer/DescompDSExplorer/stb_image_impl.cpp` | STB_IMAGE_IMPLEMENTATION |

### External/reference files
| File | Description |
|---|---|
| `D:\DescompDS\test_export\face_demo_mario.dae` | Source DAE |
| `D:\DescompDS\test_export\*.png` | 21 material textures |
| `D:\DescompDS\test_export\daerender.bmp` | Stage A screenshot |
| `D:\DescompDS\test_export\dae_renderer_stageB.bmp` | Stage B screenshot |

### Third-party source
| File | Notes |
|---|---|
| `deps/assimp/assimp-6.0.5/` | Assimp 6.0.5 source tree (NOT committed to git) |
| `tools/explorer/DescompDSExporter/build/assimp_build/` | Assimp build artifacts (NOT committed, gitignored) |

## Known path inconsistency

**IMPORTANT**: All files currently live under `tools/explorer/DescompDSExplorer/`. The directory `tools/explorer/DescompDSExporter/` does NOT exist as a directory on disk. Previous handoff documents referenced both paths, causing confusion. The REAL current locations are:
- Source: `D:\DescompDS\tools\explorer\DescompDSExplorer\`
- Build: `D:\DescompDS\tools\explorer\DescompDSExplorer\build\`
- CMakeLists.txt: `D:\DescompDS\tools\explorer\DescompDSExplorer\CMakeLists.txt`

Do NOT move files to normalize paths during handoff.

## Closed approaches

Recorded as CLOSED / LEGACY:
- **Direct BMD renderer debugging**: old `bmd.cpp` / `bmd_v2` renderer path
- **tinyobjloader for SM64DSe Extended OBJ**: `tinyobj_impl.cpp` — only used for OBJ comparison
- **Handwritten Assimp structs**: Replaced by official Assimp headers (`<assimp/Importer.hpp>` etc.)
- **Custom COLLADA parser**: Replaced by Assimp official import
- **PreTransformVertices initially thought unnecessary**: Invalidated by visual inspection — bone transforms ARE required

## Next task for Codex

Do NOT execute it.

1. **Visually test the current PreTransformVertices-based loader** — run `dae_screenshot.exe` and inspect `dae_renderer_stageB.bmp`
2. **If cap is fixed**, validate full textured render
3. **If cap is not fixed**, derive correct bind/static transform using official Assimp bone/node data (`cap_diag.cpp` output, Assimp examples)
4. **Only after visual match**, integrate `select_asset(.bmd)` into Explorer GUI
5. **Test multiple BMD models**
6. **Then mark MODEL VIEWER FOUNDATION = DONE**
7. **Immediately return to code translation/decompilation pipeline**

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
- **tinyobjloader for SM64DSe Extended OBJ**: `tinyobj_impl.cpp` — only used for OBJ comparison
- **Handwritten Assimp structs**: Replaced by official Assimp headers
- **Custom COLLADA parser**: Replaced by Assimp official import

## Git safety

- **ROM tracked**: NO (`roms/Mario.nds` was deleted in upstream commit `1a366fb`)
- **ROM staged**: NO
- **Large build artifacts committed**: NO (Assimp build artifacts in `build\` are gitignored)
- **Commercial ROM upload**: NOT staged or committed

## Notes for the next developer

1. `assimp_loader.cpp` currently uses `aiProcess_PreTransformVertices` — this is an experimental workaround
2. Visual validation of this change is NOT yet confirmed — do not claim it works
3. `cap_diag.exe` shows the manual bone offset matrix formula produces values far outside raw bounds — the manual skinning path is BROKEN
4. `dae_screenshot.cpp` uses `load_dae_to_neutral()` which already has PreTransformVertices baked in
5. `dae_renderer` and `dae_screenshot` are standalone test programs — integration into Explorer GUI via `select_asset` / `try_neutral_pipeline` is NOT done
6. All source files are in `DescompDSExplorer/`, NOT `DescompDSExporter/`
7. The `DescompDSExporter/` directory does NOT exist on disk
