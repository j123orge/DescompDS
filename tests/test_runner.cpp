#include "test_framework.h"
#include <iostream>
#include <chrono>

int main() {
    std::cout << "=========================================================\n";
    std::cout << "  Running DescompDS Test Suite (Phase 1)                 \n";
    std::cout << "=========================================================\n\n";

    const auto& tests = descomp::testing::TestRegistry::instance().tests();
    size_t passed = 0;
    size_t failed = 0;

    auto start_time = std::chrono::high_resolution_clock::now();

    for (const auto& test : tests) {
        std::cout << "  [RUN] " << test.suite_name << " :: " << test.test_name << " ... ";
        try {
            test.func();
            std::cout << "[PASSED]\n";
            ++passed;
        } catch (const std::exception& ex) {
            std::cout << "[FAILED]\n";
            std::cerr << "        Error: " << ex.what() << "\n";
            ++failed;
        } catch (...) {
            std::cout << "[FAILED]\n";
            std::cerr << "        Error: Unknown non-standard exception\n";
            ++failed;
        }
    }

    auto end_time = std::chrono::high_resolution_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time).count();

    std::cout << "\n=========================================================\n";
    std::cout << "  Test Results: " << passed << " passed, " << failed << " failed (total " << tests.size() << ") in " << elapsed << " ms\n";
    std::cout << "=========================================================\n";

    return (failed == 0) ? 0 : 1;
}
