#pragma once

#include <iostream>
#include <string>
#include <vector>
#include <functional>
#include <sstream>
#include <stdexcept>
#include <type_traits>

namespace descomp::testing {

template <typename T>
std::string format_val(const T& val) {
    if constexpr (std::is_enum_v<T>) {
        return std::to_string(static_cast<std::underlying_type_t<T>>(val));
    } else if constexpr (std::is_convertible_v<T, std::string_view>) {
        return std::string(val);
    } else {
        std::ostringstream ss;
        ss << val;
        return ss.str();
    }
}

struct TestCase {
    std::string suite_name;
    std::string test_name;
    std::function<void()> func;
};

class TestRegistry {
public:
    static TestRegistry& instance() {
        static TestRegistry reg;
        return reg;
    }

    void add_test(const std::string& suite, const std::string& name, std::function<void()> func) {
        m_tests.push_back({suite, name, std::move(func)});
    }

    const std::vector<TestCase>& tests() const { return m_tests; }

private:
    std::vector<TestCase> m_tests;
};

struct TestRegistrar {
    TestRegistrar(const std::string& suite, const std::string& name, std::function<void()> func) {
        TestRegistry::instance().add_test(suite, name, std::move(func));
    }
};

#define TEST_CASE(suite, name) \
    static void test_##suite##_##name(); \
    static ::descomp::testing::TestRegistrar reg_##suite##_##name(#suite, #name, test_##suite##_##name); \
    static void test_##suite##_##name()

#define ASSERT_TRUE(expr) \
    do { \
        if (!(expr)) { \
            std::ostringstream assert_ss; \
            assert_ss << "Assertion failed: (" #expr ") at " << __FILE__ << ":" << __LINE__; \
            throw std::runtime_error(assert_ss.str()); \
        } \
    } while (0)

#define ASSERT_FALSE(expr) ASSERT_TRUE(!(expr))

#define ASSERT_EQ(a, b) \
    do { \
        const auto& _a_val = (a); \
        const auto& _b_val = (b); \
        if (_a_val != _b_val) { \
            std::ostringstream assert_ss; \
            assert_ss << "Assertion failed: " #a " == " #b " (got " << ::descomp::testing::format_val(_a_val) \
                      << " vs " << ::descomp::testing::format_val(_b_val) << ") at " << __FILE__ << ":" << __LINE__; \
            throw std::runtime_error(assert_ss.str()); \
        } \
    } while (0)

#define ASSERT_NE(a, b) \
    do { \
        const auto& _a_val = (a); \
        const auto& _b_val = (b); \
        if (_a_val == _b_val) { \
            std::ostringstream assert_ss; \
            assert_ss << "Assertion failed: " #a " != " #b " at " << __FILE__ << ":" << __LINE__; \
            throw std::runtime_error(assert_ss.str()); \
        } \
    } while (0)

#define ASSERT_GT(a, b) \
    do { \
        auto _a_val = (a); \
        auto _b_val = (b); \
        if (!(_a_val > _b_val)) { \
            std::ostringstream assert_ss; \
            assert_ss << "Assertion failed: " #a " > " #b " at " << __FILE__ << ":" << __LINE__; \
            throw std::runtime_error(assert_ss.str()); \
        } \
    } while (0)

#define ASSERT_GE(a, b) \
    do { \
        auto _a_val = (a); \
        auto _b_val = (b); \
        if (!(_a_val >= _b_val)) { \
            std::ostringstream assert_ss; \
            assert_ss << "Assertion failed: " #a " >= " #b " at " << __FILE__ << ":" << __LINE__; \
            throw std::runtime_error(assert_ss.str()); \
        } \
    } while (0)

#define ASSERT_STR_CONTAINS(full_string, sub_string) \
    do { \
        std::string _full_str = (full_string); \
        std::string _sub_str = (sub_string); \
        if (_full_str.find(_sub_str) == std::string::npos) { \
            std::ostringstream assert_ss; \
            assert_ss << "Assertion failed: string contains \"" << _sub_str << "\" at " << __FILE__ << ":" << __LINE__; \
            throw std::runtime_error(assert_ss.str()); \
        } \
    } while (0)

} // namespace descomp::testing
