# DescompDS Fase 3.2 — Relatório Arquitetural (Runtime/Engine)

**Data:** 2026-08-23 | **Estado:** 88/90 testes (2 Validator pré-existentes intactos), Mario 1M insns `stop 0x020049E0 instruction limit`, ITCM 333k writes, C++ compila MSVC.

> Não implementar código nesta fase. Apenas relatório A-J.

---

### A) Arquitetura atual

```
ROM NDS -> NDSImage (header/FAT/FNT/overlay) -> ARMDecoder (90155) / ThumbDecoder (168k thumb válidos, não mergeado)
  -> BasicBlockBuilder (8482 blocks, is_branch fix has_rd) -> FunctionDiscoverer (386 funcs)
  -> IRTranslator (725) -> CppLifter (arm9_recompiled.cpp 4.4MB) -> descomp_tests (88/90)
  -> Runtime mínimo: CPUState {r[16],cpsr,spsr,thumb+sync} + MemoryBus flat+ITCM/DTCM/IO/BIOS/ROM + provenance 4K
     + CP15 stub (MRC c0 ID 0x41059461, MCR c1/c6/c9/c7) + nds_swi stub + deep_trace 5k + loop_analysis
  -> IRInterpreter single-thread 1M cap, call stack, branch, MRC/MCR via runtime, SWI não para
```

CLI `--emit-cpp --run --trace --max-insns --deep-trace`. Sem scheduler, sem DMA/IRQ/Timers/IPC/VRAM/2D/3D/SDL. Código copiado para ITCM via `LDR [r1],#4` post-indexed já corrigido (`writeback = !pre||W` e `compute base`).

Trace Mario: `0x02004800 BL 0x020049F0` -> CP15 init (20 MCR), `0x02004814 LDR` literal pools, `0x0200497C` memcpy 3 entradas `R1=02098FC0 R2=02098FD8` -> loop `0x0200498C-0x020049C8` (16 insns, 313 iterações/5k) `ADD R6,R4,R5; CMP; LDRMI/STRMI; BMI/BCC/BEQ`. Antes travava `R5=0` por `writeback=false`; agora `R1` avança, `itcm_writes 333k` para `0x00000000`, sai do loop e vai para `0x020049E0` (próximo loop, ainda `instruction limit`).

---

### B) Arquitetura proposta DescompDS Runtime/Engine

```
DescompDS
├── recompiler/         # Fase 1-2.6 intacto
│   ├── ARM9/ARM7 (ARMDecoder, ThumbDecoder)
│   ├── ir/ (IRTranslator, IRInterpreter com deep_trace)
│   └── lifter/ (CppLifter, CppEmitter + dummy stubs)
├── runtime/            # NOVO: DS Engine
│   ├── CPUState (banked? Fase 3.2 mantém simples)
│   ├── MemoryBus (GBATEK: Main 4M, ITCM 32K 00000000, DTCM 16K 027E0000, SharedWRAM 32K, ARM7WRAM 64K, IO 04000000 64K, BIOS9 4K, BIOS7 16K, ROM 32M, Palette, VRAM 9 bancos preparado)
│   ├── MMIO (dispatch 0x04000xxx -> IRQ/DMA/Timers/IPC/Cart)
│   ├── CP15 (control, MPU 8x, TCM, cache nop, bus_fast_refresh)
│   ├── IRQ (IME/IE/IF, raise/clear, pending cache)
│   ├── DMA (4ch x2, source/dest/len/ctrl, stall, IRQ)
│   ├── Timers (4x, TMCNT, overflow)
│   ├── IPC (IPCSYNC, FIFOs)
│   ├── Cartridge (ROMCTRL, KEY1, secure area, save)
│   ├── Scheduler (2 slots, 64 SYS ciclos, arm9x2, next_event = min(LCD,SPU,RTC) stub)
│   └── provenance (4K page gen, per-byte written)
├── graphics/
│   ├── DSGraphics (reset/write_register/write_command/submit_frame/present) - conceitos DS, não Unity
│   ├── 2D/3D abstração
│   └── backends/OpenGL (SDL janela 2 telas, framebuffer 256x192)
├── audio/ (stub)
├── input/ (stub)
└── host/SDL (janela, input)
```

Princípio: `GAME CODE (func_XXXXXXXX) -> nds_read32/write32/dma/irq/gpu (runtime) -> host (OpenGL/SDL)`. Troca `OpenGL->Vulkan` sem recompilar jogo. Preserva `recompiler/` e `CPUState` existentes, apenas estende `runtime/`.

### C) Diferenças vs ndsrecomp

| ndsrecomp | DescompDS proposto | Decisão |
|---|---|---|
| `recompiler/armv4t` + `finder/function_finder` 81k TOML `[[code_copy]]` + `reloc_scan` + superblocks + live validation | Mantém `BasicBlockBuilder/FunctionDiscoverer` simples, sem TOML por enquanto; `code_copy` adiado para overlays comprimidos | Não copiar TOML agora, evoluir quando overlay BLZ for gargalo |
| `runner/bus.cpp` 58k `resolve()+NdsBusFastWin` + `g_*_generation` | Já temos `resolve_region` + `g_main_generation` simplificado; falta `WRAMCNT` e `bus_fast_refresh` em CP15/WRAM | Adotar `bus_fast_refresh` + `WRAM` split, mas sem `libslirp` |
| `scheduler.cpp` 24k `switch_to` + `kIterCap 64` + `DMA/GXFIFO stall` | Implementar scheduler mínimo round-robin 64 ciclos, sem `GXFIFO` por enquanto | Evidência-guiado: só quando `IPCSYNC`/`DMA` aparecer no trace |
| `cp15.cpp` `set_mpu_region` 64-bit, `tcm_bytes` | Nosso `Cp15State` já tem `control/mpu[8]/dtcm/itcm`, falta `c2/c3` cachability | Manter stub cache, implementar MPU `c6` mínimo |
| `vram.cpp` 9 bancos OR-combine + `gpu2d.cpp` 89k + `gpu3d` compute | Fase 3.2 cria `DSGraphics` abstração vazia + `DSGraphicsOpenGL` que só apresenta framebuffer, sem 2D/3D | Não copiar VRAM banking ainda, só preparar `write_register` |

DescompDS não será `ndsrecomp` com outro nome; usará **conceitos** (provenance, fast win, 64 ciclos) adaptados ao nosso `CPUState` flat.

### D) O que aproveitar conceitualmente do W.I.N.D.S.

W.I.N.D.S. (DuffsDevice/winds) é **emulador interpretado** documentado, não recompiler. Conceitos úteis:
- `Memory.h` mapa GBATEK explícito com `ReadHandler/WriteHandler` por região - inspirar nosso `MemoryBus` dispatch sem copiar handlers.
- `CP15.h` e `TCM` com `enable` bits - já espelha nosso `Cp15State`.
- `GPU.h` 2D com `DISPCNT/BGCNT/BGOFS` por engine A/B - base para `DSGraphics::write_register` (registradores 0x04000000).
- `DMA.h` com `SAD/DAD/CNT` 4 canais - referência para `DMA` mínimo (não copiar struct).
- Documentação de `IPC` e `cartridge` com `KEY1` - validar nosso `Cart` futuro.
Usar como **checagem semântica GBATEK**, não como código-fonte.

### E) O que usar do melonDS como oráculo comportamental

melonDS 1.0rc `oracle/` TCP `127.0.0.1:19843` (`TCP.md`) com `regs/read_mem/framebuffer/insn_trace` + `find_first_diverge.py` por VBlank.
- Não linkar GPL no runner (isolamento como ndsrecomp faz).
- Criar `tools/oracle_snapshot.py` que captura `CPUState` + `mem 4K page` + `framebuffer` a cada `N` insns e compara com melonDS rodando mesmo ROM.
- Primeira divergência: `PC Descomp==0x020049E0` vs `Oracle PC` após `1M` - melonDS deve ter saído do loop para `0x02004XXX` e feito `SWI 0x0B` ou `DMA`.
- Usar `cyc@equal-retired-index` como ndsrecomp faz para calibrar, não apenas estado final.

Para Fase 3.2, **interface documentada** basta: `docs/oracle_interface.md` descrevendo `snapshot {pc,cpsr,r[16],mem[addr:val],framebuffer}` e `differential_report.json` com `first_divergence pc, expected vs actual, subsystem`.

### F) O que Mario realmente acessa no trace atual (evidência)

De `run_trace_summary.json` + `deep_trace.json` + `loop_analysis.json` + `bus` log:
- **CP15:** `MRC c0 0/1 ID/cache` 2 reads, `MCR c1/c6/c9/c7` 20 writes (`MCR c1 0x00050078` control ITCM/DTCM, `MCR c7 c5/c6` cache invalidate, `MCR c9` DTCM/ITCM base) - **necessário, já stubbed e funciona**.
- **Memory:** `RAM 187k reads`, `ITCM 333k writes` (cópia), `DTCM 0`, `Main 4M` ok, `SharedWRAM` ainda 0.
- **MMIO:** `IO 0/0`, `BIOS 0`, `ROM 0`, `Unmapped 4 reads 0xEB014F30` (literal pools fora do mapa, determinístico 0, logado). **Nenhum acesso a DMA 0x040000B0, timers 0x04000100, IRQ 0x04000200, IPC 0x04000180, cart 0x04100010, VRAM 0x06000000** no último 1M. Ou seja, **próximo gargalo não é DMA/IRQ ainda**, é o próprio loop de cópia que agora progrediu mas ainda atinge `instruction limit` por ser longo (333k ITCM writes / 4 = 83k iterações).
- **SWI:** 0 (nenhum `SWI 0x0B/0x05` ainda), **DMA:** 0, **IRQ:** 0.
- **Thumb:** 0 (ARM puro até aqui).
- **Código copiado:** `ITCM 0x00000000 32K` recebe `LDR [r1],#4` -> `STR [r4],#4` (post-indexado corrigido). Origem `0x02098FC0` (heap Main RAM, 3 entradas), destino `0x00000000` (ITCM) e `0x02093100` (heap). `FunctionDiscoverer` ainda só conhece `386` funcs ARM9 estáticas, **não conhece código em ITCM** - prioridade alta.

### G) Menor conjunto para primeiro frame visível

1. **ITCM execução dinâmica (P0):** Sem isso, código copiado para `0x00000000` nunca executa; `0x020049E0` é logo após cópia, próximo `BL` deve chamar `0x00000000`. Implementar `Tier-3` ou `discover_on_write`: quando `write` para `ITCM` com `has_write_provenance`, marcar página como `dirty` e permitir `FunctionDiscoverer` re-scan ou `IRInterpreter` fallback para `0x00000000` (já temos `deep_trace` e `Tier-3` conceito).
2. **SWI 0x0B CpuSet / 0x0C CpuFastSet (P0):** Mario usa para `memcpy` rápido após ITCM; nosso `nds_swi` atual só conta, não copia. Implementar `memcpy` real `R0=src,R1=dst,R2=len|mode` com `bus_read/write`.
3. **Timers/IRQ base (P1):** Para `VBlankIntrWait 0x05` e `HALT` (`MCR c7 c0,4`) que virão logo após cópia. `IME/IE/IF` stub + `HALT` com `scheduler` mínimo.
4. **DSGraphics stub (P1):** `write_register 0x04000000 DISPCNT` etc logado, `submit_frame` apresenta framebuffer preto 256x192 via `DSGraphicsOpenGL` + SDL janela 2 telas. Sem 2D/3D, só `present()`.

Sem VRAM banking, sem 3D, sem áudio por enquanto.

### H) Arquivos existentes a modificar (incremental)

- `tools/recompiler/include/runtime/nds_runtime.h` - adicionar `is_itcm/dctm/wram` helpers, `is_dma/timer/irq` range checks, `SubsystemAccess` contadores, `try_execute_itcm`
- `tools/recompiler/src/runtime/nds_runtime.cpp` - implementar `is_dma_range`, `handle_itcm_write` com `g_itcm_generation` e `discover_on_write` hook, `nds_swi` CpuSet memcpy real
- `tools/recompiler/src/ir/ir_interpreter.cpp` - `deep_trace` já, adicionar `try_execute_itcm` fallback quando `pc==0x00000000` e `has_write_provenance`
- `tools/recompiler/src/main.cpp` - `--max-insns` já, adicionar `--itcm-trace`, gerar `subsystem_access.json` a partir de `bus_stats` + `deep_trace`
- `tools/recompiler/src/analysis/function.cpp` - adicionar `discover_dynamic` para ITCM (chamado por `note_write_provenance`)
- `tools/recompiler/include/analysis/function.h` - declarar `discover_dynamic`
- `tests/phase3/test_phase3.cpp` - adicionar `ROM->ITCM` e `SWI CpuSet` testes (já temos `MemoryBusRegions`)
- `CMakeLists.txt` - nada

**Não modificar:** `validator.cpp`, `test_validator.cpp`, `arm_decoder.cpp` (exceto já corrigido `has_rd`), `lifter/`, `cfg` core.

### I) Novos arquivos a criar (pequenos, Fase 3.2)

- `tools/recompiler/include/runtime/subsystem_access.h` - contadores `dma/timer/irq/ipc/cart/vram` + `to_json`
- `tools/recompiler/include/graphics/ds_graphics.h` - `class DSGraphics { reset(); write_register(addr,val); write_command(fifo); submit_frame(); present(); }`
- `tools/recompiler/include/graphics/ds_graphics_opengl.h` - `class DSGraphicsOpenGL : DSGraphics` com SDL+GL
- `tools/recompiler/src/graphics/ds_graphics.cpp` - stub log `[DS-RUNTIME] MMIO WRITE 0x...`
- `tools/recompiler/src/graphics/ds_graphics_opengl.cpp` - SDL window 512x384 (2x192), GL context, `present()` com framebuffer preto
- `tools/recompiler/include/host/sdl_host.h` - `SDLHost` cria janela
- `docs/oracle_interface.md` - documenta snapshot `melonDS` TCP e `differential_report.json` formato
- `tools/oracle_snapshot.py` - script que roda melonDS headless e DescompDS e compara `first divergence`
- `output_mario/subsystem_access.json` - gerado por `main.cpp`
- `output_mario/differential_report.json` - gerado por `oracle_snapshot.py`

Nenhum `host/sdl` completo ainda, apenas `DSGraphicsOpenGL::present()` chamado a cada `submit_frame`.

### J) Ordem em pequenas etapas (evidência-guiada)

1. **ITCM execução dinâmica (1 dia)** - `note_write_provenance` para `0x00000000` seta flag `g_itcm_dirty`, `IRInterpreter::execute` tenta `try_execute_itcm` quando `pc < 0x8000` e `dirty`, teste `ROM->ITCM` e `execução posterior`.
2. **SWI CpuSet (0x0B/0x0C) real (0.5 dia)** - `nds_swi` faz `memcpy` via `bus_read/write` com `len = R2 & 0x1FFFFF`, `mode = R2>>26`, teste `SWI CpuSet` com 16 bytes.
3. **SubsystemAccess tracking (0.5 dia)** - `nds_runtime` contadores por range `0x040000B0/0x04000100/0x04000200/0x04000180/0x04100010/0x06000000`, `main.cpp` gera `subsystem_access.json`, `trace` já mostra `0` para DMA etc - prova que próximo não é DMA.
4. **Aumentar limite e medir (0.5 dia)** - `--max-insns 5000000` e 10M, coletar novo `hot_pc` (deve ser `0x020049E0` ou `0x00000000`), `loop_analysis` mostra saída para `0x00000000`.
5. **DSGraphics stub + SDL janela (1 dia)** - `DSGraphics::write_register` loga `DISPCNT 0x04000000`, `DSGraphicsOpenGL` cria janela, `main.cpp` chama `present()` a cada 1M, teste manual `SDL` abre 2 telas pretas.
6. **Oracle interface (0.5 dia)** - `docs/oracle_interface.md` + `differential_report.json` placeholder com `first_divergence` de `deep_trace` vs melonDS snapshot (manual primeiro).
7. **Validação** - `88/90` + 2 novos `ROM->ITCM` + `CpuSet`, C++ compila, Mario 5M deve sair de `0x020049E0` para `0x00000000` e mostrar primeiro `DISPCNT` write.

Estimativa Fase 3.2: 4-5 dias, sem reescrever Fases 1-3.1.

---

*Próximo passo: aguardar aprovação para implementar J1 (ITCM execução dinâmica).*
