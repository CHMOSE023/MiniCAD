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

		//--- 初始化：Device、SwapChain、Renderer、Scene

		m_device = std::make_unique<Device>();

		m_device->Initialize();

		m_swapChain = std::make_unique<SwapChain>();

		SwapChain::Options opt;
		opt.enableVSync  = false;
		opt.allowTearing = false;

		m_swapChain->Initialize(m_device.get(), m_hwnd, width, height, opt);

		m_renderer = std::make_unique<Renderer>(m_device->GetDevice(), m_device->GetContext());	  

		m_scene    = std::make_unique<Scene>();
		
		m_viewport = std::make_unique<Viewport>(m_renderer.get(), width, height); // 传入m_renderer

		AddEntity();

		return m_hwnd != nullptr;
	}

	void MainWindow::AddEntity()
	{
		if (!m_scene)return;
		 // 添加直线
		using ObjectID = MiniCAD::Object::ObjectID;
		static ObjectID nextId = 1;

		// 1️ X 轴正方向直线 (红色)
		{
			
			auto lineX = std::make_unique<LineEntity>(nextId++, XMFLOAT3(0.5, 0.5, 0), XMFLOAT3(1, 0, 0));
			lineX->GetAttr().Color = XMFLOAT4(1, 0, 0, 1); // 红色
			m_scene->AddEntity(std::move(lineX));
		}

		// 2 Y 轴正方向直线 (绿色)
		{
			auto lineY = std::make_unique<LineEntity>(nextId++, XMFLOAT3(0.5, 0.5, 0), XMFLOAT3(0, 1, 0));
			lineY->GetAttr().Color = XMFLOAT4(0, 1, 0, 1); // 绿色
			m_scene->AddEntity(std::move(lineY));
		}

		// 3️ 对角线 (黄色)
		{
			auto lineDiag = std::make_unique<LineEntity>(nextId++, XMFLOAT3(0.5, 0.5, 0), XMFLOAT3(1, 1, 0) );
			lineDiag->GetAttr().Color = XMFLOAT4(1, 1, 0, 1); // 黄色
			m_scene->AddEntity(std::move(lineDiag));
		}


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
		case WM_MBUTTONDOWN:
		{
			SetCapture(hwnd); 
			m_viewport->BeginPan();

			m_lastMousePos.x = GET_X_LPARAM(lParam);
			m_lastMousePos.y = GET_Y_LPARAM(lParam);
		}
		break;

		case WM_MBUTTONUP:
		{
			m_viewport->EndPan();
			ReleaseCapture();
		}
		break; 
		  
		case WM_MOUSEMOVE:
		{
			POINT currentPos;

			currentPos.x = GET_X_LPARAM(lParam);
			currentPos.y = GET_Y_LPARAM(lParam);

			int dx = currentPos.x - m_lastMousePos.x;
			int dy = currentPos.y - m_lastMousePos.y;

			m_viewport->Pan((float)dx, (float)dy);
			 
			m_lastMousePos = currentPos;
		}
		break;  

		case WM_MOUSEWHEEL:
		{
		
			short delta = GET_WHEEL_DELTA_WPARAM(wParam);
			// ***微软设计***   	 
			// WM_MOUSEWHEEL -> LOWORD/HIWORD(lParam) 是屏幕坐标！ 
			POINT pt;
			pt.x = GET_X_LPARAM(lParam);   // GET_X/Y_LPARAM，避免负坐标截断
			pt.y = GET_Y_LPARAM(lParam);
			ScreenToClient(hwnd, &pt);     // 转换为客户区坐标  
			m_viewport->Zoom((float)delta / WHEEL_DELTA,  pt.x, pt.y);
		}
		break;

		case WM_SIZE:
		{
			UINT w = LOWORD(lParam);
			UINT h = HIWORD(lParam);

			if (m_swapChain)
			{
				m_swapChain->Resize(w, h);
			}

			if (m_viewport)
			{
				m_viewport->Resize((float)w, (float)h);
			}
			
		}
		break;

		case WM_DESTROY:
			PostQuitMessage(0);
			return 0;

		default:
			return DefWindowProcW(hwnd, msg, wParam, lParam);
		}

		return 0;
	}


	void MainWindow::RenderFrame()
	{
		auto target = m_swapChain->GetRenderTarget();	
		 
		m_viewport->Draw(*m_scene, target);
		 
		m_swapChain->Present();         // 显示帧

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
