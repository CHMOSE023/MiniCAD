// MiniCADWin：MiniCAD 桌面版入口（界面由 MiniGUI 绘制）
// 用法：MiniCADWin.exe [--selftest] [--ui 界面描述文件.json] [--] [图纸路径 ...]
// 以 USE_WIN32 编译时为窗口程序（没有控制台，日志和自测输出不可见）
#include "Host/Win32Window.h"
#include <shellapi.h>
#include <cstdio>
#include <string>
#include <vector>

using namespace MiniCAD;

namespace
{
    std::string ToUtf8(const wchar_t* w)
    {
        const int n = WideCharToMultiByte(CP_UTF8, 0, w, -1, nullptr, 0, nullptr, nullptr);
        std::string s(static_cast<size_t>(n > 0 ? n - 1 : 0), '\0');
        if (n > 1)
            WideCharToMultiByte(CP_UTF8, 0, w, -1, s.data(), n, nullptr, nullptr);
        return s;
    }

    // 加宽控制台缓冲区，长日志不折行
    void WidenConsole()
    {
        HANDLE out = GetStdHandle(STD_OUTPUT_HANDLE);
        CONSOLE_SCREEN_BUFFER_INFO csbi = {};
        constexpr SHORT kWidth = 220;
        if (!GetConsoleScreenBufferInfo(out, &csbi) || csbi.dwSize.X >= kWidth)
            return;
        SetConsoleScreenBufferSize(out, COORD{ kWidth, csbi.dwSize.Y });
        SMALL_RECT win = csbi.srWindow;
        win.Right = win.Left + kWidth - 1;
        SetConsoleWindowInfo(out, TRUE, &win);
    }

    int RunApp()
    {
        SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);

        Win32Window window;
        MainFrame&  frame    = window.GetFrame();
        bool        selfTest = false;
        std::vector<std::string> drawingPaths;
        bool positional = false;

        // 命令行参数按 UTF-8 取（--ui 的路径可能含中文）
        int argc = 0;
        LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
        for (int i = 1; i < argc; ++i)
        {
            const std::string a = ToUtf8(argv[i]);
            if (!positional && a == "--")
                positional = true;
            else if (!positional && a == "--selftest")
                selfTest = true;
            else if (!positional && a == "--ui" && i + 1 < argc)
                frame.SetUiFile(ToUtf8(argv[++i]));
            else
                drawingPaths.push_back(a);
        }
        LocalFree(argv);

        if (selfTest)
            std::setvbuf(stdout, nullptr, _IONBF, 0);     // 自测输出不缓冲：中途崩溃也能看到已经执行的检查
        else
            WidenConsole();
        frame.SetUseUserLayout(!selfTest);      // 自测不受本机保存的面板布局影响，也不覆盖它
        if (!window.Initialize(L"MiniCAD", 1280, 800))
            return 2;

        if (!selfTest && !drawingPaths.empty())
            frame.OpenDrawings(drawingPaths);

        return selfTest ? frame.RunSelfTest() : window.Run();
    }
}

#ifdef USE_WIN32
int APIENTRY wWinMain(HINSTANCE, HINSTANCE, PWSTR, int)
{
    return RunApp();
}
#else
int main()
{
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);
    return RunApp();
}
#endif
