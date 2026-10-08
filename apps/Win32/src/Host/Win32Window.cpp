// ── MiniCAD 桌面版宿主：窗口、D3D11 设备、消息循环与平台服务 ─────────
#include "Host/Win32Window.h"
#include "Render/D3D11/D3D11Renderer.h"
#include "Core/Log.h"
#include "Core/UIContext.h"
#include "D3D11/D3D11Backend.h"
#include "Platform/Win32/Win32Fonts.h"
#include "Platform/Win32/Win32Frame.h"
#include "Platform/Win32/Win32Input.h"
#include "Widgets/TitleBar.h"
#include <commdlg.h>
#include <algorithm>
#include <filesystem>

#pragma comment(lib, "comdlg32.lib")

namespace MiniCAD
{
    namespace
    {
        std::wstring ToWide(const std::string& utf8)
        {
            const int n = MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), -1, nullptr, 0);
            std::wstring w(static_cast<size_t>(n > 0 ? n - 1 : 0), L'\0');
            if (n > 1)
                MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), -1, w.data(), n);
            return w;
        }

        std::string ToUtf8(const wchar_t* w)
        {
            const int n = WideCharToMultiByte(CP_UTF8, 0, w, -1, nullptr, 0, nullptr, nullptr);
            std::string s(static_cast<size_t>(n > 0 ? n - 1 : 0), '\0');
            if (n > 1)
                WideCharToMultiByte(CP_UTF8, 0, w, -1, s.data(), n, nullptr, nullptr);
            return s;
        }

        // 打开 / 保存文件对话框（返回 UTF-8 路径，空串 = 取消）
        std::string ShowFileDialog(HWND owner, bool save, FileKind kind, const std::string& suggestedName)
        {
            wchar_t buf[MAX_PATH] = L"";
            if (save && !suggestedName.empty())
                wcsncpy_s(buf, ToWide(suggestedName).c_str(), _TRUNCATE);

            OPENFILENAMEW ofn = {};
            ofn.lStructSize = sizeof(ofn);
            ofn.hwndOwner   = owner;
            // 打开：首项为全部可识别格式；保存：扩展名决定写出格式（.mcad / .dwg / .dxf / .json）
            if (kind == FileKind::Image)
                ofn.lpstrFilter = L"图像文件 (*.png;*.jpg;*.jpeg;*.bmp;*.tga;*.gif)\0*.png;*.jpg;*.jpeg;*.bmp;*.tga;*.gif\0所有文件 (*.*)\0*.*\0";
            else if (save)
                ofn.lpstrFilter = L"MiniCAD 文档 (*.mcad)\0*.mcad\0AutoCAD 图形 (*.dwg)\0*.dwg\0AutoCAD 交换文件 (*.dxf)\0*.dxf\0JSON 文件 (*.json)\0*.json\0";
            else
                ofn.lpstrFilter = L"所有支持的文件\0*.mcad;*.dwg;*.dxf;*.json\0MiniCAD 文档 (*.mcad)\0*.mcad\0AutoCAD 图形 (*.dwg)\0*.dwg\0AutoCAD 交换文件 (*.dxf)\0*.dxf\0JSON 文件 (*.json)\0*.json\0所有文件 (*.*)\0*.*\0";
            ofn.lpstrFile   = buf;
            ofn.nMaxFile    = MAX_PATH;
            ofn.lpstrDefExt = (save || kind == FileKind::Image) ? nullptr : L"mcad";   // 保存时按所选筛选器补扩展名（见下）
            ofn.Flags       = OFN_NOCHANGEDIR | (save ? OFN_OVERWRITEPROMPT : (OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST));
            if (!(save ? GetSaveFileNameW(&ofn) : GetOpenFileNameW(&ofn)))
                return {};
            const wchar_t* fileName = wcsrchr(buf, L'\\') ? wcsrchr(buf, L'\\') + 1 : buf;
            if (save && !wcschr(fileName, L'.'))
            {
                static const wchar_t* const kExt[] = { L".mcad", L".dwg", L".dxf", L".json" };
                const DWORD idx = ofn.nFilterIndex >= 1 && ofn.nFilterIndex <= 4 ? ofn.nFilterIndex - 1 : 0;
                wcsncat_s(buf, kExt[idx], _TRUNCATE);
            }
            return ToUtf8(buf);
        }
    }

    Win32Window::Win32Window() = default;

    Win32Window::~Win32Window()
    {
        if (m_uiWatch)
            FindCloseChangeNotification(m_uiWatch);
        // 成员按声明的相反顺序销毁：输入层、界面先于界面后端和设备
    }

    // =========================================================
    // 初始化
    // =========================================================
    bool Win32Window::Initialize(const wchar_t* title, int width, int height)
    {
        if (!InitWindow(title, width, height))
            return false;

        RECT rc{};
        GetClientRect(m_hwnd, &rc);
        const int w = rc.right - rc.left;
        const int h = rc.bottom - rc.top;
        if (!InitD3D11(w, h))
            return false;

        m_backend  = std::make_unique<MiniGUI::D3D11Backend>(m_device->GetDevice(), m_device->GetContext());
        m_dpiScale = static_cast<float>(GetDpiForWindow(m_hwnd)) / 96.0f;
        if (!m_main.Initialize(*this, static_cast<float>(w) / m_dpiScale, static_cast<float>(h) / m_dpiScale, m_dpiScale))
            return false;
        m_input = std::make_unique<MiniGUI::Win32Input>(&m_main.GetUI(), m_hwnd);

        // ── 无边框窗口：标题栏的空白处可以拖动窗口 ───────────────────
        m_frame->SetCaptionTest([this](int x, int y)
        {
            MiniGUI::TitleBar* bar = m_main.GetTitleBar();
            return bar && bar->IsCaptionAt({ static_cast<float>(x) / m_dpiScale, static_cast<float>(y) / m_dpiScale });
        });
        m_frame->SetOnActiveChanged([this](bool active)
        {
            if (MiniGUI::TitleBar* bar = m_main.GetTitleBar())
                bar->SetWindowActive(active);
        });

        WatchUiFile();
        ShowWindow(m_hwnd, SW_SHOW);
        return true;
    }

    bool Win32Window::InitWindow(const wchar_t* title, int width, int height)
    {
        HINSTANCE hInstance = GetModuleHandleW(nullptr);

        WNDCLASSEXW wc = {};
        wc.cbSize        = sizeof(wc);
        wc.style         = CS_DBLCLKS;
        wc.lpfnWndProc   = WndProc;
        wc.hInstance     = hInstance;
        wc.hCursor       = nullptr;     // 光标由 MiniGUI 在 WM_SETCURSOR 设置（边框上由系统设置）
        wc.lpszClassName = L"MiniCADWin";
        wc.hIcon         = (HICON)LoadImageW(nullptr, L"icons/app.ico", IMAGE_ICON, 0, 0, LR_LOADFROMFILE | LR_DEFAULTSIZE);
        wc.hIconSm       = (HICON)LoadImageW(nullptr, L"icons/app.ico", IMAGE_ICON, GetSystemMetrics(SM_CXSMICON),
                                             GetSystemMetrics(SM_CYSMICON), LR_LOADFROMFILE);
        RegisterClassExW(&wc);

        // 无边框：窗口矩形就是客户区。按系统缩放放大初始尺寸，居中到主显示器的工作区
        const float scale = static_cast<float>(GetDpiForSystem()) / 96.0f;
        const int   w     = static_cast<int>(width * scale);
        const int   h     = static_cast<int>(height * scale);
        MONITORINFO mi    = { sizeof(mi) };
        GetMonitorInfoW(MonitorFromPoint(POINT{ 0, 0 }, MONITOR_DEFAULTTOPRIMARY), &mi);
        const int x = mi.rcWork.left + std::max(0, static_cast<int>(mi.rcWork.right - mi.rcWork.left - w) / 2);
        const int y = mi.rcWork.top  + std::max(0, static_cast<int>(mi.rcWork.bottom - mi.rcWork.top - h) / 2);

        m_hwnd = CreateWindowExW(0, wc.lpszClassName, title, WS_OVERLAPPEDWINDOW,
                                 x, y, w, h, nullptr, nullptr, hInstance, this);
        if (!m_hwnd)
            return false;
        m_frame = std::make_unique<MiniGUI::Win32Frame>(m_hwnd);
        m_frame->Apply();       // 先保存再应用：Apply 触发的 WM_NCCALCSIZE 要经过 m_frame
        return true;
    }

    bool Win32Window::InitD3D11(int width, int height)
    {
        m_device = std::make_unique<Device>();
        m_device->Initialize();

        SwapChain::Options opt;
        opt.enableVSync  = true;    // 帧率钉在刷新率；输入在 Present 之前全部处理完，不会因此晚一帧
        opt.allowTearing = false;
        m_swapChain = std::make_unique<SwapChain>();
        m_swapChain->Initialize(m_device.get(), m_hwnd, width, height, opt);

        m_renderer   = std::make_unique<D3D11Renderer>(m_device->GetDevice(), m_device->GetContext());
        m_viewportRT = std::make_unique<D3D11RenderTarget>(m_device->GetDevice());
        m_viewportRT->Create(width, height);
        return true;
    }

    void Win32Window::UpdateDisplaySize()
    {
        RECT rc{};
        GetClientRect(m_hwnd, &rc);
        m_dpiScale = static_cast<float>(GetDpiForWindow(m_hwnd)) / 96.0f;
        const float w = static_cast<float>(rc.right - rc.left);
        const float h = static_cast<float>(rc.bottom - rc.top);
        m_main.OnResize(w / m_dpiScale, h / m_dpiScale, m_dpiScale);
    }

    void Win32Window::WatchUiFile()
    {
        const std::string& path = m_main.GetUiPath();
        const std::wstring dir  = std::filesystem::path(std::u8string(path.begin(), path.end())).parent_path().wstring();
        m_uiWatch = FindFirstChangeNotificationW(dir.c_str(), FALSE,
                                                 FILE_NOTIFY_CHANGE_LAST_WRITE | FILE_NOTIFY_CHANGE_FILE_NAME | FILE_NOTIFY_CHANGE_SIZE);
        if (m_uiWatch == INVALID_HANDLE_VALUE)
        {
            m_uiWatch = nullptr;
            LOG_WARN("无法监视界面描述文件所在目录，修改后请按 F5 重新加载");
        }
    }

    // =========================================================
    // 消息
    // =========================================================
    LRESULT Win32Window::WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
    {
        Win32Window* self = nullptr;
        if (msg == WM_NCCREATE)
        {
            self = static_cast<Win32Window*>(reinterpret_cast<CREATESTRUCTW*>(lParam)->lpCreateParams);
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
        }
        else
        {
            self = reinterpret_cast<Win32Window*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
        }
        return self ? self->EventProc(hwnd, msg, wParam, lParam) : DefWindowProcW(hwnd, msg, wParam, lParam);
    }

    LRESULT Win32Window::EventProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
    {
        LRESULT result = 0;
        // 无边框窗口的非客户区消息先处理（命中测试要在 MiniGUI 之前）
        if (m_frame && m_frame->HandleMessage(hwnd, msg, wParam, lParam, result))
            return result;
        if (m_input && m_input->HandleMessage(hwnd, msg, wParam, lParam, result))
            return result;

        switch (msg)
        {
        case WM_ERASEBKGND:
            return 1;

        case WM_SIZE:
            if (m_swapChain && wParam != SIZE_MINIMIZED)
            {
                m_swapChain->Resize(LOWORD(lParam), HIWORD(lParam));
                if (m_input)        // 界面初始化完成之后
                    UpdateDisplaySize();
            }
            return 0;

        case WM_GETMINMAXINFO:
        {
            auto* mmi = reinterpret_cast<MINMAXINFO*>(lParam);
            const float scale = static_cast<float>(GetDpiForWindow(hwnd)) / 96.0f;
            mmi->ptMinTrackSize = { static_cast<LONG>(640 * scale), static_cast<LONG>(420 * scale) };
            return 0;
        }

        case WM_DPICHANGED:
        {
            const RECT* r = reinterpret_cast<const RECT*>(lParam);
            SetWindowPos(hwnd, nullptr, r->left, r->top, r->right - r->left, r->bottom - r->top,
                         SWP_NOZORDER | SWP_NOACTIVATE);
            if (m_input)
                UpdateDisplaySize();
            return 0;
        }

        case WM_PAINT:
            // 消息队列里没有其他消息时才会收到 WM_PAINT：此时本帧之前的输入已全部交给 Editor
            if (m_input)
                m_main.RenderFrame();
            ValidateRect(hwnd, nullptr);
            return 0;

        case WM_CLOSE:
            m_main.RequestExit();   // 有未保存的文档时先询问
            return 0;

        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;

        default:
            return DefWindowProcW(hwnd, msg, wParam, lParam);
        }
    }

    int Win32Window::Run()
    {
        // 空闲时阻塞等待（消息或界面描述文件所在目录的变化），CPU 占用为 0；需要重绘时 UIContext 回调 InvalidateRect
        while (true)
        {
            const DWORD count = m_uiWatch ? 1 : 0;
            const DWORD r = MsgWaitForMultipleObjectsEx(count, &m_uiWatch, INFINITE, QS_ALLINPUT, MWMO_INPUTAVAILABLE);
            if (count && r == WAIT_OBJECT_0)
            {
                FindNextChangeNotification(m_uiWatch);
                m_main.CheckUiFileChanged();
            }

            MSG msg = {};
            while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE))
            {
                if (msg.message == WM_QUIT)
                    return static_cast<int>(msg.wParam);
                TranslateMessage(&msg);
                DispatchMessageW(&msg);
            }
        }
    }

    // =========================================================
    // AppPlatform：图形
    // =========================================================
    MiniGUI::IRenderBackend& Win32Window::GetUiBackend()
    {
        return *m_backend;
    }

    MiniGUI::TextureId Win32Window::RegisterViewportTexture(void* nativeShaderResource)
    {
        return m_backend->RegisterExternalTexture(static_cast<ID3D11ShaderResourceView*>(nativeShaderResource));
    }

    bool Win32Window::LoadUiFonts(MiniGUI::TextSystem& text)
    {
        return MiniGUI::LoadSystemUIFonts(text);    // 微软雅黑 UI + Segoe UI Symbol
    }

    void Win32Window::RequestRedraw()
    {
        InvalidateRect(m_hwnd, nullptr, FALSE);
    }

    void Win32Window::BeginUiPass()
    {
        ID3D11DeviceContext*    ctx = m_device->GetContext();
        ID3D11RenderTargetView* rtv = m_swapChain->GetRTV();
        const float clear[4] = { 0.1f, 0.1f, 0.1f, 1.0f };
        ctx->OMSetRenderTargets(1, &rtv, nullptr);
        ctx->ClearRenderTargetView(rtv, clear);
    }

    void Win32Window::EndUiPass()
    {
        m_swapChain->Present();
        if (m_input)
            m_input->Sync();        // 按焦点开关输入法，候选窗跟随光标
    }

    void Win32Window::WaitForGpu()
    {
        // 事件查询：GPU 执行完之前提交的全部命令后才返回（只用于自测计时）
        D3D11_QUERY_DESC desc = { D3D11_QUERY_EVENT, 0 };
        Microsoft::WRL::ComPtr<ID3D11Query> query;
        if (FAILED(m_device->GetDevice()->CreateQuery(&desc, &query)))
            return;
        ID3D11DeviceContext* ctx = m_device->GetContext();
        ctx->End(query.Get());
        BOOL done = FALSE;
        while (ctx->GetData(query.Get(), &done, sizeof(done), 0) != S_OK || !done)
            ;
    }

    // =========================================================
    // AppPlatform：窗口
    // =========================================================
    void Win32Window::SetTitle(const std::string& utf8)
    {
        SetWindowTextW(m_hwnd, ToWide(utf8).c_str());
    }

    bool Win32Window::IsWindowActive() const    { return GetActiveWindow() == m_hwnd; }
    bool Win32Window::IsWindowMaximized() const { return m_frame && m_frame->IsWindowMaximized(); }
    void Win32Window::MinimizeWindow()          { m_frame->Minimize(); }
    void Win32Window::ToggleMaximizeWindow()    { m_frame->ToggleMaximize(); }
    void Win32Window::CloseWindow()             { PostMessageW(m_hwnd, WM_CLOSE, 0, 0); }
    void Win32Window::Quit()                    { DestroyWindow(m_hwnd); }

    // =========================================================
    // AppPlatform：文件
    // =========================================================
    void Win32Window::PickFile(FileKind kind, std::function<void(const std::string& path)> done)
    {
        const std::string path = ShowFileDialog(m_hwnd, false, kind, {});
        if (!path.empty() && done)
            done(path);
    }

    std::string Win32Window::ChooseSavePath(const std::string& suggestedName)
    {
        return ShowFileDialog(m_hwnd, true, FileKind::Drawing, suggestedName);
    }

    std::string Win32Window::GetResourceDir() const
    {
        wchar_t buf[MAX_PATH] = L"";
        GetModuleFileNameW(nullptr, buf, MAX_PATH);
        const std::u8string u8 = std::filesystem::path(buf).parent_path().u8string();
        return std::string(u8.begin(), u8.end());
    }

    std::string Win32Window::GetUserDataDir() const
    {
        // %LOCALAPPDATA%\MiniCAD
        wchar_t buf[MAX_PATH] = L"";
        const DWORD n = GetEnvironmentVariableW(L"LOCALAPPDATA", buf, MAX_PATH);
        if (n == 0 || n >= MAX_PATH)
            return {};
        const std::u8string u8 = (std::filesystem::path(buf) / L"MiniCAD").u8string();
        return std::string(u8.begin(), u8.end());
    }
}
