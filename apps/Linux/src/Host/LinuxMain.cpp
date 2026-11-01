// MiniCADLinux：MiniCAD Linux 版入口（界面由 MiniGUI 绘制，SDL3 窗口 + Vulkan，不行时 OpenGL ES 3）
// 用法：MiniCADLinux [--gpu vulkan|gl] [--ui 界面描述文件.json] [--lang en|zh-CN|qps] [--] [图纸路径 ...]
// 图形后端也可以用环境变量 MINICAD_GPU 指定、界面语言用 MINICAD_LANG 指定（命令行优先）
#include "Host/LinuxApp.h"
#include "GUI/Language.h"
#include <cstdlib>
#include <string>
#include <vector>

using namespace MiniCAD;

int main(int argc, char** argv)
{
    LinuxApp   app;
    MainFrame& frame = app.GetFrame();

    const char* envGpu  = std::getenv("MINICAD_GPU");
    const char* envLang = std::getenv("MINICAD_LANG");
    std::string gpu     = envGpu ? envGpu : "";
    std::string lang    = envLang ? envLang : "";
    std::vector<std::string> drawingPaths;
    bool positional = false;

    // Linux 的命令行参数就是 UTF-8 字节
    for (int i = 1; i < argc; ++i)
    {
        const std::string a = argv[i];
        if (!positional && a == "--")
            positional = true;
        else if (!positional && a == "--gpu" && i + 1 < argc)
            gpu = argv[++i];
        else if (!positional && a == "--ui" && i + 1 < argc)
            frame.SetUiFile(argv[++i]);
        else if (!positional && a == "--lang" && i + 1 < argc)
            lang = argv[++i];
        else
            drawingPaths.push_back(a);
    }

    InitLanguage(app, lang);    // 在创建图形设备之前：它的出错信息也要翻译
    if (!app.Initialize("MiniCAD", 1280, 800, gpu))
        return 2;
    if (!drawingPaths.empty())
        frame.OpenDrawings(drawingPaths);
    return app.Run();
}
