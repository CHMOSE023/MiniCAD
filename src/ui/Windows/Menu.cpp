// ============================================================
// MiniCAD — ui/win32/Menu.cpp
// 职责：菜单命令实现，全部通过 Editor 接口操作
// 依赖：ui/win32/Menu.h, app/Editor.h
// 约束：不直接访问 Scene / core / render
// ============================================================

#include "app/Editor.h"
#include "ui/Windows/Menu.h"
#include "ui/Windows/WindowsDefs.h" 

namespace MiniCAD {

    // ============================================================
    // HandleCommand
    // ============================================================

    bool Menu::HandleCommand(WPARAM wParam, LPARAM lParam) {
        (void)lParam;
        // WM_COMMAND from menu: HIWORD(wParam) == 0
        if (HIWORD(wParam) != 0) return false;

        switch (static_cast<MenuCommandID>(LOWORD(wParam))) {
        case MenuCommandID::FILE_NEW:       OnFileNew();        return true;
        case MenuCommandID::FILE_OPEN:      OnFileOpen();       return true;
        case MenuCommandID::FILE_SAVE:      OnFileSave();       return true;
        case MenuCommandID::FILE_SAVEAS:    OnFileSaveAs();     return true;
        case MenuCommandID::FILE_EXIT:      OnFileExit();       return true;

        case MenuCommandID::EDIT_UNDO:      OnEditUndo();       return true;
        case MenuCommandID::EDIT_REDO:      OnEditRedo();       return true;
        case MenuCommandID::EDIT_DELETE:    OnEditDelete();     return true;
        case MenuCommandID::EDIT_SELECTALL: OnEditSelectAll();  return true;

        case MenuCommandID::VIEW_ZOOM_IN:     OnViewZoomIn();      return true;
        case MenuCommandID::VIEW_ZOOM_OUT:    OnViewZoomOut();     return true;
        case MenuCommandID::VIEW_ZOOM_FIT:    OnViewZoomFit();     return true;
        case MenuCommandID::VIEW_GRID_TOGGLE: OnViewGridToggle();  return true;

        default:
            return false;
        }
    }

    void Menu::UpdateMenuState(WPARAM wParam) {
        HMENU hMenu = reinterpret_cast<HMENU>(wParam);

        // 根据命令栈状态启用/禁用 Undo/Redo
        bool canUndo = Editor::Instance().CanUndo();
        bool canRedo = Editor::Instance().CanRedo();

        EnableMenuItem(hMenu, static_cast<UINT>(MenuCommandID::EDIT_UNDO),
            MF_BYCOMMAND | (canUndo ? MF_ENABLED : MF_GRAYED));
        EnableMenuItem(hMenu, static_cast<UINT>(MenuCommandID::EDIT_REDO),
            MF_BYCOMMAND | (canRedo ? MF_ENABLED : MF_GRAYED));

        // 网格可见性勾选状态
        CheckMenuItem(hMenu, static_cast<UINT>(MenuCommandID::VIEW_GRID_TOGGLE),
            MF_BYCOMMAND | (m_gridVisible ? MF_CHECKED : MF_UNCHECKED));
    }

    // ============================================================
    // 文件菜单
    // ============================================================

    void Menu::OnFileNew() {
        // Phase 1 占位：清空场景
        // TODO: 提示保存当前文档
        // Editor::Instance().GetScene().Clear();
        Editor::Instance().RequestRedraw();
    }

    void Menu::OnFileOpen() {
        // Phase 2: FileIO 实现后对接
        // 此处显示文件对话框占位
        // OPENFILENAMEW ofn = {};
        // wchar_t filePath[MAX_PATH] = {};
        // ofn.lStructSize = sizeof(ofn);
        // ofn.lpstrFilter = L"MiniCAD Files (*.mcd)\0*.mcd\0All Files (*.*)\0*.*\0";
        // ofn.lpstrFile = filePath;
        // ofn.nMaxFile = MAX_PATH;
        // ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;
        // if (GetOpenFileNameW(&ofn)) {
        //     // Phase 2: Editor::Instance().OpenDocument(filePath);
        // }
    }

    void Menu::OnFileSave() {
        // Phase 2: Archive 实现后对接
        // Phase 1 占位：调用 Editor 保存接口（当前为空实现）
        // Editor::Instance().SaveDocument();
    }

    void Menu::OnFileSaveAs() {
       /* OPENFILENAMEW ofn = {};
        wchar_t filePath[MAX_PATH] = {};
        ofn.lStructSize = sizeof(ofn);
        ofn.lpstrFilter = L"MiniCAD Files (*.mcd)\0*.mcd\0";
        ofn.lpstrFile = filePath;
        ofn.nMaxFile = MAX_PATH;
        ofn.Flags = OFN_OVERWRITEPROMPT;
        ofn.lpstrDefExt = L"mcd";
        if (GetSaveFileNameW(&ofn)) {
            Editor::Instance().SaveDocumentAs(filePath);
        }*/
    }

    void Menu::OnFileExit() {
        PostQuitMessage(0);
    }

    // ============================================================
    // 编辑菜单
    // ============================================================

    void Menu::OnEditUndo() {
        Editor::Instance().Undo();
    }

    void Menu::OnEditRedo() {
        Editor::Instance().Redo();
    }

    void Menu::OnEditDelete() {
        // 删除选中实体：通过 Editor 创建 DeleteEntityCommand
        // Editor::Instance().DeleteSelection();
    }

    void Menu::OnEditSelectAll() {
       // Editor::Instance().SelectAll();
    }

    // ============================================================
    // 视图菜单
    // ============================================================

    void Menu::OnViewZoomIn() {
       /* auto& cam = Editor::Instance().GetCamera();
        float zoom = cam.GetZoom() * 1.25f;
        cam.SetZoom(zoom);
        Editor::Instance().RequestRedraw();*/
    }

    void Menu::OnViewZoomOut() {
       /* auto& cam = Editor::Instance().GetCamera();
        float zoom = cam.GetZoom() * 0.8f;
        cam.SetZoom(zoom);
        Editor::Instance().RequestRedraw();*/
    }

    void Menu::OnViewZoomFit() {
        // Phase 1 占位：重置相机到默认视角
        // Editor::Instance().ZoomFit();
    }

    void Menu::OnViewGridToggle() {
        // m_gridVisible = !m_gridVisible;
        // Editor::Instance().SetGridVisible(m_gridVisible);
        // Editor::Instance().RequestRedraw();
    }

} // namespace MiniCAD
