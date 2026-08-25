add_executable(descomp_tests
    test_runner.cpp
    arm/test_arm_decoder.cpp
    thumb/test_thumb_decoder.cpp
    cfg/test_cfg.cpp
    cfg/test_validator.cpp
    ir/test_ir.cpp
    lifter/test_cpp_lifter.cpp
    phase25/test_phase25.cpp
    phase3/test_phase3.cpp
    roms/test_nds_rom.cpp
)

target_include_directories(descomp_tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/../tools/recompiler/include
)

target_link_libraries(descomp_tests PRIVATE
    descomp_core
)

add_test(NAME AllTests COMMAND descomp_tests)
