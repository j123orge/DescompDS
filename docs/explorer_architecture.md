# Explorer Architecture

## Overview
DescompDS Explorer is a visual project map that answers: what functions exist, what calls what, what's executed, what's dynamic, what's missing, and what's the current blocker.

## Data Flow
```
recompiler --analyze --run --rom mario.nds
    → run_trace_summary.json   (metrics: instructions, memory, subsystems)
    → functions.json           (function metadata)
    → callgraph.json           (inter-procedural call edges)
    → deep_trace.json          (instruction-level execution trace)
    → loop_analysis.json       (hot loop detection)
    → dynamic_code_regions.json (dynamic code metadata)
    → subsystem_access.json    (subsystem observation counts)
    → project_map.json         (consolidated project state)
            ↓
    DescompDSExplorer.exe
            ↓
    Visual tabs with real data
```

## Tab System
| Tab | Data Source | Content |
|-----|-----------|---------|
| OVERVIEW | all JSONs | Metrics summary, subsystem observations, memory bus |
| CODE MAP | callgraph.json | Function list with filter, block counts, callees/callers |
| CALL GRAPH | callgraph.json | Edge table (from/to/type/executed/count) |
| EXECUTION | deep_trace.json, loop_analysis.json | Hot PC, instruction trace, branch prediction |
| MEMORY | run_trace_summary.json | Memory region table with read/write counts |
| DYNAMIC CODE | dynamic_code_regions.json | Region details: source/dest/size/gen/writes/exec |
| ENGINE | run_trace_summary.json | Subsystem status: IMPLEMENTED/PARTIAL/OBSERVED/NOT IMPLEMENTED |
| PROGRESS | all JSONs | Progress bars for functions/blocks/subsystems |
| BLOCKERS | run_trace_summary.json, dynamic_code_regions.json | Evidence-based blocker identification |
| ASSETS | (none yet) | Placeholder for asset discovery results |
| GAME OBJECTS | (none yet) | Placeholder for game object identification |

## Build
```bash
# 1. Build recompiler
cd tools/recompiler && mkdir build && cd build
cmake .. -G "Visual Studio 17 2022" -A x64
cmake --build . --config Release

# 2. Generate data
./descomp_recompiler.exe --analyze --run --rom mario.nds -o ../../output_mario

# 3. Build Explorer
cd tools/explorer/DescompDSExplorer
# Download Dear ImGui: https://github.com/ocornut/imgui
# Extract into imgui/ subdirectory
mkdir build && cd build
cmake .. -G "Visual Studio 17 2022" -A x64
cmake --build . --config Release

# 4. Run Explorer
./DescompDSExplorer.exe ../../output_mario
```

## Engine Status States
| State | Meaning |
|-------|---------|
| IMPLEMENTED | Fully working, evidence-backed |
| PARTIAL | Partially implemented, data incomplete |
| OBSERVED | Behavior seen in execution, no implementation |
| NOT OBSERVED | Behavior not seen in execution |
| NOT IMPLEMENTED | No code, no evidence |

## Key Principles
- **No fabricated percentages**: Explorer shows N/A/NOT AVAILABLE for missing data
- **Single source**: project_map.json links all data
- **Evidence-driven**: Engine status derived from real execution data
- **Real text rendering**: Dear ImGui provides actual text, not colored rectangles