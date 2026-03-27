#pragma once
#include "pch.h"
#include "Device.h"	
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
		
		static LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);
		LRESULT EventProc(HWND, UINT, WPARAM, LPARAM);
		HWND m_hwnd;

		std::unique_ptr<Device>    m_device;
	};
}