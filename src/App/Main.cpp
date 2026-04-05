#include "pch.h"
#include "MainWindow.h"
#include "App/ErrorReporter.h"

using namespace MiniCAD;

int main()
{
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);

    try
    {
        auto mainWindow = std::make_unique<MainWindow>();
        if (!mainWindow->Initialize(L"MiniCAD", 800, 600))
            return 1;

        mainWindow->Run();
        return 0;
    }
    catch (const std::exception& ex)
    {
        ReportError(ex.what());
        MessageBoxA(nullptr, ex.what(), "MiniCAD Error", MB_OK | MB_ICONERROR);
        return 1;
    }
}
