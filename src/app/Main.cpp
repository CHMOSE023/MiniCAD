#include "app/Editor.h"
#include <cstdio>

int main() {
    auto& editor = MiniCAD::Editor::Instance();

    editor.Initialize();   
    printf("MiniCAD");
    editor.Shutdown();

    return 0;
}