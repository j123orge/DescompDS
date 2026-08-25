# DescompDS - Nintendo DS Static Recompiler (Fase 1)

**DescompDS** é um recompilador estático modular de ROMs de Nintendo DS (`.nds`) para C/C++ nativo para Windows (x86_64).

Nesta **Fase 1**, foi construída a fundação de análise estática de ROMs, decodificação de instruções de 32 bits (ARMv5TE) e 16 bits (Thumb-1), extração de componentes, particionamento de blocos básicos, descoberta de funções e geração de Grafos de Fluxo de Controle (CFG) com exportação Graphviz (.dot).

---

## 🚀 Pipeline da Fase 1

```text
ROM .nds
   ↓
NDS ROM Parser (Header, CRC16, Overlays, NitroFS)
   ↓
ARM9 / Overlays
   ↓
ARM32 + Thumb16 Decoder
   ↓
Basic Blocks (Líderes, Saltos Condicionais e Terminações)
   ↓
Function Discovery (Grafo de Chamadas, Caller/Callee, Detecção Indireta)
   ↓
Control Flow Graph (CFG) & Relatórios (.json, .txt, .dot)
```

---

## 📁 Estrutura do Projeto

```text
D:/DescompDS/
├── CMakeLists.txt              # Configuração CMake raiz
├── build.bat                   # Script automatizado de build e execução de testes
├── nds_recompiler.exe          # Executável CLI da ferramenta de análise e extração
├── descomp_tests.exe           # Suíte de testes unitários automatizados
│
├── tools/
│   └── recompiler/
│       ├── CMakeLists.txt
│       ├── include/
│       │   ├── nds/
│       │   │   ├── nds_header.h    # Estruturas do cabeçalho de 512 bytes e CRC16
│       │   │   ├── nds_rom.h       # Interface unificada da ROM
│       │   │   ├── nds_overlay.h   # Parser de tabela de overlays ARM9/ARM7 (y9.bin)
│       │   │   ├── nitrofs.h       # Parser do sistema de arquivos FNT/FAT
│       │   │   └── nds_image.h     # Leitura de imagem e extrator
│       │   ├── arm/
│       │   │   ├── arm_instruction.h  # Representação intermediária das instruções
│       │   │   ├── arm_decoder.h      # Decodificador ARMv5TE (32-bit)
│       │   │   ├── thumb_decoder.h    # Decodificador Thumb-1 (16-bit)
│       │   │   └── arm_registers.h    # Mapeamento de registradores R0-R15/CPSR
│       │   └── analysis/
│       │       ├── basic_block.h   # Particionamento de Blocos Básicos
│       │       ├── function.h      # Descoberta de funções e grafo de chamadas
│       │       └── cfg.h           # Grafo de Fluxo de Controle e exportador DOT
│       └── src/
│           ├── main.cpp            # Interface CLI
│           ├── nds/                # Implementação dos parsers de ROM
│           ├── arm/                # Implementação dos decodificadores ARM/Thumb
│           └── analysis/           # Implementação dos algoritmos de CFG
│
├── tests/
│   ├── test_framework.h        # Framework de testes unitários tipado
│   ├── test_runner.cpp         # Executor dos testes
│   ├── arm/test_arm_decoder.cpp
│   ├── thumb/test_thumb_decoder.cpp
│   ├── cfg/test_cfg.cpp
│   └── roms/
│       ├── test_nds_rom.cpp
│       └── generate_test_rom.cpp
│
└── docs/
```

---

## 🛠️ Como Compilar e Executar

### 1. Compilação e Execução dos Testes Unitários
Execute no terminal:
```powershell
.\build.bat
```
Ou compile diretamente com GCC (C++20):
```powershell
g++ -std=c++20 -O3 -static -I tools/recompiler/include -I tests tools/recompiler/src/nds/*.cpp tools/recompiler/src/arm/*.cpp tools/recompiler/src/analysis/*.cpp tests/test_runner.cpp tests/arm/*.cpp tests/thumb/*.cpp tests/cfg/*.cpp tests/roms/test_nds_rom.cpp -o descomp_tests.exe
.\descomp_tests.exe
```

### 2. Uso da CLI (`nds_recompiler.exe`)
```powershell
# Análise completa de uma ROM .nds:
.\nds_recompiler.exe jogo.nds --output ./saida_jogo

# Opções suportadas:
.\nds_recompiler.exe jogo.nds --extract       # Extrai arm9.bin, arm7.bin e overlays
.\nds_recompiler.exe jogo.nds --disassemble   # Gera arm9_disassembly.txt linear
.\nds_recompiler.exe jogo.nds --cfg           # Gera funções e grafos .dot
```

---

## 📦 Arquivos Gerados na Saída (`output/`)

* `rom_info.json`: Metadados completos do cabeçalho, tamanhos, CRCs e overlays.
* `arm9.bin`: Executável ARM9 extraído da ROM.
* `arm7.bin`: Executável ARM7 extraído da ROM.
* `overlays/overlay_XXX.bin`: Binários das sobreposições de código extraídas.
* `arm9_disassembly.txt`: Desmontagem formatada com endereços, hex e mnemônicos.
* `functions.json`: Lista de funções descobertas, tamanhos, blocos básicos, callers e callees.
* `cfg/function_XXXXXXXX.dot`: Grafos de fluxo de controle visualizáveis com Graphviz.
