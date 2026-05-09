#pragma once
#include "pch.h"
#include "ImGuiLayer.h"
#include "Editor/Input/ViewportInput.h"
#include <imgui.h>
#include <memory>
#include <string>
#include <unordered_map>

namespace MiniCAD
{
    class DocumentManager;

    enum class Tool : int
    {
        Select = 0, // Cursor
        /*-----*/ 
        Line,
        Circle,
        Rectangle,  // Rect
        Arc,
        Ellipse,
        Polyline,
        Spline,
        /*-----*/
        Copy,
        Move,
        Mirror,
        Rotate,
        /*-----*/
        Redo,
        Undo,
    };

    class UIManager
    {
    public:
     
        bool Init(HWND hwnd, ID3D11Device* device, ID3D11DeviceContext* context);
        void Shutdown();

        void BeginFrame();
        void EndFrame  ();

        void Render (DocumentManager& dm); 

        float GetCaptionButtonsX() const { return m_captionButtonsScreenX; }

        const ViewportInput& GetViewportInput() const { return m_viewportInput; }
        Tool GetActiveTool() const { return m_activeTool; }

    private:
        void DrawMenubar     (DocumentManager& dm);    
        void DrawToolbar     (DocumentManager& dm);
        void DrawDocumentTabs(DocumentManager& dm);
        void DrawStatusBar   (DocumentManager& dm);
        void InitToolIcons   ();

        ImTextureID LoadTextureFromFile(const char* path);

    private:
        std::unique_ptr<ImGuiLayer> m_imgui         = nullptr;
        HWND                        m_hwnd          = nullptr;
        ID3D11Device*               m_device        = nullptr;
        ViewportInput               m_viewportInput = {};
        ImVec2                      m_lastLocal     = ImVec2(0, 0);
        Tool                        m_activeTool    = Tool::Select;

        float                       m_captionButtonsScreenX = 0.f;

        std::unordered_map<std::string, ImTextureID> m_toolIcons;
    };
}
