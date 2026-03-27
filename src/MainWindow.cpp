#include "MainWindow.h"

namespace MiniCAD
{
	MainWindow::MainWindow() :m_hwnd(0) {}

	MainWindow::~MainWindow() {}

	bool MainWindow::Initialize(const wchar_t* title, int width, int height)
	{
		HINSTANCE hInstance = GetModuleHandle(NULL);

		WNDCLASSEXW wc = {};

		wc.cbSize        = sizeof(wc);
		wc.style         = CS_HREDRAW | CS_VREDRAW;
		wc.lpfnWndProc   = WndProc;
		wc.hInstance     = hInstance;
		wc.hCursor       = LoadCursor(nullptr, IDC_ARROW);
		wc.hbrBackground = nullptr;
		wc.lpszClassName = L"MiniCADMainWindows";
		wc.hIcon         = LoadIcon(nullptr, IDI_APPLICATION);
		wc.hIconSm       = wc.hIcon; 

		RegisterClassEx(&wc);

		RECT rc = { 0, 0, width, height };
		AdjustWindowRect(&rc, WS_OVERLAPPEDWINDOW, TRUE);

		m_hwnd = CreateWindowEx(0, 
			L"MiniCADMainWindows", 
			title, 
			WS_OVERLAPPEDWINDOW, 
			CW_USEDEFAULT, 
			CW_USEDEFAULT, 
			rc.right - rc.left, rc.bottom - rc.top, 
			nullptr, nullptr, hInstance, this);

		ShowWindow(m_hwnd, SW_SHOW);

		UpdateWindow(m_hwnd);


		m_device = std::make_unique<Device>();

		m_device->Initialize();

		m_swapChain = std::make_unique<SwapChain>();

		SwapChain::Options opt;
		opt.enableVSync = true;
		opt.allowTearing = false;
		m_swapChain->Initialize(m_device.get(), m_hwnd, width, height, opt);

		m_renderer = std::make_unique<Renderer>(m_device->GetDevice(), m_device->GetContext());	 

		return m_hwnd != nullptr;
	}

	LRESULT CALLBACK MainWindow::WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
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

		if (pThis) return pThis->EventProc(hwnd, msg, wParam, lParam);

		return DefWindowProcW(hwnd, msg, wParam, lParam);
	}

	LRESULT MainWindow::EventProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
	{
		switch (msg)
		{
		case WM_SIZE:
		{
			UINT w = LOWORD(lParam);
			UINT h = HIWORD(lParam);

			if (m_swapChain)
			{
				m_swapChain->Resize(w, h);
			}
		}
		break;
		case WM_DESTROY:
			PostQuitMessage(0);
			return 0;
		default:
			return DefWindowProcW(hwnd, msg, wParam, lParam);
		}

	}

	void MainWindow::RenderFrame()
	{
		auto target = m_swapChain->GetRenderTarget();
		
		// 相机
		XMMATRIX world = XMMatrixIdentity();

		XMMATRIX view = XMMatrixLookAtLH(XMVectorSet(3, 3, -3, 1), XMVectorZero(), XMVectorSet(0, 1, 0, 0));

		auto viewport = m_swapChain->GetViewport();

		XMMATRIX proj = XMMatrixPerspectiveFovLH(XM_PIDIV4, viewport.Width / viewport.Height, 0.1f, 100.0f);

		XMMATRIX mvp = world * view * proj;
		  
		// ===== 渲染 =====
		m_renderer->Begin(target, mvp);

		m_renderer->DrawLine({ 0,0,0 }, { 1,0,0 }, { 1,0,0,1 }); // X
		m_renderer->DrawLine({ 0,0,0 }, { 0,1,0 }, { 0,1,0,1 }); // Y
		m_renderer->DrawLine({ 0,0,0 }, { 0,0,1 }, { 0,0,1,1 }); // Z

		m_renderer->End();

		m_swapChain->Present();

	}
	void MainWindow::Run()
	{
		MSG msg = {};

		while (WM_QUIT != msg.message)
		{
			if (GetMessage(&msg, nullptr, 0, 0))
			{
				TranslateMessage(&msg);
				DispatchMessage(&msg);
			}

			RenderFrame();

		}
	}
	 
}
