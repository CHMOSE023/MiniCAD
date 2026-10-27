// ── MiniCAD 网页版宿主：图形设备、浏览器文件选择与下载、页面关闭提示 ─────────
#include "Host/WebApp.h"
#include "Core/Log.h"
#include "Core/UIContext.h"
#include "Document/Document.h"
#include "Platform/Web/WebFonts.h"
#include "Widgets/Dialog.h"
#include "Platform/Web/WebInput.h"
#ifdef MINICAD_WITH_SOLID
#include "Solid/SMLib/SMLibKernel.h"
#endif
#include <emscripten.h>
#include <algorithm>
#include <cctype>
#include <cstdio>
#include <memory>

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

    // 选一个本地文件夹找外部参照：只把文件名在 namesPtr（换行分隔，小写）里的文件按原来的子目录
    // 读进 /xref/<请求号>/<文件夹名>/...，完成后回调目录（取消或出错时为空串）。
    // 有 showDirectoryPicker（Chromium 系）时经目录授权逐级遍历，只读需要的文件；
    // 没有时用 <input webkitdirectory>（浏览器会询问是否上传整个文件夹，但也只读需要的文件的内容）
    EM_JS(void, MiniCADWeb_PickFolder, (int requestId, const char* namesPtr), {
        const wanted = new Set(UTF8ToString(namesPtr).split("\n").filter(s => s.length > 0));
        const matches = (name) => {
            const n = name.toLowerCase();
            return wanted.has(n) || (n.endsWith(".dwg") && wanted.has(n.slice(0, -4)));
        };
        const root = "/xref/" + requestId;
        const finish = (dir, error) => {
            const d = stringToNewUTF8(dir);
            const e = stringToNewUTF8(error || "");
            _MiniCADWeb_OnFolderPicked(requestId, d, e);
            _free(d);
            _free(e);
        };
        const save = async (relative, file) => {
            const path = root + "/" + relative.split(/[\\/]/).map(s => (s === "." || s === ".." ? "_" : s)).join("/");
            FS.mkdirTree(path.substring(0, path.lastIndexOf("/")));
            FS.writeFile(path, new Uint8Array(await file.arrayBuffer()));
        };
        if (typeof window.showDirectoryPicker === "function") {
            (async () => {
                let handle;
                try { handle = await window.showDirectoryPicker({ id: "minicad-xref", mode: "read" }); }
                catch (err) { finish("", err && err.name === "AbortError" ? "" : String(err)); return; }
                const errors = [];
                let visited = 0;
                const walk = async (dir, prefix, depth) => {
                    for await (const entry of dir.values()) {
                        if (++visited > 200000) return;
                        const relative = prefix + "/" + entry.name;
                        if (entry.kind === "directory") {
                            if (depth < 16) await walk(entry, relative, depth + 1);
                        } else if (matches(entry.name)) {
                            try { await save(relative, await entry.getFile()); }
                            catch (err) { errors.push(relative + ": " + String(err)); }
                        }
                    }
                };
                try { await walk(handle, handle.name, 0); }
                catch (err) { errors.push(String(err)); }
                finish(root, errors.join("\n"));
            })();
            return;
        }
        const input = document.createElement("input");
        input.type = "file";
        input.webkitdirectory = true;
        input.multiple = true;
        input.style.display = "none";
        let settled = false;
        input.addEventListener("cancel", () => {
            if (settled) return;
            settled = true;
            input.remove();
            finish("", "");
        });
        input.addEventListener("change", async () => {
            if (settled) return;
            settled = true;
            const files = Array.from(input.files || []);
            input.remove();
            const errors = [];
            for (const file of files) {
                if (!matches(file.name)) continue;
                const relative = file.webkitRelativePath || file.name;
                try { await save(relative, file); }
                catch (err) { errors.push(relative + ": " + String(err)); }
            }
            finish(root, errors.join("\n"));
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

    // 网址带 ?webgl（或 ?webgl=1）时强制用 WebGL2：排查问题、对比两个后端
    EM_JS(int, MiniCADWeb_PreferWebGL, (), {
        try { return new URLSearchParams(location.search).has("webgl") ? 1 : 0; } catch (e) { return 0; }
    });

    // 设备异步就绪时初始化失败：加载画面还在就显示原因（与 Module.onAbort 相同）
    EM_JS(void, MiniCADWeb_ReportFailure, (const char* what), {
        if (Module["onAbort"]) Module["onAbort"](UTF8ToString(what));
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

        EMSCRIPTEN_KEEPALIVE void MiniCADWeb_OnFolderPicked(int requestId, const char* dir, const char* error)
        {
            if (g_app)
                g_app->OnFolderPicked(requestId, dir, error);
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
        if (MiniCADWeb_PreferWebGL())
        {
            LOG_INFO("MiniCAD 网页版：网址带 ?webgl，使用 WebGL2");
            return InitializeWebGL();
        }

        // 先试 WebGPU：取适配器、设备都是异步的，回调前不建任何上下文、不画帧
        const NativeWindow window{ const_cast<char*>(m_host.GetCanvasSelector().c_str()) };
        CreateWebGPUGraphicsDeviceAsync(window, m_host.GetFramebufferWidth(), m_host.GetFramebufferHeight(),
            [this](std::unique_ptr<IGraphicsDevice> device)
            {
                bool ok = false;
                if (device)
                {
                    m_graphics = std::move(device);
                    ok = OnGraphicsReady();
                }
                else
                {
                    LOG_INFO("MiniCAD 网页版：WebGPU 不可用，改用 WebGL2");
                    ok = InitializeWebGL();
                }
                if (!ok)
                    ReportFailure("初始化失败（浏览器需要支持 WebGPU 或 WebGL2）");
            });
        return true;
    }

    bool WebApp::InitializeWebGL()
    {
        if (!m_host.CreateWebGLContext())
            return false;
        NativeWindow window{ const_cast<char*>(m_host.GetCanvasSelector().c_str()) };
        window.glContext = true;        // WebHost 刚建好 WebGL2 上下文并设为当前
        m_graphics = CreateGraphicsDevice(window, m_host.GetFramebufferWidth(), m_host.GetFramebufferHeight(), "webgl2");
        return m_graphics && OnGraphicsReady();
    }

    bool WebApp::OnGraphicsReady()
    {
        LOG_INFO("MiniCAD 网页版：图形接口 %s", m_graphics->GetName());
        // 异步等待期间画布尺寸可能变了：按当前尺寸
        m_graphics->Resize(m_host.GetFramebufferWidth(), m_host.GetFramebufferHeight());
        m_viewportRT = m_graphics->CreateRenderTarget();
        m_viewportRT->Create(m_host.GetFramebufferWidth(), m_host.GetFramebufferHeight());

        if (!m_main.Initialize(*this, m_host.GetLogicalWidth(), m_host.GetLogicalHeight(), m_host.GetPixelScale()))
            return false;
        m_input = std::make_unique<MiniGUI::WebInput>(&m_main.GetUI(), m_host.GetCanvasSelector().c_str());

        m_host.SetOnResize([this](float w, float h, float scale)
        {
            m_graphics->Resize(m_host.GetFramebufferWidth(), m_host.GetFramebufferHeight());
            m_main.OnResize(w, h, scale);
        });
        m_host.SetOnFrame ([this] { m_main.RenderFrame(); });
        MiniCADWeb_InstallUnloadGuard();
        m_host.RequestFrame();
        return true;
    }

    void WebApp::ReportFailure(const char* what)
    {
        std::printf("MiniCAD：%s\n", what);
        MiniCADWeb_ReportFailure(what);
    }

    bool WebApp::HasUnsavedDocuments() const
    {
        return m_main.HasUnsavedDocuments();
    }

    // =========================================================
    // AppPlatform：图形
    // =========================================================
    MiniGUI::IRenderBackend& WebApp::GetUiBackend() { return m_graphics->GetUiBackend(); }
    IRenderer&               WebApp::GetRenderer()  { return m_graphics->GetRenderer(); }
    IRenderTarget&           WebApp::GetViewportTarget() { return *m_viewportRT; }

    MiniGUI::TextureId WebApp::RegisterViewportTexture(void* nativeShaderResource)
    {
        return m_graphics->RegisterViewportTexture(nativeShaderResource);
    }

    // =========================================================
    // AppPlatform：三维实体内核
    // =========================================================
    Solid::ISolidKernel* WebApp::GetSolidKernel()
    {
#ifdef MINICAD_WITH_SOLID
        // 与桌面版相同：第一次请求时创建，之后一直持有。网页版没有后台线程，调用天然是串行的
        if (!m_solidKernel)
            m_solidKernel = Solid::CreateSMLibKernel();
#endif
        return m_solidKernel.get();
    }

    bool WebApp::LoadUiFonts(MiniGUI::TextSystem& text)
    {
        return MiniGUI::LoadWebUIFonts(text);
    }

    void WebApp::BeginUiPass()
    {
        m_graphics->BeginUiPass();
    }

    void WebApp::EndUiPass()
    {
        m_graphics->Present();      // 浏览器在本次回调返回后自动呈现画布，这里什么也不做
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

    bool WebApp::OpenUrl(const std::string& url)
    {
        if (url.empty())
            return false;
        // 在用户点击的回调里调用，浏览器不会把它当作弹出窗口拦截
        EM_ASM({
            window.open(UTF8ToString($0), "_blank", "noopener,noreferrer");
        }, url.c_str());
        return true;
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
        const char* accept = kind == FileKind::Image ? ".png,.jpg,.jpeg,.bmp,.tga,.gif"
                           : kind == FileKind::NodeGraph ? ".mcnode,.json"
                           : ".mcad,.dwg,.dxf,.json";
        MiniCADWeb_PickFile(id, accept, 0);
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

    bool WebApp::PickFolder(const std::vector<std::string>& fileNames, std::function<void(const std::string& dir)> done)
    {
        std::string names;
        for (const std::string& n : fileNames)
        {
            std::string lower = n;
            std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            names += lower;
            names += '\n';
        }
        const int id = m_nextPickId++;
        m_folderRequests[id] = std::move(done);
        MiniCADWeb_PickFolder(id, names.c_str());
        return true;
    }

    void WebApp::OnFolderPicked(int requestId, const char* dir, const char* error)
    {
        auto it = m_folderRequests.find(requestId);
        if (it == m_folderRequests.end()) return;
        auto done = std::move(it->second);
        m_folderRequests.erase(it);
        if (error && *error)
            MiniGUI::ShowMessageBox(m_main.GetUI(), "读取文件夹失败", error, { "确定" }, [](int) {});
        if (done && dir && *dir)
            done(dir);
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

    std::string WebApp::ChooseExportPath(FileKind kind, const std::string& suggestedName)
    {
        // 与 ChooseSavePath 相同：写进内存文件系统，写完后 OnDocumentSaved 下载
        std::string name = suggestedName.empty() ? std::string("未命名") : suggestedName;
        std::replace(name.begin(), name.end(), '/', '_');
        if (kind == FileKind::NodeGraph && !HasExtension(name, ".mcnode") && !HasExtension(name, ".json"))
            name += ".mcnode";
        EM_ASM({ try { FS.mkdir("/download"); } catch (e) {} });
        return "/download/" + name;
    }

    void WebApp::OnDocumentSaved(const std::string& path)
    {
        MiniCADWeb_Download(path.c_str());
    }

    void WebApp::RunInBackground(std::function<void()> work, std::function<void()> done)
    {
        // 没有线程：先让浏览器画出当前帧（进度条），再在界面线程上执行；执行期间页面不响应
        struct Task { std::function<void()> work, done; };
        auto* task = new Task{ std::move(work), std::move(done) };
        RequestRedraw();
        emscripten_async_call([](void* p)
        {
            std::unique_ptr<Task> t(static_cast<Task*>(p));
            t->work();
            t->done();
        }, task, 50);
    }

    void WebApp::Post(std::function<void()> fn)
    {
        auto* task = new std::function<void()>(std::move(fn));
        emscripten_async_call([](void* p)
        {
            std::unique_ptr<std::function<void()>> f(static_cast<std::function<void()>*>(p));
            (*f)();
        }, task, 0);
    }
}
