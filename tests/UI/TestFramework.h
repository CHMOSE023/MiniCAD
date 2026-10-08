#pragma once
#include <cmath>
#include <cstdio>
#include <vector>

// 最小测试框架：TEST 注册用例，CHECK / CHECK_NEAR 记录失败但不中断当前用例
namespace MiniGUI::Test
{
    struct TestCase
    {
        const char* name;
        void (*func)();
    };

    inline std::vector<TestCase>& Registry()
    {
        static std::vector<TestCase> tests;
        return tests;
    }

    inline int& FailureCount()
    {
        static int failures = 0;
        return failures;
    }

    struct Registrar
    {
        Registrar(const char* name, void (*func)()) { Registry().push_back({ name, func }); }
    };

    inline void ReportFailure(const char* file, int line, const char* expr)
    {
        std::printf("    失败 %s(%d): %s\n", file, line, expr);
        ++FailureCount();
    }

    inline void ReportNear(const char* file, int line, const char* expr, double actual, double expected)
    {
        std::printf("    失败 %s(%d): %s  实际 %.4f，期望 %.4f\n", file, line, expr, actual, expected);
        ++FailureCount();
    }
}

#define MG_TEST_CONCAT2(a, b) a##b
#define MG_TEST_CONCAT(a, b)  MG_TEST_CONCAT2(a, b)

#define TEST(name)                                                                   \
    static void name();                                                              \
    static ::MiniGUI::Test::Registrar MG_TEST_CONCAT(s_reg_, name)(#name, &name);    \
    static void name()

#define CHECK(cond)                                                                  \
    do { if (!(cond)) ::MiniGUI::Test::ReportFailure(__FILE__, __LINE__, #cond); } while (0)

#define CHECK_NEAR(actual, expected)                                                 \
    do {                                                                             \
        const double a_ = (actual), e_ = (expected);                                 \
        if (std::abs(a_ - e_) > 1e-3)                                                \
            ::MiniGUI::Test::ReportNear(__FILE__, __LINE__, #actual, a_, e_);        \
    } while (0)

// 检查节点相对父节点的矩形
// （参数不能叫 x/y：宏替换会把 b_.min.x 里的成员名也替换掉）
#define CHECK_BOUNDS(node, ex, ey, ew, eh)                                           \
    do {                                                                             \
        const ::MiniGUI::Rect& b_ = (node)->GetBounds();                             \
        CHECK_NEAR(b_.min.x, (ex));                                                  \
        CHECK_NEAR(b_.min.y, (ey));                                                  \
        CHECK_NEAR(b_.Width(), (ew));                                                \
        CHECK_NEAR(b_.Height(), (eh));                                               \
    } while (0)
