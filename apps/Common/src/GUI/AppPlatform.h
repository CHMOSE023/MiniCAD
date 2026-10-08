#pragma once
#include "Render/IRenderBackend.h"
#include <functional>
#include <string>

namespace MiniGUI
{
    class TextSystem;
}

namespace MiniCAD
{
    class IRenderer;
    class IRenderTarget;

    // 选择文件的用途（决定筛选的扩展名）
    enum class FileKind
    {
        Drawing,    // .mcad / .dwg / .dxf / .json
        Image,      // .png / .jpg / .bmp / .tga / .gif
    };

    // 主窗口（MainFrame）依赖的平台服务。Win32 版（窗口 + D3D11，apps/Win32/src/Host/Win32Window）
    // 与网页版（画布 + WebGL2，apps/Web/src/Host/WebApp）各实现一份；MainFrame 本身不含平台代码。
    class AppPlatform
    {
    public:
        virtual ~AppPlatform() = default;

        // ── 图形 ─────────────────────────────────────────────────
        virtual MiniGUI::IRenderBackend& GetUiBackend()      = 0;   // 界面渲染后端（D3D11Backend / WebGL2Backend）
        virtual IRenderer&               GetRenderer()       = 0;   // CAD 视口渲染器
        virtual IRenderTarget&           GetViewportTarget() = 0;   // CAD 视口的离屏渲染目标
        // 把视口渲染目标的着色器资源（IRenderTarget::GetNativeShaderResource）登记为界面纹理
        virtual MiniGUI::TextureId RegisterViewportTexture(void* nativeShaderResource) = 0;
        virtual bool LoadUiFonts(MiniGUI::TextSystem& text) = 0;
        virtual void RequestRedraw() = 0;       // 安排一次重绘（Win32：InvalidateRect；网页：requestAnimationFrame）
        virtual void BeginUiPass()   = 0;       // 绑定窗口的帧缓冲并清屏
        virtual void EndUiPass()     = 0;       // 呈现，并同步输入法位置等平台输入状态
        virtual void WaitForGpu() {}            // 自测计时用：等 GPU 执行完已提交的命令
        virtual const char* GetGraphicsName() const = 0;    // 关于对话框显示，例如 "D3D11"

        // ── 窗口 ─────────────────────────────────────────────────
        virtual void SetTitle(const std::string& utf8) = 0;
        virtual bool HasWindowControls() const = 0;         // 标题栏是否显示最小化 / 最大化 / 关闭
        virtual bool IsWindowActive()    const { return true; }
        virtual bool IsWindowMaximized() const { return false; }
        virtual void MinimizeWindow()       {}
        virtual void ToggleMaximizeWindow() {}
        virtual void CloseWindow() = 0;         // 相当于点了关闭按钮：由 MainFrame::RequestExit 询问未保存的文档
        virtual void Quit()        = 0;         // 确认后退出

        // ── 文件 ─────────────────────────────────────────────────
        // 选择要打开的文件；done 收到 UTF-8 路径（取消时不调用）。网页版的选择是异步的
        virtual void PickFile(FileKind kind, std::function<void(const std::string& path)> done) = 0;
        // 另存为：返回保存路径（UTF-8，空串 = 取消）。
        // 有系统对话框时（HasSaveDialog）suggestedName 为文档名，由用户在对话框里选位置和格式；
        // 没有时 MainFrame 先用自己的对话框问文件名和格式，suggestedName 是带扩展名的文件名
        virtual std::string ChooseSavePath(const std::string& suggestedName) = 0;
        virtual bool HasSaveDialog() const { return true; }
        virtual void OnDocumentSaved(const std::string& path) { (void)path; }   // 网页版：保存后下载到本机
        virtual std::string GetResourceDir() const = 0;     // icons/ ui/ patterns/ fonts/ 所在目录（UTF-8）
        virtual std::string GetUserDataDir() const = 0;     // 用户布局等文件的目录（UTF-8）；空串 = 不保存
    };
}
