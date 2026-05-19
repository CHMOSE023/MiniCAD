#include "UIManager.h"
#include "imgui.h"
#include "imgui_internal.h"
#include "Document/Document.h"
#include "Document/DocumentManager.h"
#include "Editor/Input/InputEvent.h"
#include <memory>
#include <vector>
#include <filesystem>
#include <utility>
#define STB_IMAGE_IMPLEMENTATION
#include <stb/stb_image.h> 
#include "pch.h" 
#include <functional>

namespace MiniCAD
{ 
    namespace
    {
        constexpr float kToolbarHeight   = 38.f;   // 工具栏高度
        constexpr float kToolBtnSize     = 32.f;   // 工具按钮尺寸
        constexpr float kStatusBarHeight = 26.f;   // 状态栏高度

        // 工具元数据
        struct ToolMeta 
        { 
            Tool        id; 
            const char* icon;  
            const char* tooltip;
            std::function<void(DocumentManager&)> onActivate;
        };
         
        inline  ToolMeta kTools[] =
        {
            { Tool::Select,     "Cursor",  "选择 (Esc)"   , [](DocumentManager& dm) {}   },
            /*---------------------------------------------*/
            { Tool::Line,       "Line",    "直线 (L)"      ,[](DocumentManager& dm) {dm.GetActive()->GetEditor().StartLineTool(); }  },
            { Tool::Circle,     "Circle",  "圆 (C)"        ,[](DocumentManager& dm) {dm.GetActive()->GetEditor().StartCircleTool(); }  },
            { Tool::Rectangle,  "Rect",    "矩形 (R)"      ,[](DocumentManager& dm) {dm.GetActive()->GetEditor().StartRectangleTool(); }  },
            { Tool::Arc,        "Arc",     "圆弧 (A)"      ,[](DocumentManager& dm) {dm.GetActive()->GetEditor().StartArcTool(); }  },
            { Tool::Ellipse,    "Ellipse", "椭圆 (E)"      ,[](DocumentManager& dm) {dm.GetActive()->GetEditor().StartEllipseTool(); }  },
            { Tool::Polyline,   "Pline",   "多段线 (Pl)"   ,[](DocumentManager& dm) {dm.GetActive()->GetEditor().StartPolylineTool(); }  },
            { Tool::Spline,     "Spline",  "样条曲线 (SPL)",[](DocumentManager& dm) {dm.GetActive()->GetEditor().StartSplineTool(); }   },
            { Tool::Text,       "Text",    "文字 (T)"      ,[](DocumentManager& dm) {dm.GetActive()->GetEditor().StartTextTool(); }     },
            /*---------------------------------------------*/
            { Tool::Copy,       "Copy",    "复制 (co)"     ,[](DocumentManager& dm) {dm.GetActive()->GetEditor().StartCopyTool(); }},
            { Tool::Move,       "Move",    "移动 (mv)"     ,[](DocumentManager& dm) {dm.GetActive()->GetEditor().StartMoveTool(); }},
            { Tool::Mirror,     "Mirror",  "镜像 (mi)"     ,[](DocumentManager& dm) {dm.GetActive()->GetEditor().StartMirrorTool(); }},
            { Tool::Rotate,     "Rotate",  "旋转 (R)"      ,[](DocumentManager& dm) {dm.GetActive()->GetEditor().StartRotateTool(); }}, 
            /*---------------------------------------------*/ 
            { Tool::Undo,       "Undo",    "撤销"          ,[](DocumentManager& dm) {dm.Undo();}},
            { Tool::Redo,       "Redo",    "重做"          ,[](DocumentManager& dm) {dm.Redo();}},
        };
    }
   

  
    // ── 工具函数 ────────────────────────────────────────────
    static ImVec2 RectCenter(ImVec2 min, ImVec2 size)
    {
        return ImVec2(min.x + size.x * 0.5f, min.y + size.y * 0.5f);
    }

    static void DrawMinimizeIcon(ImDrawList* dl, ImVec2 center, float size, ImU32 col)
    {
        float half = size * 0.5f;
        dl->AddLine(
            ImVec2(center.x - half, center.y ),
            ImVec2(center.x + half, center.y ),
            col, 1.2f
        );
    }

    static void DrawMaximizeIcon(ImDrawList* dl, ImVec2 center, float size, ImU32 col)
    {
        float half = size * 0.5f;
        dl->AddRect(
            ImVec2(center.x - half, center.y - half),
            ImVec2(center.x + half, center.y + half),
            col, 0.f, 0, 1.2f
        );
    }

    static void DrawRestoreIcon(ImDrawList* dl, ImVec2 center, float size, ImU32 col)
    {
        float h   = size * 0.40f;
        float off = size * 0.10f;

        // ── 后面的矩形（右上角偏移，只画三条边，左下角被前矩形遮住不画）
        ImVec2 b0 = ImVec2(center.x - h + off, center.y - h - off); // 左上
        ImVec2 b1 = ImVec2(center.x + h + off, center.y - h - off); // 右上
        ImVec2 b2 = ImVec2(center.x + h + off, center.y + h - off); // 右下
   
        dl->AddLine(b0, b1, col, 1.2f);   // 上边
        dl->AddLine(b1, b2, col, 1.2f);   // 右边 

        // ── 前面的矩形（左下角偏移，完整画四条边）
        ImVec2 f0 = ImVec2(center.x - h - off, center.y - h + off); // 左上
        ImVec2 f1 = ImVec2(center.x + h - off, center.y - h + off); // 右上
        ImVec2 f2 = ImVec2(center.x + h - off, center.y + h + off); // 右下
        ImVec2 f3 = ImVec2(center.x - h - off, center.y + h + off); // 左下

        dl->AddLine(f0, f1, col, 1.2f);
        dl->AddLine(f1, f2, col, 1.2f);
        dl->AddLine(f2, f3, col, 1.2f);
        dl->AddLine(f3, f0, col, 1.2f);
    }

    static void DrawCloseIcon(ImDrawList* dl, ImVec2 center, float size, ImU32 col)
    {
        // 像素对齐，消除亚像素偏移导致的不对称
        float cx = center.x;
        float cy = center.y;
         
        dl->AddLine(ImVec2(cx - 5.0, cy - 5.0), ImVec2(cx + 5.0, cy + 5.4), col, 1.2f);
        dl->AddLine(ImVec2(cx + 5.0, cy - 5.4), ImVec2(cx - 5.0, cy + 5.0), col, 1.2f);
    }

    static void DrawDropdownIcon(ImDrawList* dl, ImVec2 center, float size, ImU32 col)
    {
        float half = size * 0.5f;

        ImVec2 p1 = ImVec2(center.x - half, center.y - half * 0.3f);
        ImVec2 p2 = ImVec2(center.x + half, center.y - half * 0.3f);
        ImVec2 p3 = ImVec2(center.x, center.y + half);

        dl->AddTriangleFilled(p1, p2, p3, col);
    }

    static void DrawPetalLogo(ImDrawList* dl, ImVec2 center, float radius, int petalCount = 5, float amplitude = 0.12f, int segments = 120)
    {
        constexpr ImU32 col = IM_COL32(80, 180, 60, 255);   // 草绿

        std::vector<ImVec2> pts;
        pts.reserve(segments + 1);
        for (int i = 0; i <= segments; i++)
        {
            float a = IM_PI * 2.f * i / segments;
            float r = radius * (1.f + amplitude * sinf(petalCount * a));
            pts.push_back({ center.x + r * cosf(a), center.y + r * sinf(a) });
        }

        // ── 三角扇 ──────────────────────────────────────────
        for (int i = 0; i < segments; i++)
            dl->AddTriangleFilled(center, pts[i], pts[i + 1], col);
    }

    bool UIManager::Init(HWND hwnd, ID3D11Device* device, ID3D11DeviceContext* context)
    {
        m_hwnd   = hwnd; 
		m_device = device;
        InitToolIcons();
        m_imgui  = std::make_unique<ImGuiLayer>();

        if (!m_imgui->Init(hwnd, device, context))
            return false;  

        ImGuiIO& io = ImGui::GetIO();   
        io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;

        io.Fonts->AddFontFromFileTTF("C:/Windows/Fonts/msyh.ttc", 16.f, nullptr, io.Fonts->GetGlyphRangesChineseFull());  
        ImGui::StyleColorsDark();
        
        return true;
    }     
    void UIManager::Shutdown()  { m_imgui->Shutdown(); }
    void UIManager::BeginFrame(){ m_imgui->Begin(); }
    void UIManager::EndFrame()  { m_imgui->End(); }

    void UIManager::SyncFonts(DocumentManager& dm)
    {
        // BeginFrame 之后才能拿到有效的 TexID；在 doc->Render() 之前同步，
        // 保证本帧 SubmitTextured 用的是最新字体纹理。
        void* fontTex = (void*)(uintptr_t)ImGui::GetIO().Fonts->TexRef.GetTexID();
        if (auto* doc = dm.GetActive())
            doc->SetFontTexture(fontTex);
    }
     
    void UIManager::Render(DocumentManager& dm)
    {    
        ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar            |
                                 ImGuiWindowFlags_NoCollapse            |
                                 ImGuiWindowFlags_NoResize              |
                                 ImGuiWindowFlags_NoMove                |
                                 ImGuiWindowFlags_NoBringToFrontOnFocus |
                                 ImGuiWindowFlags_NoNavFocus            |
                                 ImGuiWindowFlags_NoBackground          | 
                                 ImGuiWindowFlags_MenuBar               |
                                 ImGuiWindowFlags_NoScrollbar           |      // 不显示滚动条
                                 ImGuiWindowFlags_NoScrollWithMouse     ;      // 禁止滚动行为
          
        ImGuiViewport* vp = ImGui::GetMainViewport();
        ImGui::SetNextWindowPos(vp->WorkPos);
        ImGui::SetNextWindowSize(vp->WorkSize);
        ImGui::SetNextWindowViewport(vp->ID);
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding,  ImVec2(10.0, 6.f)); // 菜单栏高度 
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.f);
        ImGui::PushStyleColor(ImGuiCol_MenuBarBg,    ImVec4(0.f, 0.f, 0.f, 0.f));
        ImGui::PushStyleColor(ImGuiCol_Border,       ImVec4(0.f, 0.f, 0.f, 0.f));
        ImGui::PushStyleColor(ImGuiCol_BorderShadow, ImVec4(0.f, 0.f, 0.f, 0.f)); 

        ImGui::Begin("MiniCAD", nullptr, flags);

        ImGui::PopStyleVar(3);
        ImGui::PopStyleColor(3);
        // ── 1. 菜单栏（含最小化/最大化/关闭） ──────────────────
        DrawMenubar(dm);

        // ── 2. 工具栏 ────────────────────────────────────────────
        DrawToolbar(dm);

        // ── 3. 绘图区（剩余高度 - 状态栏） ──────────────────────
        { 
            ImGui::BeginChild("##DocArea", ImVec2(0, -kStatusBarHeight),  false, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoMove);
            DrawDocumentTabs(dm);
            ImGui::EndChild();
        }

        // ── 4. 状态栏 ────────────────────────────────────────────
        DrawStatusBar(dm);

        // ── 5. 文字输入弹窗 ───────────────────────────────────────
        DrawTextInputPopup(dm);

        ImGuiID dockspace_id = ImGui::GetID("MainDockSpace");
        ImGui::DockSpace(dockspace_id, ImVec2(0, 0), ImGuiDockNodeFlags_PassthruCentralNode); 
        ImGui::End();
    }
  
    void UIManager::DrawTextInputPopup(DocumentManager& dm)
    {
        auto* doc = dm.GetActive();
        if (!doc) return;

        auto& req = doc->GetEditor().GetTextInputRequest();
        if (req.Active)
            ImGui::OpenPopup("##TextInput");

        ImGui::SetNextWindowSize(ImVec2(360, 0));
        if (ImGui::BeginPopupModal("##TextInput", nullptr,
                                   ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoTitleBar))
        {
            ImGui::TextUnformatted("输入文字 (Enter 确认 / Esc 取消):");
            ImGui::Separator();
            ImGui::Spacing();

            static char s_buf[512] = {};
            ImGui::SetNextItemWidth(-1.f);
            if (ImGui::IsWindowAppearing())
                ImGui::SetKeyboardFocusHere();
            bool confirm = ImGui::InputText("##textbuf", s_buf, sizeof(s_buf),
                                            ImGuiInputTextFlags_EnterReturnsTrue);
            ImGui::Spacing();

            if (confirm || ImGui::Button("确认", ImVec2(120, 0)))
            {
                doc->GetEditor().SubmitTextInput(s_buf);
                s_buf[0] = '\0';
                ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine();
            if (ImGui::Button("取消", ImVec2(120, 0)) || ImGui::IsKeyPressed(ImGuiKey_Escape))
            {
                doc->GetEditor().GetTextInputRequest().Active = false;
                s_buf[0] = '\0';
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }
    }

    void UIManager::DrawMenubar(DocumentManager& dm)
    { 
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(10.f, 6.f)); // 菜单栏高度 
        
        if (!ImGui::BeginMenuBar())
        {
            ImGui::PopStyleVar();  
            return;
        }
           
        // ── Logo 花瓣 鼠标悬浮 缓慢旋转─────────────────────────────────────────────
        {
            const float radius = 8.f;
            const float padL   = 2.f;   // Logo 左侧留白
            const float gapR   = 12.f;  // Logo 右侧距菜单间距
            ImDrawList* dl     = ImGui::GetWindowDrawList();
            ImVec2      pos    = ImGui::GetCursorScreenPos();
            float       menuH  = ImGui::GetFrameHeight();

            ImVec2 center = { pos.x + padL + radius, pos.y + menuH * 0.5f };

            DrawPetalLogo(dl, center, radius);

            // Dummy 只占 Logo 自身宽度，右侧间距交给 SameLine
            ImGui::Dummy(ImVec2(padL + radius * 2.f, menuH));
            ImGui::SameLine(0.f, gapR);  // ← gapR 控制与菜单的距离
        }

        // ── 菜单项 ───────────────────────────────────────────────
        if (ImGui::BeginMenu("文件"))
        {
            if (ImGui::MenuItem("新建", "Ctrl+N"))         { dm.New(); }
            if (ImGui::MenuItem("打开", "Ctrl+O"))         { dm.Open(); }
            ImGui::Separator();

            if (ImGui::MenuItem("保存",     "Ctrl+S"))         { dm.Save(); }
            if (ImGui::MenuItem("另存为",   "Ctrl+Shift+S"))   { dm.SaveAs(); }
            if (ImGui::MenuItem("全部保存", "Ctrl+Alt+S"))     { dm.SaveAll(); }

            ImGui::Separator();
            if (ImGui::MenuItem("退出", "Alt+F4"))
                PostMessage(m_hwnd, WM_CLOSE, 0, 0);
            ImGui::EndMenu();
        } 

        //auto& editor = dm.GetActive()->GetEditor(); 

        if (ImGui::BeginMenu("编辑"))
        {
            if (ImGui::MenuItem("撤销", "Ctrl+Z"))    { dm.Undo(); }
            if (ImGui::MenuItem("重做", "Ctrl+Y"))    { dm.Redo(); }
            if (ImGui::MenuItem("粘贴(P)", "Ctrl+V")) { dm.Paste(); }
            if (ImGui::MenuItem("复制(C)", "Ctrl+C")) { dm.CopySelected(); }
            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu("修改"))
        {
            if (ImGui::MenuItem("阵列", "Array"))  {}
            if (ImGui::MenuItem("移动", "Move"))   {}
            if (ImGui::MenuItem("镜像", "Mirror")) {}
            if (ImGui::MenuItem("旋转", "Rotate")) {}
            ImGui::EndMenu();
        }
        auto& editor = dm.GetActive()->GetEditor();
        /***auto* active = dm.GetActive();
        if (!active)
        {
            ImGui::EndMenuBar();
            ImGui::PopStyleVar();
            return;
        }
        auto& editor = active->GetEditor();***/
        if (ImGui::BeginMenu("绘图"))
        {
            if (ImGui::MenuItem("直线",     "Line"))      { editor.StartLineTool(); }
            if (ImGui::MenuItem("点",       "Point"))     { editor.StartPointTool(); }
            if (ImGui::MenuItem("矩形",     "Rectangle")) { editor.StartRectangleTool(); }
            if (ImGui::MenuItem("圆",       "Circle"))    { editor.StartCircleTool(); }
            if (ImGui::MenuItem("圆弧",     "Arc"))       { editor.StartArcTool(); }
            if (ImGui::MenuItem("旋转",     "Rotate"))    { editor.StartRotateTool(); }
            if (ImGui::MenuItem("文字",     "T"))         { editor.StartTextTool(); }
            
            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu("视图"))
        {
            auto& viewport = dm.GetActive()->GetViewport();
            static bool showGrid = true, showAxis = true, showGizmo = true;
            ImGui::MenuItem("显示网格",   nullptr, &showGrid);   { viewport.ShowGrid(showGrid); }
            ImGui::MenuItem("显示坐标轴", nullptr, &showAxis);   { viewport.ShowAxis(showAxis); }
            ImGui::MenuItem("显示Gizmo",  nullptr, &showGizmo); { viewport.ShowGizmo(showGizmo); }
            ImGui::EndMenu();
        }
        static bool showAbout = false;
        if (ImGui::BeginMenu("帮助"))
        {
            if (ImGui::MenuItem("关于")) { showAbout = true; }
            ImGui::EndMenu();
        }
        if (showAbout)
        {
            ImGui::OpenPopup("关于");
            showAbout = false;
        }
        // About 弹窗
        if (ImGui::BeginPopupModal("关于", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
        {
            ImGui::Text("MiniCAD");
            ImGui::Separator();

            ImGui::Text("版本: 1.0");
            ImGui::Text("基于 Dear ImGui");
            ImGui::Text("作者:\n            Hello");
            ImGui::Text("鸣谢:\n        Qizhiwoniu\n          七只蜗牛");
            ImGui::Spacing();

            if (ImGui::Button("关闭", ImVec2(120, 0)))
            {
                ImGui::CloseCurrentPopup();
            }

            ImGui::EndPopup();
        }
    
        // ── 右侧窗口控制按钮 ─────────────────────────────────────
        {
            const float btnW = 32.f;
            const float gap = 8.f;
            const int btnCount = 4;
            const float totalW = btnW * btnCount + gap * (btnCount - 1)
                + ImGui::GetStyle().WindowPadding.x;
            ImGui::SetCursorPosX(ImGui::GetWindowWidth() - totalW);

            float   buttonsLocalX = ImGui::GetWindowWidth() - totalW;
            ImVec2  screenPos = ImGui::GetWindowPos();
            m_captionButtonsScreenX = screenPos.x + buttonsLocalX;

            ImGui::PushStyleColor(ImGuiCol_Button,        ImVec4(0.f, 0.f, 0.f, 0.f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(1.f, 1.f, 1.f, 0.15f));
            ImGui::PushStyleColor(ImGuiCol_ButtonActive,  ImVec4(1.f, 1.f, 1.f, 0.15f));

            ImDrawList* dl = ImGui::GetWindowDrawList();
            ImU32       iconCol = IM_COL32(255, 255, 255, 255);
            const float iconSize = 10.f;
            // ── 按钮 ───────────────────────────────────────────
            ImGui::Button("##menu", ImVec2(btnW, 0.f));
            ImVec2 center = RectCenter(ImGui::GetItemRectMin(), ImGui::GetItemRectSize());
            DrawDropdownIcon(dl, center, iconSize, iconCol);
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("菜单");
            if (ImGui::IsItemClicked())
            {
                ImGui::OpenPopup("MainMenuPopup");
            }
            if (ImGui::BeginPopup("MainMenuPopup"))
            {
                if (ImGui::MenuItem("主题"))
                {
                    // TODO
                }
                if (ImGui::MenuItem("设置"))
                {
                    // TODO
                }
                ImGui::Separator();
                if (ImGui::MenuItem("退出"))
                {
                    PostMessage(m_hwnd, WM_CLOSE, 0, 0);
                }
                ImGui::EndPopup();
            }
            ImGui::SameLine(0.f, gap);
            // ── 最小化 ───────────────────────────────────────────
            ImGui::Button("##min", ImVec2(btnW, 0.f));
            DrawMinimizeIcon(dl, RectCenter(ImGui::GetItemRectMin(), ImGui::GetItemRectSize()), iconSize, iconCol);
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("最小化");
            if (ImGui::IsItemClicked()) ShowWindow(m_hwnd, SW_MINIMIZE);

            ImGui::SameLine(0.f, gap);

            // ── 最大化 / 还原 ────────────────────────────────────
            bool maximized = IsZoomed(m_hwnd);
            ImGui::Button("##max", ImVec2(btnW, 0.f));
            ImVec2 maxCenter = RectCenter(ImGui::GetItemRectMin(), ImGui::GetItemRectSize());
            if (maximized) DrawRestoreIcon (dl, maxCenter, iconSize, iconCol);
            else           DrawMaximizeIcon(dl, maxCenter, iconSize, iconCol);
            if (ImGui::IsItemHovered()) ImGui::SetTooltip(maximized ? "还原" : "最大化");
            if (ImGui::IsItemClicked()) ShowWindow(m_hwnd, maximized ? SW_RESTORE : SW_MAXIMIZE);

            ImGui::SameLine(0.f, gap);

            // ── 关闭 ─────────────────────────────────────────────
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.85f, 0.15f, 0.15f, 1.f));
            ImGui::PushStyleColor(ImGuiCol_ButtonActive,  ImVec4(0.65f, 0.05f, 0.05f, 1.f));
            ImGui::Button("##close", ImVec2(btnW, 0.f));
            DrawCloseIcon(dl, RectCenter(ImGui::GetItemRectMin(), ImGui::GetItemRectSize()), iconSize, iconCol);
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("关闭");
            if (ImGui::IsItemClicked()) PostMessage(m_hwnd, WM_CLOSE, 0, 0);
            ImGui::PopStyleColor(2);

            ImGui::PopStyleColor(3);
        }

        ImGui::EndMenuBar();   
        ImGui::PopStyleVar();
    }
     
    void UIManager::DrawToolbar(DocumentManager& dm)
    {
        ImGui::BeginChild("##Toolbar", ImVec2(0.f, kToolbarHeight), false);

        float leftPadding = 3.0f;
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + leftPadding);

        const ImVec2 btnSize(kToolBtnSize, kToolBtnSize);

        // ===== 1️ 计算背景区域 =====
        ImVec2 start = ImGui::GetCursorScreenPos();
        float padding = 3.0f;

        float bgHeight = kToolBtnSize + padding * 2.0f;
        float bgWidth = ImGui::GetContentRegionAvail().x-6;

        // 占位（关键）
        ImGui::Dummy(ImVec2(bgWidth, bgHeight));

        // ===== 2️ 画背景（不会盖按钮）=====
        ImDrawList* draw = ImGui::GetWindowDrawList();

        draw->AddRectFilled(start, ImVec2(start.x + bgWidth, start.y + bgHeight), IM_COL32(40, 40, 45, 255), 6.0f);

        // ===== 3️ 回到起点，开始画按钮 =====
        ImGui::SetCursorScreenPos(ImVec2(start.x + padding, start.y + padding));

        ImGui::BeginGroup();

        // 按钮样式（透明底）
        ImVec4 btn     = ImVec4(0, 0, 0, 0);
        ImVec4 hover   = ImVec4(0.25f, 0.25f, 0.30f, 0.8f);
        ImVec4 activeC = ImVec4(0.35f, 0.55f, 0.85f, 0.9f);
        float  imVec2  = kToolBtnSize - 3.0 * 2;
        for (auto& meta : kTools)
        {
            bool active = (m_activeTool == meta.id);

            ImGui::PushStyleColor(ImGuiCol_Button,        btn);
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, hover);
            ImGui::PushStyleColor(ImGuiCol_ButtonActive,  active ? activeC : hover); 
            ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 3.0f);
              
            ImGui::PushID(static_cast<int>(meta.id));
            
            if (ImGui::ImageButton("##icon", m_toolIcons[meta.icon], ImVec2(imVec2, imVec2)))
            {
                m_activeTool = meta.id;
                if (meta.onActivate) meta.onActivate(dm);
            }

            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("%s", meta.tooltip);

            ImGui::PopID(); 
            ImGui::PopStyleVar();
            ImGui::PopStyleColor(3); 
            ImGui::SameLine(); 
            // 分隔线
            if (meta.id == Tool::Select || meta.id == Tool::Text || meta.id == Tool::Rotate)
            {
                ImGui::SeparatorEx(ImGuiSeparatorFlags_Vertical);
                ImGui::SameLine();
            }
        }
        
 
        if (false) //---占位保留，不渲染---
        { 
            // ===== 4️ 右侧 Undo / Redo =====
            float rightOffset = bgWidth - (btnSize.x * 2.0f + padding * 2.0f + 6.0f);
            ImGui::SameLine(rightOffset);

            ImGui::PushStyleColor(ImGuiCol_Button, btn);
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, hover);
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, hover);

            ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 3.0f);

            if (ImGui::ImageButton("##iconUndo", m_toolIcons["Undo"], ImVec2(imVec2, imVec2)))
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("撤销 (Ctrl+Z)");

            ImGui::SameLine(0.f, 4.f);

            if (ImGui::ImageButton("##iconRedo", m_toolIcons["Redo"], ImVec2(imVec2, imVec2)))
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("重做 (Ctrl+Y)");

            ImGui::PopStyleVar();
            ImGui::PopStyleColor(3);
        } 

        ImGui::EndGroup();

        ImGui::EndChild();
    }
     
    void UIManager::DrawDocumentTabs(DocumentManager& dm)
    {
        m_viewportInput = {}; // 每帧重置（纯状态）

        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, 0));

        if (!ImGui::BeginTabBar("MiniCAD##Main"))
        {
            ImGui::PopStyleVar();
            return;
        }

        auto& docs       = dm.GetAll();
        Document* active = dm.GetActive();

        for (auto& docPtr : docs)
        {
            Document* doc = docPtr.get();
            bool open = true;

            ImGui::PushID(doc);

            std::string label = doc->GetName();
            if (doc->IsDirty())
                label += " *";

            if (ImGui::BeginTabItem(label.c_str(), &open))
            {
                if (doc != active)
                {
                    dm.SetActive(doc);
                    active = doc;
                }

                // =========================
                // viewport layout
                // =========================
                ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
                ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, 0));

                ImVec2 size = ImGui::GetContentRegionAvail();
                size.x = ImMax(size.x, 1.0f);
                size.y = ImMax(size.y, 1.0f);

                docPtr->GetViewport().Resize(size.x, size.y);

                auto srv = doc->GetViewport().GetRenderTarget()->GetNativeShaderResource();

                ImVec2 imageMin = ImGui::GetCursorScreenPos();
                ImGui::Image(srv, size);

                // =========================
                // 交互层
                // =========================
                ImGui::SetCursorScreenPos(imageMin);
                ImGui::InvisibleButton(
                    "##DocumentViewportInput",
                    size,
                    ImGuiButtonFlags_MouseButtonLeft |
                    ImGuiButtonFlags_MouseButtonMiddle |
                    ImGuiButtonFlags_MouseButtonRight
                );

                const bool hovered = ImGui::IsItemHovered();
                const bool activeItem = ImGui::IsItemActive();

                // =========================
                // 构建 ViewportInput（仅 active doc）
                // =========================
                if (doc == dm.GetActive())
                {
                    ImGuiIO& io = ImGui::GetIO();

                    ImVec2 mouse = io.MousePos;
                    if (!ImGui::IsMousePosValid(&mouse))
                        mouse = ImVec2(0.f, 0.f);

                    ImVec2 local = { mouse.x - imageMin.x,   mouse.y - imageMin.y  };

                    // =========================
                    // viewport meta
                    // =========================
                    m_viewportInput.Valid     = true;
                    m_viewportInput.Hovered   = hovered;
                    m_viewportInput.Active    = activeItem;
                    m_viewportInput.Focused   = hovered || activeItem;

                    m_viewportInput.Size      = { size.x, size.y };
                    m_viewportInput.ScreenMin = { imageMin.x, imageMin.y };
                    m_viewportInput.ScreenMax = { imageMin.x + size.x, imageMin.y + size.y };

                    // =========================
                    // mouse
                    // =========================
                    m_viewportInput.MouseLocal = { local.x, local.y };
                    m_viewportInput.MouseDelta = {
                        local.x - m_lastLocal.x,
                        local.y - m_lastLocal.y
                    };

                    m_viewportInput.Wheel = (hovered || activeItem) ? io.MouseWheel : 0.f;

                    // =========================
                    // MouseButtons → ButtonState（关键修正）
                    // =========================
                    for (int i = 0; i < 3; ++i)
                    {
                        auto& btn = m_viewportInput.MouseButtons[i];

                        btn.Down = io.MouseDown[i];

                        btn.Pressed = hovered && io.MouseClicked[i];
                        btn.Released = io.MouseReleased[i];
                    }

                    // =========================
                    // modifiers（唯一来源）
                    // =========================
                    m_viewportInput.Modifiers = 0;
                    if (io.KeyShift) m_viewportInput.Modifiers |= (uint8_t)ModifierKey::Shift;
                    if (io.KeyCtrl)  m_viewportInput.Modifiers |= (uint8_t)ModifierKey::Ctrl;
                    if (io.KeyAlt)   m_viewportInput.Modifiers |= (uint8_t)ModifierKey::Alt;

                    // =========================
                    // keyboard
                    // =========================
                    auto setKey = [&](ImGuiKey imguiKey, KeyCode key)  
                    {
                            auto& k = m_viewportInput.Keys[(size_t)key]; 

                            k.Down     = ImGui::IsKeyDown(imguiKey); 
                            k.Pressed  = m_viewportInput.Focused &&  ImGui::IsKeyPressed(imguiKey, false); 
                            k.Released = m_viewportInput.Focused &&  ImGui::IsKeyReleased(imguiKey);
                             

                     };

                    // =========================
                    // common keys
                    // =========================
                    setKey(ImGuiKey_Escape, KeyCode::Escape);
                    setKey(ImGuiKey_Delete, KeyCode::Delete);
                    setKey(ImGuiKey_Space,  KeyCode::Space);
                    setKey(ImGuiKey_Enter,  KeyCode::Enter);

                    setKey(ImGuiKey_A, KeyCode::A);
                    setKey(ImGuiKey_B, KeyCode::B);
                    setKey(ImGuiKey_C, KeyCode::C);
                    setKey(ImGuiKey_D, KeyCode::D);
                    setKey(ImGuiKey_E, KeyCode::E);
                    setKey(ImGuiKey_F, KeyCode::F);
                    setKey(ImGuiKey_G, KeyCode::G);

                    setKey(ImGuiKey_H, KeyCode::H);
                    setKey(ImGuiKey_I, KeyCode::I);
                    setKey(ImGuiKey_J, KeyCode::J);
                    setKey(ImGuiKey_K, KeyCode::K);
                    setKey(ImGuiKey_L, KeyCode::L);
                    setKey(ImGuiKey_M, KeyCode::M);
                    setKey(ImGuiKey_N, KeyCode::N);

                    setKey(ImGuiKey_O, KeyCode::O);
                    setKey(ImGuiKey_P, KeyCode::P);
                    setKey(ImGuiKey_Q, KeyCode::Q);
                    setKey(ImGuiKey_R, KeyCode::R);
                    setKey(ImGuiKey_S, KeyCode::S);
                    setKey(ImGuiKey_T, KeyCode::T);
                    setKey(ImGuiKey_U, KeyCode::U);

                    setKey(ImGuiKey_V, KeyCode::V);
                    setKey(ImGuiKey_W, KeyCode::W);
                    setKey(ImGuiKey_X, KeyCode::X);
                    setKey(ImGuiKey_Y, KeyCode::Y);
                    setKey(ImGuiKey_Z, KeyCode::Z);

                    // =========================
                    // numbers
                    // =========================
                    setKey(ImGuiKey_0, KeyCode::Num0);
                    setKey(ImGuiKey_1, KeyCode::Num1);
                    setKey(ImGuiKey_2, KeyCode::Num2);
                    setKey(ImGuiKey_3, KeyCode::Num3);
                    setKey(ImGuiKey_4, KeyCode::Num4);
                    setKey(ImGuiKey_5, KeyCode::Num5);
                    setKey(ImGuiKey_6, KeyCode::Num6);
                    setKey(ImGuiKey_7, KeyCode::Num7);
                    setKey(ImGuiKey_8, KeyCode::Num8);
                    setKey(ImGuiKey_9, KeyCode::Num9);

                    // =========================
                    // function keys
                    // =========================
                    setKey(ImGuiKey_F1, KeyCode::F1);
                    setKey(ImGuiKey_F2, KeyCode::F2);
                    setKey(ImGuiKey_F3, KeyCode::F3);
                    setKey(ImGuiKey_F4, KeyCode::F4);
                    setKey(ImGuiKey_F5, KeyCode::F5);
                    setKey(ImGuiKey_F6, KeyCode::F6);
                    setKey(ImGuiKey_F7, KeyCode::F7);
                    setKey(ImGuiKey_F8, KeyCode::F8);
                    setKey(ImGuiKey_F9, KeyCode::F9);
                    setKey(ImGuiKey_F10, KeyCode::F10);
                    setKey(ImGuiKey_F11, KeyCode::F11);
                    setKey(ImGuiKey_F12, KeyCode::F12);

                    // =========================
                    // modifiers
                    // =========================
                    setKey(ImGuiKey_LeftShift, KeyCode::LShift);
                    setKey(ImGuiKey_RightShift, KeyCode::RShift);

                    setKey(ImGuiKey_LeftCtrl, KeyCode::LCtrl);
                    setKey(ImGuiKey_RightCtrl, KeyCode::RCtrl);

                    setKey(ImGuiKey_LeftAlt, KeyCode::LAlt);
                    setKey(ImGuiKey_RightAlt, KeyCode::RAlt);



                    // =========================
                    // cursor
                    // =========================
                    if (hovered || activeItem)
                        ImGui::SetMouseCursor(ImGuiMouseCursor_None);

                    if (hovered || activeItem)
                        m_lastLocal = local;
                    else
                        m_viewportInput.MouseDelta = { 0.f, 0.f };

                }

                ImGui::PopStyleVar(2);
                ImGui::EndTabItem();
            }

            ImGui::PopID();

            if (!open)
            {
                dm.Close(doc);
                if (doc == active)
                    dm.SetActive(nullptr);
                break;
            }
        }

        ImGui::EndTabBar();
        ImGui::PopStyleVar();
    }

    void UIManager::DrawStatusBar(DocumentManager& dm)
    {
        if (!dm.GetActive())
            return;

        ImGuiStyle& s = ImGui::GetStyle();

        //ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(6.f, 3.f));
        //ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing,   ImVec2(8.f, 2.f));
        //ImGui::PushStyleColor(ImGuiCol_ChildBg,          ImVec4(0.15f, 0.15f, 0.15f, 1.f));

        ImGui::BeginChild("##StatusBar",
                          ImVec2(0.f, kStatusBarHeight),
                          false,
                          ImGuiWindowFlags_NoScrollbar);

          
        // ── 当前工具 ─────────────────────────────────────────────
        const char* toolNames[] = {
            "选择", "直线", "圆", "矩形", "圆弧", "椭圆", "多段线", "样条曲线", "文字", "复制", "移动", "镜像", "旋转", "撤销", "重做"
        };
        ImGui::TextDisabled("工具:");
        ImGui::SameLine();
        ImGui::TextUnformatted(toolNames[static_cast<int>(m_activeTool)]);

        ImGui::SameLine();
        ImGui::SeparatorEx(ImGuiSeparatorFlags_Vertical);
        ImGui::SameLine();

        // ── 鼠标坐标 ─────────────────────────────────────────────
        const auto& state = m_viewportInput;
        ImGui::TextDisabled("坐标:");
        ImGui::SameLine();
        if (state.Hovered)
            ImGui::Text("X: %.1f  Y: %.1f", state.MouseLocal.x, state.MouseLocal.y);
        else
            ImGui::TextDisabled("---");

        ImGui::SameLine();
        ImGui::SeparatorEx(ImGuiSeparatorFlags_Vertical);
        ImGui::SameLine();
        // ── 正交与捕捉 ────────────────────────────────────────────
        // 捕捉状态
        auto& style = ImGui::GetStyle();
        // 使用 ImGui 语义颜色（自动适配明暗主题）
        const ImVec4 colorActive = style.Colors[ImGuiCol_Text];
        const ImVec4 colorInactive = style.Colors[ImGuiCol_TextDisabled];
        ImGui::BeginChild("status_snap", ImVec2(80, 0), false);
        {

            bool snapEnabled = dm.GetActive()->GetEditor().IsSnapEnabled();
            ImVec2 btnPos = ImGui::GetCursorPos();
            ImGui::TextColored(snapEnabled ? colorActive : colorInactive, "捕捉(F3): ");
            ImGui::SameLine();
            ImGui::TextColored(snapEnabled ? colorActive : colorInactive, snapEnabled ? "开 " : "关  ");
            ImGui::SetCursorPos(btnPos);
            ImGui::InvisibleButton("snap_toggle", ImVec2(80, ImGui::GetTextLineHeight()));
            if (ImGui::IsItemClicked())
            {
                dm.GetActive()->GetEditor().ToggleSnap();
            }
        }
        ImGui::EndChild();

        ImGui::SameLine();

        // 正交状态
        ImGui::BeginChild("status_ortho", ImVec2(80, 0), false);
        {
            bool orthoEnabled = dm.GetActive()->GetEditor().IsOrthoEnabled();

            ImVec2 btnPos = ImGui::GetCursorPos();
            ImGui::TextColored(orthoEnabled ? colorActive : colorInactive, "正交(F8): ");
            ImGui::SameLine();
            ImGui::TextColored(orthoEnabled ? colorActive : colorInactive, orthoEnabled ? "开 " : "关 ");
            ImGui::SetCursorPos(btnPos);
            ImGui::InvisibleButton("ortho_toggle", ImVec2(80, ImGui::GetTextLineHeight()));
            if (ImGui::IsItemClicked())
            {
                dm.GetActive()->GetEditor().ToggleOrtho();
            }
        }
        ImGui::EndChild();
        ImGui::SameLine();
        ImGui::SeparatorEx(ImGuiSeparatorFlags_Vertical);
        ImGui::SameLine();
        // ── 当前文档 ─────────────────────────────────────────────
        Document* active = dm.GetActive();
        ImGui::TextDisabled("文档:");
        ImGui::SameLine();
        if (active)
        {
            ImGui::TextUnformatted(active->GetName().c_str());
            if (active->IsDirty())
            {
                ImGui::SameLine();
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.f, 0.8f, 0.2f, 1.f));
                ImGui::TextUnformatted("● 未保存");
                ImGui::PopStyleColor();
            }
        }
        else
        {
            ImGui::TextDisabled("无");
        }

        // ── 右侧：文档数量 ───────────────────────────────────────
        {
            char buf[32];
            snprintf(buf, sizeof(buf), "共 %zu 个文档",
                     dm.GetAll().size());
            float tw = ImGui::CalcTextSize(buf).x;
            ImGui::SetCursorPosX(ImGui::GetContentRegionMax().x - tw - s.WindowPadding.x);
            ImGui::TextDisabled("%s", buf);
        }

        ImGui::EndChild();
        //ImGui::PopStyleColor();
        //ImGui::PopStyleVar(2);
    }

    void UIManager::InitToolIcons()
    { 
        const char* iconPaths[] = {
                "icons/Arc.png",
                "icons/Circle.png",
                "icons/Copy.png",
                "icons/Cursor.png",
                "icons/Ellipse.png",
                "icons/Line.png",
                "icons/Mirror.png",
                "icons/Move.png",
                "icons/Pline.png",
                "icons/Rect.png",
                "icons/Rotate.png",
                "icons/Spline.png",
                "icons/Text.png",
                "icons/Redo.png",
                "icons/Undo.png",
        };

        m_toolIcons.clear(); 

        for (auto path : iconPaths)
        {
            std::filesystem::path p(path);
            std::string key = p.stem().string();

            auto srv = LoadTextureFromFile(path);
            if (!srv)
                continue;

            m_toolIcons.emplace(key, std::move(srv));
        }   
    }
      
    ImTextureID  UIManager::LoadTextureFromFile(const char* path)
    {
        // 用 stb_image 读取图片
        int w, h, ch;
        unsigned char* pixels = stbi_load(path, &w, &h, &ch, 4); // 强制 RGBA
        if (!pixels) return NULL;

        // 创建 D3D11 Texture2D
        D3D11_TEXTURE2D_DESC desc = {};
        desc.Width = w;
        desc.Height = h;
        desc.MipLevels = 1;
        desc.ArraySize = 1;
        desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        desc.SampleDesc.Count = 1;
        desc.Usage = D3D11_USAGE_DEFAULT;
        desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;

        D3D11_SUBRESOURCE_DATA initData = {};
        initData.pSysMem = pixels;
        initData.SysMemPitch = w * 4;

        ID3D11Texture2D* tex = nullptr;
        m_device->CreateTexture2D(&desc, &initData, &tex);
        stbi_image_free(pixels);

        if (!tex) return NULL;

        // 创建 SRV（Shader Resource View），这才是 ImGui 需要的
        ID3D11ShaderResourceView* srv = nullptr;
        HRESULT hr = m_device->CreateShaderResourceView(tex, nullptr, &srv);
        if (FAILED(hr) || !srv)
        {
            tex->Release();
            return NULL;
        }

        return (ImTextureID)srv;
    }

}  
