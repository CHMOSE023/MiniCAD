#include "TestFramework.h"

#ifdef _WIN32
#include <crtdbg.h>
#include <cstdlib>
extern "C" __declspec(dllimport) int __stdcall SetConsoleOutputCP(unsigned int);
#endif

#include <cstring>

// 用法：MiniDWGTests [名称片段]——只运行名称中含该片段的用例
int main(int argc, char** argv)
{
#ifdef _WIN32
    SetConsoleOutputCP(65001);  // UTF-8

    // 调试版的断言失败默认弹出模态对话框，会卡住自动测试：改为输出到控制台并直接结束
    _CrtSetReportMode(_CRT_ASSERT, _CRTDBG_MODE_FILE | _CRTDBG_MODE_DEBUG);
    _CrtSetReportFile(_CRT_ASSERT, _CRTDBG_FILE_STDERR);
    _CrtSetReportMode(_CRT_ERROR, _CRTDBG_MODE_FILE | _CRTDBG_MODE_DEBUG);
    _CrtSetReportFile(_CRT_ERROR, _CRTDBG_FILE_STDERR);
    _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
#endif

    using namespace MiniDWG::Test;

    // 不缓冲：用例崩溃时也能看到是哪一个
    std::setvbuf(stdout, nullptr, _IONBF, 0);

    int failedTests = 0;
    const char* filter = argc > 1 ? argv[1] : nullptr;
    std::size_t ran = 0;
    for (const TestCase& t : Registry())
    {
        if (filter != nullptr && std::strstr(t.name, filter) == nullptr)
            continue;
        ++ran;
        std::printf("[ 运行 ] %s\r", t.name);
        const int before = FailureCount();
        t.func();
        const bool ok = FailureCount() == before;
        if (!ok)
            ++failedTests;
        std::printf("[%s] %s\n", ok ? " 通过 " : " 失败 ", t.name);
    }

    std::printf("\n共 %zu 个用例，失败 %d 个\n", Registry().size(), failedTests);
    return failedTests == 0 ? 0 : 1;
}
