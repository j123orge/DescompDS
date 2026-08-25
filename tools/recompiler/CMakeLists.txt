set(RECOMPILER_HEADERS
    include/nds/nds_header.h
    include/nds/nds_rom.h
    include/nds/nds_overlay.h
    include/nds/nitrofs.h
    include/nds/nds_image.h
    include/arm/arm_instruction.h
    include/arm/arm_decoder.h
    include/arm/thumb_decoder.h
    include/arm/arm_registers.h
    include/analysis/basic_block.h
    include/analysis/function.h
    include/analysis/cfg.h
    include/analysis/validator.h
    include/analysis/code_data.h
    include/runtime/nds_runtime.h
    include/runtime/subsystem_access.h
    include/runtime/dynamic_code.h
    include/ir/ir_value.h
    include/ir/ir_instruction.h
    include/ir/ir_block.h
    include/ir/ir_translator.h
    include/lifter/cpp_emitter.h
    include/lifter/cpp_lifter.h
)

set(RECOMPILER_SOURCES
    src/nds/nds_header.cpp
    src/nds/nds_rom.cpp
    src/nds/nds_overlay.cpp
    src/nds/nitrofs.cpp
    src/nds/nds_image.cpp
    src/arm/arm_registers.cpp
    src/arm/arm_instruction.cpp
    src/arm/arm_decoder.cpp
    src/arm/thumb_decoder.cpp
    src/analysis/basic_block.cpp
    src/analysis/function.cpp
    src/analysis/cfg.cpp
    src/analysis/validator.cpp
    src/analysis/code_data.cpp
    src/runtime/nds_runtime.cpp
    src/runtime/subsystem_access.cpp
    src/runtime/dynamic_code.cpp
    src/ir/ir_value.cpp
    src/ir/ir_instruction.cpp
    src/ir/ir_translator.cpp
    src/ir/ir_interpreter.cpp
    src/lifter/cpp_emitter.cpp
    src/lifter/cpp_lifter.cpp
)

# Core static library
add_library(descomp_core STATIC
    ${RECOMPILER_HEADERS}
    ${RECOMPILER_SOURCES}
)

target_include_directories(descomp_core PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

# Main CLI executable
add_executable(nds_recompiler
    src/main.cpp
)

target_link_libraries(nds_recompiler PRIVATE
    descomp_core
)
