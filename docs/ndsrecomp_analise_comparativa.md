# Análise Comparativa: DescompDS vs ndsrecomp (mstan/ndsrecomp v0.0.1)

> **Objetivo:** Usar https://github.com/mstan/ndsrecomp como REFERÊNCIA TÉCNICA, sem cópia de código.
> **Estado atual DescompDS:** 90.155 instruções ARM9 decodificadas, 8856 BasicBlocks, 386 funções, 4,4 MB C++ gerado, execução recompilada para em 51 instruções no `0x02004810` (TEQ) por gap de CFG (`fall-through past last block`).
> **Data:** 2026-08-23 | **Autor:** Análise Muse Spark + subagentes `explore`

---

## 1. Sumário Executivo

| Dimensão | DescompDS | ndsrecomp | Gap crítico |
|---|---|---|---|
| **Filosofia** | Recompilador estático genérico (ROM → C++) + validação. Sem runner completo | Recompilador estático **TOML-driven** + **runner thin** dual-CPU + oráculo melonDS. Imutável → C, mutável → Tier-3 interpreter | DescompDS não tem runner; tenta executar via `IRInterpreter` RAM-only |
| **CPUs** | Só ARM9. ARM7 extraído mas não decodificado | ARM9 ARM946E-S (ARMv5TE) + ARM7 ARM7TDMI (ARMv4T) com `armv4t/` compartilhado (portado de `gbarecomp`) | Sem ARM7 não há boot de firmware nem IPC |
| **Memória** | `g_arm9_ram` 4 MB flat em `0x02000000`, resto retorna 0 / ignora write | Mapa GBATEK completo: Main RAM (mirror), Shared WRAM 32K (WRAMCNT 0-3), ARM7 WRAM 64K, ITCM/DTCM via CP15, VRAM 9 bancos (VRAMCNT), Palette/OAM, I/O `0x04000000`, WiFi `0x04800000`, BIOS `0xFFFF0000/0x00000000`, GBA slot open-bus | Sem mapa, qualquer MMIO trava execução real |
| **Decodificação** | ARM completo (~705 linhas), Thumb implementado mas **não usado** no pipeline `main.cpp:183` | ARM + Thumb integrados, modo por endereço, superblocks coalescendo fallthroughs <4K, validação live-bytes | Thumb precisa ser ligado para overlays e BIOS |
| **Lifting** | `IRTranslator` → `CppLifter` → `arm9_recompiled.cpp`. Boa cobertura ALU/Mem/Branch | `arm_decode.h` → `arm_ir.h` → `arm_codegen.h` → C com `runtime_dispatch`, `NdsDispatchEntry`, provenance | Conceito de **dispatch table + validação** ausente |
| **Validação** | `validator.cpp` + `code_data.cpp` heurístico | `NdsStaticValidation` por banco + `bus_live_bytes_equal` + `bus_range_has_write_provenance` por página 4K + oracle diferencial | Nossa validação não é executável |
| **Hardware** | CP15 stub (retorna `0x41059461`), resto stub | CP15 completo (MPU 8 regiões, TCM, cache, control), IRQ (IME/IE/IF), DMA 4ch com stall, Timers, IPC FIFO/SYNC, VRAM, 2D, 3D (Soft+Compute GL4.3), SPU, Cart (KEY1), Touch/SPI/RTC, WiFi (melonDS vendored) | Tudo ausente |
| **Execução** | `IRInterpreter` single-thread, `max 1M`, sem scheduler | `scheduler.cpp` dual-CPU event-aligned 64 ciclos SYS, ARM9×2, `run_slice` com HALT/DMA/GXFIFO | Sem scheduler não há sincronia ARM9/ARM7 |

**Conclusão:** DescompDS está sólido em **Fase 1-2.6** (decode → CFG → IR → C). Está em **Fase 3 embrionária** (interpreter RAM-only). Para sair de 51 instruções e rodar SM64DS é preciso **parar de estender o interpreter** e construir o **runner mínimo** inspirado no ndsrecomp, na ordem certa.

---

## 2. O que o DescompDS JÁ POSSUI (inventário verificado)

### 2.1 Decodificação ARM
- **Arquivo:** `tools/recompiler/src/arm/arm_decoder.cpp:1-705`, `arm_instruction.h:10-166`, `arm_registers.h`
- **Status:** ✅ Implementado
- **Cobre:** B/BL/BLX/BX (incl. `0xFA000000` BLX), LDR/STR/LDRB/STRB/LDRH/LDRSB/LDRSH/STRH, LDM/STM (PUSH/POP detect), MUL/MLA/UMULL/UMLAL/SMULL/SMLAL, CLZ, MRS/MSR, SWI/BKPT, MRC/MCR `0x0E000010`, NOP. Prioridade halfword > mul correta. `decode_buffer` memcpy loop.
- **Não cobre:** `SMLAxy/SMULWy/QADD/QSUB/CDP/LDC/STC` → cai em `UNKNOWN`/`DCD` (esperado, logado)

### 2.2 Decodificação Thumb
- **Arquivo:** `thumb_decoder.cpp:1-568`, `thumb_decoder.h:10-14`
- **Status:** ✅ Implementado, ⚠️ **Não usado** (`main.cpp:183` só chama `ARMDecoder::decode_buffer` em ARM9)
- **Cobre:** 19 formatos Thumb1 ARMv5TE, BL 32-bit `11110:11111`, `POP {pc}` como `is_return`, etc. Pronto para uso.

### 2.3 IR / Lifter
- **Arquivos:** `ir/ir_value.h`, `ir_instruction.h:13-79`, `ir_block.h`, `ir_translator.h/.cpp:1-725`, `lifter/cpp_lifter.cpp:1-68`, `cpp_emitter.cpp:1-799`
- **Status:** ✅ Implementado (~90% ALU/Mem/Control)
- **Destaques:** `LDM/STM` decomposto por registrador com `start_offset/delta` IA/IB/DA/DB, writeback `ADD/SUB SP`, `PC` via `LOAD+RETURN`, `MRC/MCR` com `[cp,crn,crm,op2]`, `CppEmitter` gera `func_XXXXXXXX(CPUState&)` com `goto block_XXXXXXXX`, `flag_n/z/c/v` sync, `nds_hal_*` wrappers, `t0..` temps.

### 2.4 Runtime CPUState (mínimo)
- **Arquivos:** `runtime/nds_runtime.h:10-60`, `nds_runtime.cpp:1-97`
- **Status:** ⚠️ Stub RAM-only
- **Possui:** `CPUState {r[16], cpsr=0x13, spsr, thumb, flag_n/z/c/v}`, `init_memory` (copia arm9.bin e expande para 4 MB), `read8/16/32/write8/16/32`, `nds_mrc` retorna `0x41059461` (ARM946E-S ID) / `0x0E04000A`, `nds_unimplemented` fault handler.
- **Não possui:** bancos, MPU, cache, TCM, IRQ, DMA, etc.

### 2.5 CFG / Funções / Validação
- **Arquivos:** `analysis/basic_block.cpp:39-188`, `function.cpp:59-192`, `cfg.cpp:39-184`, `validator.cpp:35-359`, `code_data.cpp:15-165`
- **Status:** ✅ Implementado (intra-procedural)
- **Possui:** Leaders (primeira + `known_entry_points` + alvos de branch + fallthrough `is_branch||is_comparison` — fix recente), sucessores (`conditional→target+fallthrough`, `B→target`, `BL→fallthrough`, `return→is_exit`), `FunctionDiscoverer` BFS a partir de `arm9_entry + overlays static_init_start` + todo `BL` target, `Cfg::export_to_dot`, `Phase1Validator` overlay-aware (103 overlays Mario), `CodeDataClassifier` heurístico (literal pools via `rn==PC`).

### 2.6 Pipeline CLI
- **Arquivo:** `main.cpp:1-400`
- **Status:** ✅ Implementado
- **Possui:** `--output`, `--extract` (rom_info.json/arm9.bin/arm7.bin/overlays), `--disassemble`, `--cfg` (`.dot`), `--emit-cpp` (arm9_recompiled.cpp + generated.h + runtime_stubs.h + per-function), `--run --trace --trace-limit 200`, `--all`. `--run` com `IRInterpreter` 1M cap, `r13=0x023FFF00`, `cpsr=0x1D`.

### 2.7 Overlays
- **Arquivos:** `nds/nds_overlay.h:11-52`, `nds_overlay.cpp:32-89`, `nds_image.h/.cpp:92-201`
- **Status:** ✅ Implementado, ⚠️ sem descompressão
- **Possui:** `RawOverlayEntry` 32B (ram_address/size, bss_size, static_init, file_id, flags `0x01000000` compressed), FAT lookup, `compressed` flag, `extract_to` escreve `overlay_XXX.bin`, entradas usadas como seeds. Validator decodifica cada overlay descomprimido (103 Mario)
- **Não possui:** Descompressão BLZ, remap MPU em runtime, linking inter-overlay.

---

## 3. O que o ndsrecomp POSSUI e nós AINDA NÃO TEMOS

### Por prioridade do enunciado (1-22)

| # | Subsistema | ndsrecomp (arquivos chave) | DescompDS | Gap |
|---|---|---|---|---|
| **1** | ARM9 ARM946E-S / ARMv5TE | `recompiler/armv4t/arm_decode.h/.cpp`, `cpu_state.h`, `runtime_arm.cpp` (CLZ/QADD/SMLA/SMUL todos, CP15) | Parcial (faltam SMLA/QADD, CP15 stub) | Pequeno |
| **2** | ARM7 ARMv4T/ARM7TDMI | `recompiler/armv4t/` compartilhado (12 arquivos), `biosnds7.toml` 145 entradas | ❌ Não decodificado (bin extraído mas ignorado) | **Crítico** |
| **3** | decoder instruções especiais | `MRS/MSR/SWI/BKPT/MRC/MCR` + `QADD/CLZ/SMLA` | Parcial (QADD→UNKNOWN) | Médio |
| **4** | static recompilation/lifting | `finder/function_finder.cpp` 81k + `config.h` TOML + `reloc_scan`, `arm_codegen.h` com `runtime_dispatch`, superblocks, live validation | `BasicBlockBuilder` simples, sem TOML, sem reloc scan, sem live validation | **Crítico** |
| **5** | runtime CPUState | `recompiler/armv4t/runtime_arm.h` `ArmCpuState g_cpu`, `NdsCpu`, banking `R8-12/SP/LR`, `pipeline_semantics.h` | `r[16]` flat, `cpsr/spsr`, sem banking, sem modo FIQ/IRQ | Médio |
| **6** | mapa memória DS | `runner/src/bus.cpp` 58k, `state.h` GBATEK completo + fast windows `NdsBusFastWin` | `0x02000000` flat 4 MB, resto `0`/drop | **Crítico** |
| **7** | MMIO / regs hardware | `runner/src/io.h/.cpp` 131k (`0x04000000`) | ❌ `// I/O, VRAM etc not yet implemented` | **Crítico** |
| **8** | CP15 / TCM / cache / MPU | `runner/src/cp15.cpp` 8k + `state.h:Cp15State` (control, MPU 8 regiões, TCM `512<<(reg>>1&1F)`, cache) | Stub (`MRC` const, `MCR` nop) | **Crítico p/ boot** |
| **9** | IRQ | `io.cpp` `IME/IE/IF`, `nds_raise_irq`, `nds_irq_pending_cache`, `scheduler.cpp` wake `IE&IF` (IME ignorado no HALT) | ❌ | Alto |
| **10** | DMA | `io.cpp` + `scheduler.cpp` 4ch/CPU, `Halted==2` debt, `nds_dma_run`, trace | ❌ | Alto |
| **11** | timers | `io.cpp` `TMCNT` ×4/CPU, `nds_tick_timers`, `nds_next_timer_overflow_time` | ❌ | Alto |
| **12** | IPC FIFO / IPCSYNC | `io.cpp` `IPCSYNC` cross-wired, `IPCFIFO cnt/send/recv` 2 dirs, `fifo9to7/fifo7to9` | ❌ | Alto (firmware precisa) |
| **13** | VRAM (9 bancos) | `runner/src/vram.h/.cpp` 19k, `nds_vram_map` MST/OFS/ENA, `nds_vram_renderer_palette`, flat `NdsVramRendererView` | ❌ | Médio (só p/ 2D/3D) |
| **14** | 2D | `runner/src/gpu2d.h/.cpp` 89k (2 engines, 4 BG+OBJ, DISPCNT/BGCNT, scanline, adaptive 256→448) | ❌ | Médio |
| **15** | 3D | `gpu3d.h/.cpp` 32k + `melonds_compute/` (SoftRenderer + Compute GL4.3, GXFIFO stall) | ❌ | Baixo p/ SM64DS inicial |
| **16** | áudio (SPU) | `runner/src/spu.h/.cpp` 21k (`nds_tick_spu` 1024 ciclos) | ❌ | Baixo |
| **17** | cartucho (Slot-1) | `io.cpp` `ROMCTRL/COMMAND/ROMDATA/AUXSPICNT`, `cart_backup.*`, KEY1 derivado de BIOS ARM7 em runtime, secure area `0x4000-0x8000` decrypt | Parcial (ROM lido, mas sem KEY1, sem ROMCTRL) | **Crítico p/ SM64DS** |
| **18** | código copiado para RAM | `tier3.h/.cpp` 23k + `live_overlay.*` + `bus.cpp` provenance per 4K página + per-byte `g_*_written[]` | ❌ (só RAM estática) | **Crítico** (SM64DS copia para ITCM/DTCM) |
| **19** | overlays | `bios/firmware_banks/` + `finder/config.h` `[[code_copy]]` + `reloc_scan` | Estático, sem `code_copy` alias, sem decompress | Alto |
| **20** | scheduler ARM9+ARM7 | `runner/src/scheduler.h/.cpp` 24k (64 SYS ciclos cap, ARM9×2, `switch_to`, `scheduler_run_round`, `next_scheduled_event_time` = min(LCD/SPU/RTC/WiFi)) | ❌ Single-thread interpreter | **Crítico** |
| **21** | SDL/input | `runner/src/frontend.h/.cpp` 107k + `main.cpp` 86k (SDL2, touch `nds_set_touch`, key `nds_set_key_mask`, `game.toml` display) | ❌ | Baixo |
| **22** | validação / differential testing | `oracle/` (melonDS 1.0rc TCP `127.0.0.1:19843`), `find_first_diverge.py` (bisect VBlank), fingerprint `{cycles,pc,cpsr,R0-15}` por retired index | `validator.cpp` estático (counts), sem oracle | Médio (mas essencial p/ calibrar) |

---

## 4. O que é REALMENTE NECESSÁRIO para executar Mario 64 DS

### 4.1 Caminho de boot SM64DS (observado no trace + ndsrecomp docs)

```
ARM9 BIOS (FFFF0000, 4K) ─┐
ARM7 BIOS (00000000, 16K) ─┤─► Firmware (256K, ae22de...) ─► ARM9 inicia em 0x02004800
Carteira SM64DS (ROM) ─────┘        │
                                  ▼
                          BL 0x020049F0 = init CP15/MPU/TCM/cache
                          MRC/MCR P15 (ID, cache type, DTCM/ITCM, MPU regiões)
                          copia código para ITCM 0x00000000 / DTCM 0x027E0000
                          DMA / timers / IPCSYNC para handshake ARM7
                          SWI 0x05 VBlankIntrWait / SWI 0x0B CpuSet / Div / Sqrt
                          cartucho: ROMCTRL + KEY1 secure area
                          → só depois: VRAM/2D/3D/audio
```

### 4.2 Classificação de necessidade

| Necessidade | Por quê SM64DS precisa AGORA | Pode esperar |
|---|---|---|
| **ESSENCIAL Fase 3.1** | CP15/MPU/TCM (sem isso `MRC/MCR` + `LDR 0x00000000` falha), mapa memória completo (ITCM/DTCM/WRAM/I/O), carregar BIOS se disponível ou FreeBIOS path, tratar `SWI` (pelo menos stub que não para), resolver gap CFG já feito, código copiado → Tier-3 ou validação live | |
| **ESSENCIAL Fase 3.2** | Scheduler ARM9+ARM7 mínimo (mesmo que round-robin simples sem ciclos precisos ainda), IRQ básico (IME/IE/IF + `SWI 0x05` → VBlank), IPC FIFO/SYNC (ARM9 espera ARM7), DMA ch3 (cart → RAM), Timers (VBlank), cartucho KEY1 + ROMCTRL | |
| **DESEJÁVEL mas não bloqueante p/ primeiros 100K insns** | VRAM banking, 2D scanline, 3D soft, SPU | Podem ser stub com retorno 0 inicialmente, mas logando acesso |
| **PODE ESPERAR** | WiFi / WFC / Wiimmfi, adaptive widescreen 21:9, overlay BLZ decompress, audio pacing, local wireless, melonds oracle, SDL apresentação | Pós-boot |

**Regra de ouro ndsrecomp:** *LLE floor sempre retido, HLE wrappers opt-in com `verify` diferencial.* Não fazer HLE precoce que esconda bug.

---

## 5. Conceitos do ndsrecomp que DEVEMOS ADAPTAR (sem copiar código)

### 5.1 Conceitos arquiteturais (prioridade máxima)

1. **Bancos imutáveis → C, mutável → Tier-3 (interpreter limitado)**
   - *ndsrecomp:* `generated/` banks C + `tier3.cpp` para RAM suja. Dispatch decide via `bus_range_has_write_provenance`.
   - *DescompDS:* Hoje só `IRInterpreter` em tudo. Adotar: `NdsDispatchEntry` + `bus_live_bytes_equal` → se página escrita, cai em interpreter com limite de ciclos, não recompila.

2. **Provenance por página 4K + por byte escrito**
   - *ndsrecomp:* `g_main_generation[1024]` (4 MB/4K), `g_main_written[4M]`, `generation++` skip 0, `note_ram_write` bump.
   - *DescompDS:* Adicionar em `nds_runtime.cpp` para decidir Tier-3 vs recompilado.

3. **Fast bus windows (`NdsBusFastWin`)**
   - *ndsrecomp:* `runtime_arm.h` `bus_read_u32` inline: se `addr in [base,base+size)` → `memcpy` + `note_write` senão `bus_*_slow`. `bus_fast_refresh()` em `MCR P15` e `WRAMCNT`.
   - *DescompDS:* Substituir `in_arm9_ram()` simples por janelas por região (Main, ITCM, DTCM, WRAM, VRAM...).

4. **Scheduler event-aligned 64 ciclos**
   - *ndsrecomp:* `scheduler_run_round()` com `kIterCap=64` SYS ciclos, `planned = min(sys+64, next_event)`, ARM9 alvo `planned<<1`, `nds_slice_begin/cap`, `nds_tick_timers`, `nds_gpu3d_run`, rendezvous `arm9>>1`, `next_scheduled_event_time()` = min(LCD 1584/2130, SPU 1024, RTC 33513982/32768, WiFi).
   - *DescompDS:* Hoje single-CPU. Adaptar mínimo: `Scheduler` com 2 slots, `switch_to(cpu)` salvando `g_cpu` + `deferred_cycles`, mesmo que inicialmente sem ciclos precisos (contar instruções).

5. **CP15 completo com MPU**
   - *ndsrecomp:* `cp15.cpp` `control` bits 13 high vectors, `tcm_bytes`, `set_mpu_region` 64-bit, `cp15_code/data_cacheable` por região.
   - *DescompDS:* Expandir `nds_mrc/mcr` para mapear `c1/c0,0` control, `c6` MPU, `c9,c1,0/1` DTCM/ITCM.

6. **TOML bank configs + `code_copy` alias**
   - *ndsrecomp:* `finder/config.h` `[[code_copy]] runtime_start=0x01FF8000 source_start=0x0200xxxx size=...`, `reloc_scan` sandbox.
   - *DescompDS:* Adotar `bios/*.toml` por banco (ARM9 BIOS `FFFF0000`, ARM7 BIOS `0`, firmware banks). Nosso `NDSImage` já parseia header/FAT, falta TOML.

7. **Live validation (byte-lock)**
   - *ndsrecomp:* `NdsDispatchEntry validation[dependency_count]` + `NdsStaticValidationRange [addr,size,hash]`, `bus_live_bytes_equal` valida bytes vivos, HLE `verify` diferencial.
   - *DescompDS:* `validator.cpp` hoje só conta. Evoluir para validação executável.

### 5.2 Conceitos de precisão (segunda onda)

8. **Ciclo por região + `arm9_cycle_combine(max(C+D-6, max(C,D)))`**
9. **DMA owns bus (Halted==2, debt carry)**
10. **Oráculo melonDS TCP + `find_first_diverge.py` bisect por VBlank + fingerprint por retired index** — ndsrecomp calibrou 13% drift ARM7, 75% undercharge ARM9 assim.
11. **VRAM OR-combine + flattened view null on overlap → fallback**
12. **GXFIFO stall como DMA**

---

## 6. Arquivos do DescompDS que DEVEM SER MODIFICADOS (por prioridade)

### Grupo A — Fase 3.1 (próximas 2-3 semanas, sem quebrar Fase 1-2)

| Arquivo | Alteração | Linhas afetadas |
|---|---|---|
| `tools/recompiler/include/runtime/nds_runtime.h` | Expandir `CPUState` (banking SPSR, modos), declarar `BusRegion`, `NdsBusFastWin`, `nds_busf_*`, `nds_cp15_*`, `nds_scheduler_*`, `nds_io_*` stubs | `10-60` |
| `tools/recompiler/src/runtime/nds_runtime.cpp` | Substituir flat 4 MB por mapa GBATEK (Main 4 MB mirroed, ITCM 32K, DTCM, WRAM 32K/64K, I/O, BIOS), adicionar `bus_fast_refresh`, provenance 4K, `nds_mrc/mcr` CP15 completo | `1-97` |
| `tools/recompiler/src/arm/arm_decoder.cpp` | Garantir `MRC/MCR` cp_op1, adicionar `QADD/SMLA` como UNKNOWN mapeado (já existe enum, só falta decode), confirmar `TEQ` já tem `is_comparison` | `595-621` |
| `tools/recompiler/include/arm/thumb_decoder.h` + `src/main.cpp:183` | **Ligar Thumb** no pipeline: detectar modo por endereço/CPSR.T, chamar `ThumbDecoder::decode_buffer` quando `thumb==true` | `main.cpp:174-184` |
| `tools/recompiler/src/analysis/basic_block.cpp` | *(já feito)* `is_branch \|\| is_comparison` — manter | `72` |
| `tools/recompiler/src/analysis/function.cpp` | Adicionar suporte a `[[code_copy]]` alias map (runtime ↔ source), tratar `Thumb` por bloco | `59-192` |
| `tools/recompiler/src/ir/ir_translator.cpp` | Adicionar `SWI` com `svc_handler` stub (não parar, só log), garantir `Thumb` flag propagada | `569-645` |
| `tools/recompiler/src/ir/ir_interpreter.cpp` | Remover `stopped` em `SWI` (chamar `nds_swi`), adicionar `HALT` handling, `MRC/MCR` via `nds_runtime` (não hard-coded) | `592-657` |
| `tools/recompiler/src/main.cpp` | Adicionar `--bios`, `--firmware`, `--freebios` flags, carregar BIOS se existir, `init_memory` por região, `scheduler` init, `IRTranslator` Thumb-aware | `51-400` |
| `tools/recompiler/CMakeLists.txt` | Adicionar `scheduler.cpp`, `bus.cpp`, `cp15.cpp`, `io.cpp` (novos) ao `RECOMPILER_SOURCES` | `1-66` |
| `bios/*.toml` (novos) | Criar `bios/biosnds9.toml`, `biosnds7.toml`, `firmware.toml` por banco (copiar estrutura ndsrecomp mas adaptar para DescompDS) | novos |

### Grupo B — Fase 3.2 (após 3.1 estabilizar)

| Arquivo | Alteração |
|---|---|
| `tools/recompiler/src/runtime/scheduler.cpp` (novo) | Dual-CPU scheduler mínimo |
| `tools/recompiler/src/runtime/bus.cpp` (novo) | `bus.cpp` GBATEK resolve + ring buffers |
| `tools/recompiler/src/runtime/cp15.cpp` (novo) | CP15/MPU/TCM |
| `tools/recompiler/src/runtime/io.cpp` (novo) | I/O `0x04000000` (IRQ/DMA/Timers/IPC) |
| `tools/recompiler/src/runtime/cart.cpp` (novo) | Cart KEY1 + ROMCTRL + save |
| `tools/recompiler/include/analysis/config.h` (novo) | TOML parser para bank configs |
| `tools/recompiler/src/analysis/reloc_scan.cpp` (novo) | Relocation scan sandbox |

### Grupo C — Não tocar agora

- `tools/recompiler/src/analysis/validator.cpp` — **NÃO alterar** os 2 testes pré-existentes (`ValidatesCleanCodeWithoutErrors` / `DetectsInvalidBranchTargets` falham por `RawNDSHeader` never copied, deixar como está)
- `tools/recompiler/src/lifter/cpp_emitter.cpp` — não reescrever, só adaptar quando bus/fast windows existirem

---

## 7. Ordem de Implementação por Prioridade (roadmap)

### Prioridade P0 — Desbloqueia execução além de 51 insns (1-2 semanas)
1. **Fix CFG já feito** (`basic_block.cpp:72` `is_comparison`) — ✅
2. **Ligar Thumb decode** em `main.cpp` (1 dia) — sem isso overlays Thumb não entram
3. **Mapa memória mínimo** em `nds_runtime.cpp`: ITCM `0x00000000` 32K, DTCM `0x027E0000` 16K, Main `0x02000000` 4 MB, I/O `0x04000000` stub logando, BIOS `0xFFFF0000`/`0x00000000` (carregar arquivo se existir)
4. **CP15 mínimo**: `MRC c0/c1` (ID/cache), `MCR c1/c0,0` control (high vectors), `c6` MPU nop, `c9,c1,0/1` DTCM/ITCM base/size → `bus_fast_refresh`
5. **SWI não parar**: `ir_interpreter.cpp:592` chamar `nds_swi` e continuar (stub `VBlankIntrWait` seta VBlank flag)

### Prioridade P1 — Faz boot chegar ao firmware (2-3 semanas)
6. **Bus `resolve()` + fast windows** (`bus.cpp` 500 linhas, inspirado mas não copiado)
7. **Scheduler mínimo** (`scheduler.cpp` 300 linhas): 2 slots, `switch_to`, `run_slice` round-robin, `next_event = min(timer, dma, lcd)` stub
8. **IRQ básico** (`io.cpp` 200 linhas): `IME/IE/IF`, `nds_raise_irq`, `scheduler` verifica `IE&IF`
9. **Timers** (`io.cpp`): 4× `TMCNT`, `nds_tick_timers`
10. **IPC FIFO/SYNC** (`io.cpp` 150 linhas): `IPCSYNC` 0x04000180, `IPCFIFO` 0x04000184/0x04000188

### Prioridade P2 — Faz SM64DS carregar (3-4 semanas)
11. **Cartucho** (`cart.cpp` 300 linhas): `ROMCTRL` 0x040001A0, `AUXSPICNT`, KEY1 (derivar de BIOS ARM7 em runtime, não hardcode), secure area decrypt, save EEPROM 8K default
12. **DMA** (`io.cpp` 300 linhas): 4ch, `nds_dma_run`, stall
13. **Código copiado → Tier-3** (`tier3.cpp` 200 linhas): provenance 4K, `bus_range_has_write_provenance` → interpreter limitado
14. **Overlays `code_copy` + BLZ decompress** (usar lz77 já em ndsrecomp `third_party/freebios` como referência)

### Prioridade P3 — Faz SM64DS jogável (pode esperar)
15. VRAM 9 bancos + 2D scanline (89k no ndsrecomp, pode ser stub inicialmente)
16. 3D SoftRenderer (32k)
17. SPU + SDL apresentação
18. Oracle melonDS + differential testing
19. WiFi / WFC (vendored melonDS, maior esforço)

---

## 8. Proposta Fase 3.1 Concreta: De 51 instruções para execução real

### 8.1 Meta

> **Sair de “executa 51 instruções e para no CFG gap” para “executa >10K instruções ARM9 do Mario 64 DS, atravessa CP15 init, cópia para ITCM, e alcança primeiro SWI/BIOS sem `fall-through past last block` nem `unknown IR`”.**

Não é ainda “jogo rodando”, é **execução real contínua** do caminho de boot ARM9.

### 8.2 Escopo (o que ENTRA e o que NÃO ENTRA)

| Entra (must) | Não entra (explicitamente fora) |
|---|---|
| Fix CFG `is_comparison` (já feito) | ARM7 (fica para 3.2) |
| Ligar Thumb decode | DMA completo |
| Mapa memória mínimo (5 regiões) | VRAM/2D/3D |
| CP15 mínimo (5 registradores) | SPU/Audio |
| SWI stub não-parante | WiFi/WFC |
| Bus fast windows + provenance | Oracle melonDS |
| Testes reproduzindo BL→TEQ→LDR | SDL/Input |
| Validação de `nds_runtime` | Overlays BLZ |

### 8.3 Tarefas Detalhadas (estimativa total 10-12 dias)

#### T1 — Finalizar CFG fix + testes (1 dia) — ✅ PARCIALMENTE FEITO
- [x] `basic_block.cpp:72` `is_branch || is_comparison`
- [x] Teste `BLFollowedByFallthroughComparision` em `tests/phase3/test_phase3.cpp:207-275`
- [ ] Adicionar teste `Thumb_Bl_FollowedByComparison` (Thumb `CMP` + fallthrough)
- [ ] Rodar `descomp_tests.exe` e garantir 81/83 (80 + novo) ou 82/83 se corrigir validator futuro — **não tocar nos 2 validator pré-existentes**
- **Arquivo:** `basic_block.cpp`, `tests/phase3/test_phase3.cpp`

#### T2 — Ligar Thumb no pipeline (1 dia)
- [ ] `main.cpp:174-184` detectar `is_thumb_mode` por `header.arm9_entry &1` ou por `overlay` `flags` (bit Thumb), branch para `ThumbDecoder::decode_buffer` quando `thumb==true`
- [ ] `FunctionDiscoverer::discover_functions` já trata `is_thumb_mode` por bloco (linha 137), só precisa receber `instructions` Thumb
- [ ] Teste: decodificar `arm7.bin` (ARM7TDMI) com Thumb e contar instruções
- [ ] Validar: `output/phase1_validation.json` deve mostrar `Thumb Instructions Decoded >0`
- **Arquivos:** `main.cpp`, `thumb_decoder.cpp` (nenhuma mudança), `function.cpp`

#### T3 — Mapa memória mínimo + Bus fast windows (3 dias)
- [ ] **a.** Definir `BusRegion` enum em `nds_runtime.h`: `MainRAM (02000000, 4M mirror), SharedWRAM (03000000, 32K), ARM7WRAM (03800000, 64K), ITCM (00000000, 32K), DTCM (027E0000, 16K), BIOS9 (FFFF0000, 4K), BIOS7 (00000000, 16K), IO (04000000, 64K), Palette (05000000), VRAM (06000000), Cart (08000000)`
- [ ] **b.** Implementar `bus_resolve(addr,len,cpu)` → `BusRegion*` com mirror (`addr & 0x003FFFFF` para Main)
- [ ] **c.** `NdsBusFastWin {base,size,ptr,generation}` por região, `bus_fast_refresh()` chamado em `MCR P15` e `WRAMCNT`
- [ ] **d.** `init_memory` por região: `g_main[4M]`, `g_itcm[32K]`, `g_dtcm[16K]`, `g_bios9[4K]`, `g_bios7[16K]`, `g_io[64K]` (logado), `g_wram[32K]`
- [ ] **e.** `read8/16/32` → `resolve()` → `memcpy` se região RAM, `0` + `log` se I/O/VRAM, `open-bus` se GBA slot vazio
- [ ] **f.** Provenance: `g_main_generation[1024]` (4M/4K), `g_main_written[4M] bool`, `bus_note_write(addr,len)` bump generation skip 0
- **Arquivos:** `runtime/nds_runtime.h:28-60`, `runtime/nds_runtime.cpp:1-97` (reescrever ~200 linhas, manter API `read32/write32` compatível)
- **Validação:** `--run --trace` deve mostrar `LDR 0x00000000` lendo ITCM não mais `0`, e `MRC P15` não mais `0`

#### T4 — CP15 mínimo (2 dias)
- [ ] **a.** `Cp15State {uint32_t control, dtcm_base, itcm_base, dtcm_size, itcm_size, mpu_region[8][2]}` em `nds_runtime.h`
- [ ] **b.** `nds_mrc(15,op1,crn,crm,op2)` expandir: `c0,0,0` ID `0x41059461`, `c0,0,1` cache type `0x0E04000A`, `c1,0,0` control, `c2,0,0/1` cachability, `c3,0,0` bufferability, `c5` perms, `c6` MPU `base = reg & ~(size-1)`, `c9,c1,0` DTCM, `c9,c1,1` ITCM, `c7,c0,4` HALT → `halted=true`
- [ ] **c.** `nds_mcr` simétrico, `apply_control` → `bus_fast_refresh()`, `tcm_bytes(reg)=512<<(reg>>1 &0x1F)`
- [ ] **d.** `ir_interpreter.cpp:610-657` delegar para `runtime::nds_mrc/mcr` (não duplicar switch)
- **Arquivos:** `runtime/nds_runtime.h:49-50`, `runtime/nds_runtime.cpp:74-88`, `ir/ir_interpreter.cpp:610-657`
- **Validação:** Trace deve mostrar `MCR P15,0,R0,C7,C5,0` (I-cache invalidate) sem `unknown`

#### T5 — SWI / BKPT não-parante (0,5 dia)
- [ ] `ir_interpreter.cpp:592-608` `SWI`/`BKPT`: chamar `runtime::nds_swi(id,cpu)` que loga e, para `SWI 0x05 VBlankIntrWait`, `SWI 0x0B CpuSet`, `SWI 0x06 Div`, `SWI 0x0D Sqrt`, retorna sem `stopped=true` (só `SWI` desconhecido para)
- [ ] `nds_runtime.h` declarar `nds_swi(uint32_t id, CPUState&)`, `nds_bkpt`
- **Arquivo:** `ir/ir_interpreter.cpp`, `runtime/nds_runtime.cpp:72-73`

#### T6 — Validação e métricas (0,5 dia)
- [ ] Rodar `nds_recompiler.exe D:\DescompDS\roms\Mario.nds --output D:\DescompDS\output_mario --run --trace --trace-limit 1000`
- [ ] Coletar: `instructions_executed`, `stop_address`, `stop_reason`, `R0-R3,SP,LR,CPSR` por 200 primeiras, `unknown IR`, `max PC thumb`
- [ ] Comparar `output/phase1_validation.json` antes/depois (BasicBlocks deve aumentar ~2-3% por `is_comparison`)
- [ ] Rodar `descomp_tests.exe` → 81/83 (80+1 novo) ou 82/83, 2 validator pré-existentes intactos

### 8.4 Critérios de Aceite Fase 3.1

| Critério | Antes | Depois (3.1) | Como verificar |
|---|---|---|---|
| Instruções executadas Mario.nds | 51 | **>10.000** (ideal >50K) | `--run --trace-limit 100000` |
| Stop reason | `fall-through past last block` 0x200480C | `SWI` / `HALT` / `indirect branch` / `instruction limit` (não mais fall-through) | `trace` log |
| Blocos criados após TEQ/CMP | 0 | ≥1 por `is_comparison` | `functions.json` + `arm9_disassembly.txt` |
| Thumb decodificado | 0 | >0 (ARM7 + overlays Thumb) | `phase1_validation.json` |
| Mapa memória | só Main RAM | 5 regiões + log I/O | `nds_runtime.cpp` |
| CP15 `MCR C7` | `unknown` ou `0` | `invalidate` sem parar | trace `MCR P15,0,R0,C7,...` |
| Testes | 80/82 | **81/83** (novo teste) + 2 validator intactos | `descomp_tests.exe` |
| C++ gerado compila | sim 4,4 MB | sim, + Thumb blocos | MSVC |

### 8.5 Fora do Escopo 3.1 (fica para 3.2)

- Scheduler dual-CPU, IRQ, DMA, Timers, IPC, VRAM/2D/3D, SPU, SDL, cart KEY1, Tier-3, overlay BLZ, oracle. Serão P1/P2 do roadmap §7.

### 8.6 Riscos e Mitigações

| Risco | Mitigação |
|---|---|
| `is_comparison` cria blocos demais (ruído) | Medir: `8856 → ~9000` (+~1,5% esperado), não explode. Se explodir, filtrar só `CMP/TST/TEQ/CMN` com `S` implícito |
| `nds_runtime` rewrite quebra `cpp_emitter` | Manter API `read32/write32` compatível, adicionar `bus_*` interno |
| Thumb ligado quebra ARM9 | Branch por `is_thumb` por endereço, não global |
| SWI stub esconde bug | Logar `SWI id` + `PC`, só `0x05/0x0B/0x06/0x0D` não param, resto ainda `stopped` |

---

## 9. Conclusão e Próximos Passos Imediatos

1. **Aprovar este relatório** (sem código copiado, só análise).
2. **Implementar Fase 3.1 T1-T6** na ordem acima, um por vez, com `descomp_tests.exe` verde a cada passo.
3. **Não reescrever** `validator.cpp` nem `cpp_emitter.cpp` agora.
4. **Após 3.1**, re-avaliar com novo trace (`>10K insns`) e decidir 3.2 (scheduler + IRQ).

**Arquivos a modificar em 3.1 (resumo):** `basic_block.cpp` ✅, `main.cpp`, `nds_runtime.h/.cpp`, `ir_interpreter.cpp`, `ir_translator.cpp` (mínimo), `tests/phase3/test_phase3.cpp` ✅, `bios/*.toml` (novos, opcional).

> **Princípio ndsrecomp adaptado:** *“LLE floor sempre retido, HLE wrappers opt-in com verify.”* Para DescompDS Fase 3.1, isso significa: **todo I/O/VRAM retorna 0 mas loga**, todo `SWI` loga mas não para (exceto desconhecido), todo `MCR` aceita mas chama `bus_fast_refresh`. Nada de HLE “mágico” que faça Mario parecer rodar sem estar correto.

---

*Fim do relatório. Próximo passo: aguardar aprovação para implementar Fase 3.1 T2 (ligar Thumb) — ou, se aprovado, executar T2-T6 sequencialmente.*
