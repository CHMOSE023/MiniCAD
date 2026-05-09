#include "MainWindow.h"  
#include "Document/DocumentManager.h"
#include "Render/D3D11/SwapChain.h"
#include <imgui.h>  
#include <dwmapi.h>
#include <cstdint>
#include <memory>

#pragma comment(lib, "dwmapi.lib")
extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

namespace MiniCAD
{
	// =========================================================
    // 辅助函数：从 ImGui IO 构建修饰键掩码
    // =========================================================
	static uint8_t BuildModifiersFromIO(const ImGuiIO& io)
	{
		uint8_t m = 0;
		if (io.KeyShift) m |= static_cast<uint8_t>(ModifierKey::Shift);
		if (io.KeyCtrl)  m |= static_cast<uint8_t>(ModifierKey::Ctrl);
		if (io.KeyAlt)   m |= static_cast<uint8_t>(ModifierKey::Alt);
		return m;
	}

	// =========================================================
	// 辅助函数：从 ImGui IO 构建鼠标按键掩码
	// =========================================================
	static uint8_t BuildMouseButtonsFromIO(const ImGuiIO& io)
	{
		uint8_t b = 0;
		if (io.MouseDown[ImGuiMouseButton_Left])   b |= static_cast<uint8_t>(MouseButtonState::Left);
		if (io.MouseDown[ImGuiMouseButton_Middle]) b |= static_cast<uint8_t>(MouseButtonState::Middle);
		if (io.MouseDown[ImGuiMouseButton_Right])  b |= static_cast<uint8_t>(MouseButtonState::Right);
		return b;
	}

	MainWindow::MainWindow()
		: m_hwnd(0)
		, m_device(nullptr)
		, m_swapChain(nullptr)
		, m_renderer(nullptr) 
		, m_docManager()
	{}

	MainWindow::~MainWindow()
	{}

	bool MainWindow::Initialize(const wchar_t* title, int width, int height)
	{
		if (!InitWindow(title, width, height))
			return false;

		RECT rc;
		GetClientRect(m_hwnd, &rc);

		int clientW = rc.right - rc.left;
		int clientH = rc.bottom - rc.top;

		if (!InitD3D11(clientW, clientH))
			return false;

		if (!InitDocument(*m_renderer, clientW, clientH))
			return false;   

		return  m_uiManager.Init(m_hwnd, m_device->GetDevice(), m_device->GetContext());
	}

	void MainWindow::Run()
	{ 
		MSG msg = {};

		bool needsRedraw = true;

		while (msg.message != WM_QUIT)
		{
			if (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE))
			{
				TranslateMessage(&msg);
				DispatchMessage(&msg);
				needsRedraw = true;
			}
			else
			{
				if (needsRedraw)
				{
					RenderFrame();					
					needsRedraw = false;
				}
				else
				{
					WaitMessage(); // 等待新消息，节省 CPU
				}
			}
		}

	} 
	LRESULT MainWindow::WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
	{
		MainWindow* pThis = nullptr; 
		if (msg == WM_NCCREATE)
		{
			auto* cs = reinterpret_cast<CREATESTRUCTW*>(lParam);
			pThis = reinterpret_cast<MainWindow*>(cs->lpCreateParams);
			SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(pThis));
		}
		else
		{
			pThis = reinterpret_cast<MainWindow*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
		}

		if (pThis)
			return pThis->EventProc(hwnd, msg, wParam, lParam);

		return DefWindowProc(hwnd, msg, wParam, lParam); 
	}
	 
	bool MainWindow::InitWindow(const wchar_t* title, int width, int height)
	{
		HINSTANCE hInstance = GetModuleHandle(NULL);
 
		WNDCLASSEXW wc = {};
		wc.cbSize        = sizeof(wc);
		wc.style         = CS_HREDRAW | CS_VREDRAW;
		wc.lpfnWndProc   = WndProc;
		wc.hInstance     = hInstance;
		wc.hCursor       = LoadCursor(nullptr, IDC_ARROW);
		wc.hbrBackground = nullptr;
		wc.lpszClassName = L"MiniCADMainWindows1";
		wc.hIcon         = LoadIcon(nullptr, IDI_APPLICATION);
		wc.hIconSm       = wc.hIcon;

		// 1. 注册窗口
		RegisterClassEx(&wc);

		RECT rc = { 0, 0, width, height }; 
		AdjustWindowRect(&rc, WS_POPUP | WS_THICKFRAME | WS_MINIMIZEBOX, TRUE); //WS_OVERLAPPEDWINDOW

		// 2. 创建窗口
		m_hwnd = CreateWindowEx(
			0,
			L"MiniCADMainWindows1",
			title,
			WS_POPUP | WS_THICKFRAME | WS_MINIMIZEBOX, //  WS_OVERLAPPEDWINDOW  ， WS_THICKFRAME 阴影
			CW_USEDEFAULT,
			CW_USEDEFAULT,
			rc.right - rc.left,
			rc.bottom - rc.top,
			nullptr,
			nullptr,
			wc.hInstance,
			this); // 参数this
		 
		MARGINS margins = { 1,1,1,1 };
		DwmExtendFrameIntoClientArea(m_hwnd, &margins); // Win10/11 都有阴影

		// ── 居中到主显示器 ──────────────────────────────────────
		{
			HMONITOR hMon = MonitorFromWindow(m_hwnd, MONITOR_DEFAULTTONEAREST);
			MONITORINFO mi = { sizeof(mi) };
			GetMonitorInfo(hMon, &mi);

			int monW = mi.rcWork.right - mi.rcWork.left;
			int monH = mi.rcWork.bottom - mi.rcWork.top;
			int winW = rc.right - rc.left;
			int winH = rc.bottom - rc.top;

			int posX = mi.rcWork.left + (monW - winW) / 2;
			int posY = mi.rcWork.top + (monH - winH) / 2;

			SetWindowPos(m_hwnd, nullptr, posX, posY, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
		}
		// 3.显示窗口
		ShowWindow(m_hwnd, SW_SHOW);

		// 4.更新窗口
		UpdateWindow(m_hwnd);
		 
		return m_hwnd != nullptr; 
	}


	LRESULT MainWindow::EventProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
	{

		if (ImGui_ImplWin32_WndProcHandler(hwnd, msg, wParam, lParam))
			return true;

		switch (msg)
		{
		case WM_ERASEBKGND:
			return 1;
		case WM_NCCALCSIZE:
		{
			if (wParam)
			{
				return 0;  // 完全移除 non-client area
			}
			break;
		}
		case WM_SIZE:
		{
			UINT w = LOWORD(lParam);
			UINT h = HIWORD(lParam);

			if (m_swapChain)
				m_swapChain->Resize(w, h);

			return 0;
		}
		case WM_NCLBUTTONDBLCLK: // 双击顶部大化与恢复
		{
			if (wParam == HTCAPTION)
			{
				if (IsZoomed(hwnd))
					ShowWindow(hwnd, SW_RESTORE);
				else
					ShowWindow(hwnd, SW_MAXIMIZE);
				return 0;
			}
			break;
		}
		case WM_NCHITTEST:
		{
			const LONG border = 6; // resize 边框厚度

			POINT pt = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
			RECT wr;
			GetWindowRect(hwnd, &wr);

			bool left = pt.x < wr.left + border;
			bool right = pt.x >= wr.right - border;
			bool top = pt.y < wr.top + border;
			bool bottom = pt.y >= wr.bottom - border;

			// 角
			if (top && left)     return HTTOPLEFT;
			if (top && right)    return HTTOPRIGHT;
			if (bottom && left)  return HTBOTTOMLEFT;
			if (bottom && right) return HTBOTTOMRIGHT;

			// 边
			if (left)   return HTLEFT;
			if (right)  return HTRIGHT;
			if (top)    return HTTOP;
			if (bottom) return HTBOTTOM;

			// 标题栏拖动
			if (pt.y >= wr.top && pt.y < wr.top + 30)
			{
				if (ImGui::GetCurrentContext() && ImGui::IsAnyItemHovered())
					return HTCLIENT;

				return HTCAPTION;
			}

			return HTCLIENT;
		}
		case WM_GETMINMAXINFO:
		{
			// 告诉 Windows 最大化时覆盖哪个显示器的工作区
			HMONITOR hMon = MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST);

			MONITORINFO mi = { sizeof(mi) };
			GetMonitorInfo(hMon, &mi);

			MINMAXINFO* mmi = (MINMAXINFO*)lParam;

			//  
			mmi->ptMaxPosition.x = mi.rcWork.left - mi.rcMonitor.left;
			mmi->ptMaxPosition.y = mi.rcWork.top - mi.rcMonitor.top;

			mmi->ptMaxSize.x = mi.rcWork.right - mi.rcWork.left;
			mmi->ptMaxSize.y = mi.rcWork.bottom - mi.rcWork.top;

			return 0;
		}
		case WM_DESTROY:
			PostQuitMessage(0);
			return 0;

		default:
			return DefWindowProc(hwnd, msg, wParam, lParam);
		}
	}

	bool MainWindow::InitD3D11(int width, int height)
	{
		m_device = std::make_unique<Device>();       // 设备
		m_device->Initialize();

		m_swapChain = std::make_unique<SwapChain>(); // 交换链

		SwapChain::Options opt;
		opt.enableVSync  = false;  // 禁止垂直同步，允许撕裂（仅限窗口模式）
		opt.allowTearing = false; // 允许撕裂（仅限窗口模式）


		m_swapChain->Initialize(m_device.get(), m_hwnd, width, height, opt);// 初始化交换链
		m_renderer = std::make_unique<Renderer>(m_device->GetDevice(), m_device->GetContext());


		return true;
	}
	 
	bool MainWindow::InitDocument(Renderer& renderer, int width, int height)
	{ 
		m_docManager.Create(renderer, width, height); // 创建1个文档 
		m_docManager.Create(renderer, width, height); // 创建2个文档 
		m_docManager.Create(renderer, width, height); // 创建3个文档 

		m_docManager.SetRenderer(&renderer);

		return true;
	}

	void MainWindow::DocumentInput()
	{
		auto* doc = m_docManager.GetActive();
		if (doc == nullptr)
			return;

		const auto& uiInput = m_uiManager.GetViewportInput();
		if (!uiInput.Valid)
			return;

		auto& viewport = doc->GetViewport();
		if (viewport.GetWidth() != uiInput.Size.x || viewport.GetHight() != uiInput.Size.y)
		{
			viewport.Resize(uiInput.Size.x, uiInput.Size.y);
		}

		for (const auto& e : m_viewportInputAdapter.BuildEvents(uiInput))
		{
			doc->OnInput(e);
		}
	}

	void MainWindow::RenderFrame()
	{   
		auto* rtv = m_swapChain->GetRTV();        
		float clear[4] = { 0.1f, 0.1f, 0.1f, 1.f };
		m_device->GetContext()->ClearRenderTargetView(rtv, clear);
		m_device->GetContext()->OMSetRenderTargets   (1, &rtv, nullptr);

		if (auto doc = m_docManager.GetActive())
		{  
			DocumentInput(); // 处理文档输入
			doc->Render();   // 离屏幕渲染
		}  

		// 需要重新设置
		m_device->GetContext()->OMSetRenderTargets(1, &rtv, nullptr);

		m_uiManager.BeginFrame();

		m_uiManager.Render(m_docManager); 

		m_uiManager.EndFrame();

		m_swapChain->Present();

	} 

}
 
