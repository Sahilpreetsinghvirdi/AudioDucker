#pragma once

#include <cstdio>
#include <cmath>
#include <concepts>
#include <functional>
#include <sstream>
#include <string>
#include <vector>

// Minimal dependency-free test harness for Audio Ducker.
//
//  - TEST(name) registers a function into a global registry.
//  - EXPECT_* record failures (printing to stderr) and continue the test.
//  - main() (in test_main.cpp) runs every registered test and returns
//    non-zero if anything failed.

namespace test {

template <typename T>
concept Streamable = requires(std::ostream& os, const T& v) { os << v; };

template <typename T>
void StreamValue(std::ostringstream& oss, const T& v) {
    if constexpr (Streamable<T>)
        oss << v;
    else
        oss << "<unprintable>";
}

struct Case {
    const char* name;
    std::function<void()> fn;
};

inline std::vector<Case>& Registry() {
    static std::vector<Case> registry;
    return registry;
}

struct Registrar {
    Registrar(const char* name, std::function<void()> fn) {
        Registry().push_back(Case{name, std::move(fn)});
    }
};

inline int& FailureCount() {
    static int failures = 0;
    return failures;
}

inline void Fail(const char* file, int line, const std::string& message) {
    FailureCount()++;
    std::fprintf(stderr, "  FAIL %s:%d: %s\n", file, line, message.c_str());
}

template <typename A, typename B>
void ExpectEq(const A& a, const B& b, const char* file, int line, const char* ea, const char* eb) {
    if (!(a == b)) {
        std::ostringstream oss;
        oss << "EXPECT_EQ(" << ea << ", " << eb << ")\n"
            << "    left : ";
        StreamValue(oss, a);
        oss << "\n    right: ";
        StreamValue(oss, b);
        Fail(file, line, oss.str());
    }
}

template <typename A, typename B>
void ExpectNear(A a, B b, double eps, const char* file, int line, const char* ea, const char* eb) {
    double diff = std::fabs(static_cast<double>(a) - static_cast<double>(b));
    if (diff > eps) {
        std::ostringstream oss;
        oss << "EXPECT_NEAR(" << ea << ", " << eb << ") diff " << diff << " > " << eps;
        Fail(file, line, oss.str());
    }
}

} // namespace test

#define TEST(name)                                                                             \
    static void test_##name();                                                                 \
    static ::test::Registrar reg_##name(#name, test_##name);                                   \
    static void test_##name()

#define EXPECT_TRUE(cond)                                                                      \
    do {                                                                                       \
        if (!(cond)) ::test::Fail(__FILE__, __LINE__, "EXPECT_TRUE(" #cond ")");               \
    } while (0)

#define EXPECT_FALSE(cond)                                                                     \
    do {                                                                                       \
        if (cond) ::test::Fail(__FILE__, __LINE__, "EXPECT_FALSE(" #cond ")");                 \
    } while (0)

#define EXPECT_EQ(a, b) ::test::ExpectEq((a), (b), __FILE__, __LINE__, #a, #b)

#define EXPECT_NEAR(a, b, eps) ::test::ExpectNear((a), (b), (eps), __FILE__, __LINE__, #a, #b)
