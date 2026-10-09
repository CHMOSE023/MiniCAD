// ── MiniCAD 网页版宿主：WebGL2、浏览器文件选择与下载、页面关闭提示 ─────────
#include "Host/WebApp.h"
#include "GLES3/GLES3Renderer.h"
#include "GLES3/GLRenderTarget.h"
#include "WebGL2/WebGL2Backend.h"
#include "Core/Log.h"
#include "Core/UIContext.h"
#include "Document/Document.h"
#include "Platform/Web/WebFonts.h"
#include "Widgets/Dialog.h"
#include "Platform/Web/WebInput.h"
#include <emscripten.h>
#include <GLES3/gl3.h>
#include <algorithm>
#include <cctype>

namespace MiniCAD
{
    namespace
    {
        WebApp* g_app = nullptr;

        std::string Lower(std::string s)
        {
            std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            return s;
        }

        bool HasExtension(const std::string& name, const char* ext)
        {
            const std::string lower = Lower(name);
            const std::string e     = ext;
            return lower.size() > e.size() && lower.compare(lower.size() - e.size(), e.size(), e) == 0;
        }
    }

    // =========================================================
    // JavaScript：文件选择、下载、页面关闭提示
    // =========================================================
    EM_JS_DEPS(MiniCADWebApp, "$stringToNewUTF8,$UTF8ToString,$FS");

    // 每次请求和每个文件使用独立目录，保留文件名；全部上传完成后统一打开。
    EM_JS(void, MiniCADWeb_PickFile, (int requestId, const char* acceptPtr, int multiple), {
        const input = document.createElement("input");
        input.type = "file";
        input.accept = UTF8ToString(acceptPtr);
        input.multiple = !!multiple;
        input.style.display = "none";
        let settled = false;
        const complete = (error) => {
            const p = stringToNewUTF8(error || "");
            _MiniCADWeb_OnFilePickCompleted(requestId, p);
            _free(p);
        };
        input.addEventListener("cancel", () => {
            if (settled) return;
            settled = true;
            input.remove();
            complete("");
        });
        input.addEventListener("change", async () => {
            if (settled) return;
            settled = true;
            const files = Array.from(input.files || []);
            input.remove();
            const errors = [];
            for (let i = 0; i < files.length; ++i) {
                const file = files[i];
                try {
                    const buf = await file.arrayBuffer();
                    const directory = "/upload/" + requestId + "/" + i;
                    FS.mkdirTree(directory);
                    const name = file.name.replace(/[\\/]/g, "_");
                    const path = directory + "/" + name;
                    FS.writeFile(path, new Uint8Array(buf));
                    const p = stringToNewUTF8(path);
                    _MiniCADWeb_OnFilePicked(requestId, p);
                    _free(p);
                } catch (err) {
                    errors.push(file.name + ": " + String(err));
                }
            }
            complete(errors.join("\n"));
        });
        document.body.appendChild(input);
        input.click();
    });

    // 把虚拟文件系统里的文件下载到本机
    EM_JS(void, MiniCADWeb_Download, (const char* pathPtr), {
        const path = UTF8ToString(pathPtr);
        let data;
        try { data = FS.readFile(path); } catch (e) { console.error("MiniCAD: 找不到要下载的文件 " + path); return; }
        const blob = new Blob([data], { type: "application/octet-stream" });
        const url = URL.createObjectURL(blob);
        const a = document.createElement("a");
        a.href = url;
        a.download = path.substring(path.lastIndexOf("/") + 1);
        document.body.appendChild(a);
        a.click();
        a.remove();
        setTimeout(() => URL.revokeObjectURL(url), 10000);
    });

    EM_JS(void, MiniCADWeb_SetTitle, (const char* title), {
        document.title = UTF8ToString(title);
    });

    // 有未保存的文档时，关闭或刷新页面前让浏览器询问
    EM_JS(void, MiniCADWeb_InstallUnloadGuard, (), {
        window.addEventListener("beforeunload", (e) => {
            if (_MiniCADWeb_HasUnsaved()) { e.preventDefault(); e.returnValue = ""; }
        });
    });

    extern "C"
    {
        EMSCRIPTEN_KEEPALIVE void MiniCADWeb_OnFilePicked(int requestId, const char* path)
        {
            if (g_app)
                g_app->OnFilePicked(requestId, path);
        }

        EMSCRIPTEN_KEEPALIVE void MiniCADWeb_OnFilePickCompleted(int requestId, const char* error)
        {
            if (g_app)
                g_app->OnFilePickCompleted(requestId, error);
        }

        EMSCRIPTEN_KEEPALIVE int MiniCADWeb_HasUnsaved()
        {
            return g_app && g_app->HasUnsavedDocuments() ? 1 : 0;
        }
    }

    // =========================================================
    // 初始化
    // =========================================================
    WebApp::WebApp()
    {
        g_app = this;
    }

    WebApp::~WebApp()
    {
        if (g_app == this)
            g_app = nullptr;
    }

    bool WebApp::Initialize()
    {
        if (!m_host.Initialize())
            return false;

        m_backend    = std::make_unique<MiniGUI::WebGL2Backend>();
        m_renderer   = std::make_unique<GLES3Renderer>();
        m_viewportRT = std::make_unique<GLRenderTarget>();
        m_viewportRT->Create(m_host.GetFramebufferWidth(), m_host.GetFramebufferHeight());

        if (!m_main.Initialize(*this, m_host.GetLogicalWidth(), m_host.GetLogicalHeight(), m_host.GetPixelScale()))
            return false;
        m_input = std::make_unique<MiniGUI::WebInput>(&m_main.GetUI(), m_host.GetCanvasSelector().c_str());

        m_host.SetOnResize([this](float w, float h, float scale) { m_main.OnResize(w, h, scale); });
        m_host.SetOnFrame ([this] { m_main.RenderFrame(); });
        MiniCADWeb_InstallUnloadGuard();
        m_host.RequestFrame();
        return true;
    }

    bool WebApp::HasUnsavedDocuments() const
    {
        return m_main.HasUnsavedDocuments();
    }

    // =========================================================
    // AppPlatform：图形
    // =========================================================
    MiniGUI::IRenderBackend& WebApp::GetUiBackend() { return *m_backend; }
    IRenderer&               WebApp::GetRenderer()  { return *m_renderer; }
    IRenderTarget&           WebApp::GetViewportTarget() { return *m_viewportRT; }

    MiniGUI::TextureId WebApp::RegisterViewportTexture(void* nativeShaderResource)
    {
        // 视口画面来自帧缓冲：第一行在底部，显示时上下翻转
        const auto texture = static_cast<GLuint>(reinterpret_cast<uintptr_t>(nativeShaderResource));
        return m_backend->RegisterExternalTexture(texture, true);
    }

    bool WebApp::LoadUiFonts(MiniGUI::TextSystem& text)
    {
        return MiniGUI::LoadWebUIFonts(text);
    }

    void WebApp::BeginUiPass()
    {
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        glViewport(0, 0, m_host.GetFramebufferWidth(), m_host.GetFramebufferHeight());
        glClearColor(0.1f, 0.1f, 0.1f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
    }

    void WebApp::EndUiPass()
    {
        // 浏览器在本次回调返回后自动呈现画布
        if (m_input)
            m_input->Sync();        // 文字焦点变化时切换 textarea，候选窗跟随光标
    }

    // =========================================================
    // AppPlatform：窗口
    // =========================================================
    void WebApp::SetTitle(const std::string& utf8)
    {
        MiniCADWeb_SetTitle(utf8.c_str());
    }

    void WebApp::CloseWindow()
    {
        m_main.RequestExit();
    }

    void WebApp::Quit()
    {
        // 网页不能自己关闭标签页：未保存的修改已经处理完，什么也不做
        LOG_INFO("MiniCAD 网页版：关闭浏览器标签页即可退出");
    }

    // =========================================================
    // AppPlatform：文件
    // =========================================================
    void WebApp::PickFile(FileKind kind, std::function<void(const std::string& path)> done)
    {
        const int id = m_nextPickId++;
        m_pickRequests[id] = { [done = std::move(done)](const std::vector<std::string>& paths)
        {
            if (done && !paths.empty()) done(paths.front());
        }, {} };
        MiniCADWeb_PickFile(id, kind == FileKind::Image ? ".png,.jpg,.jpeg,.bmp,.tga,.gif" : ".mcad,.dwg,.dxf,.json", 0);
    }

    void WebApp::PickDrawingFiles(std::function<void(const std::vector<std::string>&)> done)
    {
        const int id = m_nextPickId++;
        m_pickRequests[id] = { std::move(done), {} };
        MiniCADWeb_PickFile(id, ".mcad,.dwg,.dxf,.json", 1);
    }

    void WebApp::OnFilePicked(int requestId, const char* path)
    {
        auto it = m_pickRequests.find(requestId);
        if (it == m_pickRequests.end())
            return;
        if (path && *path)
            it->second.paths.emplace_back(path);
    }

    void WebApp::OnFilePickCompleted(int requestId, const char* error)
    {
        auto it = m_pickRequests.find(requestId);
        if (it == m_pickRequests.end()) return;
        auto request = std::move(it->second);
        m_pickRequests.erase(it);
        if (request.done && !request.paths.empty())
            request.done(request.paths);
        if (error && *error)
            MiniGUI::ShowMessageBox(m_main.GetUI(), "读取文件失败", error, { "确定" }, [](int) {});
        m_host.RequestFrame();
    }

    std::string WebApp::ChooseSavePath(const std::string& suggestedName, CadSaveVersion&)
    {
        // 浏览器没有"另存为"对话框：保存到内存文件系统，保存完成后（OnDocumentSaved）下载。
        // 已经带 .mcad / .dwg / .dxf / .json 扩展名的按原格式保存，否则存为 .mcad
        std::string name = suggestedName.empty() ? std::string("未命名") : suggestedName;
        std::replace(name.begin(), name.end(), '/', '_');
        if (!HasExtension(name, ".mcad") && !HasExtension(name, ".dwg") && !HasExtension(name, ".dxf") && !HasExtension(name, ".json"))
            name += ".mcad";
        EM_ASM({ try { FS.mkdir("/download"); } catch (e) {} });
        return "/download/" + name;
    }

    void WebApp::OnDocumentSaved(const std::string& path)
    {
        MiniCADWeb_Download(path.c_str());
    }
}
