#include <stdio.h> 
#include "WinAppDX11.h"
#include <windows.h>
int main()
{ 
	WinAppDX11 winAppDX11; 

	// 设置控制台输出 UTF-8，解决中文乱码
	SetConsoleOutputCP(CP_UTF8);
	SetConsoleCP(CP_UTF8);

	winAppDX11.InitInstance(GetModuleHandle(NULL), SW_SHOW)	;
	winAppDX11.Run();
	
}