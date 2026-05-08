#include "pch.h"
#include "MainWindow.h"
using namespace MiniCAD;
int main()
{
	SetConsoleOutputCP(CP_UTF8);
	SetConsoleCP(CP_UTF8);


	MainWindow mainWindow;

	mainWindow.Initialize(L"MiniCAD",600,400);
	mainWindow.Run();

}