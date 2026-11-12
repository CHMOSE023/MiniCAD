// ── MiniCAD 网页版宿主：图形设备、浏览器文件选择与下载、从网址打开、页面关闭提示 ─────────
#include "Host/WebApp.h"
#include "Host/WebEmbed.h"
#include "Core/Log.h"
#include "MiniI18n/I18n.h"
#include "Core/UIContext.h"
#include "Document/Document.h"
#include "GUI/Language.h"
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
#include <cstdlib>
#include <filesystem>
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

    // 网址的 ?open=<URL>（可重复）：逐个取回，写进 /remote/<请求号>/<序号>/<文件名>，每个回调一次，最后回调完成。
    // 返回网址个数（0 = 没有 ?open=，不回调）。
    // 默认只取本网站的网址（带 Cookie，same-origin）；嵌入它的网页提供了 window.MiniCADHost.fetch(url) 时由它取，
    // 可以加请求头、取别处的文件。文件名依次取：Content-Disposition、网址路径的最后一段（是图纸扩展名时）、
    // 值是图纸路径的查询参数（如 ?path=项目/一层平面.dwg）。失败时 status 为 HTTP 状态码，-1 为不是本网站的网址，0 为网络错误。
    // EM_JS 的代码经 C 预处理器变成 C 字符串再编译：字符串引号以外不能写反斜杠转义（\\ 会变成 \，\. 变成 .），
    // 引号要成对。所以正则不用反斜杠（用 [.] [*]），里面的引号写成 \x22 / \x27（编译后正好是引号）。
    // urlsJson 不为空时打开它列出的网址（JSON 数组，嵌入网页的 window.MiniCAD.open），否则用网址的 ?open=；
    // 全部处理完后还会通知 Module.miniCADEmbed.opened(请求号)（有桥接接口时）
    EM_JS(int, MiniCADWeb_OpenUrls, (int requestId, const char* urlsJson), {
        let urls;
        try {
            const given = urlsJson ? UTF8ToString(urlsJson) : "";
            urls = given ? JSON.parse(given).map(String) : new URLSearchParams(location.search).getAll("open");
            urls = urls.filter(s => s.length > 0);
        } catch (e) { urls = []; }
        if (urls.length === 0) return 0;
        const host = window.MiniCADHost;
        const hostFetch = !!host && typeof host.fetch === "function";
        const QUOTE = String.fromCharCode(34), BACKSLASH = String.fromCharCode(92);
        const isDrawing = s => /[.](dwg|dxf|mcad|json)$/i.test(s);
        const lastSegment = s => s.split(BACKSLASH).join("/").split("/").filter(p => p.length > 0).pop() || "";
        const decode = s => { try { return decodeURIComponent(s); } catch (e) { return s; } };
        const guessName = (url) => {
            const seg = lastSegment(decode(url.pathname));
            if (isDrawing(seg)) return seg;
            for (const [, v] of url.searchParams)
                if (isDrawing(v)) return lastSegment(v);
            return seg;
        };
        const nameOf = (url, response) => {
            const cd = response.headers.get("Content-Disposition") || "";
            let m = /filename[*] *= *UTF-8\x27\x27([^;]+)/i.exec(cd);
            if (m) return lastSegment(decode(m[1].trim()));
            m = /filename *= *\x22?([^\x22;]+)\x22?/i.exec(cd);
            if (m) return lastSegment(m[1].trim());
            return guessName(url);
        };
        const safeName = s => {
            const n = Array.from(s, c => ("/:*?<>|".includes(c) || c === QUOTE || c === BACKSLASH ? "_" : c)).join("");
            return n && n !== "." && n !== ".." ? n : "drawing.dwg";
        };
        const fetched = (path, url) => {
            const p = stringToNewUTF8(path), u = stringToNewUTF8(url);
            _MiniCADWeb_OnRemoteFetched(requestId, p, u);
            _free(p); _free(u);
        };
        const failed = (name, status, message) => {
            const n = stringToNewUTF8(name), m = stringToNewUTF8(message || "");
            _MiniCADWeb_OnRemoteFailed(requestId, n, status, m);
            _free(n); _free(m);
        };
        (async () => {
            for (let i = 0; i < urls.length; ++i) {
                let url;
                try { url = new URL(urls[i], location.href); } catch (e) { failed(urls[i], 0, String(e)); continue; }
                const display = guessName(url) || url.href;
                if (!hostFetch && url.origin !== location.origin) { failed(display, -1, ""); continue; }
                try {
                    const response = hostFetch ? await host.fetch(url.href) : await fetch(url.href, { credentials: "same-origin" });
                    if (!response.ok) { failed(display, response.status, response.statusText); continue; }
                    const buf = await response.arrayBuffer();
                    const directory = "/remote/" + requestId + "/" + i;
                    FS.mkdirTree(directory);
                    const path = directory + "/" + safeName(nameOf(url, response));
                    FS.writeFile(path, new Uint8Array(buf));
                    fetched(path, url.href);
                } catch (err) {
                    failed(display, 0, String(err));
                }
            }
            _MiniCADWeb_OnRemoteCompleted(requestId);
            if (Module.miniCADEmbed) Module.miniCADEmbed.opened(requestId);
        })();
        return urls.length;
    });

    // 从网址打开的图纸缺外部参照时，交给嵌入它的网页找：
    //   window.MiniCADHost.findXrefs({ drawingUrl, names }) → Promise<[{ name, url }]>（只列找到的）
    // 取回的文件写进 /xref/<请求号>/<name>，完成后回调目录（一个也没取到时为空串）与出错信息。
    // 网页没有提供 findXrefs 时返回 0（不回调），由用户选文件夹
    EM_JS(int, MiniCADWeb_FindXrefs, (int requestId, const char* urlPtr, const char* namesPtr), {
        const host = window.MiniCADHost;
        if (!host || typeof host.findXrefs !== "function") return 0;
        const drawingUrl = UTF8ToString(urlPtr);
        const names = UTF8ToString(namesPtr).split("\n").filter(s => s.length > 0);
        const root = "/xref/" + requestId;
        const finish = (dir, error) => {
            const d = stringToNewUTF8(dir), e = stringToNewUTF8(error || "");
            _MiniCADWeb_OnXrefsFetched(requestId, d, e);
            _free(d); _free(e);
        };
        (async () => {
            let found;
            try { found = await host.findXrefs({ drawingUrl, names }); }
            catch (err) { finish("", String(err)); return; }
            const errors = [];
            let count = 0;
            for (const f of Array.isArray(found) ? found : []) {
                if (!f || !f.url || !f.name) continue;
                const name = Array.from(String(f.name), c => ("/:*?<>|".includes(c) || c.charCodeAt(0) === 34 || c.charCodeAt(0) === 92 ? "_" : c)).join("");
                if (name === "." || name === "..") continue;
                try {
                    const response = typeof host.fetch === "function" ? await host.fetch(f.url) : await fetch(f.url, { credentials: "same-origin" });
                    if (!response.ok) throw new Error("HTTP " + response.status);
                    const buf = await response.arrayBuffer();
                    FS.mkdirTree(root);
                    FS.writeFile(root + "/" + name, new Uint8Array(buf));
                    ++count;
                } catch (err) {
                    errors.push(name + ": " + String(err));
                }
            }
            finish(count > 0 ? root : "", errors.join("\n"));
        })();
        return 1;
    });

    // 把虚拟文件系统里的文件下载到本机
    EM_JS(void, MiniCADWeb_Download, (const char* pathPtr), {
        const path = UTF8ToString(pathPtr);
        let data;
        try { data = FS.readFile(path); } catch (e) { console.error("MiniCAD: file to download not found " + path); return; }
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

    // 界面语言：网址的 ?lang=（优先）、浏览器语言；"设置 > 语言"存在 localStorage（网页版没有用户数据目录）。
    // 返回的字符串由调用方 free
    EM_JS(char*, MiniCADWeb_UrlLanguage, (), {
        try { return stringToNewUTF8(new URLSearchParams(location.search).get("lang") || ""); } catch (e) { return stringToNewUTF8(""); }
    });

    EM_JS(char*, MiniCADWeb_BrowserLanguage, (), {
        return stringToNewUTF8(navigator.language || "");
    });

    EM_JS(void, MiniCADWeb_SetDocumentLanguage, (const char* code), {
        document.documentElement.lang = UTF8ToString(code);
    });

    EM_JS(char*, MiniCADWeb_ReadSetting, (const char* key), {
        try { return stringToNewUTF8(localStorage.getItem("MiniCAD." + UTF8ToString(key)) || ""); } catch (e) { return stringToNewUTF8(""); }
    });

    EM_JS(int, MiniCADWeb_WriteSetting, (const char* key, const char* value), {
        try { localStorage.setItem("MiniCAD." + UTF8ToString(key), UTF8ToString(value)); return 1; } catch (e) { return 0; }
    });

    namespace
    {
        std::string TakeJsString(char* s)
        {
            std::string r = s ? s : "";
            std::free(s);
            return r;
        }
    }

    EM_JS(void, MiniCADWeb_SetTitle, (const char* title), {
        document.title = UTF8ToString(title);
    });

    // 有未保存的文档时，关闭或刷新页面前让浏览器询问
    // 下载网页旁 fonts/ 下的字体，写进虚拟文件系统的 /fonts/
    EM_JS(void, MiniCADWeb_FetchFont, (const char* namePtr), {
        const name = UTF8ToString(namePtr);
        fetch("fonts/" + name)
            .then(r => { if (!r.ok) throw new Error("HTTP " + r.status); return r.arrayBuffer(); })
            .then(buf => {
                FS.mkdirTree("/fonts");
                FS.writeFile("/fonts/" + name, new Uint8Array(buf));
                const p = stringToNewUTF8(name);
                _MiniCADWeb_OnFontFetched(p);
                _free(p);
            })
            .catch(err => console.warn("MiniCAD: font " + name + ": " + err));
    });

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

        EMSCRIPTEN_KEEPALIVE void MiniCADWeb_OnFontFetched(const char* name)
        {
            if (g_app)
                g_app->OnFontFetched(name);
        }

        EMSCRIPTEN_KEEPALIVE void MiniCADWeb_OnRemoteFetched(int requestId, const char* path, const char* url)
        {
            if (g_app)
                g_app->OnRemoteFetched(requestId, path, url);
        }

        EMSCRIPTEN_KEEPALIVE void MiniCADWeb_OnRemoteFailed(int requestId, const char* name, int status, const char* message)
        {
            if (g_app)
                g_app->OnRemoteFailed(requestId, name, status, message);
        }

        EMSCRIPTEN_KEEPALIVE void MiniCADWeb_OnRemoteCompleted(int requestId)
        {
            if (g_app)
                g_app->OnRemoteCompleted(requestId);
        }

        EMSCRIPTEN_KEEPALIVE void MiniCADWeb_OnXrefsFetched(int requestId, const char* dir, const char* error)
        {
            if (g_app)
                g_app->OnXrefsFetched(requestId, dir, error);
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

    std::string WebApp::GetSystemLanguage() const
    {
        return TakeJsString(MiniCADWeb_BrowserLanguage());
    }

    void WebApp::OnLanguageChanged(const std::string& code)
    {
        MiniCADWeb_SetDocumentLanguage(code.c_str());
        const std::string font = MiniGUI::BundledUiFontForLanguage(code);
        for (const char* name : MiniGUI::kWebLazyFontNames)
            if (font == name)
                FetchLazyFont(font);
    }

    void WebApp::FetchLazyFont(const std::string& name)
    {
        if (m_fontRequests.insert(name).second)
            MiniCADWeb_FetchFont(name.c_str());
    }

    void WebApp::OnFontFetched(const char* name)
    {
        LOG_INFO("MiniCAD web: font %s loaded", name);
        if (m_uiReady)
            m_main.OnFontsAdded();
    }

    std::string WebApp::ReadUserSetting(const std::string& key) const
    {
        return TakeJsString(MiniCADWeb_ReadSetting(key.c_str()));
    }

    bool WebApp::WriteUserSetting(const std::string& key, const std::string& value)
    {
        return MiniCADWeb_WriteSetting(key.c_str(), value.c_str()) != 0;
    }

    bool WebApp::Initialize()
    {
        OnLanguageChanged(InitLanguage(*this, TakeJsString(MiniCADWeb_UrlLanguage())));    // 在请求图形设备之前：它的出错信息也要翻译
        m_embed = IsWebEmbedRequested();
        if (!m_host.Initialize())
            return false;
        if (MiniCADWeb_PreferWebGL())
        {
            LOG_INFO_TR("MiniCAD 网页版：网址带 ?webgl，使用 WebGL2");
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
                    LOG_INFO_TR("MiniCAD 网页版：WebGPU 不可用，改用 WebGL2");
                    ok = InitializeWebGL();
                }
                if (!ok)
                    ReportFailure(Tr("初始化失败（浏览器需要支持 WebGPU 或 WebGL2）").c_str());
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
        LOG_INFO_TR("MiniCAD 网页版：图形接口 {0}", m_graphics->GetName());
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
        m_uiReady = true;
        if (m_embed)
            InstallWebEmbed(*this);     // window.MiniCAD：界面就绪后才导出，网页据此判断可以调用
        m_host.RequestFrame();
        OpenStartupUrls();
        return true;
    }

    void WebApp::ReportFailure(const char* what)
    {
        std::printf("MiniCAD: %s\n", what);
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

    std::vector<std::shared_ptr<MiniGUI::Font>> WebApp::LoadLanguageUiFonts(const std::string& language)
    {
        return MiniGUI::LoadWebLanguageFonts(language);
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
        LOG_INFO_TR("MiniCAD 网页版：关闭浏览器标签页即可退出");
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
                           : ".mcad,.dwg,.dxf,.exb,.json";
        MiniCADWeb_PickFile(id, accept, 0);
    }

    void WebApp::PickDrawingFiles(std::function<void(const std::vector<std::string>&)> done)
    {
        // 图纸里可能有繁体、日文：选文件、读图纸的同时下载
        for (const char* name : MiniGUI::kWebLazyFontNames)
            FetchLazyFont(name);
        const int id = m_nextPickId++;
        m_pickRequests[id] = { std::move(done), {} };
        MiniCADWeb_PickFile(id, ".mcad,.dwg,.dxf,.exb,.json", 1);
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
            MiniGUI::ShowMessageBox(m_main.GetUI(), Tr("读取文件失败"), error, { Tr("确定") }, [](int) {});
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
            MiniGUI::ShowMessageBox(m_main.GetUI(), Tr("读取文件夹失败"), error, { Tr("确定") }, [](int) {});
        if (done && dir && *dir)
            done(dir);
        m_host.RequestFrame();
    }

    // =========================================================
    // 从网址打开（?open=）与到图纸所在的网站找外部参照
    // =========================================================
    namespace
    {
        // OpenDrawings 后文档路径可能被规范化：存、查都用规范化后的路径
        std::string RemoteKey(const std::string& path)
        {
            return std::filesystem::path(path).lexically_normal().generic_string();
        }
    }

    void WebApp::OpenStartupUrls()
    {
        const int id = m_nextPickId++;
        m_remoteRequests[id] = {};
        if (MiniCADWeb_OpenUrls(id, nullptr) == 0)
        {
            m_remoteRequests.erase(id);
            return;
        }
        // 图纸里可能有繁体、日文：取文件、读图纸的同时下载（同 PickDrawingFiles）
        for (const char* name : MiniGUI::kWebLazyFontNames)
            FetchLazyFont(name);
    }

    void WebApp::SetEmbedCompact(bool compact)
    {
        if (m_embedCompact == compact)
            return;
        m_embedCompact = compact;
        if (m_uiReady)      // 按当前尺寸重新选布局（MainFrame::WantsCompactLayout 先看 ForcesCompactLayout）
            m_main.OnResize(m_host.GetLogicalWidth(), m_host.GetLogicalHeight(), m_host.GetPixelScale());
        m_host.RequestFrame();
    }

    int WebApp::OpenUrls(const std::string& urlsJson)
    {
        const int id = m_nextPickId++;
        m_remoteRequests[id] = {};
        if (MiniCADWeb_OpenUrls(id, urlsJson.c_str()) == 0)
        {
            m_remoteRequests.erase(id);
            return 0;
        }
        for (const char* name : MiniGUI::kWebLazyFontNames)
            FetchLazyFont(name);
        return id;
    }

    void WebApp::OnRemoteFetched(int requestId, const char* path, const char* url)
    {
        auto it = m_remoteRequests.find(requestId);
        if (it == m_remoteRequests.end() || !path || !*path)
            return;
        it->second.paths.emplace_back(path);
        m_remoteSources[RemoteKey(path)] = url ? url : "";
    }

    void WebApp::OnRemoteFailed(int requestId, const char* name, int status, const char* message)
    {
        auto it = m_remoteRequests.find(requestId);
        if (it == m_remoteRequests.end())
            return;
        std::string reason;
        switch (status)
        {
        case -1:  reason = Tr("只能打开本网站上的文件"); break;
        case 0:   reason = message && *message ? std::string(message) : Tr("网络错误"); break;
        case 401: reason = Tr("需要登录，或登录已过期：请重新登录后再打开"); break;
        case 403: reason = Tr("没有权限访问这个文件"); break;
        case 404: reason = Tr("文件不存在"); break;
        default:  reason = TrF("服务器返回 {0}", status); break;
        }
        std::string& errors = it->second.errors;
        if (!errors.empty()) errors += "\n\n";
        errors += std::string(name ? name : "") + "\n" + reason;
    }

    void WebApp::OnRemoteCompleted(int requestId)
    {
        auto it = m_remoteRequests.find(requestId);
        if (it == m_remoteRequests.end())
            return;
        RemoteRequest request = std::move(it->second);
        m_remoteRequests.erase(it);
        if (!request.paths.empty())
            m_main.OpenDrawings(request.paths);
        if (!request.errors.empty())
            MiniGUI::ShowMessageBox(m_main.GetUI(), Tr("打开图纸失败"), request.errors, { Tr("确定") }, [](int) {});
        m_host.RequestFrame();
    }

    bool WebApp::FindXrefsForDrawing(const std::string& drawingPath, const std::vector<std::string>& fileNames,
                                     std::function<void(const std::string& dir)> done)
    {
        // 只管从网址打开的图纸；本机选的图纸照旧由用户选文件夹
        auto source = m_remoteSources.find(RemoteKey(drawingPath));
        if (source == m_remoteSources.end() || source->second.empty() || fileNames.empty())
            return false;
        std::string names;
        for (const std::string& n : fileNames)
        {
            names += n;
            names += '\n';
        }
        const int id = m_nextPickId++;
        m_xrefRequests[id] = std::move(done);
        if (MiniCADWeb_FindXrefs(id, source->second.c_str(), names.c_str()) == 0)
        {
            m_xrefRequests.erase(id);       // 网页没有提供 findXrefs
            return false;
        }
        return true;
    }

    void WebApp::OnXrefsFetched(int requestId, const char* dir, const char* error)
    {
        auto it = m_xrefRequests.find(requestId);
        if (it == m_xrefRequests.end())
            return;
        auto done = std::move(it->second);
        m_xrefRequests.erase(it);
        // 不单独弹框：还缺的参照随后由 MainFrame 一并提示，可再选本地文件夹
        if (error && *error)
            LOG_WARN("MiniCAD web: xref lookup by host page: %s", error);
        if (done)
            done(dir ? dir : "");
        m_host.RequestFrame();
    }

    std::string WebApp::ChooseSavePath(const std::string& suggestedName, CadSaveVersion&)
    {
        // 浏览器没有"另存为"对话框：保存到内存文件系统，保存完成后（OnDocumentSaved）下载。
        // 已经带 .mcad / .dwg / .dxf / .json 扩展名的按原格式保存，否则存为 .mcad
        std::string name = suggestedName.empty() ? Tr("未命名") : suggestedName;
        std::replace(name.begin(), name.end(), '/', '_');
        if (!HasExtension(name, ".mcad") && !HasExtension(name, ".dwg") && !HasExtension(name, ".dxf") && !HasExtension(name, ".json"))
            name += ".mcad";
        EM_ASM({ try { FS.mkdir("/download"); } catch (e) {} });
        return "/download/" + name;
    }

    std::string WebApp::ChooseExportPath(FileKind kind, const std::string& suggestedName)
    {
        // 与 ChooseSavePath 相同：写进内存文件系统，写完后 OnDocumentSaved 下载
        std::string name = suggestedName.empty() ? Tr("未命名") : suggestedName;
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
