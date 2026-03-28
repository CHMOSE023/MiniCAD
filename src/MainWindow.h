#pragma once
#include "pch.h"
#include "Device.h"	
#include "SwapChain.h"	
#include "Renderer.h"	
#include "Scene.h"	
#include "Camera.h"	

namespace MiniCAD 
{
	class MainWindow
	{
	public:
		MainWindow();
		~MainWindow(); 
		bool Initialize( const wchar_t* title, int width, int height);
		void Run();

	private:
		void RenderFrame();
		static LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);
		LRESULT EventProc(HWND, UINT, WPARAM, LPARAM);
		HWND m_hwnd;

		std::unique_ptr<Device>       m_device;
		std::unique_ptr<SwapChain>    m_swapChain;
		std::unique_ptr<Renderer>     m_renderer;
		std::unique_ptr<Camera>       m_camera;	

	};
}