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
		opt.enableVSync = true;
		opt.allowTearing = false;

		m_swapChain->Initialize(m_device.get(), m_hwnd, width, height, opt);

		m_renderer = std::make_unique<Renderer>(m_device->GetDevice(), m_device->GetContext());	 

		m_scene    = std::make_unique<Scene>(width, height);

		AddEntity();

		return m_hwnd != nullptr;
	}

	void MainWindow::AddEntity()
	{
		if (!m_scene)return;
		 // 添加直线
		using ObjectID = MiniCAD::Object::ObjectID;
		static ObjectID nextId = 1;

		// 1️⃣ X 轴正方向直线 (红色)
		{
			auto lineX = std::make_shared<LineEntity>(nextId++, XMFLOAT3(0.5, 0.5, 0), XMFLOAT3(1, 0, 0));
			lineX->GetAttr().Color = XMFLOAT4(1, 0, 0, 1); // 红色
			m_scene->AddEntity(lineX);
		}

		// 2️⃣ Y 轴正方向直线 (绿色)
		{
			auto lineY = std::make_shared<LineEntity>(nextId++, XMFLOAT3(0.5, 0.5, 0), XMFLOAT3(0, 1, 0));
			lineY->GetAttr().Color = XMFLOAT4(0, 1, 0, 1); // 绿色
			m_scene->AddEntity(lineY);
		}

		// 3️⃣ 对角线 (黄色)
		{
			auto lineDiag = std::make_shared<LineEntity>(nextId++, XMFLOAT3(0.5, 0.5, 0), XMFLOAT3(1, 1, 0) );
			lineDiag->GetAttr().Color = XMFLOAT4(1, 1, 0, 1); // 黄色
			m_scene->AddEntity(lineDiag);
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
			m_isPanning = true;

			m_lastMousePos.x = LOWORD(lParam);
			m_lastMousePos.y = HIWORD(lParam);
		}
		break;

		case WM_MBUTTONUP:
		{
			m_isPanning = false;
			ReleaseCapture();
		}
		break; 
		  
		case WM_MOUSEMOVE:
		{
			POINT currentPos;
			currentPos.x = LOWORD(lParam);
			currentPos.y = HIWORD(lParam);

			int dx = currentPos.x - m_lastMousePos.x;
			int dy = currentPos.y - m_lastMousePos.y;

			if (m_isPanning)
			{
				m_scene->GetCamera()->Update((float)dx, (float)dy, 0.0f, m_isPanning);
			}

			m_lastMousePos = currentPos;
		}
		break;  

		case WM_MOUSEWHEEL:
		{
			short delta = GET_WHEEL_DELTA_WPARAM(wParam);

			m_scene->GetCamera()->Update(0.0f, 0.0f, (float)delta / WHEEL_DELTA, false, LOWORD(lParam), HIWORD(lParam));
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

			if (m_scene)
			{
				auto camera = m_scene->GetCamera();
				if (camera)
				{
					camera->Resize((float)w, (float)h);
				}
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

		XMMATRIX world = XMMatrixIdentity(); 
		auto viewProj  = m_scene->GetCamera()->GetViewProj();

		XMMATRIX mvp   = world* viewProj; 

		// ===== 渲染 =====
		m_renderer->Begin(target, mvp);  // 开始收集顶点并绑定 pipeline	

		m_scene->Draw(m_renderer.get()); // Scene 内部调用 DrawGrad、DrawLine 绘制网格和实体

		m_renderer->End();              // 结束 提交 GPU

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
