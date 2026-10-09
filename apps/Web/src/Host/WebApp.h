#pragma once
#include "GUI/AppPlatform.h"
#include "GUI/MainFrame.h"
#include "Host/WebHost.h"
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>

namespace MiniGUI
{
    class WebGL2Backend;
    class WebInput;
}

namespace MiniCAD
{
    class GLES3Renderer;
    class GLRenderTarget;

    // MiniCAD 网页版的宿主：画布 + WebGL2（WebHost）+ 浏览器事件，实现 AppPlatform，界面与桌面版共用 MainFrame。
    //   - 打开文件：浏览器的文件选择框，选中的文件读进内存文件系统 /upload 后打开
    //   - 保存：写到内存文件系统后由浏览器下载到本机（另存为默认 .mcad，打开的 .dwg / .dxf 按原格式保存）
    //   - 资源（字体、图标、填充图案、界面描述文件）打包在虚拟文件系统根目录下
    //   - 有未保存的文档时，关闭或刷新页面前浏览器会询问
    class WebApp : public AppPlatform
    {
    public:
        WebApp();
        ~WebApp() override;

        WebApp(const WebApp&) = delete;
        WebApp& operator=(const WebApp&) = delete;

        bool Initialize();
        MainFrame& GetFrame() { return m_main; }

        // 由 JavaScript 回调（内部使用）
        void OnFilePicked(int requestId, const char* path);
        void OnFilePickCompleted(int requestId, const char* error);
        bool HasUnsavedDocuments() const;

        // ── AppPlatform ─────────────────────────────────────────
        MiniGUI::IRenderBackend& GetUiBackend() override;
        IRenderer&               GetRenderer() override;
        IRenderTarget&           GetViewportTarget() override;
        MiniGUI::TextureId RegisterViewportTexture(void* nativeShaderResource) override;
        bool LoadUiFonts(MiniGUI::TextSystem& text) override;
        void RequestRedraw() override { m_host.RequestFrame(); }
        void BeginUiPass() override;
        void EndUiPass() override;
        const char* GetGraphicsName() const override { return "WebGL2"; }

        void SetTitle(const std::string& utf8) override;
        bool HasWindowControls() const override { return false; }
        void CloseWindow() override;
        void Quit() override;

        void PickFile(FileKind kind, std::function<void(const std::string& path)> done) override;
        void PickDrawingFiles(std::function<void(const std::vector<std::string>&)> done) override;
        std::string ChooseSavePath(const std::string& suggestedName, CadSaveVersion& version) override;
        void OnDocumentSaved(const std::string& path) override;
        bool HasSaveDialog() const override { return false; }  // 文件名和格式由 MainFrame 的另存为对话框询问
        std::string GetResourceDir() const override { return ""; }     // 资源在虚拟文件系统根目录：/icons、/ui ...
        std::string GetUserDataDir() const override { return ""; }     // 暂不保存用户布局

    private:
        // 声明顺序即构造顺序，销毁时相反：输入层 → 界面（MainFrame）→ 渲染器 → 界面后端 → WebGL 上下文
        WebHost                                 m_host;
        std::unique_ptr<MiniGUI::WebGL2Backend> m_backend;
        std::unique_ptr<GLES3Renderer>          m_renderer;
        std::unique_ptr<GLRenderTarget>         m_viewportRT;
        MainFrame                               m_main;
        std::unique_ptr<MiniGUI::WebInput>      m_input;

        struct PickRequest
        {
            std::function<void(const std::vector<std::string>&)> done;
            std::vector<std::string> paths;
        };
        std::unordered_map<int, PickRequest> m_pickRequests;
        int m_nextPickId = 1;
    };
}
