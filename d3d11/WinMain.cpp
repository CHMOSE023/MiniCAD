#include <stdio.h> 
#include "WinAppDX11.h"

int main()
{

	WinAppDX11 winAppDX11; 

	winAppDX11.InitInstance(GetModuleHandle(NULL), SW_SHOW)	;
	winAppDX11.Run();
	
}