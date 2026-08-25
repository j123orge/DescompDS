# DescompDS — Plano Engine/Runtime (Fase 3.2 → 3.11)

**Estado atual (não destruir):** 88/90 testes (2 Validator `test_validator.cpp:39/63` pré-existentes intactos), ARM9 90155 insns, Thumb 168k válidos, IRTranslator/CppLifter/CppEmitter, CPUState+MemoryBus (Main 4M 02000000, ITCM 32K 00000000, DTCM 16K 027E0000, IO/BIOS/ROM, provenance 4K, `writeback=!pre||W` fix), CP15 (ID 0x41059461, control, MPU, TCM), `nds_swi` stub, `deep_trace 5k` + `loop_analysis`, Mario 51→1M `stop 0x020049E0` com `itcm_writes 333k`, C++ `arm9_recompiled.cpp` compila MSVC `/std:c++20` 0 errors. **Não reescrever Fases 1-3.1.**

**Meta:** `ROM -> decoder -> IR -> C++ (GAME CODE) -> CPUState + NDS Runtime/Engine -> Memory/MMIO/IRQ/DMA/Timers/IPC/Cartridge/VRAM -> DSGraphics -> OpenGL/SDL -> janela -> Mario 64 DS`. Código recompilado vê `nds_read32/write32/dma/irq/gpu`, nunca `Windows/SDL`.

---

### 1) Arquitetura proposta (incremental, conceitual, sem copiar)

```
DescompDS/
├── recompiler/          # preservado
│   ├── arm/ (ARMDecoder, ThumbDecoder) + analysis/ (BasicBlock, Function, CFG, CodeData, Validator)
│   └── ir/lifter (IRTranslator, IRInterpreter, CppLifter/Emitter)
├── runtime/             # NOVO DS Engine (independente de recompiler)
│   ├── CPUState (r[16],cpsr/spsr,thumb+sync, modos futuro)
│   ├── MemoryBus (GBATEK: Main/ITCM/DTCM/SharedWRAM/ARM7WRAM/IO/BIOS/ROM/Palette/VRAM stub, resolve(), bus_fast_refresh(), provenance)
│   ├── MMIO (dispatch 0x04000xxx -> subhandlers)
│   ├── CP15 (control, MPU 8x, TCM, cache nop, high vectors)
│   ├── IRQ (IME/IE/IF, raise/clear, HALT)
│   ├── DMA (4ch x2, SAD/DAD/CNT, stall, IRQ) - stub primeiro
│   ├── Timers (4x TMCNT)
│   ├── IPC (IPCSYNC, FIFO 9to7/7to9)
│   ├── Cartridge (ROMCTRL/KEY1 secure area 0x4000-0x8000, save)
│   ├── Scheduler (2 slots, 64 SYS ciclos, arm9x2, next_event=min(LCD,SPU,RTC))
│   └── DynamicCode (Tier-3: detecta write ITCM/DTCM/RAM com has_write_provenance, recompila, atualiza CFG, executa)
├── graphics/
│   ├── DSGraphics (reset/write_register/write_command/submit_frame/present) - conceitos DS, não Unity
│   ├── 2D/3D abstração (DISPCNT/BGCNT/GXFIFO stub)
│   └── backends/OpenGL (DSGraphicsOpenGL: SDL janela 512x384, 2x 256x192, present preto)
├── audio/ (stub)
├── input/ (stub)
├── host/SDL (SDLHost)
└── assets/ (AssetDiscovery)
```

**Princípio:** `GAME CODE` chama `nds_*` (runtime). Backend `OpenGL->Vulkan` troca sem recompilar jogo. Referências: `ndsrecomp` (arquitetura recompilação/runtime), `W.I.N.D.S.` (mapa DS), `melonDS` (oráculo comportamento) - **conceitos, não código**.

---

### 2) O que já pode ser reutilizado vs o que precisa ser criado

**Reutilizável (não reescrever):**
- `arm_decoder.cpp` (com `has_rd` fix), `thumb_decoder.cpp`, `arm_instruction.h` (166), `basic_block.cpp` (8482 blocks com `is_branch` fix), `function.cpp` (386 funcs), `cfg.cpp`, `ir_translator.cpp` (725), `cpp_emitter.cpp` (dummy stubs), `nds_runtime.h/cpp` base (já tem `Region`, `BusStats`, `Cp15State`), `ir_interpreter` com `deep_trace`.

**Estender (modificar incremental):**
- `runtime/nds_runtime.h/cpp` - adicionar `DynamicCode` + `SubsystemAccess` + `is_dma/timer/irq` ranges
- `ir/ir_interpreter.h/cpp` - `try_execute_itcm` fallback, `deep_trace` já, adicionar `Tier-3` hook
- `analysis/function.cpp` - `discover_dynamic(addr,size,thumb)` para ITCM
- `main.cpp` - `--max-insns` já, adicionar `--itcm-trace`, gerar `subsystem_access.json`, chamar `DSGraphics`
- `CMakeLists.txt` - adicionar `graphics/`, `host/`, `runtime/subsystem`
- `tests/phase3/test_phase3.cpp` - já tem `ROM->ITCM` e `SWI CpuSet`, adicionar `DynamicCode` teste

**Criar novo (pequenos, Fase 3.2):**
- `include/runtime/dynamic_code.h` + `src/runtime/dynamic_code.cpp` (Tier-3, 200 linhas)
- `include/runtime/subsystem_access.h` (contadores DMA/Timer/IRQ/IPC/Cart/VRAM)
- `include/graphics/ds_graphics.h` (5 métodos)
- `src/graphics/ds_graphics.cpp` (log `[DS-RUNTIME] MMIO WRITE 0x04000000`)
- `include/graphics/ds_graphics_opengl.h` + `src/graphics/ds_graphics_opengl.cpp` (SDL+GL, 300 linhas)
- `include/host/sdl_host.h` + `src/host/sdl_host.cpp` (janela)
- `include/assets/asset_discovery.h` + `src/assets/asset_discovery.cpp` (Fase 3.4, não agora)
- `docs/oracle_interface.md` + `tools/oracle_snapshot.py` (já esboçado)
- `output_mario/subsystem_access.json`, `differential_report.json` (gerados)

---

### 3) Metadados além de C++ (preservar estrutura do jogo)

Gerar em `output_mario/` a cada `--emit-cpp`:
- `functions.json` (já: address, size, ARM/Thumb, blocks, callees/callers)
- `blocks.json` (novo: por função, succ/pred, is_exit)
- `callgraph.json` (novo: edge `from->to` com tipo `BL/B`)
- `references.json` (novo: `LDR [pc]` literal pools -> `data` addr, `STR` -> `data`, `BL` -> `func`)
- `data.json` (novo: `CodeDataClassifier` + `is_literal_target`, `region`)
- `overlays.json` (já parcial: `arm9_overlays()` id/src/dst/size)
- `assets.json` (Fase 3.4: `type: model/texture/palette/animation/sprite` com `ptr table 0x020XXXXX` e `size`)
- `memory_map.json` (novo: `Main/ITCM/DTCM/IO/BIOS/ROM` com `base/size/generation`)
- `symbols.json` (novo: quando Ghidra symbols disponíveis, `addr->name`)

Cada função: `address, size, thumb, callers, callees, blocks, refs, region, overlay, strings, assets[]`. Teste: `functions.json` já tem `386` com `thumb` flag; expandir sem quebrar.

### 4) Asset Discovery (Mario 64 DS, evidência, não invenção)

ROM `S.MARIO64DS` 8M, FAT `001DF14C`, FNT `001D4B64`, NitroFS `data/` com `model`, `texture`, `animation` já visível em `rom_info.json`. Fase 3.4 investigará:
- Ponteiros: tabelas em `0x02004AD8` literal pools (`LDR r0,[pc,#156]`) apontam para `0x02004XXX` data; seguir `references.json` para achar `model header` (ex: `NSBMD` magic `BMD0`).
- Formatos: `NSBMD` (model), `NSBTX` (texture), `NSBCA` (anim) - validar via `W.I.N.D.S.` `Model.cpp` sem copiar, apenas magic.
- Personagem Mario: buscar `mario` string em `data.json` + `xrefs` para `func_02XXXXXX` que carrega `0x02098FC0` tabela (já vista no loop `0x0200499C` com `R1=02098FC0`).
Criar `tools/asset_scanner.cpp` que lista `assets.json` com `ptr, size, type, refs[]` - milestone `Mario model` visual com `DSGraphics::present()` mostrando wireframe.

### 5) Dependências e riscos

- **Dependências:** Fase 3.2 depende de `deep_trace` já; 3.3 depende de `MemoryBus` + `CP15`; 3.4 depende de `references.json`; 3.5 depende de `DSGraphics` stub; 3.6 depende de `IRQ/Timers`; 3.7 depende de `Scheduler`.
- **Riscos:** `writeback` e `compute` já corrigidos (post-indexed), mas `halfword` e `block` ainda podem ter `writeback` incorreto -> mitigar com `test_decode` para `E4914004`; `Tier-3` pode criar `func_00000000` duplicado -> mitigar com `g_itcm_generation` check; `C++` labels dummy já mitiga `block_XXXXXXXX` indefinido; `Thumb` merge não fazer (mantido separado).
- **Testes:** Cada novo arquivo tem `test_*.cpp` com `TEST_CASE` e `descomp_tests` deve manter `88/90` + novos `ROM->ITCM`, `SWI CpuSet`, `DynamicCode` (+3 -> 91/93). `validator` não tocar.

### 6) Primeiro milestone visual (evidência-guiado)

**Mario 64 DS Engine Boot** (Fase 3.2-3.3):
1. `DSGraphicsOpenGL` cria `SDL_Window 512x384` (2x 256x192), `GL context`, `present()` com `framebuffer` preto + `DISPCNT` log.
2. `NDSRuntime` carrega ROM, `CPUState` `r13=023FFF00`, executa `func_02004800` até `itcm_writes 333k`.
3. `DynamicCode` detecta `write 0x00000000` e recompila `func_00000000`, `Scheduler` tenta `execute 0x00000000` e mostra `PC=00000000` no `deep_trace`.
4. `asset_scanner` lista `Mario model` em `assets.json` e `DSGraphics::submit_frame` desenha wireframe com `OpenGL` (primeiro visual real).

Critério: janela abre, `trace` mostra `PC 00000000` executado, `assets.json` contém `mario/model` com `refs [0x02004AD8]`, `descomp_tests 91/93`, C++ compila.

Estimativa: Fase 3.2 (1 semana), 3.3 (1 semana), 3.4 (1 semana), 3.5-3.6 (2 semanas) -> Engine Boot em 3 semanas.

---

*Próximo passo: aguardar aprovação para J1 (DynamicCode ITCM) - 1 dia, sem reescrever Fases 1-3.1.*
