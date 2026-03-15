#include "app/Editor.h"
#include "ui/Windows/MainWindow.h"
#include <cstdio>

int main() {
    // auto& editor = MiniCAD::Editor::Instance();

    // editor.Initialize();   
   

    //editor.Shutdown();

	MiniCAD::MainWindow mainWindow;

	mainWindow.Initialize( L"MiniCAD", 800, 600);

    printf("Run->MiniCAD");

    mainWindow.Run();

    
    return 0;
}