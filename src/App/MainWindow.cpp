#include "MainWindow.h"
#include "Input/InputEvent.h"
#include "App/Command/AddEntityCommand.h" 
#include "ErrorReporter.h"
namespace MiniCAD
{
	MainWindow::MainWindow() :m_hwnd(0) {}

	MainWindow::~MainWindow() {}

	bool MainWindow::Initialize(const wchar_t* title, int width, int height)
	{
		return InitWindow(title, width, height) && InitD3D11(width, height) && InitViewportAndDocument(width, height);
	}

	bool MainWindow::InitWindow(const wchar_t* title, int width, int height)
	{
		HINSTANCE hInstance = GetModuleHandle(NULL);

		WNDCLASSEXW wc = {};

		wc.cbSize = sizeof(wc);
		wc.style = CS_HREDRAW | CS_VREDRAW;
		wc.lpfnWndProc = WndProc;
		wc.hInstance = hInstance;
		wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
		wc.hbrBackground = nullptr;
		wc.lpszClassName = L"MiniCADMainWindows";
		wc.hIcon = LoadIcon(nullptr, IDI_APPLICATION);
		wc.hIconSm = wc.hIcon;

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

		// 设置错误处理器，直接弹窗显示错误信息
		SetErrorHandler([this](const std::string& msg)
			{   
				int len = MultiByteToWideChar(CP_UTF8, 0, msg.c_str(), -1, nullptr, 0);
				std::wstring wmsg(len, 0);
				MultiByteToWideChar(CP_UTF8, 0, msg.c_str(), -1, wmsg.data(), len); 

				MessageBox(m_hwnd, wmsg.c_str(), L"错误", MB_OK | MB_ICONERROR);
			});
		return m_hwnd != nullptr;
	}

	bool MainWindow::InitD3D11(int width, int height)
	{   
		m_device = std::make_unique<Device>();

		m_device->Initialize();

		m_swapChain = std::make_unique<SwapChain>();

		SwapChain::Options opt;
		opt.enableVSync  = false;
		opt.allowTearing = false;

		m_swapChain->Initialize(m_device.get(), m_hwnd, width, height, opt);

		m_renderer = std::make_unique<Renderer>(m_device->GetDevice(), m_device->GetContext());

		return true;
	};


	bool MainWindow::InitViewportAndDocument(int width, int height)
	{ 
		m_viewport = std::make_unique<Viewport>(m_renderer.get(), width, height); // 传入m_renderer

		m_document = std::make_unique<Document>();

		m_document->GetEditor().SetViewContext(m_viewport.get());
		 
		auto& scene = m_document->GetScene();

		//// 直线1
		//auto id = scene.NextObjectID();
		//auto entity = std::make_unique<LineEntity>(id,XMFLOAT3(1.5, 1.5, 0), XMFLOAT3(1.5, 0, 0));
		//entity->GetAttr().Color = XMFLOAT4(1, 0, 0, 1);
		//auto cmd = std::make_unique<AddEntityCommand>(std::move(entity));
		//m_document->GetCommandStack().Execute(std::move(cmd), scene);
		//
		//// 直线2
		//id = scene.NextObjectID();
		//entity = std::make_unique<LineEntity>(id, XMFLOAT3(0.5, 0.5, 0), XMFLOAT3(1, 0, 0));
		//entity->GetAttr().Color = XMFLOAT4(1, 0, 0, 1);
		//cmd = std::make_unique<AddEntityCommand>(std::move(entity));
		//m_document->GetCommandStack().Execute(std::move(cmd), scene);

		//// 直线3
		//id = scene.NextObjectID();
		//entity = std::make_unique<LineEntity>(id, XMFLOAT3(0.5, 0.5, 0), XMFLOAT3(0, 1, 0));
		//entity->GetAttr().Color = XMFLOAT4(0, 1, 0, 1);
		//cmd = std::make_unique<AddEntityCommand>(std::move(entity));
		//m_document->GetCommandStack().Execute(std::move(cmd), scene);

		//// 直线4
		//id = scene.NextObjectID();
		//entity = std::make_unique<LineEntity>(id, XMFLOAT3(0.5, 0.5, 0), XMFLOAT3(1, 1, 0));
		//entity->GetAttr().Color = XMFLOAT4(1, 1, 0, 1);
		//cmd = std::make_unique<AddEntityCommand>(std::move(entity));
		//m_document->GetCommandStack().Execute(std::move(cmd), scene);


		// ── 输入系统初始化 ──────────────────────────────
		m_input.SetViewport(m_viewport.get());

		// 责任链：优先级从前到后 ,UI Layer 就在最前面 PushHandler
		m_input.PushHandler(m_viewport.get());         // pan / zoom
		m_input.PushHandler(m_document.get());         // 文档输入
		m_input.PushHandler(&m_document->GetEditor()); // 绘图逻辑
		 
		return true; 
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
		case WM_SIZE: // ── 窗口生命周期（MainWindow 自己处理）────────────
		{
			UINT w = LOWORD(lParam), h = HIWORD(lParam);
			if (m_swapChain) m_swapChain->Resize(w, h);
			if (m_viewport)  m_viewport->Resize((float)w, (float)h);
			return 0;
		}
		case WM_MBUTTONDOWN: // ── 中键 pan 的平台职责（SetCapture 必须在 Win32 层）─
			SetCapture(hwnd);
			m_lastMousePos = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
			m_input.Dispatch(hwnd, msg, wParam, lParam);
			return 0;

		case WM_MBUTTONUP:
			m_input.Dispatch(hwnd, msg, wParam, lParam);
			ReleaseCapture();
			return 0;

		case WM_MOUSEMOVE:
		{
			// delta 计算在 EventProc 层，pan 的物理位移只有这里能算
			POINT cur = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
			int dx = cur.x - m_lastMousePos.x;
			int dy = cur.y - m_lastMousePos.y;
			m_lastMousePos = cur;

			// 把 dx/dy 注入 Viewport（pan 需要），
			// 然后再走正常的 Dispatch 让 Editor 也收到 MouseMove
			if (GetAsyncKeyState(VK_MBUTTON) & 0x8000)
				m_viewport->Pan((float)dx, (float)dy);

			m_input.Dispatch(hwnd, msg, wParam, lParam);
			return 0;
		}

		// ── 其余所有输入消息交给 InputSystem ─────────────
		case WM_LBUTTONDOWN:
		case WM_LBUTTONUP:
		case WM_RBUTTONDOWN:
		case WM_RBUTTONUP:
		case WM_MOUSEWHEEL:
		case WM_KEYDOWN:
		case WM_KEYUP:
			m_input.Dispatch(hwnd, msg, wParam, lParam);
			return 0;

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
		 
		const auto& scene = m_document->GetScene();

		auto selection   = m_document->GetEditor().GetSelection();
		auto hovered     = m_document->GetEditor().GetHovered();

		m_viewport->Draw(scene, selection, hovered,target);
		 
		m_swapChain->Present();         // 显示帧

	} 

	void MainWindow::Run()
	{
		MSG msg = {};

		bool needsRedraw = true;
		int a = 0;
		while (msg.message != WM_QUIT)
		{
			if (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE))
			{
				TranslateMessage(&msg);
				DispatchMessage(&msg);
				needsRedraw = true;  // 有任何消息都标记需要重绘
			}
			else
			{
				// 空闲时只在需要时渲染一帧
				if (needsRedraw)
				{
					//printf("%d\n",a++);
					RenderFrame();
					needsRedraw = false;
				}
				else
				{
					// 真正空闲：让出 CPU，等待下一条消息
					WaitMessage();
				}
			}
		}
	}
	 
}
