#include "app/Editor.h"
#include "ui/Windows/MainWindow.h" 
#include <windows.h>
#include <cstdio>

int main() {

    // GetModuleHandleW(nullptr) 返回当前进程的 HINSTANCE
    HINSTANCE hInstance = GetModuleHandle(nullptr);

    MiniCAD::MainWindow window;
    if (!window.Create(hInstance, L"MiniCAD", 1280, 720)) {
        return -1;
    }

    window.Show(SW_SHOWDEFAULT);   // SW_SHOWDEFAULT 比 NULL 语义更清晰

    // 消息循环
    MSG msg = {};
    while (GetMessage(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    return static_cast<int>(msg.wParam);
}