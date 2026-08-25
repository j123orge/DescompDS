# Reference Architecture — NDS Emulation Reference

## Sources
- ndsrecomp: NDS recompilation engine
- melonDS: Open-source NDS emulator
- winds: NDS emulation framework

**Note**: These are conceptual references only. No code copied.

## Required Subsystems for NDS Emulation
| Subsystem | Status | Evidence | Priority |
|-----------|--------|----------|----------|
| CPU ARM9 | IMPLEMENTED | 90K ARM + 168K Thumb decoded | Core |
| CPU ARM7 | NOT IMPLEMENTED | No execution data | Phase 2+ |
| Memory Bus | PARTIAL | RAM+ITCM reads/writes observed | Core |
| CP15 | PARTIAL | Read/write observed | Core |
| ITCM | IMPLEMENTED | 32KB region tracked | Core |
| DTCM | PARTIAL | 16KB region observed | Core |
| DMA | OBSERVED | DMA reads/writes counted | Phase 2+ |
| Timers | OBSERVED | Timer reads/writes counted | Phase 2+ |
| IRQ | OBSERVED | IRQ acks counted | Phase 2+ |
| IPC | OBSERVED | IPC messages counted | Phase 2+ |
| VRAM | OBSERVED | VRAM reads/writes counted | Phase 2+ |
| GPU 2D | OBSERVED | GPU 2D reads/writes counted | Phase 3 |
| GPU 3D | OBSERVED | GPU 3D reads/writes counted | Phase 3 |
| Audio | OBSERVED | Audio reads/writes counted | Phase 3+ |
| Cartridge | OBSERVED | Cartridge reads/writes counted | Phase 2+ |
| Graphics Backend | NOT IMPLEMENTED | No rendering | Phase 3 |
| Scheduler | NOT IMPLEMENTED | No scheduling | Phase 2+ |

## NDS Memory Map
| Region | Address | Size | Usage |
|--------|---------|------|-------|
| BIOS9 | 0xFFFF0000 | 4 KB | ARM9 BIOS |
| BIOS7 | 0x00000000 | 4 KB | ARM7 BIOS |
| ITCM | 0x01000000 | 32 KB | Instruction TCM |
| DTCM | 0x027E0000 | 16 KB | Data TCM |
| Main RAM | 0x02000000 | 4 MB | Main memory |
| Shared WRAM | 0x03000000 | 32 KB | ARM9/ARM7 shared |
| IO/MMIO | 0x04000000 | 64 KB | Hardware registers |
| VRAM | 0x06000000 | 96 KB | Video memory |
| ROM | 0x08000000 | 32 MB | Game cartridge |

## Phase Progression
1. **Phase 1**: Static analysis (disassembly, functions, blocks) — COMPLETE
2. **Phase 1.5**: Validator and metrics — COMPLETE
3. **Phase 2**: Execution engine (interpreter, dynamic code) — COMPLETE
4. **Phase 2.5**: Bus model and memory — COMPLETE
5. **Phase 2.6**: Loop diagnosis — COMPLETE
6. **Phase 3**: Engine construction — IN PROGRESS
7. **Phase 3.2**: Subsystems and Explorer — IN PROGRESS

## Next Steps
1. **J4**: DSGraphics implementation (GPU registers)
2. **J5**: Execution verification and first frame
3. **Asset Discovery**: Identify game assets in ROM
4. **Game Objects**: Identify objects from asset data