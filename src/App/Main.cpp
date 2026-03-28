#include "MainWindow.h" 
using namespace MiniCAD;
int main() 
{  
	SetConsoleOutputCP(CP_UTF8);
	SetConsoleCP(CP_UTF8); 
    
	auto mainWindow = std::make_unique<MainWindow>();

	mainWindow->Initialize( L"MiniCAD", 800, 600);

	mainWindow->Run(); 

	mainWindow.reset(); 

	return 0;

}	