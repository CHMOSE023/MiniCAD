#pragma once
#include "pch.h"
#include "Render/D3D11/Device.h"	
#include "Render/D3D11/SwapChain.h"	
#include "Render/D3D11/Renderer.h"	 
#include "Render/Viewport/Viewport.h"	
#include "App/Scene/Scene.h"	
#include "App/Document/Document.h"	


namespace MiniCAD 
{
	class MainWindow
	{
	public:
		MainWindow();
		~MainWindow(); 
		bool Initialize( const wchar_t* title, int width, int height);
		void AddEntity();
		void Run();

	private:
		// ── Win32 ─────────────────────────────────────────────
		static LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);
		LRESULT EventProc(HWND, UINT, WPARAM, LPARAM);

		// ── 初始化分步 ────────────────────────────────────────
		bool InitWindow(const wchar_t* title, int width, int height);
		bool InitD3D11(int width, int height);
		bool InitScene(int width, int height);

		// ── 渲染 ──────────────────────────────────────────────
		void RenderFrame();

		// ── 输入转换 ──────────────────────────────────────────
		// InputEvent     BuildEvent(UINT msg, WPARAM wParam, LPARAM lParam);
		// static uint8_t GetModifiers();
	private:
		// 窗口
		HWND m_hwnd;

		// ── D3D11 层（硬件资源）──────────────────────────────
		std::unique_ptr<Device>       m_device;
		std::unique_ptr<SwapChain>    m_swapChain;
		std::unique_ptr<Renderer>     m_renderer; 

		// ── 应用层 ────────────────────────────────────────────
		std::unique_ptr<Scene>        m_scene;
		std::unique_ptr<Viewport>     m_viewport;

		// ── UI 层（预留）─────────────────────────────────────
		// std::unique_ptr<IUILayer>  m_uiLayer;
		POINT m_lastMousePos = { 0, 0 }; 

	};
}