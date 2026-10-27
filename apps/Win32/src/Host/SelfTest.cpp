// ── MiniCAD 桌面版自测（--selftest）：注入窗口消息，验证输入在下一帧生效，以及主窗口的标题栏、多文档、图层 ─────────
// MainFrame 的成员函数，只在 Win32 版编译；窗口句柄由 Win32Window 提供
#include "GUI/MainFrame.h"
#include "Host/Win32Window.h"
#include "Host/Win32FloatingWindow.h"
#include "Editor/Input/InputEvent.h"
#include "Document/Document.h"
#include "Core/Entity/LineEntity.hpp"
#include "Core/Entity/CircleEntity.hpp"
#include "Core/Entity/MTextEntity.hpp"
#include "Core/Entity/OleFrameEntity.hpp"
#include "Core/Entity/HatchEntity.hpp"
#include "Core/Entity/ArcEntity.hpp"
#include "Core/Entity/TextEntity.hpp"
#include "Core/Entity/DimensionEntity.hpp"
#include "Core/Entity/RectangleEntity.hpp"
#include "Core/Entity/RegionEntity.hpp"
#include "Core/Entity/Solid3DEntity.hpp"
#include "Core/Entity/ViewportEntity.hpp"
#include "Document/Command/AddEntityCommand.h"
#include "Document/Command/MoveCommand.h"
#include "Core/Log.h"
#include "Core/UIContext.h"
#include "GUI/StatusBarView.h"
#include "Widgets/AutoComplete.h"
#include "Widgets/Button.h"
#include "Widgets/Ribbon.h"
#include "Widgets/ComboBox.h"
#include "Widgets/CommandConsole.h"
#include "Widgets/CommandUI.h"
#include "Widgets/Dialog.h"
#include "Widgets/Label.h"
#include "Widgets/ListView.h"
#include "Widgets/Menu.h"
#include "Widgets/Panel.h"
#include "Widgets/TabView.h"
#include "Widgets/TextBox.h"
#include "Scene/BlockTable.h"
#include "Scene/HatchPatternLibrary.h"
#include "Widgets/TitleBar.h"
#include "Style/Theme.hpp"
#include "Widgets/ViewportHost.h"
#include "GUI/ViewCube.h"
#include "GUI/NodeEditor.h"
#include "NodeGraph/Examples.h"
#include "Widgets/DockSpace.h"
#include "Widgets/UiLayout.h"
#include <psapi.h>
#include <shellapi.h>
#include <shlobj.h>
#include <cstring>
#include <algorithm>
#include <chrono>
#include <functional>
#include <filesystem>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <thread>
#include <unordered_set>
#include <utility>

namespace MiniCAD
{
    namespace
    {
        // 视口渲染目标读回 CPU（RGBA8），用于检查三维视图的显示（M6）
        struct ViewportPixels
        {
            int W = 0, H = 0;
            std::vector<uint8_t> Rgba;
            const uint8_t* At(int x, int y) const { return &Rgba[(static_cast<size_t>(y) * W + x) * 4]; }
            bool Inside(int x, int y) const { return x >= 0 && y >= 0 && x < W && y < H; }
        };

        ViewportPixels ReadViewport(AppPlatform& platform)
        {
            ViewportPixels out;
            if (!static_cast<Win32Window&>(platform).GetGraphics().ReadPixels(platform.GetViewportTarget(), out.W, out.H, out.Rgba))
                out = {};
            return out;
        }

        // 整个窗口（含界面）截图：PrintWindow 取 DWM 合成后的内容
        ViewportPixels CaptureWindow(HWND hwnd)
        {
            ViewportPixels out;
            RECT rc{};
            GetClientRect(hwnd, &rc);
            const int w = rc.right - rc.left, h = rc.bottom - rc.top;
            if (w <= 0 || h <= 0)
                return out;
            HDC screen = GetDC(hwnd);
            HDC mem    = CreateCompatibleDC(screen);
            HBITMAP bmp = CreateCompatibleBitmap(screen, w, h);
            HGDIOBJ old = SelectObject(mem, bmp);
            PrintWindow(hwnd, mem, PW_CLIENTONLY | PW_RENDERFULLCONTENT);
            BITMAPINFO bi{};
            bi.bmiHeader.biSize        = sizeof(bi.bmiHeader);
            bi.bmiHeader.biWidth       = w;
            bi.bmiHeader.biHeight      = -h;   // 自上而下
            bi.bmiHeader.biPlanes      = 1;
            bi.bmiHeader.biBitCount    = 32;
            bi.bmiHeader.biCompression = BI_RGB;
            std::vector<uint8_t> bgra(static_cast<size_t>(w) * h * 4);
            GetDIBits(mem, bmp, 0, h, bgra.data(), &bi, DIB_RGB_COLORS);
            SelectObject(mem, old);
            DeleteObject(bmp);
            DeleteDC(mem);
            ReleaseDC(hwnd, screen);
            out.W = w;
            out.H = h;
            out.Rgba.resize(bgra.size());
            for (size_t i = 0; i < bgra.size(); i += 4)
            {
                out.Rgba[i] = bgra[i + 2]; out.Rgba[i + 1] = bgra[i + 1]; out.Rgba[i + 2] = bgra[i]; out.Rgba[i + 3] = 255;
            }
            return out;
        }

        // 24 位 BMP（看自测的画面用）
        void SaveBmp(const ViewportPixels& px, const std::filesystem::path& path)
        {
            const int rowBytes = (px.W * 3 + 3) & ~3;
            const uint32_t dataSize = static_cast<uint32_t>(rowBytes * px.H);
            uint8_t header[54] = { 'B', 'M' };
            auto put32 = [&](int at, uint32_t v) { for (int i = 0; i < 4; ++i) header[at + i] = static_cast<uint8_t>(v >> (8 * i)); };
            put32(2, 54 + dataSize);
            put32(10, 54);
            put32(14, 40);
            put32(18, static_cast<uint32_t>(px.W));
            put32(22, static_cast<uint32_t>(px.H));
            header[26] = 1;
            header[28] = 24;
            put32(34, dataSize);
            std::ofstream f(path, std::ios::binary);
            f.write(reinterpret_cast<const char*>(header), sizeof(header));
            std::vector<uint8_t> row(rowBytes, 0);
            for (int y = px.H - 1; y >= 0; --y)
            {
                for (int x = 0; x < px.W; ++x)
                {
                    const uint8_t* c = px.At(x, y);
                    row[x * 3] = c[2]; row[x * 3 + 1] = c[1]; row[x * 3 + 2] = c[0];
                }
                f.write(reinterpret_cast<const char*>(row.data()), rowBytes);
            }
        }
    }

    // =========================================================
    // 自测：注入窗口消息，验证输入在下一帧（而不是下下帧）生效，以及主窗口的标题栏、多文档、图层
    // =========================================================
    int MainFrame::RunSelfTest(bool full)
    {
        const HWND hwnd = static_cast<Win32Window*>(m_platform)->GetHwnd();
        std::setvbuf(stdout, nullptr, _IONBF, 0);   // 输出重定向到文件时，中途崩溃也能看到已跑过的检查
        int failures = 0;
        auto check = [&](bool ok, const char* what)
        {
            std::printf("[%s] %s\n", ok ? "通过" : "失败", what);
            if (!ok)
                ++failures;
        };
        // 当前测试项：控制台先打印名字，上一项结束时打印它的耗时，最后列出最慢的几项
        std::vector<std::pair<std::string, double>> sectionTimes;
        std::string sectionName;
        int64_t     sectionStart = NowTicks();
        auto endSection = [&]
        {
            if (sectionName.empty())
                return;
            const double seconds = static_cast<double>(NowTicks() - sectionStart) / static_cast<double>(kTicksPerSecond);
            std::printf("    （%s 用时 %.2f 秒）\n", sectionName.c_str(), seconds);
            sectionTimes.emplace_back(sectionName, seconds);
            sectionName.clear();
        };
        auto section = [&](const char* name)
        {
            endSection();
            sectionName  = name;
            sectionStart = NowTicks();
            std::printf("\n▶ %s\n", name);
        };
        auto pump = [&]
        {
            MSG msg;
            while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE))
            {
                TranslateMessage(&msg);
                DispatchMessageW(&msg);
            }
        };

        // 打开图纸在后台读取：只处理消息（绘制也走 WM_PAINT，与实际运行相同，不额外调用 RenderFrame），
        // 直到读完并加入（最多 30 秒）
        auto waitOpen = [&]
        {
            const int64_t start = NowTicks();
            while (IsOpeningDrawings() && NowTicks() - start < 30 * kTicksPerSecond)
            {
                MsgWaitForMultipleObjects(0, nullptr, FALSE, 20, QS_ALLINPUT);
                pump();
            }
            pump();
        };

        // 处理完启动时的消息，渲染第一帧。v0.2.0 起启动时不再创建演示文档，自测要先有一个活动文档
        pump();
        // 经命令新建：命令执行后通知界面同步（显示视口、隐藏“没有文档”提示）。直接 DocumentManager::New 时视口仍隐藏、
        // 尺寸为 0，下面按视口中心注入的鼠标消息全部落空
        if (!m_docManager.GetActive())
            m_commands.Execute("file.new");
        RenderFrame();

        const MiniGUI::Rect vr = m_viewport->GetScreenBounds();
        const int left = static_cast<int>(std::lround(vr.min.x * m_dpiScale));
        const int top  = static_cast<int>(std::lround(vr.min.y * m_dpiScale));
        const int cx   = static_cast<int>(std::lround(vr.Center().x * m_dpiScale));
        const int cy   = static_cast<int>(std::lround(vr.Center().y * m_dpiScale));
        auto send = [&](UINT m, WPARAM wp, int x, int y) { SendMessageW(hwnd, m, wp, MAKELPARAM(x, y)); };

        Editor&       editor = m_docManager.GetEditor();
        // “全部显示”等把范围中心放在没被功能区、面板、命令行挡住的区域中心（Camera::SetFitInsets）
        auto atFitCenter = [](const Camera& cam, const Math::Point3& world)
        {
            const Camera::FitArea fa = cam.GetFitArea();
            const Math::Point2 p = cam.WorldToScreen(world);
            return std::abs(p.x - (cam.GetWidth() * 0.5 + fa.dx)) < 1.0 && std::abs(p.y - (cam.GetHeight() * 0.5 + fa.dy)) < 1.0;
        };
        const Camera& camera = m_docManager.GetViewport().GetCamera();
        editor.SetViewTransitionSeconds(0.0);   // 视图切换立即生效，切换后马上检查相机（过渡动画单独测）
        const Math::Point3 origin(0.0, 0.0, 0.0);

        // ── 1. 十字光标：移动后的第一帧就画在新位置 ──────────────
        section("1. 十字光标：移动后的第一帧就画在新位置");
        uint64_t frames = m_frames;
        send(WM_MOUSEMOVE, 0, cx + 7, cy + 3);
        RenderFrame();
        {
            const ViewState vs = editor.BuildViewState();
            std::printf("    光标 = (%.0f, %.0f)，期望 (%d, %d)\n", vs.MouseX, vs.MouseY, cx + 7 - left, cy + 3 - top);
            check(m_frames - frames == 1 && std::abs(vs.MouseX - (cx + 7 - left)) < 0.5 && std::abs(vs.MouseY - (cy + 3 - top)) < 0.5,
                  "十字光标在移动后的第一帧到位");
        }

        // ── 2. 中键平移：一帧内生效，位移与鼠标一致 ──────────────
        section("2. 中键平移：一帧内生效，位移与鼠标一致");
        const Math::Point2 before = camera.WorldToScreen(origin);
        send(WM_MBUTTONDOWN, MK_MBUTTON, cx, cy);
        send(WM_MOUSEMOVE, MK_MBUTTON, cx + 40, cy + 25);
        RenderFrame();
        const Math::Point2 after = camera.WorldToScreen(origin);
        std::printf("    原点屏幕位置 (%.1f, %.1f) → (%.1f, %.1f)\n", before.x, before.y, after.x, after.y);
        check(std::abs((after.x - before.x) - 40.0) < 0.5 && std::abs((after.y - before.y) - 25.0) < 0.5,
              "中键平移在下一帧生效，位移等于鼠标位移");

        // ── 3. 拖出视口：捕获期间继续平移 ────────────────────────
        section("3. 拖出视口：捕获期间继续平移");
        send(WM_MOUSEMOVE, MK_MBUTTON, left - 60, cy + 25);     // 拖到左侧面板上
        RenderFrame();
        const Math::Point2 outside = camera.WorldToScreen(origin);
        check(std::abs((outside.x - after.x) - (left - 60 - (cx + 40))) < 0.5,
              "拖出视口到面板上，平移继续（指针捕获）");
        send(WM_MBUTTONUP, 0, left - 60, cy + 25);

        // ── 4. 滚轮缩放：以光标为中心，光标下的世界点不动 ────────
        section("4. 滚轮缩放：以光标为中心，光标下的世界点不动");
        send(WM_MOUSEMOVE, 0, cx, cy);
        RenderFrame();
        const Math::Point3 anchor = camera.ScreenToWorld(cx - left, cy - top);
        const Math::Point2 farBefore = camera.WorldToScreen(Math::Point3(anchor.x + 100.0, anchor.y, 0.0));
        POINT sp = { cx, cy };
        ClientToScreen(hwnd, &sp);
        frames = m_frames;
        SendMessageW(hwnd, WM_MOUSEWHEEL, MAKEWPARAM(0, WHEEL_DELTA), MAKELPARAM(sp.x, sp.y));
        RenderFrame();
        const Math::Point3 anchorAfter = camera.ScreenToWorld(cx - left, cy - top);
        const Math::Point2 farAfter    = camera.WorldToScreen(Math::Point3(anchor.x + 100.0, anchor.y, 0.0));
        std::printf("    光标下世界点 (%.3f, %.3f) → (%.3f, %.3f)；100 单位长度 %.1f → %.1f 像素\n",
                    anchor.x, anchor.y, anchorAfter.x, anchorAfter.y,
                    farBefore.x - (cx - left), farAfter.x - (cx - left));
        const double scale = std::abs(camera.WorldToScreen(Math::Point3(anchor.x + 1.0, anchor.y, 0.0)).x
                                    - camera.WorldToScreen(anchor).x);
        check(m_frames - frames == 1 && std::abs(farAfter.x - farBefore.x) > 1.0,
              "滚轮缩放在下一帧生效");
        check(std::abs(anchorAfter.x - anchor.x) * scale < 1.0 && std::abs(anchorAfter.y - anchor.y) * scale < 1.0,
              "缩放以光标为中心（光标下的点偏移小于 1 像素）");

        // ── 5. 空闲时不渲染 ──────────────────────────────────────
        section("5. 空闲时不渲染");
        const uint64_t vpFrames = m_viewportFrames;
        m_ui->Update();
        check(!m_viewport->RenderContent() && m_viewportFrames == vpFrames, "没有输入时视口不重新渲染");

        // ── 6. 无边框窗口的命中测试：标题空白处可拖动，菜单和按钮不行，上边缘可缩放 ──
        section("6. 无边框窗口的命中测试：标题空白处可拖动，菜单和按钮不行，上边缘可缩放");
        {
            MiniGUI::TitleBar* bar = m_layout->GetTitleBar();
            check(bar != nullptr, "界面描述文件里有标题栏");
            if (bar)
            {
                auto hit = [&](MiniGUI::Vec2 logical)
                {
                    POINT p = { static_cast<LONG>(std::lround(logical.x * m_dpiScale)), static_cast<LONG>(std::lround(logical.y * m_dpiScale)) };
                    ClientToScreen(hwnd, &p);
                    return SendMessageW(hwnd, WM_NCHITTEST, 0, MAKELPARAM(p.x, p.y));
                };
                const MiniGUI::Rect tb    = bar->GetScreenBounds();
                const MiniGUI::Rect title = bar->GetTitleRect();
                const MiniGUI::Rect menu  = m_layout->GetMenuBar() ? m_layout->GetMenuBar()->GetScreenBounds() : MiniGUI::Rect{};
                std::printf("    标题 \"%s\" 位于 x = %.0f～%.0f\n", bar->GetTitle().c_str(), title.min.x, title.max.x);
                // 文档标签在标题栏里（SolidCraft 式）时不显示标题文字：改查标签条右端的空白处
                if (!title.IsEmpty())
                    check(hit(title.Center()) == HTCAPTION, "标题文字处是可拖动的标题区域");
                else
                {
                    const MiniGUI::Rect tabs = m_docTabs->GetScreenBounds();
                    check(m_docTabs->IsVisible() && hit({ tabs.max.x - 12.0f, tabs.Center().y }) == HTCAPTION, "标题栏里文档标签条右端的空白处可拖动窗口");
                }
                check(!menu.IsEmpty() && hit({ menu.min.x + 12.0f, tb.Center().y }) == HTCLIENT, "菜单栏不是标题区域");
                check(hit({ tb.max.x - MiniGUI::TitleBar::kButtonWidth * 0.5f, tb.Center().y }) == HTCLIENT, "关闭按钮不是标题区域");
                const LRESULT edge = hit({ tb.Center().x, 0.0f });
                std::printf("    上边缘命中 %lld（HTTOP = %d），窗口%s\n", static_cast<long long>(edge), HTTOP, IsZoomed(hwnd) ? "已最大化" : "未最大化");
                check(edge == HTTOP, "窗口上边缘可以拖动缩放");
            }
        }

        // ── 7. 最大化 / 还原：命令驱动，界面按新尺寸布局，按钮显示还原图标 ──
        section("7. 最大化 / 还原：命令驱动，界面按新尺寸布局，按钮显示还原图标");
        {
            RECT before{}, after{};
            GetClientRect(hwnd, &before);
            m_commands.Execute("window.maximize");
            pump();
            GetClientRect(hwnd, &after);
            const MiniGUI::Vec2 ds = m_ui->GetDisplaySize();
            check(IsZoomed(hwnd) && m_commands.IsChecked("window.maximize"), "最大化后命令显示为选中（按钮为还原）");
            check(after.right > before.right && std::abs(ds.x * m_dpiScale - after.right) < 1.0f, "最大化后界面按新的客户区布局");
            MONITORINFO mi = { sizeof(mi) };
            GetMonitorInfoW(MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST), &mi);
            RECT wr{};
            GetWindowRect(hwnd, &wr);
            std::printf("    最大化窗口 (%ld, %ld)～(%ld, %ld)，工作区 (%ld, %ld)～(%ld, %ld)\n", wr.left, wr.top, wr.right, wr.bottom,
                        mi.rcWork.left, mi.rcWork.top, mi.rcWork.right, mi.rcWork.bottom);
            POINT o = { 0, 0 };
            ClientToScreen(hwnd, &o);
            check(o.x == mi.rcWork.left && o.y == mi.rcWork.top && after.right == mi.rcWork.right - mi.rcWork.left,
                  "最大化时客户区正好铺满工作区（边框不伸出屏幕）");
            m_commands.Execute("window.maximize");
            pump();
            check(!IsZoomed(hwnd) && !m_commands.IsChecked("window.maximize"), "再次执行还原窗口");
        }

        // ── 8. 多文档：新建、切换（相机随文档恢复）、关闭 ──────────
        section("8. 多文档：新建、切换（相机随文档恢复）、关闭");
        {
            Document* first = m_docManager.GetActive();
            const Math::Point2 firstOrigin = camera.WorldToScreen(origin);
            m_commands.Execute("file.new");
            Document* second = m_docManager.GetActive();
            check(second != first && m_docTabs->GetTabCount() == 2 && m_docTabs->GetSelected() == 1, "新建文档后多一个标签并选中它");
            check(m_title.find(second->GetName()) == 0, "标题栏显示当前文档名");

            m_docTabs->SetSelected(0);          // 相当于点击第一个标签
            const Math::Point2 back = camera.WorldToScreen(origin);
            check(m_docManager.GetActive() == first && std::abs(back.x - firstOrigin.x) < 0.5 && std::abs(back.y - firstOrigin.y) < 0.5,
                  "点击标签切换文档，相机恢复为该文档的视图");

            // 未保存的文档关闭时询问；Esc 取消后文档还在
            m_commands.Execute("doc.next");
            check(m_docManager.GetActive() == second, "Ctrl+Tab 切到下一个文档");
            m_commands.Execute("file.close");
            check(m_ui->HasModal(), "关闭未保存的文档时询问是否保存");
            m_ui->KeyDown(MiniGUI::Key::Escape, 0);
            m_ui->KeyUp(MiniGUI::Key::Escape, 0);
            check(!m_ui->HasModal() && m_docManager.GetAll().size() == 2, "取消后文档没有关闭");

            second->MarkSaved();
            m_commands.Execute("file.close");
            RenderFrame();
            check(m_docManager.GetAll().size() == 1 && m_docManager.GetActive() == first && m_docTabs->GetTabCount() == 1,
                  "关闭当前文档后回到相邻的文档");
        }

        // ── 9. 图层：新建后图层面板和工具栏下拉框同步 ────────────
        section("9. 图层：新建后图层面板和工具栏下拉框同步");
        {
            const int rows = m_layerList->GetItemCount();
            m_commands.Execute("layer.new");
            RenderFrame();
            const size_t layers = m_docManager.GetActive()->GetLayerManager().GetAllLayerIDs().size();
            check(m_layerList->GetItemCount() == rows + 1 && static_cast<size_t>(m_layerList->GetItemCount()) == layers,
                  "新建图层后图层面板多一行");
            const LayerID current = m_docManager.GetActive()->GetLayerManager().GetActiveLayerID();
            check(m_layerIds.size() == layers && std::find(m_layerIds.begin(), m_layerIds.end(), current) != m_layerIds.end(),
                  "新建的图层成为当前图层并出现在面板里");

            // 图层操作走命令栈：撤销 → 图层消失、当前图层退回；重做 → 同一个 ID 回来，面板同步
            const LayerID newId = current;
            m_commands.Execute("edit.undo");
            RenderFrame();
            LayerManager& lm = m_docManager.GetActive()->GetLayerManager();
            check(lm.GetLayer(newId) == nullptr && m_layerList->GetItemCount() == rows && lm.GetActiveLayerID() == Layer::DefaultLayerID,
                  "撤销新建图层：图层消失，当前图层退回 0 层，面板少一行");
            m_commands.Execute("edit.redo");
            RenderFrame();
            check(lm.GetLayer(newId) != nullptr && lm.GetActiveLayerID() == newId && m_layerList->GetItemCount() == rows + 1,
                  "重做：同一个 ID 的图层回来并重新成为当前");
        }

        // ── 9.5 工具栏图层框跟随选择集；绘图区有焦点时 Alt+F 打开菜单 ──
        section("9.5 工具栏图层框跟随选择集；绘图区有焦点时 Alt+F 打开菜单");
        {
            Document* doc   = m_docManager.GetActive();
            Scene&    scene = doc->GetScene();
            Editor&   editor = m_docManager.GetEditor();
            LayerManager& lm = doc->GetLayerManager();
            const LayerID active = lm.GetActiveLayerID();
            const auto& ids = SortedLayerIds(*doc);
            auto indexOf = [&ids](LayerID id) { return static_cast<int>(std::find(ids.begin(), ids.end(), id) - ids.begin()); };
            const auto lid = scene.NextObjectID();
            scene.AddEntity(std::make_unique<LineEntity>(lid, Math::Point3{ -500, -500, 0 }, Math::Point3{ -400, -500, 0 }));
            const LayerID entityLayer = static_cast<const Entity*>(scene.GetEntity(lid))->GetAttr().LayerId;
            check(m_layerBarLayer != nullptr, "工具栏有图层下拉框");
            if (m_layerBarLayer && entityLayer != active)
            {
                RenderFrame();
                check(m_layerBarLayer->GetSelectedIndex() == indexOf(active), "没有选择：图层框显示当前图层");
                editor.SetSelection({ lid });
                StateChanged();
                RenderFrame();
                check(m_layerBarLayer->GetSelectedIndex() == indexOf(entityLayer), "选中对象：图层框显示对象所在的图层");
                editor.SetSelection({});
                StateChanged();
                RenderFrame();
                check(m_layerBarLayer->GetSelectedIndex() == indexOf(active), "取消选择：图层框回到当前图层");
            }
            else
                check(false, "自测前提：当前图层与新对象的图层不同");
            scene.RemoveEntity(lid);

            m_viewport->Focus();
            RenderFrame();
            const uint8_t alt = static_cast<uint8_t>(MiniGUI::ModifierKey::Alt);
            m_ui->KeyDown(MiniGUI::Key::Alt, alt);
            m_ui->KeyDown(MiniGUI::Key::F, alt);
            m_ui->KeyUp(MiniGUI::Key::F, alt);
            m_ui->KeyUp(MiniGUI::Key::Alt, 0);
            RenderFrame();
            check(m_ui->GetTopPopup() != nullptr, "绘图区有焦点时 Alt+F 打开文件菜单");
            m_ui->CloseAllPopups();
            m_viewport->Focus();
            RenderFrame();
        }

        // ── 10. 命令行：绘图区直接打字、补全、空格执行、提示、Esc、空回车重复 ──
        section("10. 命令行：绘图区直接打字、补全、空格执行、提示、Esc、空回车重复");
        {
            m_viewport->Focus();
            RenderFrame();
            SendMessageW(hwnd, WM_KEYDOWN, 'L', 0);
            SendMessageW(hwnd, WM_CHAR, 'l', 0);
            SendMessageW(hwnd, WM_KEYUP, 'L', 0);
            RenderFrame();
            MiniGUI::AutoComplete* ac = m_main.console->GetInput()->GetAutoComplete();
            check(m_main.console->HasInputFocus() && m_main.console->GetInputText() == "l", "绘图区按 L：焦点进入命令行，字母落入输入框");
            check(ac && ac->IsOpen() && ac->GetCurrent() == 0 && ac->GetSuggestions().front().text == "Line",
                  "别名 L 对应的 Line 排在候选第一位并默认选中");

            SendMessageW(hwnd, WM_CHAR, ' ', 0);      // 空格 = 回车
            RenderFrame();
            const auto& lines = editor.GetCmdLine().Lines();
            check(editor.IsActiveTool() && editor.GetLastCommand() == "Line", "空格执行候选：直线工具启动");
            check(!lines.empty() && lines.back() == "命令: Line" && m_main.console->GetLogCount() == static_cast<int>(lines.size()),
                  "回显同步到命令行");
            check(m_viewport->HasFocus() && m_commands.IsChecked("draw.line"), "执行后焦点回到绘图区，工具栏的直线按钮选中");
            std::printf("    提示 \"%s\"\n", m_main.console->GetPrompt().c_str());
            check(m_main.console->GetPrompt() != "命令:" && m_main.console->GetPrompt() == editor.GetCmdLine().Prompt(), "命令行显示当前工具的提示");

            // 工具进行中字母交给工具（不进入命令行）；Esc 取消工具，提示恢复
            SendMessageW(hwnd, WM_KEYDOWN, VK_ESCAPE, 0);
            SendMessageW(hwnd, WM_KEYUP, VK_ESCAPE, 0);
            RenderFrame();
            check(!editor.IsActiveTool() && m_main.console->GetPrompt() == "命令:", "Esc 取消工具，提示恢复为“命令:”");

            // 命令行里没有输入时回车 = 重复上一条命令；Esc 清空并把焦点还给绘图区
            m_main.console->FocusInput();
            m_ui->KeyDown(MiniGUI::Key::Enter, 0);
            RenderFrame();
            check(editor.IsActiveTool() && editor.GetLastCommand() == "Line", "空回车重复上一条命令");
            m_main.console->FocusInput();
            m_main.console->SetInputText("cir");
            m_ui->KeyDown(MiniGUI::Key::Escape, 0);
            RenderFrame();
            std::printf("    输入 \"%s\"，工具%s，视口%s焦点\n", m_main.console->GetInputText().c_str(),
                        editor.IsActiveTool() ? "进行中" : "已结束", m_viewport->HasFocus() ? "有" : "没有");
            check(m_main.console->GetInputText().empty() && !editor.IsActiveTool() && m_viewport->HasFocus(),
                  "命令行里 Esc：清空输入、取消工具、焦点回到绘图区");
            check(!m_main.console->GetHistory().empty() && m_main.console->GetHistory().back() == "Line", "历史记录执行过的命令");

            // ZOOM：Z 空格 → 等待选项，E 空格 → 缩放到全图（相机真的移动到图形中心）
            {
                Document* zdoc = m_docManager.GetActive();
                const CameraState zcam0 = m_docManager.GetViewport().GetCamera().GetState();   // 缩放会改相机，测完还原
                auto zline = std::make_unique<LineEntity>(zdoc->GetScene().NextObjectID(),
                                                          Math::Point3{ 90000, 70000, 0 }, Math::Point3{ 90100, 70050, 0 });
                const Object::ObjectID zid = zline->GetID();
                zdoc->GetScene().AddEntity(std::move(zline));
                m_viewport->Focus();
                RenderFrame();
                SendMessageW(hwnd, WM_KEYDOWN, 'Z', 0);
                SendMessageW(hwnd, WM_CHAR, 'z', 0);
                SendMessageW(hwnd, WM_KEYUP, 'Z', 0);
                RenderFrame();
                SendMessageW(hwnd, WM_CHAR, ' ', 0);
                RenderFrame();
                check(editor.IsZoomPending(), "Z 空格：ZOOM 等待选项");
                Camera& zcam = m_docManager.GetViewport().GetCamera();
                const bool dirty0 = zdoc->IsDirty();
                // A：全部（缩放到全图），可撤销 / 重做，且不让文档变成已修改
                SendMessageW(hwnd, WM_KEYDOWN, 'A', 0);
                SendMessageW(hwnd, WM_CHAR, 'a', 0);
                SendMessageW(hwnd, WM_KEYUP, 'A', 0);
                RenderFrame();   // 敲下 A 即执行，不再需要空格
                AABB zbox = AABB::Empty();
                zdoc->GetScene().GetExtents(zbox);
                const CameraState zall = zcam.GetState();
                check(!editor.IsZoomPending() && atFitCenter(zcam, zbox.Center()),
                      "Z 空格 A：缩放到全图，图形居中到没被面板挡住的区域");
                check(zdoc->IsDirty() == dirty0, "缩放不改变文档的已修改状态");
                m_docManager.Undo();
                check(std::abs(zcam.GetState().Target.x - zcam0.Target.x) < 1e-6 && std::abs(zcam.GetState().Zoom - zcam0.Zoom) < 1e-6, "撤销：回到缩放前的视图");
                m_docManager.Redo();
                check(std::abs(zcam.GetState().Target.x - zall.Target.x) < 1e-6 && std::abs(zcam.GetState().Zoom - zall.Zoom) < 1e-6, "重做：再次缩放到全图");

                // E：框选区域缩放（窗口缩放）：两个角点 → 区域居中并充满视口
                zcam.SetState(zcam0);
                SendMessageW(hwnd, WM_KEYDOWN, 'Z', 0);
                SendMessageW(hwnd, WM_CHAR, 'z', 0);
                SendMessageW(hwnd, WM_KEYUP, 'Z', 0);
                RenderFrame();
                SendMessageW(hwnd, WM_CHAR, ' ', 0);
                RenderFrame();
                SendMessageW(hwnd, WM_KEYDOWN, 'E', 0);
                SendMessageW(hwnd, WM_CHAR, 'e', 0);
                SendMessageW(hwnd, WM_KEYUP, 'E', 0);
                RenderFrame();
                check(editor.IsActiveTool() && editor.GetCmdLine().Prompt().find("窗口") != std::string::npos, "Z 空格 E：进入框选窗口，提示指定角点");
                editor.SubmitPoint(Math::Point3(100.0, 100.0, 0.0));
                editor.SubmitPoint(Math::Point3(300.0, 200.0, 0.0));
                RenderFrame();
                const CameraState zwin = zcam.GetState();
                const Camera::FitArea fa = zcam.GetFitArea();
                const double zexpect = std::max(100.0 * fa.scaleY, 200.0 / (zcam.GetWidth() / zcam.GetHeight()) * fa.scaleX);
                check(atFitCenter(zcam, Math::Point3(200.0, 150.0, 0.0))
                      && std::abs(zwin.Zoom - zexpect) < zexpect * 0.01, "框选区域缩放：区域居中并充满没被面板挡住的部分");   // 视口宽高比会随布局微调，缩放值允许 1% 误差
                m_docManager.Undo();
                check(std::abs(zcam.GetState().Target.x - zcam0.Target.x) < 1e-6 && std::abs(zcam.GetState().Zoom - zcam0.Zoom) < 1e-6, "撤销：回到框选缩放前的视图");
                m_docManager.Redo();
                check(std::abs(zcam.GetState().Target.x - zwin.Target.x) < 1e-6, "重做：再次框选缩放");
                zdoc->GetScene().RemoveEntity(zid);   // 清理，不影响后面的检查
                m_docManager.GetViewport().GetCamera().SetState(zcam0);
                RenderFrame();
            }

        }

        // ── 11. 命令行坐标输入：绝对、相对、相对极坐标 ─────────────
        section("11. 命令行坐标输入：绝对、相对、相对极坐标");
        Document* doc = m_docManager.GetActive();
        auto entityCount = [doc] { return doc->GetScene().GetAllIDs().size(); };
        {
            const size_t n0 = entityCount();
            RunCommandLine("Line");
            RunCommandLine("0,0");
            RunCommandLine("@10,0");
            RunCommandLine("@5<90");
            RenderFrame();
            check(editor.IsActiveTool() && entityCount() == n0 + 2, "命令行坐标：0,0 → @10,0 → @5<90 画出两段直线");
            Math::Point3 anchor;
            const bool hasAnchor = editor.TryGetAnchor(anchor);
            std::printf("    当前锚点 (%.3f, %.3f)\n", anchor.x, anchor.y);
            check(hasAnchor && std::abs(anchor.x - 10.0) < 1e-6 && std::abs(anchor.y - 5.0) < 1e-6, "相对坐标按上一点计算");

            // ── 12. 动态输入：光标旁显示，按数字进入长度框，回车按长度定点 ──
            section("12. 动态输入：光标旁显示，按数字进入长度框，回车按长度定点");
            const Math::Point2 a = camera.WorldToScreen(anchor);
            send(WM_MOUSEMOVE, 0, left + static_cast<int>(a.x) + 80, top + static_cast<int>(a.y));     // 水平向右
            RenderFrame();
            check(IsDynInputVisible() && !DynInputHasFocus(), "有锚点时光标旁显示动态输入，焦点仍在绘图区");
            SendMessageW(hwnd, WM_KEYDOWN, '7', 0);
            SendMessageW(hwnd, WM_CHAR, '7', 0);
            SendMessageW(hwnd, WM_KEYUP, '7', 0);
            RenderFrame();
            check(DynInputHasFocus() && GetDynInputText(0) == "7", "按数字：焦点进入长度框，替换实时值");
            SendMessageW(hwnd, WM_KEYDOWN, VK_RETURN, 0);
            SendMessageW(hwnd, WM_KEYUP, VK_RETURN, 0);
            RenderFrame();
            editor.TryGetAnchor(anchor);
            std::printf("    动态输入后锚点 (%.3f, %.3f)\n", anchor.x, anchor.y);
            // 方向取自光标（像素取整会有微小偏差），距离必须正好是键入的 7
            check(entityCount() == n0 + 3 && std::abs(std::hypot(anchor.x - 10.0, anchor.y - 5.0) - 7.0) < 1e-6 && anchor.x > 16.9,
                  "回车：沿光标方向按键入的长度定点");
            check(m_viewport->HasFocus(), "提交后焦点回到绘图区");
            SendKeyToEditor(MiniGUI::Key::Escape);
            RenderFrame();
            check(!editor.IsActiveTool() && !IsDynInputVisible(), "Esc 结束工具，动态输入隐藏");
        }

        // ── 13. 文字原位编辑：单行提交、多行取消 ────────────────
        section("13. 文字原位编辑：单行提交、多行取消");
        {
            const size_t n0 = entityCount();
            RunCommandLine("Text");
            editor.SubmitPoint(Math::Point3(20.0, 20.0, 0.0));
            StateChanged();
            check(m_textPopup != nullptr && editor.GetTextInputRequest().Active, "单行文字：拾取插入点后打开原位编辑框");
            m_ui->TextInput("MiniGUI 文字");
            m_ui->KeyDown(MiniGUI::Key::Enter, 0);
            m_ui->KeyUp(MiniGUI::Key::Enter, 0);
            RenderFrame();
            check(m_textPopup == nullptr && !editor.GetTextInputRequest().Active && entityCount() == n0 + 1, "回车提交，生成文字");
            if (editor.IsActiveTool())
                SendKeyToEditor(MiniGUI::Key::Escape);

            RunCommandLine("MText");
            editor.SubmitPoint(Math::Point3(20.0, 10.0, 0.0));
            if (!editor.GetMTextInputRequest().Active)
                editor.SubmitPoint(Math::Point3(40.0, 0.0, 0.0));      // 多行文字可能还要指定对角点
            StateChanged();
            check(m_textPopup != nullptr && editor.GetMTextInputRequest().Active, "多行文字：打开多行原位编辑框");
            m_ui->TextInput("不会保存");
            m_ui->KeyDown(MiniGUI::Key::Escape, 0);
            RenderFrame();
            check(m_textPopup == nullptr && !editor.GetMTextInputRequest().Active && entityCount() == n0 + 1, "Esc 取消，不生成文字");
            if (editor.IsActiveTool())
                SendKeyToEditor(MiniGUI::Key::Escape);
        }

        // 点选第 11 步用坐标画的竖线 (10,0)-(10,5) 的中点（在当前视图内）
        auto pickLine = [&]
        {
            Camera& cam = m_docManager.GetViewport().GetCamera();       // 先把视图中心对准这条线
            CameraState st = cam.GetState();
            st.Target = Math::Point3(10.0, 2.5, 0.0);
            cam.SetState(st);
            m_viewport->RequestRender();
            RenderFrame();
            const Math::Point2 p = camera.WorldToScreen(Math::Point3(10.0, 2.5, 0.0));
            const int x = left + static_cast<int>(std::lround(p.x));
            const int y = top + static_cast<int>(std::lround(p.y));
            send(WM_MOUSEMOVE, 0, x, y);
            send(WM_LBUTTONDOWN, MK_LBUTTON, x, y);
            send(WM_LBUTTONUP, 0, x, y);
            RenderFrame();
            std::printf("    点选 (%d, %d)：工具%s，选中 %zu 个\n", x, y, editor.IsActiveTool() ? editor.GetLastCommand().c_str() : "无",
                        editor.GetSelection().size());
            return editor.GetSelection().size();
        };

        // ── 14. 定义块：选择 → B → 基点 → 块名对话框 → 回车 ─────
        section("14. 定义块：选择 → B → 基点 → 块名对话框 → 回车");
        {
            check(pickLine() == 1, "点选一个对象");
            RunCommandLine("Block");
            editor.SubmitPoint(Math::Point3(0.0, 0.0, 0.0));
            StateChanged();
            const std::string name = editor.GetBlockNameRequest().DefaultName;
            check(m_blockNameDialog != nullptr && !name.empty(), "拾取基点后打开块名对话框");
            m_ui->KeyDown(MiniGUI::Key::Enter, 0);
            RenderFrame();
            check(m_blockNameDialog == nullptr && doc->GetScene().GetBlockTable().FindByName(name) != BlockTable::InvalidID,
                  "回车确定：用默认块名定义块");
        }

        // ── 15. 插入块：对话框选择 → 放置工具 ──────────────────────
        section("15. 插入块：对话框选择 → 放置工具");
        {
            RunCommandLine("Insert");
            check(m_insertDialog != nullptr, "插入块：打开块表对话框");
            m_ui->KeyDown(MiniGUI::Key::Enter, 0);
            RenderFrame();
            check(m_insertDialog == nullptr && editor.IsActiveTool(), "回车插入：进入放置工具");
            SendKeyToEditor(MiniGUI::Key::Escape);
        }

        // ── 16. 阵列：对话框、实时预览、确定 ───────────────────────
        section("16. 阵列：对话框、实时预览、确定");
        {
            check(pickLine() == 1, "再点选一个对象");
            const size_t n0 = entityCount();
            RunCommandLine("Array");
            check(m_arrayDialog != nullptr, "阵列：打开对话框");
            const auto& params = editor.GetArrayRequest().Params;
            const int expected = params.rows * params.cols - 1;
            if (m_arrayDialog)
            {
                m_arrayDialog->Focus();
                m_ui->KeyDown(MiniGUI::Key::Enter, 0);
            }
            RenderFrame();
            std::printf("    阵列前 %zu 个对象，之后 %zu 个（期望增加 %d）\n", n0, entityCount(), expected);
            check(m_arrayDialog == nullptr && !editor.GetArrayRequest().Active && entityCount() == n0 + static_cast<size_t>(expected),
                  "回车确定：按默认 3 行 4 列生成副本");
        }

        // ── 16b. 图案填充：对话框选图案 → 回车 → 拾取内部点 ──────────────
        section("16b. 图案填充：对话框选图案 → 回车 → 拾取内部点");
        {
            RunCommandLine("Rectangle");
            editor.SubmitPoint(Math::Point3(200.0, 200.0, 0.0));
            editor.SubmitPoint(Math::Point3(240.0, 220.0, 0.0));
            if (editor.IsActiveTool())
                SendKeyToEditor(MiniGUI::Key::Escape);
            const size_t n0 = entityCount();

            RunCommandLine("Hatch");
            check(m_hatchDialog != nullptr && editor.GetHatchRequest().Active && !editor.IsActiveTool(), "填充：先打开图案对话框");
            editor.GetHatchSettings().Pattern = "BRICK";
            editor.GetHatchSettings().Scale   = 0.5;
            if (m_hatchDialog)
            {
                m_hatchDialog->Focus();
                m_ui->KeyDown(MiniGUI::Key::Enter, 0);
            }
            RenderFrame();
            check(m_hatchDialog == nullptr && editor.IsActiveTool() && editor.GetLastCommand() == "Hatch", "回车确定：进入拾取内部点");

            editor.SubmitPoint(Math::Point3(220.0, 210.0, 0.0));
            RenderFrame();
            const HatchEntity* made = nullptr;
            doc->GetScene().ForEachObject([&](const Object& o)
            {
                if (o.IsKindOf<HatchEntity>() && static_cast<const HatchEntity&>(o).GetPattern().Name == "BRICK")
                    made = static_cast<const HatchEntity*>(&o);
            });
            check(entityCount() == n0 + 1 && made && made->GetScale() == 0.5,
                  "在矩形内部点击：按所选图案、比例生成填充");
            if (editor.IsActiveTool())
                SendKeyToEditor(MiniGUI::Key::Escape);
        }

        // ── 16c. 标注工具：角度（两条直线）、半径、弧长、折弯、坐标 ─────────
        section("16c. 标注工具：角度（两条直线）、半径、弧长、折弯、坐标");
        {
            Scene& scene = doc->GetScene();
            auto add = [&](std::unique_ptr<Entity> e) { doc->GetCommandStack().Execute(std::make_unique<AddEntityCommand>(std::move(e)), scene); };
            add(std::make_unique<LineEntity>(scene.NextObjectID(), Math::Point3(300, 300, 0), Math::Point3(320, 300, 0)));
            add(std::make_unique<LineEntity>(scene.NextObjectID(), Math::Point3(300, 300, 0), Math::Point3(300, 320, 0)));
            add(std::make_unique<CircleEntity>(scene.NextObjectID(), Math::Point3(330, 310, 0), 5.0));
            add(std::make_unique<ArcEntity>(scene.NextObjectID(), Math::Point3(345, 310, 0), 5.0, 0.0, Math::HalfPI));

            Camera& cam = m_docManager.GetViewport().GetCamera();       // 视图对准这组图形，工具按屏幕位置拾取
            CameraState st = cam.GetState();
            st.Target = Math::Point3(322.0, 310.0, 0.0);
            cam.SetState(st);
            m_viewport->RequestRender();
            RenderFrame();

            // 找到某类型、测量值匹配的标注
            auto found = [&](DimType type, double measure)
            {
                bool ok = false;
                scene.ForEachObject([&](const Object& o)
                {
                    if (!o.IsKindOf<DimensionEntity>()) return;
                    const auto& d = static_cast<const DimensionEntity&>(o);
                    ok = ok || (d.GetType() == type && std::abs(d.Measurement() - measure) < 1e-6);
                });
                return ok;
            };
            auto run = [&](const char* cmd, std::initializer_list<Math::Point3> pts)
            {
                RunCommandLine(cmd);
                for (const auto& p : pts)
                {
                    editor.SubmitPoint(p);
                    RenderFrame();
                }
                if (editor.IsActiveTool())
                    SendKeyToEditor(MiniGUI::Key::Escape);
            };

            const double s45 = std::sqrt(0.5);
            run("DAN", { { 310, 300, 0 }, { 300, 310, 0 }, { 306, 306, 0 } });
            check(found(DimType::Angular, Math::HalfPI), "角度标注：选两条直线，标注 90°");
            run("DRA", { { 335, 310, 0 }, { 340, 315, 0 } });
            check(found(DimType::Radius, 5.0), "半径标注：选圆，R5");
            run("DDI", { { 335, 310, 0 }, { 340, 315, 0 } });
            check(found(DimType::Diameter, 10.0), "直径标注：选圆，直径 10");
            run("DAR", { { 345 + 5 * s45, 310 + 5 * s45, 0 }, { 345 + 8 * s45, 310 + 8 * s45, 0 } });
            check(found(DimType::ArcLength, 5.0 * Math::HalfPI), "弧长标注：选圆弧，弧长 2.5π");
            run("DJO", { { 335, 310, 0 }, { 322, 322, 0 }, { 336, 316, 0 }, { 333, 314, 0 } });
            check(found(DimType::JoggedRadius, 5.0), "折弯标注：选圆 → 替代圆心 → 尺寸线 → 折弯，R5");
            run("DOR", { { 300, 300, 0 }, { 312, 302, 0 } });
            check(found(DimType::Ordinate, 300.0), "坐标标注：引线偏水平，标注 Y 坐标 300");
        }

        // ── 16d. 文字样式：对话框；新建文字使用当前样式与其固定字高 ──────────
        section("16d. 文字样式：对话框；新建文字使用当前样式与其固定字高");
        {
            RunCommandLine("ST");
            check(m_textStyleDialog != nullptr && editor.GetTextStyleRequest().Active, "文字样式：打开对话框");
            if (m_textStyleDialog)
            {
                m_textStyleDialog->Focus();
                m_ui->KeyDown(MiniGUI::Key::Enter, 0);
            }
            RenderFrame();
            check(m_textStyleDialog == nullptr && !editor.GetTextStyleRequest().Active, "回车关闭文字样式对话框");

            Scene& scene = doc->GetScene();
            TextStyleRecord rec;
            rec.Name     = "自测样式";
            rec.FontFile = "GB2312.ttf";
            rec.Height   = 7.0;
            const TextStyleID sid = scene.GetTextStyleTable().Add(rec);
            scene.SetCurrentTextStyle(sid);

            RunCommandLine("Text");
            editor.SubmitPoint(Math::Point3(400.0, 400.0, 0.0));
            StateChanged();
            editor.SubmitTextInput("样式自测");
            StateChanged();
            RenderFrame();
            bool ok = false;
            scene.ForEachObject([&](const Object& o)
            {
                if (!o.IsKindOf<TextEntity>()) return;
                const auto& t = static_cast<const TextEntity&>(o);
                ok = ok || (t.GetText() == "样式自测" && t.GetStyleId() == sid && t.GetHeight() == 7.0f);
            });
            check(ok, "新建文字使用当前文字样式及其固定字高");
            if (editor.IsActiveTool())
                SendKeyToEditor(MiniGUI::Key::Escape);
            scene.SetCurrentTextStyle(TextStyleTable::StandardID);
        }

        // ── 16c'. 三维实体（M3）：命令行建模、拉伸、布尔、显示，一步撤销 ─────────
        section("16c'. 三维实体（M3）：命令行建模、拉伸、布尔、显示，一步撤销");
        {
            Scene& scene = doc->GetScene();
            auto solids = [&]
            {
                std::vector<const Solid3DEntity*> out;
                scene.ForEachObject([&](const Object& o) { if (o.IsKindOf<Solid3DEntity>()) out.push_back(static_cast<const Solid3DEntity*>(&o)); });
                return out;
            };
            Solid::ISolidKernel* kernel = editor.GetSolidKernel();
            check(kernel != nullptr, "三维实体：桌面版经 AppPlatform 提供 SMLib 内核");

            if (kernel)
            {
                const size_t before = solids().size();
                RunCommandLine("BOX");
                editor.SubmitPoint(Math::Point3(2000.0, 2000.0, 0.0));
                editor.SubmitPoint(Math::Point3(2010.0, 2020.0, 0.0));
                RunCommandLine("30");
                if (editor.IsActiveTool())
                    SendKeyToEditor(MiniGUI::Key::Escape);
                auto all = solids();
                const Solid3DEntity* box = all.size() == before + 1 ? all.back() : nullptr;
                for (auto* s : all)
                    if (s->GetBoundingBox().Min.x >= 1999.0) box = s;
                check(box && std::abs(kernel->Volume(box->GetWorldBody(*kernel)) - 6000.0) < 1e-6,
                      "BOX：命令行输入两角点和高度 30，生成体积 6000 的长方体");

                // 显示：实体的 12 条边进入场景顶点（线框）
                size_t wireVerts = 0;
                RenderFrame();
                editor.ForEachCachedVertex([&](const Vertex_P3_C4& v)
                {
                    if (v.pos.x >= 1999.0f && v.pos.x <= 2011.0f && v.pos.y >= 1999.0f && v.pos.y <= 2021.0f) ++wireVerts;
                });
                check(wireVerts >= 24, "BOX：线框画进场景顶点");

                // 拉伸一个圆，再与长方体求并集（一步撤销还原）
                RunCommandLine("Circle");
                editor.SubmitPoint(Math::Point3(2010.0, 2010.0, 0.0));
                editor.SubmitPoint(Math::Point3(2015.0, 2010.0, 0.0));
                if (editor.IsActiveTool())
                    SendKeyToEditor(MiniGUI::Key::Escape);
                Object::ObjectID circle = Object::InvalidID;
                scene.ForEachObject([&](const Object& o)
                {
                    if (o.IsKindOf<CircleEntity>() && static_cast<const CircleEntity&>(o).GetCircle().Center.x == 2010.0) circle = o.GetID();
                });
                editor.SetSelection({ circle });
                RunCommandLine("EXT");
                RunCommandLine("30");
                check(solids().size() == before + 2 && scene.GetEntity(circle) == nullptr, "EXTRUDE：圆拉伸为实体，原来的圆被替换");

                std::unordered_set<Object::ObjectID> pair;
                for (auto* s : solids())
                    if (s->GetBoundingBox().Min.x >= 1999.0) pair.insert(s->GetID());
                editor.SetSelection(pair);
                RunCommandLine("UNION");
                all = solids();
                const Solid3DEntity* merged = all.size() == before + 1 ? all.back() : nullptr;
                const double expected = 6000.0 + 0.5 * 3.14159265358979323846 * 25.0 * 30.0;    // 圆心在长方体右边上：圆柱一半在外
                check(merged && std::abs(kernel->Volume(merged->GetWorldBody(*kernel)) - expected) < 1e-3,
                      "UNION：长方体与圆柱合成一个实体");
                m_docManager.Undo();
                check(solids().size() == before + 2, "UNION：一次撤销还原两个实体");
                m_docManager.Undo();   // 拉伸：圆回来
                m_docManager.Undo();   // 圆
                m_docManager.Undo();   // 长方体
                check(solids().size() == before && scene.GetEntity(circle) == nullptr, "撤销拉伸、圆、长方体：实体全部移除");
                editor.SetSelection({});

                // M5：旋转（命令行空回车 = 整圈），圆角边（键入半径、点选边、绘图区的 Tab 切换到重叠的底边）
                Camera& camera = m_docManager.GetViewport().GetCamera();
                const CameraState savedCamera = camera.GetState();
                camera.ZoomToBounds(2990.0, 2990.0, 3030.0, 3030.0);

                RunCommandLine("Rectangle");
                editor.SubmitPoint(Math::Point3(3002.0, 3000.0, 0.0));
                editor.SubmitPoint(Math::Point3(3004.0, 3003.0, 0.0));
                if (editor.IsActiveTool())
                    SendKeyToEditor(MiniGUI::Key::Escape);
                Object::ObjectID rect = Object::InvalidID;
                scene.ForEachObject([&](const Object& o)
                {
                    if (o.IsKindOf<RectangleEntity>() && std::abs(static_cast<const RectangleEntity&>(o).GetBoundingBox().Min.x - 3002.0) < 0.5) rect = o.GetID();
                });
                editor.SetSelection({ rect });
                RunCommandLine("REV");
                editor.SubmitPoint(Math::Point3(3000.0, 3000.0, 0.0));
                editor.SubmitPoint(Math::Point3(3000.0, 3010.0, 0.0));
                RunCommandLine("");
                all = solids();
                const double pi = 3.14159265358979323846;
                check(all.size() == before + 1 && scene.GetEntity(rect) == nullptr
                      && std::abs(kernel->Volume(all.back()->GetWorldBody(*kernel)) - 36.0 * pi) < 1e-3,
                      "REVOLVE：矩形绕竖直轴，命令行空回车为整圈，体积 36π");

                RunCommandLine("BOX");
                editor.SubmitPoint(Math::Point3(3010.0, 3010.0, 0.0));
                editor.SubmitPoint(Math::Point3(3020.0, 3020.0, 0.0));
                RunCommandLine("10");
                if (editor.IsActiveTool())
                    SendKeyToEditor(MiniGUI::Key::Escape);
                RunCommandLine("FILLETEDGE");
                RunCommandLine("2");
                editor.SubmitPoint(Math::Point3(3015.0, 3010.0, 0.0));   // 顶边
                SendKeyToEditor(MiniGUI::Key::Tab);                      // 切换到重叠的底边
                editor.SubmitPoint(Math::Point3(3015.0, 3010.0, 0.0));
                RunCommandLine("");
                const Solid3DEntity* filleted = nullptr;
                for (auto* s : solids())
                    if (s->GetBoundingBox().Min.x >= 3009.0) filleted = s;
                check(filleted && std::abs(kernel->Volume(filleted->GetWorldBody(*kernel)) - (1000.0 - 2.0 * (4.0 - pi) * 10.0)) < 1e-2,
                      "FILLETEDGE：键入半径 2，点选顶边、Tab 再选底边，回车后两条边倒圆角");
                m_docManager.Undo();   // 圆角
                m_docManager.Undo();   // 长方体
                m_docManager.Undo();   // 旋转：矩形回来
                m_docManager.Undo();   // 矩形
                check(solids().size() == before && scene.GetEntity(rect) == nullptr, "撤销圆角、长方体、旋转、矩形：实体全部移除");

                // M6：西南等轴测 + 着色，读回视口像素：面有颜色，被挡住的边看不见，前面的边看得见
                RunCommandLine("BOX");
                editor.SubmitPoint(Math::Point3(4000.0, 4000.0, 0.0));
                editor.SubmitPoint(Math::Point3(4010.0, 4010.0, 0.0));
                RunCommandLine("10");
                if (editor.IsActiveTool())
                    SendKeyToEditor(MiniGUI::Key::Escape);
                RunCommandLine("CYL");                // 旁边一个圆柱：曲面的平滑着色（看截图）
                editor.SubmitPoint(Math::Point3(4022.0, 4005.0, 0.0));
                RunCommandLine("4");
                RunCommandLine("12");
                if (editor.IsActiveTool())
                    SendKeyToEditor(MiniGUI::Key::Escape);
                RunCommandLine("ViewSWIso");
                camera.ZoomToBounds(AABB({ 3995.0, 3995.0, -5.0 }, { 4030.0, 4015.0, 15.0 }));
                check(!camera.IsPlanView(), "ViewSWIso：切换到西南等轴测");
                Viewport& view3D = m_docManager.GetViewport();
                const bool gridWas = view3D.IsGridShown(), axisWas = view3D.IsAxisShown();
                view3D.ShowGrid(true);   // 截图里看三维视图的网格与坐标轴
                view3D.ShowAxis(true);
                RenderFrame();
                RenderFrame();
                const ViewportPixels px = ReadViewport(*m_platform);
                SaveBmp(px, std::filesystem::temp_directory_path() / "minicad_selftest_3d.bmp");
                SaveBmp(CaptureWindow(hwnd), std::filesystem::temp_directory_path() / "minicad_selftest_window.bmp");
                view3D.ShowGrid(gridWas);
                view3D.ShowAxis(axisWas);
                const bool light = m_docManager.GetViewport().IsLightBackground();
                auto isLine = [&](const uint8_t* c) { return light ? (c[0] < 40 && c[1] < 40 && c[2] < 40) : (c[0] > 245 && c[1] > 245 && c[2] > 245); };
                auto lineNear = [&](const Math::Point3& w) {
                    const auto sp = camera.WorldToScreen(w);
                    for (int dy = -2; dy <= 2; ++dy)
                        for (int dx = -2; dx <= 2; ++dx)
                        {
                            const int x = static_cast<int>(sp.x) + dx, y = static_cast<int>(sp.y) + dy;
                            if (px.Inside(x, y) && isLine(px.At(x, y)))
                                return true;
                        }
                    return false;
                };
                auto shadedAt = [&](const Math::Point3& w) {
                    const auto sp = camera.WorldToScreen(w);
                    const int x = static_cast<int>(sp.x), y = static_cast<int>(sp.y);
                    if (!px.Inside(x, y))
                        return false;
                    const uint8_t* c = px.At(x, y);
                    return c[3] > 200 && !isLine(c) && (c[0] + c[1] + c[2]) > 60;
                };
                // 采样点避开视口中央的十字光标
                check(px.W > 0 && shadedAt({ 4008.0, 4003.0, 10.0 }) && shadedAt({ 4003.0, 4000.0, 3.0 }), "着色：等轴测里顶面、前面画出带明暗的面");
                check(px.W > 0 && lineNear({ 4005.0, 4000.0, 0.0 }) && lineNear({ 4000.0, 4000.0, 5.0 }), "着色：前面的边画在面上，看得见");
                check(px.W > 0 && !lineNear({ 4010.0, 4005.0, 0.0 }) && !lineNear({ 4005.0, 4010.0, 0.0 }), "着色：被实体挡住的底边看不见（深度测试）");
                RunCommandLine("WIREFRAME");
                RenderFrame();
                const ViewportPixels wire = ReadViewport(*m_platform);
                {
                    const auto sp = camera.WorldToScreen({ 4010.0, 4005.0, 0.0 });
                    bool found = false;
                    for (int dy = -2; dy <= 2 && !found; ++dy)
                        for (int dx = -2; dx <= 2 && !found; ++dx)
                        {
                            const int x = static_cast<int>(sp.x) + dx, y = static_cast<int>(sp.y) + dy;
                            found = wire.Inside(x, y) && isLine(wire.At(x, y));
                        }
                    check(found, "线框：被挡住的底边也画出来");
                }
                RunCommandLine("SHADE");
                m_docManager.Undo();   // 视图
                check(camera.IsPlanView(), "撤销视图：回到俯视");

                // 视图方块：点“上”面靠前的棱 → 前上方 45°；再点“前”面中央 → 前视；拖动 → 环绕；撤销逐步回到俯视
                if (auto* cube = static_cast<ViewCube*>(m_main.cube); cube && cube->IsVisible())
                {
                    m_ui->Update();
                    const MiniGUI::Rect cb = cube->GetScreenBounds();
                    check(cb.max.x > m_viewport->GetScreenBounds().Center().x, "视图方块在绘图区右上角");
                    auto at = [&](const Math::Vec3& w) {
                        const MiniGUI::Vec2 p = cube->Project(w);
                        return MiniGUI::Vec2{ cb.min.x + p.x, cb.min.y + p.y };
                    };
                    auto click = [&](MiniGUI::Vec2 p) {
                        m_ui->PointerMove(p, 0);
                        m_ui->PointerDown(p, MiniGUI::MouseButton::Left, 0);
                        m_ui->PointerUp(p, MiniGUI::MouseButton::Left, 0);
                        RenderFrame();
                    };
                    click(at({ 0.0, -0.8, 1.0 }));
                    check(std::abs(camera.GetAzimuth() + 90.0) < 1e-6 && std::abs(camera.GetElevation() - 45.0) < 1e-6,
                          "视图方块：点顶面靠前的棱，切到前上方 45° 的视图");
                    click(at({ 0.0, -1.0, 0.0 }));
                    check(std::abs(camera.GetAzimuth() + 90.0) < 1e-6 && std::abs(camera.GetElevation()) < 1e-6, "视图方块：点“前”面中央，切到前视");
                    const MiniGUI::Vec2 p0 = at({ 0.0, -1.0, 0.0 });
                    m_ui->PointerMove(p0, 0);
                    m_ui->PointerDown(p0, MiniGUI::MouseButton::Left, 0);
                    m_ui->PointerMove({ p0.x + 20.0f, p0.y }, 0);
                    m_ui->PointerMove({ p0.x + 40.0f, p0.y }, 0);
                    m_ui->PointerUp({ p0.x + 40.0f, p0.y }, MiniGUI::MouseButton::Left, 0);
                    RenderFrame();
                    check(std::abs(camera.GetAzimuth() + 90.0) > 5.0 && std::abs(camera.GetElevation()) < 1e-6, "视图方块：拖动环绕（左右拖动改方位角）");
                    // 点指南针的“东”：从东边看，仰角不变（此时为 0，即右视）
                    click(at({ 2.5, 0.0, -1.1 }));
                    check(std::abs(camera.GetAzimuth()) < 1e-6 && std::abs(camera.GetElevation()) < 1e-6, "视图方块：点指南针的“东”，从东边看（右视）");
                    // 点角（上、前、右三面相交）：东南等轴测
                    click(at({ 1.0, -0.85, 0.85 }));   // 右视里落在“右”面的左上角块
                    check(std::abs(camera.GetAzimuth() + 45.0) < 1e-6 && std::abs(camera.GetElevation() - 35.26) < 0.01, "视图方块：点角块，切到东南等轴测");
                    for (int i = 0; i < 4; ++i)
                        m_docManager.Undo();
                    check(camera.IsPlanView(), "视图方块：逐步撤销回到俯视");

                    // 过渡动画：开启后切到前视，只处理消息（不手动绘制、没有鼠标输入），动画也要自己一帧帧推进到前视
                    editor.SetViewTransitionSeconds(0.3);
                    RunCommandLine("ViewFront");
                    // （pump 会一直处理到消息队列空：动画每帧都补发下一帧，所以整个过渡都在 pump 里完成）
                    const uint64_t animFrames = m_frames;
                    const int64_t  animStart  = NowTicks();
                    for (; editor.IsViewTransitioning() && NowTicks() - animStart < 3 * kTicksPerSecond;)
                    {
                        MsgWaitForMultipleObjects(0, nullptr, FALSE, 10, QS_ALLINPUT);
                        pump();
                    }
                    const double animSeconds = static_cast<double>(NowTicks() - animStart) / static_cast<double>(kTicksPerSecond);
                    std::printf("    过渡用了 %.2f 秒、%llu 帧\n", animSeconds, static_cast<unsigned long long>(m_frames - animFrames));
                    check(m_frames - animFrames >= 5 && animSeconds > 0.25, "视图切换平缓过渡：没有输入也逐帧推进约 0.3 秒");
                    check(!editor.IsViewTransitioning() && std::abs(camera.GetAzimuth() + 90.0) < 1e-6 && std::abs(camera.GetElevation()) < 1e-6,
                          "视图切换过渡结束后正好是前视");
                    m_docManager.Undo();
                    editor.SetViewTransitionSeconds(0.0);

                    // 隐藏与显示：命令行 NAVVCUBE OFF / ON / 无选项切换，菜单命令 view.viewcube 切换
                    RunCommandLine("NAVVCUBE OFF");
                    check(!cube->IsVisible(), "视图方块：NAVVCUBE OFF 隐藏");
                    m_commands.Execute("view.viewcube");
                    check(cube->IsVisible() && m_commands.IsChecked("view.viewcube"), "视图方块：菜单“显示视图方块”重新显示（菜单项打勾）");
                    RunCommandLine("navvcube");
                    check(!cube->IsVisible() && !m_commands.IsChecked("view.viewcube"), "视图方块：NAVVCUBE 不带选项时切换");
                    RunCommandLine("VIEWCUBE ON");
                    check(cube->IsVisible(), "视图方块：VIEWCUBE ON 显示");
                }
                m_docManager.Undo();   // 圆柱
                m_docManager.Undo();   // 长方体
                camera.SetState(savedCamera);
            }
        }

        // ── 16c''. 节点编辑器（docs/NodeGraph/PLAN.md）：经界面输入连线、键入数值、拖滑块，绘图区实时预览，烘焙与撤销 ──
        section("16c''. 节点编辑器（docs/NodeGraph/PLAN.md）：经界面输入连线、键入数值、拖滑块，绘图区实时预览，烘焙与撤销");
        if (Solid::ISolidKernel* kernel = editor.GetSolidKernel())
        {
            using namespace Nodes;
            Scene& scene = doc->GetScene();
            m_nodeEvalSync = true;   // 下面逐帧检查求值结果：同步求值（后台求值在本节最后单独检查）
            // 1280 × 800 的窗口里功能区占了高度，停靠在下方的节点编辑器放不下下面摆的节点：本节收起功能区，结束后恢复
            if (MiniGUI::Ribbon* ribbon = m_layout->GetRibbon())
                ribbon->SetCollapsed(true);
            m_ui->Update();         // 先按收起后的高度布局：节点面板打开时按文档区当前高度分配
            ShowNodePanel(false);
            m_ui->Update();
            RenderFrame();
            auto* dock = m_layout->GetDock("main");
            check(m_nodeEditor && dock && dock->IsPanelVisible("nodes") && m_nodeEditor->GetSize().y > 150.0f,
                  "节点编辑器：打开面板（停靠在绘图区下方）");
            Graph& graph = doc->GetNodeGraph();
            if (m_nodeEditor && graph.Empty())
            {
                NodeEditorView& ne = *m_nodeEditor;
                const NodeId slider = ne.AddNodeAt("input.integer", { 30.0f, 20.0f });
                std::printf("    节点编辑器 %.0f × %.0f\n", ne.GetSize().x, ne.GetSize().y);
                const NodeId box    = ne.AddNodeAt("solid.box", { 30.0f, 150.0f });
                const NodeId arr    = ne.AddNodeAt("xform.lineararray", { 300.0f, 40.0f });
                const NodeId uni    = ne.AddNodeAt("solid.union", { 560.0f, 60.0f });
                ne.SetSelection({});
                m_ui->Update();
                RenderFrame();
                auto toWindow = [&](MiniGUI::Vec2 local)
                {
                    const MiniGUI::Rect r = ne.GetScreenBounds();
                    return MiniGUI::Vec2{ r.min.x + local.x, r.min.y + local.y };
                };
                auto drag = [&](MiniGUI::Vec2 a, MiniGUI::Vec2 b, uint8_t mods = 0)
                {
                    m_ui->PointerMove(a, mods);
                    m_ui->PointerDown(a, MiniGUI::MouseButton::Left, mods);
                    m_ui->PointerMove({ (a.x + b.x) * 0.5f, (a.y + b.y) * 0.5f }, mods);
                    m_ui->PointerMove(b, mods);
                    m_ui->PointerUp(b, MiniGUI::MouseButton::Left, mods);
                    RenderFrame();
                };
                auto port = [&](NodeId id, int i, bool output) { return toWindow(*ne.PortPosition(id, i, output)); };
                drag(port(box, 0, true), port(arr, 0, false));
                drag(port(arr, 0, true), port(uni, 0, false));
                drag(port(slider, 0, true), port(arr, 2, false));
                check(graph.GetLinks().size() == 3 && graph.IsInputConnected(arr, 2), "节点编辑器：从输出端口拖到输入端口连线（3 条）");

                // 双击“间距”的内联值，键入 8, 0, 0 回车：相邻长方体重叠 2
                const MiniGUI::Vec2 field = toWindow(*ne.InputFieldPosition(arr, 1));
                m_ui->PointerMove(field, 0);
                m_ui->PointerDown(field, MiniGUI::MouseButton::Left, 0);
                m_ui->PointerUp(field, MiniGUI::MouseButton::Left, 0);
                m_ui->PointerDown(field, MiniGUI::MouseButton::Left, 0);
                m_ui->PointerUp(field, MiniGUI::MouseButton::Left, 0);
                RenderFrame();
                m_ui->TextInput("8, 0, 0");
                m_ui->KeyDown(MiniGUI::Key::Enter, 0);
                m_ui->KeyUp(MiniGUI::Key::Enter, 0);
                RenderFrame();
                const Node* arrNode = graph.GetNode(arr);
                check(arrNode && arrNode->InputValues[1].Type() == DataType::Vector && std::abs(arrNode->InputValues[1].AsVector().x - 8.0) < 1e-9,
                      "节点编辑器：双击内联值键入向量");
                auto unionVolume = [&]
                {
                    const Node* n = graph.GetNode(uni);
                    return n && !n->Outputs[0].empty() ? kernel->Volume(n->Outputs[0][0].AsBody()) : -1.0;
                };
                check(editor.GetPreviewEntityCount() == 6, "节点编辑器：选中的节点（阵列）额外高亮预览它的 5 个输出");
                ne.SetSelection({});
                RenderFrame();
                check(std::abs(unionVolume() - (10.0 + 4 * 8.0) * 100.0) < 1e-3 && editor.GetPreviewEntityCount() == 1,
                      "节点编辑器：5 个长方体并成一个（42 × 10 × 10），绘图区预览并集结果");

                // 拖动整数滑块到最右端（10）：下游重算，长方体本身不重算
                const uint64_t boxSolves = graph.GetNode(box)->SolveCount;
                const MiniGUI::Vec2 knob = toWindow(*ne.ParamPosition(slider, 0));
                drag(knob, { knob.x + 400.0f, knob.y });
                check(graph.GetNode(slider)->Outputs[0][0].AsInteger() == 10 && std::abs(unionVolume() - (10.0 + 9 * 8.0) * 100.0) < 1e-3 &&
                      graph.GetNode(box)->SolveCount == boxSolves, "节点编辑器：拖滑块到 10，阵列与并集重算，上游的长方体不重算");

                // 三维视图里看预览（截图）
                RunCommandLine("ViewSWIso");
                Camera& camera = m_docManager.GetViewport().GetCamera();
                if (AABB pb; editor.GetPreviewBounds(pb))
                    camera.ZoomToBounds(AABB({ pb.Min.x - 10, pb.Min.y - 10, pb.Min.z }, { pb.Max.x + 10, pb.Max.y + 10, pb.Max.z }));
                ne.SetSelection({ box });   // 选中的节点高亮预览
                RenderFrame();
                RenderFrame();
                check(!editor.GetShadedVertices().empty(), "节点编辑器：三维视图里预览实体着色");
                SaveBmp(CaptureWindow(hwnd), std::filesystem::temp_directory_path() / "minicad_selftest_nodes.bmp");
                ne.SetSelection({});
                RenderFrame();

                // 烘焙：一个实体加入图纸，一步撤销
                auto solidCount = [&] { int n = 0; scene.ForEachObject([&](const Object& o) { n += o.IsKindOf<Solid3DEntity>() ? 1 : 0; }); return n; };
                const int solidsBefore = solidCount();
                m_commands.Execute("nodes.bake");
                check(solidCount() == solidsBefore + 1, "节点编辑器：烘焙把并集结果加入图纸");
                m_docManager.Undo();
                check(solidCount() == solidsBefore, "节点编辑器：烘焙可撤销");

                // 节点编辑器有焦点时 Ctrl+Z 撤销节点图的编辑（滑块回到 5）
                ne.Focus();
                m_ui->KeyDown(MiniGUI::Key::Z, static_cast<uint8_t>(MiniGUI::ModifierKey::Ctrl));
                m_ui->KeyUp(MiniGUI::Key::Z, static_cast<uint8_t>(MiniGUI::ModifierKey::Ctrl));
                RenderFrame();
                check(graph.GetNode(slider) && graph.GetNode(slider)->Outputs[0][0].AsInteger() == 5 &&
                      std::abs(unionVolume() - (10.0 + 4 * 8.0) * 100.0) < 1e-3, "节点编辑器：Ctrl+Z 撤销节点图的修改（滑块回到 5）");

                // Delete 删除选中的节点，下游缺少输入
                ne.SetSelection({ box });
                m_ui->KeyDown(MiniGUI::Key::Delete, 0);
                m_ui->KeyUp(MiniGUI::Key::Delete, 0);
                RenderFrame();
                check(!graph.GetNode(box) && graph.GetNode(arr)->State == NodeState::Warning && editor.GetPreviewEntityCount() == 0,
                      "节点编辑器：Delete 删除节点，下游提示缺少输入，预览清空");

                m_docManager.Undo();   // 视图
                graph.Clear();
                ne.ResetView();
                ne.FrameAll();   // 空图：回到原点、缩放 1
                RenderFrame();

                // 搜索框：Tab 打开，键入中文名回车，节点加在光标处
                auto key = [&](MiniGUI::Key k, uint8_t mods = 0)
                {
                    m_ui->KeyDown(k, mods);
                    m_ui->KeyUp(k, mods);
                    RenderFrame();
                };
                auto lastNode = [&]() -> const Node* { return graph.GetNodes().empty() ? nullptr : graph.GetNodes().back().get(); };
                const uint8_t ctrlKey = static_cast<uint8_t>(MiniGUI::ModifierKey::Ctrl);
                ne.Focus();
                m_ui->PointerMove(toWindow({ 60.0f, 40.0f }), 0);
                key(MiniGUI::Key::Tab);
                const bool searchOpened = ne.IsSearchOpen();
                m_ui->TextInput("长方体");
                key(MiniGUI::Key::Enter);
                const Node* sbox = lastNode();
                const MiniGUI::Vec2 boxAt = ne.LocalToGraph({ 60.0f, 40.0f });
                check(searchOpened && !ne.IsSearchOpen() && graph.GetNodes().size() == 1 && sbox->TypeId == "solid.box" &&
                      std::abs(sbox->X - std::round(boxAt.x)) < 1.0f && std::abs(sbox->Y - std::round(boxAt.y)) < 1.0f,
                      "节点编辑器：Tab 打开搜索框，键入“长方体”回车，在光标处添加节点");
                const NodeId boxId = sbox->Id;

                // 空白处双击打开搜索框，按英文 ID 搜索
                const MiniGUI::Vec2 blank = toWindow({ 320.0f, 40.0f });
                m_ui->PointerMove(blank, 0);
                m_ui->PointerDown(blank, MiniGUI::MouseButton::Left, 0);
                m_ui->PointerUp(blank, MiniGUI::MouseButton::Left, 0);
                m_ui->PointerDown(blank, MiniGUI::MouseButton::Left, 0);
                m_ui->PointerUp(blank, MiniGUI::MouseButton::Left, 0);
                RenderFrame();
                const bool dblOpened = ne.IsSearchOpen();
                m_ui->TextInput("linear");
                key(MiniGUI::Key::Enter);
                check(dblOpened && graph.GetNodes().size() == 2 && lastNode()->TypeId == "xform.lineararray",
                      "节点编辑器：空白处双击打开搜索框，按英文 ID（linear）添加线性阵列");
                const NodeId arrId = lastNode()->Id;

                // 从输出端口拖到空白处：打开搜索框，选中的节点自动连上；Esc 关闭不添加
                drag(port(boxId, 0, true), toWindow({ 620.0f, 60.0f }));
                const bool linkOpened = ne.IsSearchOpen();
                key(MiniGUI::Key::Escape);
                check(linkOpened && !ne.IsSearchOpen() && graph.GetNodes().size() == 2, "节点编辑器：拖到空白处打开搜索框，Esc 关闭不添加节点");
                drag(port(boxId, 0, true), toWindow({ 620.0f, 60.0f }));
                m_ui->TextInput("并集");
                key(MiniGUI::Key::Enter);
                const NodeId uniId = lastNode()->Id;
                check(graph.GetNodes().size() == 3 && lastNode()->TypeId == "solid.union" && graph.LinksInto(uniId, 0).size() == 1 &&
                      graph.LinksInto(uniId, 0)[0].FromNode == boxId, "节点编辑器：从输出端口拖到空白处搜索“并集”，新节点自动连上");
                drag(port(boxId, 0, true), port(arrId, 0, false));   // 一条连到复制范围以外的连线

                // Alt + 单击引脚：输入引脚断开它的连线，输出引脚断开它的全部连线，各自一步撤销
                const uint8_t altKey = static_cast<uint8_t>(MiniGUI::ModifierKey::Alt);
                auto altClick = [&](MiniGUI::Vec2 p)
                {
                    m_ui->PointerMove(p, altKey);
                    m_ui->PointerDown(p, MiniGUI::MouseButton::Left, altKey);
                    m_ui->PointerUp(p, MiniGUI::MouseButton::Left, altKey);
                    RenderFrame();
                };
                ne.Focus();
                altClick(port(arrId, 0, false));
                const bool inCut = graph.LinksInto(arrId, 0).empty() && graph.LinksFrom(boxId, 0).size() == 1;
                key(MiniGUI::Key::Z, ctrlKey);
                const bool inUndone = graph.LinksInto(arrId, 0).size() == 1;
                altClick(port(boxId, 0, true));
                const bool outCut = graph.GetLinks().empty() && graph.GetNodes().size() == 3;
                key(MiniGUI::Key::Z, ctrlKey);
                check(inCut && inUndone && outCut && graph.LinksFrom(boxId, 0).size() == 2,
                      "节点编辑器：Alt + 单击输入 / 输出引脚断开连线，Ctrl+Z 恢复");
                const size_t linksBeforeAlt = graph.GetLinks().size();
                altClick(port(arrId, 0, true));   // 没有连线的输出引脚：什么也不做，也不开始拖线
                check(graph.GetLinks().size() == linksBeforeAlt && !ne.IsSearchOpen(), "节点编辑器：Alt + 单击没有连线的引脚不做任何事");

                // 复制粘贴：Ctrl+C 写入剪贴板，Ctrl+V 粘贴到光标处，ID 重新分配，内部连线保留、外部连线丢弃
                MiniGUI::IClipboard& clip = m_ui->GetClipboard();
                const std::string savedClip = clip.GetText();
                ne.SetSelection({ boxId, uniId });
                ne.Focus();
                key(MiniGUI::Key::C, ctrlKey);
                const bool copied = clip.GetText().find("MiniCAD-NodeGraph") != std::string::npos;
                const MiniGUI::Vec2 pasteLocal{ 450.0f, 120.0f };
                m_ui->PointerMove(toWindow(pasteLocal), 0);
                key(MiniGUI::Key::V, ctrlKey);
                const auto& sel = ne.GetSelection();
                const Node* pbox = nullptr;
                const Node* puni = nullptr;
                for (NodeId id : sel)
                    if (const Node* n = graph.GetNode(id))
                        (n->TypeId == "solid.box" ? pbox : puni) = n;
                const MiniGUI::Vec2 pasteAt = ne.LocalToGraph(pasteLocal);
                check(copied && graph.GetNodes().size() == 5 && sel.size() == 2 && pbox && puni && pbox->Id > uniId && puni->Id > uniId &&
                      std::abs(pbox->X - std::round(pasteAt.x)) < 1.0f && std::abs(pbox->Y - std::round(pasteAt.y)) < 1.0f,
                      "节点编辑器：Ctrl+C / Ctrl+V 复制粘贴节点到光标处，ID 重新分配");
                check(pbox && puni && graph.GetLinks().size() == 3 && graph.LinksInto(puni->Id, 0).size() == 1 &&
                      graph.LinksInto(puni->Id, 0)[0].FromNode == pbox->Id && graph.LinksInto(arrId, 0).size() == 1,
                      "节点编辑器：粘贴保留内部连线，丢弃连到外部的连线");
                key(MiniGUI::Key::Z, ctrlKey);
                check(graph.GetNodes().size() == 3 && graph.GetLinks().size() == 2, "节点编辑器：Ctrl+Z 撤销粘贴");
                clip.SetText(savedClip);

                // 参数表（N7）：两个参数（一个带公式）与一个类型；在参数表里切换类型、点“确定”，参数值与公式结果更新，一步撤销
                graph.Clear();
                const NodeId pw = ne.AddNodeAt(kParamNodeType, { 30.0f, 20.0f });
                const NodeId ph = ne.AddNodeAt(kParamNodeType, { 30.0f, 140.0f });
                graph.SetParam(pw, 0, std::string("宽度"));
                graph.SetParam(pw, 2, 600.0);
                graph.SetParam(ph, 0, std::string("高度"));
                graph.SetParam(ph, 3, std::string("宽度 × 2"));
                graph.SaveParamType("小", { { "宽度", 300.0 } });
                RenderFrame();
                OpenNodeParamsDialog();
                m_ui->Update();
                RenderFrame();
                auto* dlg = dynamic_cast<MiniGUI::Dialog*>(m_ui->GetTopPopup());
                // 对话框里的控件（按出现顺序）
                std::vector<MiniGUI::ComboBox*> combos;
                std::vector<MiniGUI::TextBox*> boxes;
                std::vector<MiniGUI::Button*> buttons;
                std::function<void(MiniGUI::Node*)> walk = [&](MiniGUI::Node* n)
                {
                    if (auto* c = dynamic_cast<MiniGUI::ComboBox*>(n)) combos.push_back(c);
                    if (auto* t = dynamic_cast<MiniGUI::TextBox*>(n)) boxes.push_back(t);
                    if (auto* b = dynamic_cast<MiniGUI::Button*>(n)) buttons.push_back(b);
                    for (const auto& ch : n->GetChildren())
                        walk(ch.get());
                };
                if (dlg)
                    walk(dlg);
                // 每个参数一行：名称、值、公式三个文字框 + 类别下拉框；最后是类型下拉框与新类型名称
                check(dlg && dlg->GetTitle() == "参数" && combos.size() == 3 && boxes.size() == 7 && boxes[1]->GetText() == "600" &&
                      boxes[4]->GetText() == "1200", "参数表：打开，列出参数，公式参数显示算出的值");
                SaveBmp(CaptureWindow(hwnd), std::filesystem::temp_directory_path() / "minicad_selftest_params.bmp");
                auto clickButton = [&](const char* text)
                {
                    for (MiniGUI::Button* b : buttons)
                        if (b->GetLabel() && b->GetLabel()->GetText() == text)
                        {
                            const MiniGUI::Vec2 c = b->GetScreenBounds().Center();
                            m_ui->PointerMove(c, 0);
                            m_ui->PointerDown(c, MiniGUI::MouseButton::Left, 0);
                            m_ui->PointerUp(c, MiniGUI::MouseButton::Left, 0);
                            RenderFrame();
                            return true;
                        }
                    return false;
                };
                if (combos.size() == 3 && boxes.size() == 7)
                {
                    combos[2]->Focus(MiniGUI::FocusReason::Program);
                    key(MiniGUI::Key::Down);   // 收起时 ↓ 直接选中下一项（类型“小”）
                    check(combos[2]->GetSelectedText() == "小" && boxes[1]->GetText() == "300", "参数表：选择类型，值填进参数");
                    check(clickButton("确定") && m_ui->GetTopPopup() == nullptr, "参数表：确定后关闭");
                    SyncNodeGraph();
                    const auto& vals = graph.Parameters().Values;
                    check(vals.count("宽度") && std::abs(vals.at("宽度").AsNumber() - 300.0) < 1e-9 && vals.count("高度") &&
                          std::abs(vals.at("高度").AsNumber() - 600.0) < 1e-9, "参数表：切换类型后参数与公式结果更新");
                    ne.Focus();
                    key(MiniGUI::Key::Z, ctrlKey);
                    SyncNodeGraph();
                    check(std::abs(graph.Parameters().Values.at("宽度").AsNumber() - 600.0) < 1e-9, "参数表：Ctrl+Z 一步撤销");
                }
                else if (dlg)
                    dlg->Cancel();

                // 示例（N10，示例是 nodes/examples 里的节点图文件）：点工具行的“示例”，菜单里 ↓↓↓ 回车选第三项“实体：法兰座”，
                // 插入后求值得到法兰座，一步撤销
                graph.Clear();
                MiniGUI::Button* exampleButton = nullptr;
                std::function<void(MiniGUI::Node*)> findButton = [&](MiniGUI::Node* n)
                {
                    if (auto* b = dynamic_cast<MiniGUI::Button*>(n); b && b->GetLabel() && b->GetLabel()->GetText() == "示例")
                        exampleButton = b;
                    for (const auto& ch : n->GetChildren())
                        findButton(ch.get());
                };
                if (ne.GetParent())
                    findButton(ne.GetParent());
                if (exampleButton)
                {
                    const MiniGUI::Vec2 c = exampleButton->GetScreenBounds().Center();
                    m_ui->PointerMove(c, 0);
                    m_ui->PointerDown(c, MiniGUI::MouseButton::Left, 0);
                    m_ui->PointerUp(c, MiniGUI::MouseButton::Left, 0);
                    RenderFrame();
                }
                const bool menuOpened = m_ui->GetTopPopup() != nullptr;
                for (int i = 0; i < 3; ++i)
                    key(MiniGUI::Key::Down);
                key(MiniGUI::Key::Enter);
                SyncNodeGraph();
                const Node* flange = nullptr;
                for (const auto& n : graph.GetNodes())
                    if (n->TypeId == "solid.difference")
                        flange = n.get();
                check(exampleButton && menuOpened && graph.GetNodes().size() > 10 && ne.GetSelection().size() == graph.GetNodes().size() && flange &&
                      flange->State == NodeState::Ok && !flange->Outputs[0].empty(),
                      "节点编辑器：“示例”菜单从文件插入法兰座，求值得到实体");
                RenderFrame();
                SaveBmp(CaptureWindow(hwnd), std::filesystem::temp_directory_path() / "minicad_selftest_example.bmp");
                ne.Focus();
                key(MiniGUI::Key::Z, ctrlKey);
                check(graph.Empty(), "节点编辑器：Ctrl+Z 撤销插入的示例");

                graph.Clear();
                RenderFrame();

                // N11 后台求值：界面不等待；改动正在算的节点取消重来；“停止”后不再自动求值，修改节点图后继续
                m_nodeEvalSync = false;
                m_nodeSyncBudget = -1.0;   // 先检查后台：不论轻重都交给工作线程
                const NodeId bbox = ne.AddNodeAt("solid.box", { 30.0f, 20.0f });
                const NodeId barr = ne.AddNodeAt("xform.lineararray", { 300.0f, 20.0f });
                const NodeId buni = ne.AddNodeAt("solid.union", { 560.0f, 20.0f });
                graph.SetInputValue(barr, 1, Value(Math::Vec3{ 5, 0, 0 }));
                graph.SetInputValue(barr, 2, int64_t(40));
                graph.Connect({ bbox, 0, barr, 0 });
                graph.Connect({ barr, 0, buni, 0 });
                auto bgVolume = [&]
                {
                    const Node* n = graph.GetNode(buni);
                    return n && !n->Dirty && !n->Outputs[0].empty() ? kernel->Volume(n->Outputs[0][0].AsBody()) : -1.0;
                };
                auto waitNodes = [&]
                {
                    const int64_t start = NowTicks();
                    while (m_nodeJob && NowTicks() - start < 60 * kTicksPerSecond)
                    {
                        std::this_thread::sleep_for(std::chrono::milliseconds(10));
                        RenderFrame();
                    }
                    RenderFrame();
                };
                int64_t t0 = NowTicks();
                RenderFrame();
                const double frameMs = static_cast<double>(NowTicks() - t0) * 1000.0 / static_cast<double>(kTicksPerSecond);
                const bool started = m_nodeJob != nullptr;
                check(started && graph.GetNode(buni)->Dirty && m_nodeStop && m_nodeStop->IsVisible() && m_nodeStatus &&
                      m_nodeStatus->GetText().find("求值中") != std::string::npos,
                      "后台求值：节点图交给工作线程，这一帧不等结果，工具行显示进度与“停止”");
                LOG_INFO("后台求值：开始求值的一帧 %.1f ms", frameMs);
                graph.SetInputValue(barr, 2, int64_t(30));
                RenderFrame();
                check(!m_nodeJob || !m_nodeJob->cancel.load(), "后台求值：改了正在算的阵列数量，这次求值不取消（算完先显示，再接着算新的值）");
                waitNodes();
                check(!m_nodeJob && std::abs(bgVolume() - (10.0 + 29 * 5.0) * 100.0) < 1e-3 && !graph.NeedsEvaluate(NodeSolveContext(*doc)) &&
                      !m_nodeStop->IsVisible(), "后台求值：重新求值得到 30 个长方体的并集");

                graph.SetInputValue(barr, 2, int64_t(60));
                RenderFrame();
                bool stopped = false;
                if (m_nodeJob && !m_nodeJob->finished.load())
                {
                    const MiniGUI::Vec2 c = m_nodeStop->GetScreenBounds().Center();
                    m_ui->PointerMove(c, 0);
                    m_ui->PointerDown(c, MiniGUI::MouseButton::Left, 0);
                    m_ui->PointerUp(c, MiniGUI::MouseButton::Left, 0);
                    stopped = m_nodeJob && m_nodeJob->userStop.load();
                    waitNodes();
                    RenderFrame();
                }
                check(stopped && !m_nodeJob && graph.GetNode(buni)->Dirty && m_nodeStatus->GetText().find("已停止") != std::string::npos,
                      "后台求值：点“停止”取消求值，之后不再自动求值");
                graph.SetInputValue(barr, 2, int64_t(20));
                RenderFrame();
                waitNodes();
                check(std::abs(bgVolume() - (10.0 + 19 * 5.0) * 100.0) < 1e-3, "后台求值：修改节点图后继续求值");

                // 轻的节点图：上次耗时已知且不多时在界面线程上当帧算完，不经过后台（与 N11 之前一样没有延迟）
                m_nodeSyncBudget = 40.0;
                graph.Clear();
                const NodeId ls = ne.AddNodeAt("input.number", { 30.0f, 20.0f });
                const NodeId la = ne.AddNodeAt("math.add", { 260.0f, 20.0f });
                graph.Connect({ ls, 0, la, 0 });
                RenderFrame();
                waitNodes();   // 新节点耗时未知：第一次交给后台
                graph.SetParam(ls, 0, 7.0);
                RenderFrame();
                const Node* laNode = graph.GetNode(la);
                check(!m_nodeJob && laNode && !laNode->Dirty && !laNode->Outputs[0].empty() && std::abs(laNode->Outputs[0][0].AsNumber() - 7.0) < 1e-9,
                      "后台求值：轻的节点图改值后当帧出结果，不交给后台");
                graph.Clear();
                RenderFrame();
                m_nodeEvalSync = true;

                // N11b 组节点：Ctrl+G 建组，双击进入，组内键入数值同步到实例，Esc 返回，Ctrl+Shift+G 解组，Ctrl+Z 撤销
                {
                    const NodeId gs  = ne.AddNodeAt("input.number", { 30.0f, 60.0f });
                    const NodeId ga  = ne.AddNodeAt("math.add", { 260.0f, 60.0f });
                    const NodeId gm  = ne.AddNodeAt("math.mul", { 490.0f, 60.0f });
                    const NodeId gn  = ne.AddNodeAt("math.neg", { 720.0f, 60.0f });
                    graph.SetParam(gs, 0, 3.0);
                    graph.SetInputValue(ga, 1, 1.0);
                    graph.SetInputValue(gm, 1, 2.0);
                    graph.Connect({ gs, 0, ga, 0 });
                    graph.Connect({ ga, 0, gm, 0 });
                    graph.Connect({ gm, 0, gn, 0 });
                    auto negValue = [&](NodeId id)
                    {
                        const Node* n = graph.GetNode(id);
                        return n && !n->Outputs.empty() && !n->Outputs[0].empty() ? n->Outputs[0][0].AsNumber() : 1e9;
                    };
                    ne.Focus();
                    ne.SetSelection({ ga, gm });
                    key(MiniGUI::Key::G, ctrlKey);
                    const NodeId inst = ne.GetSelection().size() == 1 ? *ne.GetSelection().begin() : kInvalidNode;
                    const Node* instNode = graph.GetNode(inst);
                    check(instNode && instNode->IsGroup() && graph.GetNodes().size() == 3 && graph.GetLinks().size() == 2 && std::abs(negValue(gn) + 8.0) < 1e-9,
                          "组节点：Ctrl+G 把选中的两个节点收成组，外部连线接到组实例，结果不变");

                    ne.FrameAll();   // 选中的组实例放到画布中央
                    RenderFrame();
                    // 双击组实例的标题栏进入组内
                    const MiniGUI::Vec2 head = toWindow(ne.GraphToLocal(instNode->X + 30.0f, instNode->Y + NodeEditorView::kHeaderH * 0.5f));
                    m_ui->PointerMove(head, 0);
                    for (int i = 0; i < 2; ++i)
                    {
                        m_ui->PointerDown(head, MiniGUI::MouseButton::Left, 0);
                        m_ui->PointerUp(head, MiniGUI::MouseButton::Left, 0);
                    }
                    RenderFrame();
                    Graph* inner = ne.CurrentGraph();
                    check(ne.GetPath().size() == 1 && inner && inner != &graph && inner->GetNode(gm), "组节点：双击组实例进入组内编辑，画布显示组内的图");
                    SaveBmp(CaptureWindow(hwnd), std::filesystem::temp_directory_path() / "minicad_selftest_group.bmp");

                    // 组内双击乘法的 B 键入 3：写回定义，主图的结果更新
                    if (inner && ne.InputFieldPosition(gm, 1))
                    {
                        const MiniGUI::Vec2 f = toWindow(*ne.InputFieldPosition(gm, 1));
                        m_ui->PointerMove(f, 0);
                        for (int i = 0; i < 2; ++i)
                        {
                            m_ui->PointerDown(f, MiniGUI::MouseButton::Left, 0);
                            m_ui->PointerUp(f, MiniGUI::MouseButton::Left, 0);
                        }
                        RenderFrame();
                        m_ui->TextInput("3");
                        key(MiniGUI::Key::Enter);
                    }
                    SyncNodeGraph();
                    check(std::abs(negValue(gn) + 12.0) < 1e-9, "组节点：组内改值同步到组定义，主图的结果更新为 -(3 + 1) × 3");

                    // Esc：先取消选择，再返回主图（选中刚才的组）
                    ne.Focus();
                    key(MiniGUI::Key::Escape);
                    key(MiniGUI::Key::Escape);
                    check(ne.GetPath().empty() && ne.CurrentGraph() == &graph && ne.GetSelection() == std::set<NodeId>{ inst },
                          "组节点：Esc 返回主图，选中刚才的组");

                    // 解组：节点放回主图，组定义随之删除；Ctrl+Z 恢复组
                    key(MiniGUI::Key::G, static_cast<uint8_t>(ctrlKey | static_cast<uint8_t>(MiniGUI::ModifierKey::Shift)));
                    SyncNodeGraph();
                    check(!graph.GetNode(inst) && graph.GetNodes().size() == 4 && graph.Groups().Empty() && std::abs(negValue(gn) + 12.0) < 1e-9,
                          "组节点：Ctrl+Shift+G 解组，节点与连线放回主图，没有实例的组定义删除");
                    key(MiniGUI::Key::Z, ctrlKey);
                    SyncNodeGraph();
                    check(graph.GetNode(inst) && graph.GetNode(inst)->IsGroup() && !graph.Groups().Empty() && std::abs(negValue(gn) + 12.0) < 1e-9,
                          "组节点：Ctrl+Z 撤销解组");
                    graph.Clear();
                    RenderFrame();
                }

                // 节点图文件：空图时直接替换，再插入一份，Ctrl+Z 撤销插入（文件对话框不能自动操作，直接用读出的文本）
                {
                    const std::string text = graph.CopyToString({});   // 空图：不是节点图
                    const std::string demo = Nodes::ReadTextFile(m_platform->GetResourceDir() + "/nodes/examples/meshes.mcnode");
                    check(text.empty() && LoadNodeGraphText(demo, true), "节点图文件：替换为文件里的节点图");
                    const size_t n = graph.GetNodes().size();
                    check(n > 0 && LoadNodeGraphText(demo, false) && graph.GetNodes().size() == 2 * n, "节点图文件：插入到已有节点的右侧");
                    ne.Focus();
                    key(MiniGUI::Key::Z, ctrlKey);
                    check(graph.GetNodes().size() == n, "节点图文件：Ctrl+Z 撤销插入");
                    key(MiniGUI::Key::Z, ctrlKey);
                    check(graph.Empty(), "节点图文件：Ctrl+Z 撤销替换，回到空图");
                    graph.Clear();
                    RenderFrame();
                }
            }
            ShowNodePanel(true);
            m_ui->Update();
            check(!dock || !dock->IsPanelVisible("nodes"), "节点编辑器：再次执行切换命令关闭面板");
            m_nodeEvalSync = false;
        }

        // ── 16e. 面域布尔运算：两个交叠的矩形 → 并 / 差 / 交，一步撤销 ─────────
        if (MiniGUI::Ribbon* ribbon = m_layout->GetRibbon())
            ribbon->SetCollapsed(false);
        RenderFrame();

        section("16e. 面域布尔运算：两个交叠的矩形 → 并 / 差 / 交，一步撤销");
        {
            Scene& scene = doc->GetScene();
            auto drawRect = [&](double x0, double y0, double x1, double y1)
            {
                RunCommandLine("Rectangle");
                editor.SubmitPoint(Math::Point3(x0, y0, 0.0));
                editor.SubmitPoint(Math::Point3(x1, y1, 0.0));
                if (editor.IsActiveTool())
                    SendKeyToEditor(MiniGUI::Key::Escape);
            };
            auto findRects = [&]
            {
                std::unordered_set<Object::ObjectID> ids;
                scene.ForEachObject([&](const Object& o)
                {
                    if (!o.IsKindOf<RectangleEntity>()) return;
                    const auto& r = static_cast<const RectangleEntity&>(o).GetRectangle();
                    if (r.P1.x >= 500.0 && r.P1.y >= 500.0) ids.insert(o.GetID());
                });
                return ids;
            };
            auto regionArea = [&](size_t& count)
            {
                double area = -1.0;
                count = 0;
                scene.ForEachObject([&](const Object& o)
                {
                    if (!o.IsKindOf<RegionEntity>()) return;
                    area = static_cast<const RegionEntity&>(o).Area();
                    ++count;
                });
                return area;
            };

            drawRect(500.0, 500.0, 510.0, 510.0);
            drawRect(505.0, 505.0, 515.0, 515.0);
            const auto rects = findRects();
            check(rects.size() == 2, "布尔运算：画出两个交叠的矩形");

            struct Case { const char* cmd; double area; const char* what; };
            const Case cases[] = {
                { "Union",     175.0, "并集：一个面域，面积 175，两个矩形被替换" },
                { "Subtract",   75.0, "差集：先画的减去后画的，面积 75" },
                { "Intersect",  25.0, "交集：面积 25" },
            };
            for (const Case& c : cases)
            {
                editor.SetSelection(rects);
                RunCommandLine(c.cmd);
                size_t regions = 0;
                const double area = regionArea(regions);
                check(regions == 1 && std::abs(area - c.area) < 1e-6 && findRects().empty() && editor.GetSelection().empty(), c.what);

                m_docManager.Undo();
                regions = 0;
                regionArea(regions);
                check(regions == 0 && findRects() == rects, "一次撤销还原两个矩形、面域消失");
            }

            // REGION：两个互相交叠的矩形 → 两个独立的面域（不合成一个自相交的面域）；一次撤销全部消失
            editor.SetSelection(rects);
            RunCommandLine("Region");
            size_t regions2 = 0;
            regionArea(regions2);
            check(regions2 == 2 && findRects() == rects, "Region：交叠的两个矩形各生成一个面域，原对象保留");
            m_docManager.Undo();
            regionArea(regions2);
            check(regions2 == 0, "Region：一次撤销移除全部新面域");

            // AREA：报告选中对象的面积（命令行回显）
            editor.SetSelection(rects);
            const size_t logBefore = editor.GetCmdLine().Lines().size();
            RunCommandLine("Area");
            const auto& logLines = editor.GetCmdLine().Lines();
            check(logLines.size() >= logBefore + 3 && logLines.back().find("总面积 = 200.0000") != std::string::npos,
                  "Area：逐个报告并给出总面积 200");

            // 修改属性（特性面板 / MatchProp 的入口）：改颜色 → 场景顶点里出现新颜色，撤销后消失
            {
                auto hasColor = [&](double r, double g, double b)
                {
                    m_viewport->RequestRender();   // 直接调 Editor 改属性不会请求重绘（界面里由特性面板请求）
                    RenderFrame();
                    // 拼接流上传 GPU 后已释放，查逐实体缓存；顶点颜色为 8 位，按 8 位比较
                    const VertexColor want(static_cast<float>(r), static_cast<float>(g), static_cast<float>(b), 1.0f);
                    bool found = false;
                    editor.ForEachCachedVertex([&](const Vertex_P3_C4& v)
                    {
                        found |= v.color.r == want.r && v.color.g == want.g && v.color.b == want.b;
                    });
                    return found;
                };
                check(!hasColor(0.123, 0.456, 0.789), "改颜色前：场景里没有这个颜色");
                editor.SetSelection(rects);
                AttrChange ch;
                ch.Color = EntityColor::FromRgb(0.123, 0.456, 0.789);
                check(editor.ChangeSelectionAttr(ch), "改选择集颜色：有对象被修改");
                check(hasColor(0.123, 0.456, 0.789), "改颜色后：对象重新细分，场景顶点是新颜色");
                check(!editor.ChangeSelectionAttr(ch), "再次改成同一颜色：没有变化");
                m_docManager.Undo();
                m_viewport->RequestRender();        // 界面里的撤销命令会走 StateChanged 请求重绘，脚本里要自己请求
                check(!hasColor(0.123, 0.456, 0.789), "撤销后：颜色还原");
                editor.SetSelection({});
                check(!editor.ChangeSelectionAttr(ch), "没有选择集：不修改");
            }

            // 只选一个对象：不运算
            editor.SetSelection({ *rects.begin() });
            RunCommandLine("Union");
            size_t regions = 0;
            regionArea(regions);
            check(regions == 0 && findRects() == rects, "只选一个对象：提示后不做修改");
            editor.SetSelection({});

            // BREAK：画一条水平线，点两个点打断中间一段 → 变成两条；撤销还原
            {
                auto countLines = [&](size_t& n, double& total)
                {
                    n = 0; total = 0.0;
                    scene.ForEachObject([&](const Object& o)
                    {
                        if (!o.IsKindOf<LineEntity>()) return;
                        const Line& l = static_cast<const LineEntity&>(o).GetLine();
                        if (std::abs(l.Start.y - 900.0) > 1e-6) return;
                        ++n; total += std::abs(l.End.x - l.Start.x);
                    });
                };
                RunCommandLine("Line");
                editor.SubmitPoint(Math::Point3(800.0, 900.0, 0.0));
                editor.SubmitPoint(Math::Point3(900.0, 900.0, 0.0));
                if (editor.IsActiveTool())
                    SendKeyToEditor(MiniGUI::Key::Escape);

                Camera& cam = m_docManager.GetViewport().GetCamera();
                const CameraState savedState = cam.GetState();      // 测完还原，免得后面依赖视图的测试看不到对象
                CameraState st = savedState;
                st.Target = Math::Point3(850.0, 900.0, 0.0);
                cam.SetState(st);
                m_viewport->RequestRender();
                RenderFrame();
                auto clickWorld = [&](double wx)
                {
                    RenderFrame();                      // 先让布局稳定；视口位置会随布局变化，这里重新取
                    const MiniGUI::Rect bounds = m_viewport->GetScreenBounds();
                    const Math::Point2 p = camera.WorldToScreen(Math::Point3(wx, 900.0, 0.0));
                    const int x = static_cast<int>(std::lround(bounds.min.x * m_dpiScale)) + static_cast<int>(std::lround(p.x));
                    const int y = static_cast<int>(std::lround(bounds.min.y * m_dpiScale)) + static_cast<int>(std::lround(p.y));
                    send(WM_MOUSEMOVE, 0, x, y);
                    send(WM_LBUTTONDOWN, MK_LBUTTON, x, y);
                    send(WM_LBUTTONUP, 0, x, y);
                    RenderFrame();
                };
                size_t nLines = 0; double total = 0.0;
                countLines(nLines, total);
                check(nLines == 1 && std::abs(total - 100.0) < 1e-6, "打断：先画一条长 100 的线");

                editor.SetSelection({});
                RunCommandLine("Break");
                clickWorld(830.0);
                clickWorld(870.0);
                countLines(nLines, total);
                check(nLines == 2 && std::abs(total - 60.0) < 0.5, "打断：两个断点之间的一段被去掉，剩两条共长约 60");
                if (editor.IsActiveTool())
                    SendKeyToEditor(MiniGUI::Key::Escape);

                m_docManager.Undo();
                countLines(nLines, total);
                check(nLines == 1 && std::abs(total - 100.0) < 1e-6, "打断：一次撤销还原整条线");
                m_docManager.Undo();         // 撤销画线
                cam.SetState(savedState);
                m_viewport->RequestRender();
                RenderFrame();
            }

            // 几何特性（属性描述表）：画一个圆，选中后经 SetSelectionProperty 改半径，撤销还原
            {
                RunCommandLine("Circle");
                editor.SubmitPoint(Math::Point3(1200.0, 1200.0, 0.0));
                editor.SubmitPoint(Math::Point3(1205.0, 1200.0, 0.0));
                if (editor.IsActiveTool())
                    SendKeyToEditor(MiniGUI::Key::Escape);
                auto circleRadius = [&](Object::ObjectID& id)
                {
                    double r = -1.0;
                    id = Object::InvalidID;
                    scene.ForEachObject([&](const Object& o)
                    {
                        if (!o.IsKindOf<CircleEntity>()) return;
                        const auto& c = static_cast<const CircleEntity&>(o).GetCircle();
                        if (c.Center.x < 1100.0) return;
                        r = c.Radius; id = o.GetID();
                    });
                    return r;
                };
                Object::ObjectID cid = Object::InvalidID;
                check(std::abs(circleRadius(cid) - 5.0) < 1e-9, "几何特性：画出半径 5 的圆");
                editor.SetSelection({ cid });
                StateChanged();
                bool hasRadius = false;
                for (const auto& p : editor.GetSelectionProperties())
                    if (std::string(p.name) == "半径" && p.editable && p.value && std::abs(std::get<double>(*p.value) - 5.0) < 1e-9) hasRadius = true;
                check(hasRadius, "几何特性：选中圆后公共特性里有可编辑的半径 5");
                check(editor.SetSelectionProperty("半径", PropValue(8.0)) && std::abs(circleRadius(cid) - 8.0) < 1e-9, "几何特性：改半径为 8");
                check(!editor.SetSelectionProperty("半径", PropValue(-1.0)) && std::abs(circleRadius(cid) - 8.0) < 1e-9, "几何特性：非法半径被拒绝");
                check(editor.GetSelection().count(cid) == 1, "几何特性：修改后选择集不变");
                m_docManager.Undo();
                check(std::abs(circleRadius(cid) - 5.0) < 1e-9, "几何特性：一次撤销还原半径");
                m_docManager.Undo();         // 撤销画圆
                editor.SetSelection({});
                StateChanged();
            }

            // 缩放：选中一条线，基点 → 不保留源 → 比例因子（点到距基点 2 的位置 = 2 倍），撤销还原
            {
                RunCommandLine("Line");
                editor.SubmitPoint(Math::Point3(3000.0, 3000.0, 0.0));
                editor.SubmitPoint(Math::Point3(3010.0, 3000.0, 0.0));
                if (editor.IsActiveTool())
                    SendKeyToEditor(MiniGUI::Key::Escape);
                auto lineLength = [&](Object::ObjectID& id)
                {
                    double len = -1.0;
                    id = Object::InvalidID;
                    scene.ForEachObject([&](const Object& o)
                    {
                        if (!o.IsKindOf<LineEntity>()) return;
                        const Line& l = static_cast<const LineEntity&>(o).GetLine();
                        if (l.Start.x < 2900.0) return;
                        len = l.Length(); id = o.GetID();
                    });
                    return len;
                };
                Object::ObjectID lid = Object::InvalidID;
                check(std::abs(lineLength(lid) - 10.0) < 1e-9, "缩放：画出长 10 的线");
                editor.SetSelection({ lid });
                RunCommandLine("Scale");
                editor.SubmitPoint(Math::Point3(3000.0, 3000.0, 0.0));        // 基点
                SendKeyToEditor(MiniGUI::Key::Enter);                          // 不保留源
                editor.SubmitPoint(Math::Point3(3002.0, 3000.0, 0.0));        // 距基点 2 → 比例 2
                check(std::abs(lineLength(lid) - 20.0) < 1e-9, "缩放：比例 2，线长变为 20");
                m_docManager.Undo();
                check(std::abs(lineLength(lid) - 10.0) < 1e-9, "缩放：一次撤销还原线长");

                // 工具提交后、还没有新的输入事件时就撤销：工具里保存的 EditorContext 指针已失效，
                // OnSceneChanged / Cancel 不能再通过它访问 overlay（曾经会访问冲突崩溃）
                editor.SetSelection({ lid });
                RunCommandLine("Move");
                editor.SubmitPoint(Math::Point3(3000.0, 3000.0, 0.0));
                editor.SubmitPoint(Math::Point3(3000.0, 3005.0, 0.0));
                m_docManager.Undo();
                check(std::abs(lineLength(lid) - 10.0) < 1e-9, "移动提交后立即撤销：不崩溃，线回到原位");

                // 拉伸：交叉窗口框住线的右端点 → 基点 → 目标点（右移 5），线变长；撤销还原
                RunCommandLine("Stretch");
                editor.SubmitPoint(Math::Point3(3005.0, 2995.0, 0.0));         // 窗口角点 1（只框住右端点 (3010,3000)）
                editor.SubmitPoint(Math::Point3(3015.0, 3005.0, 0.0));         // 窗口角点 2
                editor.SubmitPoint(Math::Point3(3010.0, 3000.0, 0.0));         // 基点
                editor.SubmitPoint(Math::Point3(3015.0, 3000.0, 0.0));         // 目标点：位移 (5, 0)
                check(std::abs(lineLength(lid) - 15.0) < 1e-9, "拉伸：窗口内的端点右移 5，线长变为 15");
                m_docManager.Undo();
                check(std::abs(lineLength(lid) - 10.0) < 1e-9, "拉伸：一次撤销还原线长");

                // 全选：Ctrl+A 命令选中所有可拾取对象；锁定图层上的对象不选；工具进行中不处理
                {
                    size_t pickable = 0, total = 0;
                    scene.ForEachObject([&](const Object& o) { ++total; if (o.IsKindOf<Entity>()) ++pickable; });
                    const size_t selected = editor.SelectAll();
                    check(selected > 0 && selected == editor.GetSelection().size() && selected <= pickable, "全选：选中全部可拾取对象");
                    check(m_commands.IsEnabled("edit.selectAll"), "全选：命令可用");

                    const LayerID lockedId = scene.GetLayerManager().AddLayer("全选自测锁定层");
                    const auto lockedEntity = scene.NextObjectID();
                    auto locked = std::make_unique<LineEntity>(lockedEntity, Math::Point3{ 5000, 0, 0 }, Math::Point3{ 5010, 0, 0 });
                    locked->GetAttr().LayerId = lockedId;
                    scene.AddEntity(std::move(locked));
                    scene.GetLayerManager().GetLayer(lockedId)->SetLocked(true);
                    editor.SelectAll();
                    check(editor.GetSelection().count(lockedEntity) == 0, "全选：锁定图层上的对象不选");
                    scene.RemoveEntity(lockedEntity);
                    editor.SetSelection({});

                    RunCommandLine("Line");
                    check(editor.SelectAll() == 0 && editor.GetSelection().empty(), "全选：工具进行中不处理");
                    if (editor.IsActiveTool())
                        SendKeyToEditor(MiniGUI::Key::Escape);
                    editor.SetSelection({});
                }

                // 界面入口：新增的菜单命令都已注册；删除命令删除选择集，可撤销；命令行别名可用
                {
                    for (const char* id : { "draw.xline", "draw.ray", "dim.leader", "dim.mleader", "modify.offset", "modify.scale", "modify.stretch", "modify.break", "edit.delete", "edit.selectAll" })
                        check(m_commands.Find(id) != nullptr, (std::string("命令已注册：") + id).c_str());

                    check(!m_commands.IsEnabled("edit.delete"), "删除命令：没有选择集时不可用");
                    RunCommandLine("Line");
                    editor.SubmitPoint(Math::Point3(6000.0, 0.0, 0.0));
                    editor.SubmitPoint(Math::Point3(6010.0, 0.0, 0.0));
                    if (editor.IsActiveTool())
                        SendKeyToEditor(MiniGUI::Key::Escape);
                    Object::ObjectID did = Object::InvalidID;
                    scene.ForEachObject([&](const Object& o)
                    {
                        if (o.IsKindOf<LineEntity>() && static_cast<const LineEntity&>(o).GetLine().Start.x > 5900.0) did = o.GetID();
                    });
                    editor.SetSelection({ did });
                    StateChanged();
                    check(m_commands.IsEnabled("edit.delete"), "删除命令：有选择集时可用");
                    RunCommandLine("Erase");
                    check(scene.GetEntity(did) == nullptr, "删除（Erase 命令）：对象被删除");
                    m_docManager.Undo();
                    check(scene.GetEntity(did) != nullptr, "删除：一次撤销还原对象");
                    m_docManager.Undo();         // 撤销画线
                    editor.SetSelection({});
                }

                // 圆角：两条直角相交的线，键入半径 2，依次点选两条线 → 生成圆角弧、两线缩到切点；撤销还原
                {
                    auto drawLine = [&](double x0, double y0, double x1, double y1)
                    {
                        RunCommandLine("Line");
                        editor.SubmitPoint(Math::Point3(x0, y0, 0.0));
                        editor.SubmitPoint(Math::Point3(x1, y1, 0.0));
                        if (editor.IsActiveTool())
                            SendKeyToEditor(MiniGUI::Key::Escape);
                    };
                    drawLine(7000.0, 0.0, 7010.0, 0.0);
                    drawLine(7000.0, 0.0, 7000.0, 10.0);
                    auto measure = [&](size_t& arcs, double& xStart)
                    {
                        arcs = 0; xStart = -1.0;
                        scene.ForEachObject([&](const Object& o)
                        {
                            if (o.IsKindOf<ArcEntity>() && static_cast<const ArcEntity&>(o).GetArc().Center.x > 6900.0) ++arcs;
                            if (o.IsKindOf<LineEntity>())
                            {
                                const Line& l = static_cast<const LineEntity&>(o).GetLine();
                                if (l.Start.x > 6900.0 && std::abs(l.End.y - l.Start.y) < 1e-9 && l.End.x > l.Start.x) xStart = l.Start.x;
                            }
                        });
                    };
                    size_t arcs = 0; double xStart = 0.0;
                    measure(arcs, xStart);
                    check(arcs == 0 && std::abs(xStart - 7000.0) < 1e-9, "圆角：画出两条直角相交的线");

                    RunCommandLine("Fillet");
                    check(editor.SubmitCoordinateText("2"), "圆角：键入数字 2 修改半径");
                    editor.SubmitPoint(Math::Point3(7005.0, 0.0, 0.0));        // 点选第一条线
                    editor.SubmitPoint(Math::Point3(7000.0, 5.0, 0.0));        // 点选第二条线
                    measure(arcs, xStart);
                    check(arcs == 1 && std::abs(xStart - 7002.0) < 1e-9, "圆角：生成一段圆角弧，水平线缩到切点 (7002, 0)");
                    if (editor.IsActiveTool())
                        SendKeyToEditor(MiniGUI::Key::Escape);
                    m_docManager.Undo();
                    measure(arcs, xStart);
                    check(arcs == 0 && std::abs(xStart - 7000.0) < 1e-9, "圆角：一次撤销去掉圆角弧并还原两条线");
                    m_docManager.Undo();         // 撤销画线
                    m_docManager.Undo();
                    editor.SetSelection({});
                }

                // 倒角：两条直角相交的线，键入 "2,3" 设置两个距离，依次点选 → 多一条倒角线、两线缩到倒角点；撤销还原
                {
                    auto drawLine = [&](double x0, double y0, double x1, double y1)
                    {
                        RunCommandLine("Line");
                        editor.SubmitPoint(Math::Point3(x0, y0, 0.0));
                        editor.SubmitPoint(Math::Point3(x1, y1, 0.0));
                        if (editor.IsActiveTool())
                            SendKeyToEditor(MiniGUI::Key::Escape);
                    };
                    drawLine(8000.0, 0.0, 8010.0, 0.0);
                    drawLine(8000.0, 0.0, 8000.0, 10.0);
                    auto measure = [&](size_t& lines, double& xStart)
                    {
                        lines = 0; xStart = -1.0;
                        scene.ForEachObject([&](const Object& o)
                        {
                            if (!o.IsKindOf<LineEntity>()) return;
                            const Line& l = static_cast<const LineEntity&>(o).GetLine();
                            if (l.Start.x < 7900.0 || l.Start.x > 8100.0) return;
                            ++lines;
                            if (std::abs(l.End.y - l.Start.y) < 1e-9 && l.End.x > l.Start.x) xStart = l.Start.x;
                        });
                    };
                    size_t lines = 0; double xStart = 0.0;
                    measure(lines, xStart);
                    check(lines == 2 && std::abs(xStart - 8000.0) < 1e-9, "倒角：画出两条直角相交的线");

                    RunCommandLine("Chamfer");
                    check(editor.SubmitCoordinateText("2,3"), "倒角：键入 2,3 设置两个距离");
                    editor.SubmitPoint(Math::Point3(8005.0, 0.0, 0.0));        // 点选第一条线（D1 = 2）
                    editor.SubmitPoint(Math::Point3(8000.0, 5.0, 0.0));        // 点选第二条线（D2 = 3）
                    measure(lines, xStart);
                    check(lines == 3 && std::abs(xStart - 8002.0) < 1e-9, "倒角：多一条倒角线，水平线缩到 (8002, 0)");
                    if (editor.IsActiveTool())
                        SendKeyToEditor(MiniGUI::Key::Escape);
                    m_docManager.Undo();
                    measure(lines, xStart);
                    check(lines == 2 && std::abs(xStart - 8000.0) < 1e-9, "倒角：一次撤销去掉倒角线并还原两条线");
                    m_docManager.Undo();         // 撤销画线
                    m_docManager.Undo();
                    editor.SetSelection({});
                }

                // 分解：画一个矩形，选中后分解 → 4 条直线并被选中；撤销还原矩形
                {
                    RunCommandLine("Rectangle");
                    editor.SubmitPoint(Math::Point3(9000.0, 0.0, 0.0));
                    editor.SubmitPoint(Math::Point3(9010.0, 5.0, 0.0));
                    if (editor.IsActiveTool())
                        SendKeyToEditor(MiniGUI::Key::Escape);
                    auto countFar = [&](size_t& rects, size_t& lines, Object::ObjectID& rectId)
                    {
                        rects = 0; lines = 0; rectId = Object::InvalidID;
                        scene.ForEachObject([&](const Object& o)
                        {
                            if (o.IsKindOf<RectangleEntity>() && static_cast<const RectangleEntity&>(o).GetRectangle().P1.x > 8900.0) { ++rects; rectId = o.GetID(); }
                            if (o.IsKindOf<LineEntity>() && static_cast<const LineEntity&>(o).GetLine().Start.x > 8900.0) ++lines;
                        });
                    };
                    size_t rects = 0, lines = 0; Object::ObjectID rid = Object::InvalidID;
                    countFar(rects, lines, rid);
                    check(rects == 1 && lines == 0, "分解：画出一个矩形");

                    RunCommandLine("Explode");
                    check(rects == 1 && editor.GetSelection().empty(), "分解：没有选择集时不处理");
                    editor.SetSelection({ rid });
                    check(m_commands.IsEnabled("modify.explode"), "分解：有选择集时命令可用");
                    RunCommandLine("Explode");
                    countFar(rects, lines, rid);
                    check(rects == 0 && lines == 4, "分解：矩形变成 4 条直线");
                    check(editor.GetSelection().size() == 4, "分解：分解出的 4 条直线被选中");

                    // 直线不能分解：保持不变，不入撤销栈
                    const bool canUndoBefore = m_docManager.GetActive()->CanUndo();
                    RunCommandLine("Explode");
                    countFar(rects, lines, rid);
                    check(lines == 4 && m_docManager.GetActive()->CanUndo() == canUndoBefore, "分解：直线不能再分解，对象不变");

                    m_docManager.Undo();
                    countFar(rects, lines, rid);
                    check(rects == 1 && lines == 0, "分解：一次撤销还原矩形");
                    m_docManager.Undo();         // 撤销画矩形
                    editor.SetSelection({});
                }

                // 锁定语义：锁定图层上的对象可以点选、可以捕捉，但不能删除 / 改属性 / 改特性 / 移动 / 用夹点修改；解锁后恢复
                {
                    const LayerID lockLayer = scene.GetLayerManager().AddLayer("锁定语义自测");
                    const auto lockedId = scene.NextObjectID();
                    auto line = std::make_unique<LineEntity>(lockedId, Math::Point3{ 10000, 0, 0 }, Math::Point3{ 10010, 0, 0 });
                    line->GetAttr().LayerId = lockLayer;
                    scene.AddEntity(std::move(line));
                    scene.GetLayerManager().GetLayer(lockLayer)->SetLocked(true);

                    Camera& cam = m_docManager.GetViewport().GetCamera();
                    const CameraState savedState = cam.GetState();
                    CameraState st = savedState;
                    st.Target = Math::Point3(10005.0, 0.0, 0.0);
                    cam.SetState(st);
                    m_viewport->RequestRender();
                    RenderFrame();

                    editor.SetSelection({});
                    {
                        const MiniGUI::Rect bounds = m_viewport->GetScreenBounds();
                        const Math::Point2 p = camera.WorldToScreen(Math::Point3(10005.0, 0.0, 0.0));
                        const int x = static_cast<int>(std::lround(bounds.min.x * m_dpiScale)) + static_cast<int>(std::lround(p.x));
                        const int y = static_cast<int>(std::lround(bounds.min.y * m_dpiScale)) + static_cast<int>(std::lround(p.y));
                        send(WM_MOUSEMOVE, 0, x, y);
                        send(WM_LBUTTONDOWN, MK_LBUTTON, x, y);
                        send(WM_LBUTTONUP, 0, x, y);
                        RenderFrame();
                    }
                    check(editor.GetSelection().count(lockedId) == 1, "锁定：锁定图层上的对象可以点选");

                    editor.DeleteSelected();
                    check(scene.GetEntity(lockedId) != nullptr, "锁定：不能删除");
                    AttrChange ch;
                    ch.Color = EntityColor::FromAci(1);
                    check(!editor.ChangeSelectionAttr(ch), "锁定：不能改常规属性");
                    check(!editor.SetSelectionProperty("长度", PropValue(20.0)), "锁定：不能改几何特性");
                    check(editor.GetEditableSelectedObjects(false).empty() && editor.GetSelectedObjects().size() == 1, "锁定：选中了但没有可修改的对象");
                    RunCommandLine("Move");
                    check(!editor.IsActiveTool(), "锁定：只选了锁定对象时，移动工具不启动");
                    RunCommandLine("Explode");
                    check(scene.GetEntity(lockedId) != nullptr, "锁定：不能分解");

                    scene.GetLayerManager().GetLayer(lockLayer)->SetLocked(false);
                    check(editor.SetSelectionProperty("长度", PropValue(20.0)), "解锁后：可以改几何特性");
                    m_docManager.Undo();

                    scene.RemoveEntity(lockedId);
                    editor.SetSelection({});
                    cam.SetState(savedState);
                    m_viewport->RequestRender();
                    RenderFrame();
                }

                // 多段线打断：两个拐角之间的一段被去掉，剩两条多段线；撤销还原
                {
                    const auto plId = scene.NextObjectID();
                    scene.AddEntity(std::make_unique<PolylineEntity>(plId,
                        std::vector<Math::Point3>{ { 11000, 0, 0 }, { 11010, 0, 0 }, { 11010, 10, 0 } }));
                    auto stats = [&](size_t& count, double& total)
                    {
                        count = 0; total = 0.0;
                        scene.ForEachObject([&](const Object& o)
                        {
                            if (!o.IsKindOf<PolylineEntity>()) return;
                            const Polyline& pl = static_cast<const PolylineEntity&>(o).GetPolyline();
                            if (pl.Points.empty() || pl.Points.front().x < 10900.0) return;
                            ++count; total += pl.Length();
                        });
                    };
                    size_t count = 0; double total = 0.0;
                    stats(count, total);
                    check(count == 1 && std::abs(total - 20.0) < 1e-9, "多段线打断：先有一条长 20 的多段线");

                    RunCommandLine("Break");
                    editor.SubmitPoint(Math::Point3(11004.0, 0.0, 0.0));           // 第一断点：第一段上
                    editor.SubmitPoint(Math::Point3(11010.0, 4.0, 0.0));           // 第二断点：第二段上
                    stats(count, total);
                    check(count == 2 && std::abs(total - 10.0) < 1e-9, "多段线打断：去掉中间 10，剩两条共长 10（4 + 6）");
                    if (editor.IsActiveTool())
                        SendKeyToEditor(MiniGUI::Key::Escape);
                    m_docManager.Undo();
                    stats(count, total);
                    check(count == 1 && std::abs(total - 20.0) < 1e-9, "多段线打断：一次撤销还原整条多段线");
                    scene.RemoveEntity(plId);
                    editor.SetSelection({});
                }

                // 多段线的修剪 / 延伸 / 偏移：用合成点击跑完整个工具流程，撤销还原
                {
                    auto addPoly = [&](std::vector<Math::Point3> pts)
                    {
                        const auto id = scene.NextObjectID();
                        scene.AddEntity(std::make_unique<PolylineEntity>(id, std::move(pts)));
                        return id;
                    };
                    auto addLine = [&](double x0, double y0, double x1, double y1)
                    {
                        const auto id = scene.NextObjectID();
                        scene.AddEntity(std::make_unique<LineEntity>(id, Math::Point3{ x0, y0, 0 }, Math::Point3{ x1, y1, 0 }));
                        return id;
                    };
                    auto polyStats = [&](double xMin, double xMax, size_t& count, double& total)
                    {
                        count = 0; total = 0.0;
                        scene.ForEachObject([&](const Object& o)
                        {
                            if (!o.IsKindOf<PolylineEntity>()) return;
                            const Polyline& pl = static_cast<const PolylineEntity&>(o).GetPolyline();
                            if (pl.Points.empty() || pl.Points.front().x < xMin || pl.Points.front().x > xMax) return;
                            ++count; total += pl.Length();
                        });
                    };
                    size_t count = 0; double total = 0.0;

                    // 修剪：竖线在 u = 0.5 处切断多段线，点击左半段把它剪掉
                    const auto trimPoly = addPoly({ { 13000, 0, 0 }, { 13010, 0, 0 }, { 13010, 10, 0 } });
                    const auto trimEdge = addLine(13005, -5, 13005, 5);
                    RunCommandLine("Trim");
                    editor.SubmitPoint(Math::Point3(13002.0, 0.0, 0.0));
                    polyStats(12900, 13100, count, total);
                    check(count == 1 && std::abs(total - 15.0) < 1e-6, "多段线修剪：剪掉切口左边的一段，剩长 15 的多段线");
                    if (editor.IsActiveTool())
                        SendKeyToEditor(MiniGUI::Key::Escape);
                    m_docManager.Undo();
                    polyStats(12900, 13100, count, total);
                    check(count == 1 && std::abs(total - 20.0) < 1e-6, "多段线修剪：一次撤销还原");
                    scene.RemoveEntity(trimPoly);
                    scene.RemoveEntity(trimEdge);

                    // 延伸：边界在多段线终点右边 5 处，点击终点附近
                    const auto extPoly = addPoly({ { 14000, 0, 0 }, { 14010, 0, 0 } });
                    const auto extEdge = addLine(14015, -5, 14015, 5);
                    RunCommandLine("Extend");
                    editor.SubmitPoint(Math::Point3(14009.0, 0.0, 0.0));
                    polyStats(13900, 14100, count, total);
                    check(count == 1 && std::abs(total - 15.0) < 1e-6, "多段线延伸：终点延伸到边界，长度 10 → 15");
                    if (editor.IsActiveTool())
                        SendKeyToEditor(MiniGUI::Key::Escape);
                    m_docManager.Undo();
                    polyStats(13900, 14100, count, total);
                    check(count == 1 && std::abs(total - 10.0) < 1e-6, "多段线延伸：一次撤销还原");
                    scene.RemoveEntity(extPoly);
                    scene.RemoveEntity(extEdge);

                    // 偏移：L 形多段线，通过点在内侧 2 处 → 新的 L 形，拐角在 (15008, 2)
                    const auto offPoly = addPoly({ { 15000, 0, 0 }, { 15010, 0, 0 }, { 15010, 10, 0 } });
                    RunCommandLine("Offset");
                    editor.SubmitPoint(Math::Point3(15005.0, 0.0, 0.0));        // 选源对象
                    editor.SubmitPoint(Math::Point3(15005.0, 2.0, 0.0));        // 通过点
                    polyStats(14900, 15100, count, total);
                    bool corner = false;
                    scene.ForEachObject([&](const Object& o)
                    {
                        if (!o.IsKindOf<PolylineEntity>() || o.GetID() == offPoly) return;
                        const Polyline& pl = static_cast<const PolylineEntity&>(o).GetPolyline();
                        if (pl.Points.size() == 3 && std::abs(pl.Points[1].x - 15008.0) < 1e-6 && std::abs(pl.Points[1].y - 2.0) < 1e-6) corner = true;
                    });
                    check(count == 2 && corner, "多段线偏移：多一条偏移后的多段线，拐角在 (15008, 2)");
                    if (editor.IsActiveTool())
                        SendKeyToEditor(MiniGUI::Key::Escape);
                    m_docManager.Undo();
                    polyStats(14900, 15100, count, total);
                    check(count == 1, "多段线偏移：一次撤销去掉偏移出的多段线");
                    scene.RemoveEntity(offPoly);
                    editor.SetSelection({});
                }
                m_docManager.Undo();         // 撤销画线
                editor.SetSelection({});
                StateChanged();
            }
        }

        // ── 17. F10 = 极轴追踪（同 AutoCAD）：输入框有焦点时也生效，不激活菜单栏 ──
        section("17. F10 = 极轴追踪（同 AutoCAD）：输入框有焦点时也生效，不激活菜单栏");
        {
            const bool before = editor.IsPolarEnabled();
            m_main.console->FocusInput();
            m_ui->KeyDown(MiniGUI::Key::F10, 0);
            m_ui->KeyUp(MiniGUI::Key::F10, 0);
            const bool menuMode = m_layout->GetMenuBar() && m_layout->GetMenuBar()->IsKeyboardMode();
            check(editor.IsPolarEnabled() != before && !menuMode && m_commands.IsChecked("aux.polar") == editor.IsPolarEnabled(),
                  "F10 切换极轴，不激活菜单栏");
            m_ui->KeyDown(MiniGUI::Key::F10, 0);
            m_ui->KeyUp(MiniGUI::Key::F10, 0);
            m_viewport->Focus();
        }

        // ── 18. 关于 ─────────────────────────────────────────────
        section("18. 关于");
        m_commands.Execute("help.about");
        check(m_ui->HasModal(), "F1 打开关于对话框");
        m_ui->KeyDown(MiniGUI::Key::Escape, 0);
        check(!m_ui->HasModal(), "Esc 关闭关于对话框");

        // ── 19. 帧耗时（只打印，不判定）：2 万条线的图纸上平移；只有界面变化（悬停工具栏）──
        section("19. 帧耗时（只打印，不判定）：2 万条线的图纸上平移；只有界面变化（悬停工具栏）");
        {
            m_commands.Execute("file.new");
            Document* big = m_docManager.GetActive();
            Scene& scene = big->GetScene();
            for (int i = 0; i < 20000; ++i)
            {
                const double a = i * 0.0031;
                const double r = 5.0 + (i % 200) * 0.1;
                scene.AddEntity(std::make_unique<LineEntity>(scene.NextObjectID(),
                    Math::Point3(r * std::cos(a), r * std::sin(a), 0), Math::Point3((r + 1.0) * std::cos(a + 0.5), (r + 1.0) * std::sin(a + 0.5), 0)));
            }
            big->MarkSaved();
            m_viewport->RequestRender();
            RenderFrame();          // 第一帧生成场景顶点，不计入

            struct Sum { double layout = 0, viewport = 0, ui = 0, present = 0; int frames = 0, vpFrames = 0; };
            auto measure = [&](bool syncGpu, auto step)
            {
                m_syncGpu = syncGpu;
                Sum sum;
                for (int i = 0; i < 60; ++i)
                {
                    step(i);
                    RenderFrame();
                    sum.layout += m_timing.layout; sum.viewport += m_timing.viewport;
                    sum.ui += m_timing.ui; sum.present += m_timing.present;
                    sum.frames++; sum.vpFrames += m_timing.viewportRendered ? 1 : 0;
                }
                m_syncGpu = false;
                return sum;
            };
            auto print = [](const char* what, const Sum& s)
            {
                const double n = s.frames;
                std::printf("    %s：布局 %.3f ms，视口 %.3f ms（%d/%d 帧渲染了视口），界面 %.3f ms，Present %.3f ms\n",
                            what, s.layout / n, s.viewport / n, s.vpFrames, s.frames, s.ui / n, s.present / n);
            };

            const int px = cx, py = cy;
            send(WM_MOUSEMOVE, 0, px, py);
            send(WM_MBUTTONDOWN, MK_MBUTTON, px, py);
            auto pan = [&](int i) { send(WM_MOUSEMOVE, MK_MBUTTON, px + (i % 2 ? 3 : -3), py + i % 7); };
            print("平移（CPU 计时，GPU 并行）", measure(false, pan));
            print("平移（每阶段等 GPU 做完）", measure(true, pan));
            send(WM_MBUTTONUP, 0, px, py);

            // 只移动光标（不平移）：场景缓存层不重画，只复制缓存层 + 画光标 / 悬停等动态内容
            const Viewport& view = m_docManager.GetViewport();
            const uint64_t layerBefore = view.GetLayerRedraws();
            const uint64_t rtBefore = m_viewTexRebuilds;
            auto move = [&](int i) { send(WM_MOUSEMOVE, 0, px + (i % 2 ? 9 : -9), py + i % 11); };
            print("移动光标（每阶段等 GPU 做完）", measure(true, move));
            check(view.IsLayerCached() && view.GetLayerRedraws() == layerBefore, "移动光标不重画场景缓存层");
            check(m_viewTexRebuilds == rtBefore, "移动光标时视口渲染目标不重建（尺寸没变）");
            const uint64_t layerPan = view.GetLayerRedraws();
            send(WM_MBUTTONDOWN, MK_MBUTTON, px, py);
            send(WM_MOUSEMOVE, MK_MBUTTON, px + 20, py);
            RenderFrame();
            send(WM_MBUTTONUP, 0, px + 20, py);
            check(view.GetLayerRedraws() == layerPan + 1, "平移时重画一次场景缓存层");

            // 只有界面变化：指针在工具栏的两个按钮之间来回移动（悬停高亮），视口不重新渲染
            MiniGUI::Rect tb{};
            if (MiniGUI::Node* bar = m_layout->GetToolBar("draw"))
                tb = bar->GetScreenBounds();
            auto hover = [&](int i)
            {
                const float x = tb.min.x + 14.0f + (i % 2 ? 30.0f : 0.0f);
                send(WM_MOUSEMOVE, 0, static_cast<int>(x * m_dpiScale), static_cast<int>(tb.Center().y * m_dpiScale));
            };
            print("悬停工具栏（每阶段等 GPU 做完）", measure(true, hover));

            m_commands.Execute("file.close");
            RenderFrame();
        }

        // ── 20. 大图纸的非逐帧开销（只打印，不判定）：添加实体、首次生成显示数据、整体重建、保存、打开 ──
        section("20. 大图纸的非逐帧开销（只打印，不判定）：添加实体、首次生成显示数据、整体重建、保存、打开");
        // 20 万条线（debug 版约 24 秒）只在 --selftest --full 时测
        for (const int count : { 20000, 200000 })
        {
            if (count > 20000 && !full)
            {
                std::printf("    20 万条线的测量已跳过（加 --full 运行）\n");
                continue;
            }
            auto msSince = [this](int64_t from) { return static_cast<double>(NowTicks() - from) * 1000.0 / static_cast<double>(kTicksPerSecond); };
            m_commands.Execute("file.new");
            Document* big = m_docManager.GetActive();
            Scene& scene = big->GetScene();

            int64_t t = NowTicks();
            for (int i = 0; i < count; ++i)
            {
                const double a = i * 0.0031;
                const double r = 5.0 + (i % 200) * 0.1;
                scene.AddEntity(std::make_unique<LineEntity>(scene.NextObjectID(),
                    Math::Point3(r * std::cos(a), r * std::sin(a), 0), Math::Point3((r + 1.0) * std::cos(a + 0.5), (r + 1.0) * std::sin(a + 0.5), 0)));
            }
            const double addMs = msSince(t);

            m_syncGpu = true;
            m_viewport->RequestRender();
            RenderFrame();                                      // 首次生成场景顶点
            const double firstMs = m_timing.viewport;
            scene.MarkDisplayDirty();                           // 改了图层颜色：显示重建，拾取索引不动
            m_viewport->RequestRender();
            RenderFrame();
            const double displayMs = m_timing.viewport;
            scene.MarkDirty();                                  // 全局几何变化（例如撤销）：显示与拾取索引都重建
            m_viewport->RequestRender();
            RenderFrame();
            const double rebuildMs = m_timing.viewport;
            m_viewport->RequestRender();                        // 场景和相机都没变（例如移动光标）：只复制缓存层
            RenderFrame();
            const double panMs = m_timing.viewport;
            m_syncGpu = false;

            wchar_t tmp[MAX_PATH] = L"";
            GetTempPathW(MAX_PATH, tmp);
            const std::filesystem::path file = std::filesystem::path(tmp) / L"minicad_selftest_big.mcad";
            const std::u8string u8 = file.u8string();
            const std::string path(u8.begin(), u8.end());
            t = NowTicks();
            const bool saved = big->SaveAs(path);
            const double saveMs = msSince(t);
            std::error_code ec;
            const auto bytes = std::filesystem::file_size(file, ec);
            big->MarkSaved();
            m_commands.Execute("file.close");

            t = NowTicks();
            Document* loaded = saved ? m_docManager.Open(path) : nullptr;
            const double openMs = msSince(t);
            m_viewport->RequestRender();
            m_syncGpu = true;
            RenderFrame();
            m_syncGpu = false;
            const double openFirstMs = m_timing.viewport;

            std::printf("    %d 条线：添加 %.0f ms，首次显示 %.0f ms，改图层颜色 %.0f ms，几何整体重建 %.0f ms，移动光标一帧 %.1f ms，"
                        "保存 %.0f ms（%.1f MB），打开 %.0f ms + 首次显示 %.0f ms\n",
                        count, addMs, firstMs, displayMs, rebuildMs, panMs, saveMs, static_cast<double>(bytes) / 1048576.0, openMs, openFirstMs);
            if (loaded)
            {
                loaded->MarkSaved();
                CloseDocument(loaded);
            }
            std::filesystem::remove(file, ec);
            RenderFrame();
        }

        // ── 21. 带线宽、远离原点的大图纸：平移不重新细分；细线显示时缩放也不重新细分 ──
        section("21. 带线宽、远离原点的大图纸：平移不重新细分；细线显示时缩放也不重新细分");
        {
            m_commands.Execute("file.new");
            Document* big = m_docManager.GetActive();
            Editor&   bigEditor = m_docManager.GetEditor();   // 每个文档有自己的编辑器：新文档的要重新取
            Scene& scene = big->GetScene();
            const double ox = 512345.678, oy = 3456789.123;     // 测绘坐标量级，屏幕坐标的舍入误差明显
            for (int i = 0; i < 20000; ++i)
            {
                const double a = i * 0.0031;
                const double r = 50.0 + (i % 200) * 1.0;
                auto line = std::make_unique<LineEntity>(scene.NextObjectID(),
                    Math::Point3(ox + r * std::cos(a), oy + r * std::sin(a), 0), Math::Point3(ox + (r + 10.0) * std::cos(a + 0.5), oy + (r + 10.0) * std::sin(a + 0.5), 0));
                EntityAttr attr = line->GetAttr();
                attr.Lineweight = Lineweight::W050;
                line->SetAttr(attr);
                scene.AddEntity(std::move(line));
            }
            big->MarkSaved();
            m_docManager.ZoomAll();
            m_viewport->RequestRender();
            RenderFrame();

            // v0.2.0 起默认细线显示（忽略线宽）：这一项测线宽的显示，先关掉，结束时恢复
            const bool thinWas = bigEditor.IsThinLines();
            bigEditor.SetThinLines(false);
            m_viewport->RequestRender();
            RenderFrame();

            const int px = cx, py = cy;
            uint64_t version = bigEditor.BuildViewState().SceneVersion;
            send(WM_MOUSEMOVE, 0, px, py);
            send(WM_MBUTTONDOWN, MK_MBUTTON, px, py);
            int64_t t = NowTicks();
            for (int i = 0; i < 30; ++i)
            {
                send(WM_MOUSEMOVE, MK_MBUTTON, px + (i % 2 ? 7 : -7), py + i % 5);
                RenderFrame();
            }
            const double panMs = static_cast<double>(NowTicks() - t) * 1000.0 / static_cast<double>(kTicksPerSecond) / 30.0;
            send(WM_MBUTTONUP, 0, px, py);
            check(bigEditor.BuildViewState().SceneVersion == version, "线宽图纸（远离原点）平移时不重新细分场景");

            POINT sp = { cx, cy };
            ClientToScreen(hwnd, &sp);
            auto wheel = [&](int n)
            {
                const int64_t t0 = NowTicks();
                for (int i = 0; i < n; ++i)
                {
                    SendMessageW(hwnd, WM_MOUSEWHEEL, MAKEWPARAM(0, (i % 2 ? -WHEEL_DELTA : WHEEL_DELTA)), MAKELPARAM(sp.x, sp.y));
                    RenderFrame();
                }
                return static_cast<double>(NowTicks() - t0) * 1000.0 / static_cast<double>(kTicksPerSecond) / n;
            };
            version = bigEditor.BuildViewState().SceneVersion;
            uint64_t dynVersion = bigEditor.BuildViewState().SceneDynVersion;
            const double zoomMs = wheel(10);
            check(bigEditor.BuildViewState().SceneDynVersion != dynVersion && bigEditor.BuildViewState().SceneVersion == version,
                  "线宽图纸缩放时只重新细分动态流（线宽按像素等宽），静态流不动");

            m_commands.Execute("view.thinLines");
            RenderFrame();
            check(bigEditor.IsThinLines() && m_commands.IsChecked("view.thinLines"), "细线显示开关");
            version = bigEditor.BuildViewState().SceneVersion;
            dynVersion = bigEditor.BuildViewState().SceneDynVersion;
            const double thinZoomMs = wheel(10);
            check(bigEditor.BuildViewState().SceneVersion == version && bigEditor.BuildViewState().SceneDynVersion == dynVersion, "细线显示时缩放不重新细分");
            m_commands.Execute("view.thinLines");
            RenderFrame();
            check(!bigEditor.IsThinLines(), "关闭细线显示");
            bigEditor.SetThinLines(thinWas);

            std::printf("    2 万条 0.5mm 线宽的线：平移一帧 %.1f ms，滚轮缩放一帧 %.1f ms，细线显示时缩放一帧 %.1f ms\n",
                        panMs, zoomMs, thinZoomMs);
            m_commands.Execute("file.close");
            RenderFrame();
        }

        // ── 22. 关闭大图纸后归还内存（提交内存 = 任务管理器的"提交大小"）──
        section("22. 关闭大图纸后归还内存（提交内存 = 任务管理器的\"提交大小\"）");
        {
            auto privateMb = []
            {
                PROCESS_MEMORY_COUNTERS_EX pmc = {};
                pmc.cb = sizeof(pmc);
                K32GetProcessMemoryInfo(GetCurrentProcess(), reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&pmc), sizeof(pmc));
                return static_cast<double>(pmc.PrivateUsage) / 1048576.0;
            };
            const double before = privateMb();
            m_commands.Execute("file.new");
            Document* big = m_docManager.GetActive();
            Scene& scene = big->GetScene();
            for (int i = 0; i < 200000; ++i)
            {
                const double a = i * 0.0031;
                const double r = 5.0 + (i % 200) * 0.1;
                scene.AddEntity(std::make_unique<LineEntity>(scene.NextObjectID(),
                    Math::Point3(r * std::cos(a), r * std::sin(a), 0), Math::Point3((r + 1.0) * std::cos(a + 0.5), (r + 1.0) * std::sin(a + 0.5), 0)));
            }
            big->MarkSaved();
            m_viewport->RequestRender();
            RenderFrame();
            send(WM_MOUSEMOVE, 0, cx, cy);      // 悬停 / 拾取：建立空间索引
            RenderFrame();
            const double opened = privateMb();
            m_commands.Execute("file.close");
            RenderFrame();
            const double closed = privateMb();
            std::printf("    20 万条线：打开前 %.0f MB，打开后 %.0f MB，关闭后 %.0f MB\n", before, opened, closed);
            check(closed - before < (opened - before) * 0.25, "关闭大图纸后归还大部分内存");
        }

        // ── 23. 公共打开入口及真实 WM_DROPFILES 消息走同一 DWG 管线。
        section("23. 公共打开入口及真实 WM_DROPFILES 消息走同一 DWG 管线");
        {
            const auto dir = std::filesystem::temp_directory_path() /
                ("minicad-selftest-open-" + std::to_string(NowTicks()));
            std::filesystem::create_directories(dir);
            const auto file = dir / L"拖放 测试.dwg";
            const auto u8 = file.u8string();
            const std::string path(u8.begin(), u8.end());
            Scene drawing;
            drawing.AddEntity(std::make_unique<LineEntity>(drawing.NextObjectID(),
                Math::Point3{500000, 3000000, 0}, Math::Point3{500100, 3000060, 0}));
            const auto data = ExportCad(drawing, CadFileKind::Dwg);
            { std::ofstream out(file, std::ios::binary); out.write(reinterpret_cast<const char*>(data.data()), data.size()); }
            const auto before = m_docManager.GetAll().size();
            ShowStartPage(true);            // 从起始页打开：读取期间就进入绘图区（没有文档时起始页本来就在显示）
            OpenDrawings({path, path});
            check(IsOpeningDrawings() && m_openBar && m_openBar->IsVisible(), "打开图纸时状态栏显示读取进度");
            check(!m_startPage->IsVisible() && m_viewport->IsVisible(), "开始读取图纸时离开起始页、显示绘图区");
            waitOpen();
            check(!m_openBar->IsVisible(), "读完后隐藏读取进度");
            Document* opened = m_docManager.GetActive();
            check(opened && opened->GetPath() == path && m_docManager.GetAll().size() == before + 1 &&
                  m_docTabs->GetTabCount() == static_cast<int>(before + 1), "公共打开入口加载 DWG、去重并同步标签");
            AABB openedBounds = AABB::Empty();
            const auto openedCamera = m_docManager.GetViewport().GetCamera().GetState();
            (void)openedCamera;
            check(opened && opened->GetScene().GetExtents(openedBounds) &&
                  atFitCenter(m_docManager.GetViewport().GetCamera(), Math::Point3(500050, 3000030, 0)),
                  "DWG 真彩图层可见，远坐标内容自动居中");
            const auto wide = file.wstring();
            const size_t bytes = sizeof(DROPFILES) + (wide.size() + 2) * sizeof(wchar_t);
            HGLOBAL memory = GlobalAlloc(GHND, bytes);
            auto* drop = static_cast<DROPFILES*>(GlobalLock(memory));
            if (drop)
            {
                drop->pFiles = sizeof(DROPFILES);
                drop->fWide = TRUE;
                std::memcpy(reinterpret_cast<char*>(drop) + sizeof(DROPFILES), wide.c_str(), (wide.size() + 1) * sizeof(wchar_t));
                GlobalUnlock(memory);
                SendMessageW(hwnd, WM_DROPFILES, reinterpret_cast<WPARAM>(memory), 0); // 宿主 DragFinish 释放内存
                RenderFrame();
                check(m_docManager.GetActive() == opened && m_docManager.GetAll().size() == before + 1,
                      "Unicode WM_DROPFILES 打开同一 DWG 时激活已有文档");
            }
            else
            {
                if (memory) GlobalFree(memory);
                check(false, "分配拖放测试数据");
            }
            const auto missing = (dir / "missing.dwg").u8string();
            OpenDrawings({ std::string(missing.begin(), missing.end()), path });
            waitOpen();
            check(m_ui->HasModal() && m_docManager.GetActive() == opened && m_docManager.GetAll().size() == before + 1,
                  "批量打开失败给出可见提示，后续有效图纸仍可打开");
            send(WM_KEYDOWN, VK_ESCAPE, 0, 0);
            send(WM_KEYUP, VK_ESCAPE, 0, 0);
            RenderFrame();
            check(!m_ui->HasModal(), "打开失败提示可以关闭");
            if (opened && opened->GetPath() == path) CloseDocument(opened);
            RenderFrame();
            std::error_code ec;
            std::filesystem::remove(file, ec);
            std::filesystem::remove(dir, ec);
        }

        // ── 24. 模型 / 布局标签条：新建与打开的图纸都显示，点标签切换布局 ──────────
        section("24. 模型 / 布局标签条：新建与打开的图纸都显示，点标签切换布局");
        {
            auto barShown = [&]
            {
                // 在浮动状态栏的左侧
                const MiniGUI::Rect b    = m_main.layoutTabsBar->GetScreenBounds();
                const MiniGUI::Rect card = m_main.statusBar->GetParent()->GetScreenBounds();
                return m_main.layoutTabsBar->IsVisible() && b.max.y - b.min.y > 20.0f &&
                       b.min.y >= card.min.y - 0.5f && b.max.y <= card.max.y + 0.5f && b.min.x < card.min.x + 10.0f;
            };

            m_commands.Execute("file.new");
            RenderFrame();
            Document* fresh = m_docManager.GetActive();
            check(barShown(), "新建图纸：状态栏左侧显示模型 / 布局标签条");
            check(m_main.layoutTabs->GetTabCount() == 3 && m_main.layoutTabs->GetTabTitle(0) == "模型" &&
                  m_main.layoutTabs->GetTabTitle(1) == "布局1" && m_main.layoutTabs->GetTabTitle(2) == "布局2" &&
                  m_main.layoutTabs->GetSelected() == 0, "新建图纸：标签为 模型 / 布局1 / 布局2，选中模型");

            // 模型页先切到西南等轴测，再进布局：布局是二维图纸，回到俯视、不显示视图方块
            const bool cubeOn = m_viewCubeVisible;
            SetViewCubeVisible(true);
            Camera& cam = m_docManager.GetViewport().GetCamera();
            m_docManager.GetEditor().SetViewDirection(-135.0, 35.26);
            for (int i = 0; i < 30; ++i) RenderFrame();      // 走完视图过渡动画
            const double modelElevation = cam.GetElevation();
            check(m_main.cube->IsVisible() && modelElevation < 89.0, "模型页：三维视图里显示视图方块");

            m_main.layoutTabs->SetSelected(1);       // 相当于点击「布局1」
            RenderFrame();
            check(fresh && fresh->ActiveLayout() == 1 && fresh->ActiveLayoutIsPaper(), "点击布局1 标签切到图纸空间");
            check(!m_main.cube->IsVisible(), "布局页不显示视图方块");
            check(cam.IsPlanView(), "从三维视图进布局时回到俯视");
            m_commands.Execute("view.swiso");
            for (int i = 0; i < 30; ++i) RenderFrame();
            check(cam.IsPlanView(), "布局页上标准视图命令不切三维视图");

            m_main.layoutTabs->SetSelected(0);
            RenderFrame();
            check(fresh && fresh->ActiveLayout() == 0, "点回模型标签回到模型空间");
            check(m_main.cube->IsVisible() && std::abs(cam.GetElevation() - modelElevation) < 1e-6,
                  "回到模型页：视图方块重新显示，恢复原来的三维视图");
            SetViewCubeVisible(cubeOn);
            m_commands.Execute("view.top");
            for (int i = 0; i < 30; ++i) RenderFrame();

            // 没有视口的布局不画模型内容（模型里有直线时切到布局1）
            Editor& ed = m_docManager.GetEditor();
            fresh->GetCommandStack().Execute(std::make_unique<AddEntityCommand>(std::make_unique<LineEntity>(
                fresh->GetScene().NextObjectID(), Math::Point3{ 0, 0, 0 }, Math::Point3{ 300, 200, 0 })), fresh->GetScene());
            // 静态场景流上传 GPU 后即释放（ViewState 里为空），按拼接时的统计数判断
            auto drawnCount = [&]
            {
                const auto st = ed.GetSceneStreamStats();
                const ViewState vs = ed.BuildViewState();
                return st.lines + st.fills + st.texts + st.glyphs + vs.SceneDyn.size() + vs.SceneDynFill.size() +
                       vs.Shaded.size() + vs.Silhouettes.size();
            };
            m_viewport->RequestRender();       // 直接执行命令加的实体：请求重画（界面命令会自动请求）
            RenderFrame();
            check(drawnCount() > 0, "模型页显示模型里的直线（对照）");
            m_main.layoutTabs->SetSelected(1);
            RenderFrame();
            check(drawnCount() == 0, "没有视口的布局不显示模型内容");

            // 布局上画直线：落在该布局上，只在该布局显示
            {
                const MiniGUI::Rect vr = m_viewport->GetScreenBounds();
                auto clickAt = [&](float fx, float fy)
                {
                    const MiniGUI::Vec2 p{ vr.min.x + (vr.max.x - vr.min.x) * fx, vr.min.y + (vr.max.y - vr.min.y) * fy };
                    m_ui->PointerMove(p, 0);
                    m_ui->PointerDown(p, MiniGUI::MouseButton::Left, 0);
                    m_ui->PointerUp(p, MiniGUI::MouseButton::Left, 0);
                    RenderFrame();
                };
                const int before = fresh->GetScene().EntityCount();
                m_commands.Execute("draw.line");
                clickAt(0.4f, 0.5f);
                clickAt(0.6f, 0.5f);
                m_ui->KeyDown(MiniGUI::Key::Escape, 0);
                m_ui->KeyUp(MiniGUI::Key::Escape, 0);
                RenderFrame();
                bool onSheet = false;
                const uint64_t sheetKey = fresh->ActiveLayoutOwnerKey();
                fresh->GetScene().ForEachObject([&](const Object& o)
                {
                    if (o.IsKindOf<LineEntity>() && static_cast<const Entity&>(o).GetAttr().InPaperSpace &&
                        static_cast<const Entity&>(o).GetAttr().LayoutKey == sheetKey)
                        onSheet = true;
                });
                check(fresh->GetScene().EntityCount() == before + 1 && onSheet && drawnCount() > 0,
                      "布局上画的直线落在该布局的图纸空间并显示出来");
            }

            // 新建 / 删除布局，可撤销
            m_commands.Execute("layout.new");
            RenderFrame();
            check(m_main.layoutTabs->GetTabCount() == 4 && m_main.layoutTabs->GetTabTitle(3) == "布局3", "新建布局：标签条多出 布局3");
            m_main.layoutTabs->SetSelected(3);
            RenderFrame();
            check(fresh->ActiveLayout() == 3, "切到新建的布局3");
            m_commands.Execute("layout.delete");
            RenderFrame();
            check(m_main.layoutTabs->GetTabCount() == 3 && m_main.layoutTabs->GetSelected() == 0 && !fresh->ActiveLayoutIsPaper() &&
                  m_main.cube->IsVisible() == m_viewCubeVisible,
                  "删除当前布局：标签去掉，回到模型页");
            m_commands.Execute("edit.undo");
            RenderFrame();
            check(m_main.layoutTabs->GetTabCount() == 4 && m_main.layoutTabs->GetTabTitle(3) == "布局3", "撤销删除：布局3 回来");
            m_main.layoutTabs->SetSelected(3);
            RenderFrame();
            m_commands.Execute("edit.undo");          // 撤销新建：当前布局没了，回到模型页
            RenderFrame();
            check(m_main.layoutTabs->GetTabCount() == 3 && !fresh->ActiveLayoutIsPaper(), "撤销新建：布局3 去掉，当前在它上面时回到模型页");
            m_main.layoutTabs->SetSelected(0);
            RenderFrame();

            // 带布局的图纸：模型里一条线，图纸A 上一个视口看着它
            const auto dir = std::filesystem::temp_directory_path() /
                ("minicad-selftest-layout-" + std::to_string(NowTicks()));
            std::filesystem::create_directories(dir);
            const auto file = dir / L"布局 测试.dxf";
            const auto u8 = file.u8string();
            const std::string path(u8.begin(), u8.end());
            {
                Scene drawing;
                std::vector<DrawingLayout> layouts(1);
                DrawingLayout a;
                a.Name = "图纸A"; a.Model = false; a.Order = 1;
                layouts.push_back(a);
                drawing.SetLayouts(std::move(layouts));
                drawing.AddEntity(std::make_unique<LineEntity>(drawing.NextObjectID(),
                    Math::Point3{ 0, 0, 0 }, Math::Point3{ 100, 50, 0 }));
                auto vp = std::make_unique<ViewportEntity>(drawing.NextObjectID(), Math::Point3{ 210, 148, 0 }, 300.0, 200.0);
                vp->SetViewCenter(Math::Point3{ 50, 25, 0 });
                vp->SetViewHeight(80.0);
                vp->GetAttr().InPaperSpace = true;
                drawing.AddEntity(std::move(vp));
                const auto data = ExportCad(drawing, CadFileKind::Dxf);
                std::ofstream out(file, std::ios::binary);
                out.write(reinterpret_cast<const char*>(data.data()), data.size());
            }
            OpenDrawings({ path });
            waitOpen();
            RenderFrame();
            Document* opened = m_docManager.GetActive();
            bool hasSheet = false;
            for (int i = 0; i < m_main.layoutTabs->GetTabCount(); ++i)
                hasSheet = hasSheet || m_main.layoutTabs->GetTabTitle(i) == "图纸A";
            check(opened && opened != fresh && barShown() && hasSheet &&
                  m_main.layoutTabs->GetTabCount() == static_cast<int>(opened->GetScene().Layouts().size()),
                  "打开带布局的图纸：标签条显示图纸里的布局");

            m_docTabs->SetSelected(m_docTabs->FindTabData(reinterpret_cast<uintptr_t>(fresh)));
            RenderFrame();
            check(m_main.layoutTabs->GetTabCount() == 3 && m_main.layoutTabs->GetTabTitle(1) == "布局1",
                  "切换文档后标签条换成该文档的布局");

            if (opened) { opened->MarkSaved(); CloseDocument(opened); }
            if (fresh)  { fresh->MarkSaved();  CloseDocument(fresh); }
            RenderFrame();
            std::error_code ec;
            std::filesystem::remove(file, ec);
            std::filesystem::remove(dir, ec);
        }

        section("25. 起始页：首页键显示、Esc 返回，切换 / 新建文档后回到视口");
        {
            m_commands.Execute("file.new");
            RenderFrame();
            Document* a = m_docManager.GetActive();
            check(m_viewport->IsVisible() && !m_startPage->IsVisible(), "有文档时显示视口");

            m_commands.Execute("view.start");
            RenderFrame();
            check(m_startPage->IsVisible() && !m_viewport->IsVisible() && m_docManager.GetActive() == a &&
                  m_commands.IsChecked("view.start"), "首页键：显示起始页，当前文档不变");

            m_ui->KeyDown(MiniGUI::Key::Escape, 0);
            m_ui->KeyUp(MiniGUI::Key::Escape, 0);
            RenderFrame();
            check(m_viewport->IsVisible() && !m_startPage->IsVisible() && m_viewport->HasFocus(), "Esc 返回视口，键盘焦点回到绘图区");

            m_commands.Execute("view.start");
            m_commands.Execute("file.new");
            RenderFrame();
            Document* b = m_docManager.GetActive();
            check(b != a && m_viewport->IsVisible() && !m_startPage->IsVisible(), "起始页上新建：显示新文档的视口");

            m_commands.Execute("view.start");
            m_docTabs->SetSelected(m_docTabs->FindTabData(reinterpret_cast<uintptr_t>(a)));
            RenderFrame();
            check(m_docManager.GetActive() == a && m_viewport->IsVisible(), "起始页上点另一个文档标签：回到视口");

            // 导航列的“节点编辑器”：回到当前图纸并显示节点面板（已显示时不收起）
            if (MiniGUI::DockSpace* dock = m_layout->GetDock("main"); dock && dock->HasPanel("nodes"))
            {
                const bool wasVisible = dock->IsPanelVisible("nodes");
                for (int i = 0; i < 2; ++i)
                {
                    m_commands.Execute("view.start");
                    RenderFrame();
                    OpenNodeEditorFromStart(false);
                    RenderFrame();
                    check(m_docManager.GetActive() == a && !m_startPage->IsVisible() && m_viewport->IsVisible() &&
                          dock->IsPanelVisible("nodes"), i == 0 ? "起始页上点节点编辑器：回到图纸并显示节点面板"
                                                                : "节点面板已显示时再点：仍然显示");
                }
                if (!wasVisible)
                    dock->ClosePanel("nodes");
            }

            for (Document* d : { a, b })
                if (d) { d->MarkSaved(); CloseDocument(d); }
            RenderFrame();
            if (!m_docManager.GetActive())
                check(m_startPage->IsVisible() && !m_commands.IsEnabled("view.start"), "没有文档：显示起始页，首页键不可用");
        }

        section("25b. 没有文档时打开图纸：读取期间显示绘图区，状态栏里看得到读取进度");
        {
            while (Document* d = m_docManager.GetActive()) { d->MarkSaved(); CloseDocument(d); }
            RenderFrame();
            check(!m_docManager.GetActive() && m_startPage->IsVisible(), "关闭全部图纸后显示起始页");
            {
                const auto file = std::filesystem::temp_directory_path() / ("minicad-selftest-first-" + std::to_string(NowTicks()) + ".dxf");
                Scene drawing;
                drawing.AddEntity(std::make_unique<LineEntity>(drawing.NextObjectID(), Math::Point3{0, 0, 0}, Math::Point3{100, 60, 0}));
                const auto data = ExportCad(drawing, CadFileKind::Dxf);
                { std::ofstream out(file, std::ios::binary); out.write(reinterpret_cast<const char*>(data.data()), data.size()); }
                const auto u8 = file.u8string();
                const std::string path(u8.begin(), u8.end());
                OpenDrawings({ path });
                RenderFrame();
                bool shown = m_openBar != nullptr;
                for (MiniGUI::Node* n = m_openBar; shown && n; n = n->GetParent())
                    shown = n->IsVisible();
                check(!m_startPage->IsVisible() && m_viewport->IsVisible() && shown && m_openBar->GetScreenBounds().Width() > 0.0f,
                      "没有文档时打开图纸：显示绘图区，状态栏里看得到读取进度");
                waitOpen();
                Document* first = m_docManager.GetActive();
                check(first && first->GetPath() == path && m_main.statusBar->IsVisible(), "第一张图纸打开后状态栏正常显示");
                if (first) { first->MarkSaved(); CloseDocument(first); }
                RenderFrame();
                std::error_code ec;
                std::filesystem::remove(file, ec);
            }
        }

        section("26. 功能区命令搜索：Ctrl+K、按名称 / 选项卡查找、Enter 执行、Esc 关闭");
        {
            m_commands.Execute("file.new");
            RenderFrame();
            MiniGUI::Ribbon*  ribbon = m_layout->GetRibbon();
            MiniGUI::TextBox* box    = ribbon ? ribbon->GetSearchBox() : nullptr;
            check(box != nullptr, "功能区有命令搜索框");
            if (box)
            {
                auto has = [&](const std::string& q, const char* id)
                {
                    const auto r = ribbon->SearchCommands(q);
                    return std::find(r.begin(), r.end(), id) != r.end();
                };
                const auto circ = ribbon->SearchCommands("circ");
                check(!circ.empty() && circ.front() == "draw.circle", "英文：circ 排第一的是 draw.circle");
                check(has("圆", "draw.circle") && has("直线", "draw.line"), "中文名称：圆、直线");
                check(has("绘图", "draw.line") && has("绘图 直线", "draw.line") && !has("修改 直线", "draw.line"),
                      "按选项卡名查找，多个词都要匹配");
                check(ribbon->SearchCommands("qqqzzz").empty(), "无匹配时结果为空");

                auto key = [&](MiniGUI::Key k, uint8_t mods = 0)
                {
                    m_ui->KeyDown(k, mods);
                    m_ui->KeyUp(k, mods);
                    RenderFrame();
                };
                const uint8_t ctrlKey = static_cast<uint8_t>(MiniGUI::ModifierKey::Ctrl);
                m_viewport->Focus();
                RenderFrame();
                const float closedW = box->GetSize().x;
                key(MiniGUI::Key::K, ctrlKey);
                RenderFrame();
                check(box->HasFocus() && box->GetSize().x > closedW + 50.0f, "Ctrl+K：搜索框获得焦点并变宽");

                m_ui->TextInput("直线");
                RenderFrame();
                MiniGUI::AutoComplete* ac = box->GetAutoComplete();
                check(ac && ac->IsOpen() && ac->GetCurrent() == 0 && !ac->GetSuggestions().empty() &&
                      ac->GetSuggestions().front().text == "直线" &&
                      ac->GetSuggestions().front().detail.find("绘图") != std::string::npos,
                      "键入“直线”：列表第一项是直线，并注明所在选项卡");
                SaveBmp(CaptureWindow(hwnd), std::filesystem::temp_directory_path() / "minicad_selftest_search.bmp");

                key(MiniGUI::Key::Enter);
                check(m_docManager.GetEditor().IsActiveTool() && m_docManager.GetEditor().GetLastCommand() == "Line", "Enter：执行直线命令");
                check(m_viewport->HasFocus() && box->GetText().empty() && box->GetSize().x < closedW + 1.0f,
                      "执行后焦点回到绘图区，搜索框清空并恢复宽度");
                key(MiniGUI::Key::Escape);

                key(MiniGUI::Key::K, ctrlKey);
                m_ui->TextInput("qqq");
                RenderFrame();
                key(MiniGUI::Key::Escape);
                check(m_viewport->HasFocus() && box->GetText().empty(), "Esc：关闭搜索框，焦点回到绘图区");
            }
            if (Document* d = m_docManager.GetActive()) { d->MarkSaved(); CloseDocument(d); }
            RenderFrame();
        }

        section("27. 浮动界面：绘图区铺满，空白处点击落到绘图区，浮动工具条上显示指针，功能区收起按钮");
        {
            m_commands.Execute("file.new");
            m_ui->Update();
            RenderFrame();
            const MiniGUI::Rect vpRect     = m_viewport->GetScreenBounds();
            const MiniGUI::Rect statusRect = m_main.statusBar->GetParent()->GetScreenBounds();
            MiniGUI::Ribbon*    ribbon     = m_layout->GetRibbon();
            check(ribbon && vpRect.min.y <= ribbon->GetScreenBounds().min.y && vpRect.max.y >= statusRect.max.y &&
                  vpRect.max.x >= statusRect.max.x, "绘图区铺满标题栏以下：功能区、状态栏浮在绘图区上");

            // 文档区（没被面板挡住的部分）、状态栏上方的缝：点击都落到绘图区（卡片之间竖向的缝是分隔条，可拖动调整宽度）
            const MiniGUI::Vec2 holeCenter = m_main.area->GetScreenBounds().Center();
            check(m_ui->HitTest(holeCenter) == m_viewport &&
                  m_ui->HitTest(MiniGUI::Vec2{ statusRect.min.x + 20.0f, statusRect.min.y - 3.0f }) == m_viewport,
                  "文档区、状态栏上方的缝点击落到绘图区");

            // 浮动导航条上：显示箭头指针，绘图区不画十字光标；移回绘图区后恢复
            m_ui->PointerMove(holeCenter, 0);
            RenderFrame();
            const bool crossOnCanvas = m_main.hovered && m_ui->GetCursor() == MiniGUI::CursorShape::Hidden;
            const MiniGUI::Rect nav = m_main.navBar->GetChildren().front()->GetScreenBounds();
            m_ui->PointerMove(MiniGUI::Vec2{ nav.Center().x, nav.min.y + 16.0f }, 0);
            RenderFrame();
            check(crossOnCanvas && !m_main.hovered && m_ui->GetCursor() != MiniGUI::CursorShape::Hidden,
                  "导航条上显示鼠标指针，绘图区不画十字光标");
            m_ui->PointerMove(holeCenter, 0);
            RenderFrame();
            check(m_main.hovered && m_ui->GetCursor() == MiniGUI::CursorShape::Hidden, "移回绘图区：十字光标");

            // 导航条竖排在视图方块下面，靠右与方块的右边对齐
            const MiniGUI::Rect cube = m_main.cube->GetScreenBounds();
            check(nav.min.y >= cube.max.y && std::abs(nav.max.x - cube.max.x) < 1.0f && nav.Height() > nav.Width(),
                  "导航条竖排在视图方块下面、贴着绘图区右边");

            // 命令行在状态栏上方居中
            const MiniGUI::Rect cmd = m_main.cmdCard->GetScreenBounds();
            check(IsCommandLineShown() && cmd.max.y <= statusRect.min.y &&
                  std::abs(cmd.Center().x - m_main.area->GetScreenBounds().Center().x) < 1.0f, "命令行浮在状态栏上方、文档区居中");
            m_commands.Execute("view.panel.commandline");
            RenderFrame();
            const bool hidden = !IsCommandLineShown();
            m_commands.Execute("view.panel.commandline");
            RenderFrame();
            check(hidden && IsCommandLineShown(), "Ctrl+9 隐藏 / 显示命令行");

            // 功能区收起按钮
            if (ribbon && ribbon->GetCollapseButton())
            {
                const float before = ribbon->GetScreenBounds().Height();
                const MiniGUI::Vec2 c = ribbon->GetCollapseButton()->GetScreenBounds().Center();
                m_ui->PointerDown(c, MiniGUI::MouseButton::Left, 0);
                m_ui->PointerUp(c, MiniGUI::MouseButton::Left, 0);
                m_ui->Update();
                RenderFrame();
                const bool collapsed = ribbon->IsCollapsed() && ribbon->GetScreenBounds().Height() < before - 50.0f;
                const MiniGUI::Vec2 c2 = ribbon->GetCollapseButton()->GetScreenBounds().Center();
                m_ui->PointerDown(c2, MiniGUI::MouseButton::Left, 0);
                m_ui->PointerUp(c2, MiniGUI::MouseButton::Left, 0);
                m_ui->Update();
                RenderFrame();
                check(collapsed && !ribbon->IsCollapsed(), "功能区右上角的按钮收起 / 展开功能区");
            }
            if (Document* d = m_docManager.GetActive()) { d->MarkSaved(); CloseDocument(d); }
            RenderFrame();
        }

        section("28. 重新加载界面、重置面板布局：重建后界面可用（不访问已销毁的节点）");
        {
            // 指针扫过整个窗口：悬停、提示、光标都会碰到重建后的节点
            auto sweep = [&]
            {
                const MiniGUI::Vec2 size = m_ui->GetRoot()->GetSize();
                for (float y = 4.0f; y < size.y; y += 37.0f)
                    for (float x = 4.0f; x < size.x; x += 53.0f)
                        m_ui->PointerMove(MiniGUI::Vec2{ x, y }, 0);
                m_ui->Update();
                RenderFrame();
            };
            m_commands.Execute("file.new");
            RenderFrame();
            sweep();
            std::printf("    重新加载界面（有文档）\n");
            m_commands.Execute("ui.reload");
            pump();
            RenderFrame();
            sweep();
            std::printf("    重置面板布局\n");
            m_commands.Execute("view.resetLayout");
            pump();
            RenderFrame();
            sweep();
            std::printf("    起始页上重新加载\n");
            m_commands.Execute("view.start");
            RenderFrame();
            m_commands.Execute("ui.reload");
            pump();
            RenderFrame();
            sweep();
            m_commands.Execute("view.start");
            RenderFrame();
            std::printf("    连续两次重新加载\n");
            m_commands.Execute("ui.reload");
            m_commands.Execute("ui.reload");
            pump();
            RenderFrame();
            sweep();
            check(m_layout->GetRibbon() && m_layout->GetTitleBar() && m_viewport->IsVisible(), "重新加载、重置布局后界面完整");
            if (Document* d = m_docManager.GetActive()) { d->MarkSaved(); CloseDocument(d); }
            std::printf("    没有文档时重新加载\n");
            m_commands.Execute("ui.reload");
            pump();
            RenderFrame();
            sweep();
            check(m_layout->GetTitleBar() != nullptr, "没有文档时重新加载界面");
        }

        // ── 29. 文档拖出成独立窗口：拖出、在里面平移 / 缩放 / 画线 / 选择、拖回、关闭 ──────────
        section("29. 文档窗口：拖出、它自己的界面（标题栏、功能区、快捷键、动态输入）、平移 / 缩放 / 画线 / 选择、最大化 / 最小化、拖回、关闭");
        {
            // 两个文档：A 在主窗口里，B 拖出去
            m_commands.Execute("file.new");
            Document* docA = m_docManager.GetActive();
            m_commands.Execute("file.new");
            Document* docB = m_docManager.GetActive();
            RenderFrame();
            auto addLine = [](Document& d, double y)
            {
                Scene& s = d.GetScene();
                d.GetCommandStack().Execute(std::make_unique<AddEntityCommand>(
                    std::make_unique<LineEntity>(s.NextObjectID(), Math::Point3{ -50, y, 0 }, Math::Point3{ 50, y, 0 })), s);
            };
            addLine(*docA, 0.0);
            addLine(*docB, 0.0);
            const int entitiesA = docA->GetScene().EntityCount();
            const int entitiesB = docB->GetScene().EntityCount();

            // 标签拖到标签条下方：经 TabView::OnTabDragOut → FloatDocument，一拖出就建窗口，之后窗口跟着光标走
            auto tabPosOf = [this](Document* d)
            {
                const MiniGUI::Rect tl = m_docTabs->GetTabRect(m_docTabs->FindTabData(reinterpret_cast<uintptr_t>(d)));
                return m_docTabs->GetScreenBounds().min + MiniGUI::Vec2{ tl.min.x + 12.0f, tl.Center().y };
            };
            MiniGUI::Vec2 tabPos = tabPosOf(docB);
            int tx = static_cast<int>(std::lround(tabPos.x * m_dpiScale)), ty = static_cast<int>(std::lround(tabPos.y * m_dpiScale));
            const int dropY = static_cast<int>(std::lround((tabPos.y + 200.0f) * m_dpiScale));

            // 先拖出再拖回标签条松开：同一次拖动里合并回去
            send(WM_MOUSEMOVE, 0, tx, ty);
            send(WM_LBUTTONDOWN, MK_LBUTTON, tx, ty);
            send(WM_MOUSEMOVE, MK_LBUTTON, tx + 8, ty);
            send(WM_MOUSEMOVE, MK_LBUTTON, tx + 8, dropY);
            RenderFrame();
            pump();
            check(m_docTabs->IsTornDragging() && DocWindowCount() == 1 && m_docManager.IsDetached(docB) && m_docTabs->GetTabCount() == 1,
                  "标签拖到标签条下方：没松开就已经是独立窗口（拖动中能看到窗口）");
            const int stripX = static_cast<int>(std::lround((m_docTabs->GetScreenBounds().min.x + 10.0f) * m_dpiScale));
            send(WM_MOUSEMOVE, MK_LBUTTON, stripX, ty);
            check(m_docTabs->GetDropIndicator() == 0, "继续拖到标签条上：标签条画出落点");
            send(WM_LBUTTONUP, 0, stripX, ty);
            RenderFrame();
            pump();
            check(DocWindowCount() == 0 && !m_docManager.IsDetached(docB) && m_docTabs->GetTabCount() == 2 &&
                  m_docTabs->GetTabData(0) == reinterpret_cast<uintptr_t>(docB) && !m_docTabs->IsTornDragging() && m_docTabs->GetDropIndicator() < 0,
                  "松开在标签条上：窗口关闭，B 插回落点处");

            // 再拖出，松开在标签条外：窗口留下
            tabPos = tabPosOf(docB);
            tx = static_cast<int>(std::lround(tabPos.x * m_dpiScale));
            ty = static_cast<int>(std::lround(tabPos.y * m_dpiScale));
            send(WM_MOUSEMOVE, 0, tx, ty);
            send(WM_LBUTTONDOWN, MK_LBUTTON, tx, ty);
            send(WM_MOUSEMOVE, MK_LBUTTON, tx + 8, ty);
            send(WM_MOUSEMOVE, MK_LBUTTON, tx + 8, dropY);
            RenderFrame();
            pump();
            DocWindow* dw = FindDocWindow(docB);
            if (dw)
            {
                const MiniGUI::Rect before = dw->window->GetRect();
                send(WM_MOUSEMOVE, MK_LBUTTON, tx + 68, dropY + 40);
                const MiniGUI::Rect after = dw->window->GetRect();
                check(std::abs((after.min.x - before.min.x) * m_dpiScale - 60.0f) < 2.0f && std::abs((after.min.y - before.min.y) * m_dpiScale - 40.0f) < 2.0f &&
                      std::abs(after.Width() - before.Width()) < 1.0f, "拖动中窗口跟着光标走，尺寸不变");
            }
            send(WM_LBUTTONUP, 0, tx + 68, dropY + 40);
            RenderFrame();
            pump();
            dw = FindDocWindow(docB);
            check(dw != nullptr && DocWindowCount() == 1 && m_docManager.IsDetached(docB) && !m_docTabs->IsTornDragging(), "松开在标签条外：文档 B 留在独立窗口里");
            check(dw && dw->window->GetUI().GetFocus() == dw->host, "松开后激活文档窗口，焦点在它的视口上");
            check(m_docManager.GetActive() == docB && m_docManager.GetMainDocument() == docA && m_docTabs->GetTabCount() == 1,
                  "B 仍是当前文档，主视口显示 A，标签条上只剩 A");
            check(m_title.find(docB->GetName()) == 0, "标题栏显示当前文档（B）的名字");
            if (dw)
            {
                const HWND fw = static_cast<Win32FloatingWindow*>(dw->window.get())->GetHwnd();
                check(fw != nullptr && IsWindowVisible(fw), "文档窗口是可见的系统窗口");
                auto sendF = [&](UINT m, WPARAM wp, int x, int y) { SendMessageW(fw, m, wp, MAKELPARAM(x, y)); };
                auto paintF = [&] { RedrawWindow(fw, nullptr, nullptr, RDW_INVALIDATE | RDW_UPDATENOW); };
                paintF();
                RenderFrame();
                Viewport*  vpB   = m_docManager.ViewportOf(docB);
                Viewport&  vpMain = m_docManager.GetMainViewport();
                const MiniGUI::Rect hb = dw->host->GetScreenBounds();     // 文档窗口里视口的位置（它自己的逻辑坐标）
                const float fs = dw->window->GetUI().GetPixelScale();
                const int fcx = static_cast<int>(std::lround(hb.Center().x * fs)), fcy = static_cast<int>(std::lround(hb.Center().y * fs));
                check(vpB && vpB != &vpMain && vpB->GetWidth() == static_cast<float>(dw->target->GetWidth()) && vpB->GetWidth() > 100.0f,
                      "文档窗口有自己的视口与渲染目标，尺寸随窗口");
                {
                    const ViewChrome& c = dw->chrome;
                    MiniGUI::UIContext* fui = &dw->window->GetUI();
                    const bool own = c.cube && c.navBar && c.console && c.statusBar && c.layoutTabs && c.cmdCard &&
                                     c.cube->GetContext() == fui && c.navBar->GetContext() == fui && c.console->GetContext() == fui &&
                                     c.statusBar->GetContext() == fui && c.layoutTabs->GetContext() == fui;
                    check(own && c.cube->IsVisible() && IsCommandLineShown(c) && c.layoutTabsBar && c.layoutTabsBar->IsVisible() &&
                          c.layoutTabs->GetTabCount() == static_cast<int>(docB->GetScene().Layouts().size()),
                          "文档窗口有自己的视图方块、导航条、命令行与状态栏（含模型 / 布局标签），显示 B 的布局");
                    check(DocOf(c) == docB && DocOf(m_main) == docA, "文档窗口的一套绑定 B，主窗口的一套绑定主视口里的 A");
                }

                // 平移：中键拖动只动 B 的相机
                const Math::Point2 bBefore = vpB->GetCamera().WorldToScreen(origin);
                const Math::Point2 aBefore = vpMain.GetCamera().WorldToScreen(origin);
                sendF(WM_MOUSEMOVE, 0, fcx, fcy);
                sendF(WM_MBUTTONDOWN, MK_MBUTTON, fcx, fcy);
                sendF(WM_MOUSEMOVE, MK_MBUTTON, fcx + 30, fcy + 20);
                sendF(WM_MBUTTONUP, 0, fcx + 30, fcy + 20);
                const Math::Point2 bAfter = vpB->GetCamera().WorldToScreen(origin);
                const Math::Point2 aAfter = vpMain.GetCamera().WorldToScreen(origin);
                check(std::abs((bAfter.x - bBefore.x) - 30.0) < 0.5 && std::abs((bAfter.y - bBefore.y) - 20.0) < 0.5, "在文档窗口里中键拖动：B 的视图平移");
                check(aAfter.x == aBefore.x && aAfter.y == aBefore.y, "主窗口里 A 的视图不动");

                // 滚轮缩放
                const double zoomBefore = vpB->GetCamera().GetState().Zoom;
                POINT sp{ fcx, fcy };
                ClientToScreen(fw, &sp);
                SendMessageW(fw, WM_MOUSEWHEEL, MAKEWPARAM(0, WHEEL_DELTA), MAKELPARAM(sp.x, sp.y));
                check(vpB->GetCamera().GetState().Zoom != zoomBefore && vpMain.GetCamera().GetState().Zoom != vpB->GetCamera().GetState().Zoom,
                      "滚轮只缩放 B 的视图");

                // 画线：在文档窗口里按 L、空格（字母进主窗口的命令行，空格执行），再点两点、Esc
                Editor& edB = m_docManager.GetEditor(*docB);
                sendF(WM_KEYDOWN, 'L', 0, 0);
                SendMessageW(fw, WM_CHAR, 'l', 0);
                sendF(WM_KEYUP, 'L', 0, 0);
                RenderFrame();
                check(dw->chrome.console->HasInputFocus() && dw->chrome.console->GetInputText() == "l" &&
                      !m_main.console->HasInputFocus() && m_main.console->GetInputText().empty(),
                      "文档窗口里按 L：字母进入文档窗口自己的命令行，主窗口的命令行不受影响");
                SendMessageW(fw, WM_CHAR, ' ', 0);
                paintF();
                RenderFrame();
                check(edB.IsActiveTool() && edB.GetLastCommand() == "Line" && !m_docManager.GetEditor(*docA).IsActiveTool(),
                      "空格执行：直线工具在 B 的编辑器里启动");
                check(dw->chrome.console->GetPrompt() == edB.GetCmdLine().Prompt() && !edB.GetCmdLine().Prompt().empty() &&
                      m_main.console->GetPrompt() == "命令:",
                      "文档窗口的命令行显示 B 的提示，主窗口的命令行显示 A 的（空闲）");
                sendF(WM_MOUSEMOVE, 0, fcx - 40, fcy - 30);
                sendF(WM_LBUTTONDOWN, MK_LBUTTON, fcx - 40, fcy - 30);
                sendF(WM_LBUTTONUP, 0, fcx - 40, fcy - 30);
                sendF(WM_MOUSEMOVE, 0, fcx + 40, fcy + 30);
                paintF();
                RenderFrame();
                check(IsDynInputVisible(dw->chrome) && !IsDynInputVisible(m_main),
                      "有锚点时动态输入框显示在文档窗口里（光标旁），主窗口的不显示");
                sendF(WM_KEYDOWN, '5', 0, 0);
                SendMessageW(fw, WM_CHAR, '5', 0);
                sendF(WM_KEYUP, '5', 0, 0);
                paintF();
                check(DynInputHasFocus(dw->chrome) && GetDynInputText(dw->chrome, 0) == "5" && !m_main.console->HasInputFocus(),
                      "在文档窗口里按数字：进它自己的长度框");
                sendF(WM_KEYDOWN, VK_ESCAPE, 0, 0);     // 第一次 Esc 只清掉键入
                sendF(WM_KEYUP, VK_ESCAPE, 0, 0);
                dw->host->Focus();
                sendF(WM_LBUTTONDOWN, MK_LBUTTON, fcx + 40, fcy + 30);
                sendF(WM_LBUTTONUP, 0, fcx + 40, fcy + 30);
                sendF(WM_KEYDOWN, VK_ESCAPE, 0, 0);
                sendF(WM_KEYUP, VK_ESCAPE, 0, 0);
                RenderFrame();
                check(docB->GetScene().EntityCount() == entitiesB + 1 && docA->GetScene().EntityCount() == entitiesA && !edB.IsActiveTool(),
                      "在文档窗口里画了一条线到 B（A 不变），Esc 结束工具");

                // 选择：点刚画的线的中点
                sendF(WM_MOUSEMOVE, 0, fcx, fcy);
                sendF(WM_LBUTTONDOWN, MK_LBUTTON, fcx, fcy);
                sendF(WM_LBUTTONUP, 0, fcx, fcy);
                check(edB.GetSelection().size() == 1 && m_docManager.GetEditor(*docA).GetSelection().empty(), "在文档窗口里点选：选中 B 里的线");

                // 文档窗口有自己的标题栏工具、功能区与图层下拉框（界面描述的 layouts.document），快捷键登记在它自己的界面上
                {
                    MiniGUI::UIContext* fui = &dw->window->GetUI();
                    const MiniGUI::Ribbon* rb = dw->layout->GetRibbon();
                    check(rb && rb->GetContext() == fui && rb->IsVisible() && dw->layout->GetMenuBar() && dw->layout->GetMenuBar()->GetContext() == fui &&
                          dw->layout->GetToolBar("quick") && dw->titleBar == dw->layout->GetTitleBar() && dw->tabs->GetParent() &&
                          dw->layerBar && dw->layerBar->GetContext() == fui && rb != m_layout->GetRibbon(),
                          "文档窗口有自己的“文件”菜单、快速访问、功能区与图层下拉框");
                    docB->GetLayerManager().AddLayer("多窗口阶段3");
                    m_commands.NotifyStateChanged();
                    check(dw->layerBar->GetItems().size() == SortedLayerIds(*docB).size() &&
                          m_layerBarLayer->GetItems().size() == SortedLayerIds(*docA).size() &&
                          dw->layerBar->GetItems().size() != m_layerBarLayer->GetItems().size(),
                          "各窗口的图层下拉框显示自己窗口里文档的图层（B 多一个，主窗口的显示 A 的）");

                    const auto keepSel = edB.GetSelection();
                    fui->KeyDown(MiniGUI::Key::A, static_cast<uint8_t>(MiniGUI::ModifierKey::Ctrl));
                    fui->KeyUp(MiniGUI::Key::A, static_cast<uint8_t>(MiniGUI::ModifierKey::Ctrl));
                    check(edB.GetSelection().size() == docB->GetScene().EntityCount() && m_docManager.GetEditor(*docA).GetSelection().empty(),
                          "文档窗口的快捷键（Ctrl+A）在它自己的界面上触发，作用于 B");
                    edB.SetSelection({ keepSel.begin(), keepSel.end() });

                    // 点主窗口的功能区：主窗口的 A 成为当前文档；再点文档窗口的功能区：B
                    const MiniGUI::Rect mr = m_layout->GetRibbon()->GetScreenBounds();
                    const int rx = static_cast<int>(std::lround((mr.max.x - 3.0f) * m_dpiScale)), ry = static_cast<int>(std::lround((mr.max.y - 3.0f) * m_dpiScale));
                    send(WM_MOUSEMOVE, 0, rx, ry);
                    send(WM_LBUTTONDOWN, MK_LBUTTON, rx, ry);
                    send(WM_LBUTTONUP, 0, rx, ry);
                    const bool mainActive = m_docManager.GetActive() == docA;
                    const MiniGUI::Rect fr = rb->GetScreenBounds();
                    const int frx = static_cast<int>(std::lround((fr.max.x - 3.0f) * fs)), fry = static_cast<int>(std::lround((fr.max.y - 3.0f) * fs));
                    sendF(WM_MOUSEMOVE, 0, frx, fry);
                    sendF(WM_LBUTTONDOWN, MK_LBUTTON, frx, fry);
                    sendF(WM_LBUTTONUP, 0, frx, fry);
                    check(mainActive && m_docManager.GetActive() == docB, "点哪个窗口的功能区，哪个窗口的文档成为当前文档（命令随后作用于它）");

                    // 重新加载界面描述、切换主题：文档窗口一起
                    const MiniGUI::TitleBar* oldBar = dw->titleBar;
                    ReloadUi();
                    RenderFrame();
                    paintF();
                    check(dw->titleBar && dw->titleBar != oldBar && dw->titleBar->GetContext() == fui && dw->tabs->GetContext() == fui &&
                          dw->layout->GetRibbon() && dw->layerBar->GetContext() == fui && dw->host->GetContext() == fui,
                          "重新加载界面描述：文档窗口按 layouts.document 一起重建，标签条、视口、图层下拉框还是原来的");
                    const bool dark = m_ui->GetTheme().IsDark();
                    m_commands.Execute("ui.theme");
                    const bool themed = fui->GetTheme().IsDark() == m_ui->GetTheme().IsDark() && m_ui->GetTheme().IsDark() != dark;
                    m_commands.Execute("ui.theme");
                    check(themed && fui->GetTheme().IsDark() == dark, "切换主题：文档窗口跟着换");
                    RenderFrame();
                    paintF();
                }
                paintF();
                RenderFrame();
                {
                    ViewportPixels px;
                    const bool read = static_cast<Win32Window*>(m_platform)->GetGraphics().ReadPixels(*dw->target, px.W, px.H, px.Rgba);
                    int lit = 0;
                    if (read)
                        for (int y = 0; y < px.H; ++y)
                            for (int x = 0; x < px.W; ++x)
                                if (px.At(x, y)[0] > 60 || px.At(x, y)[1] > 60 || px.At(x, y)[2] > 60) ++lit;
                    check(read && lit > 50, "文档窗口的视口里画出了内容");
                    if (read)
                        SaveBmp(px, std::filesystem::temp_directory_path() / "minicad_selftest_docwindow.bmp");
                    const ViewportPixels ui = CaptureWindow(fw);
                    SaveBmp(ui, std::filesystem::temp_directory_path() / "minicad_selftest_docwindow_ui.bmp");
                    // 视口的空白处（功能区下面、文档区的左上角）是窗口底色，与主窗口的绘图区相同，而不是浮动面板的面板色
                    const MiniGUI::Rect ab = dw->chrome.area->GetScreenBounds();
                    const int bx = static_cast<int>((ab.min.x + 20.0f) * fs), by = static_cast<int>((ab.min.y + 20.0f) * fs);
                    const MiniGUI::Color32 bg = m_ui->GetTheme().Get(MiniGUI::ThemeColor::Background);
                    bool bgOk = ui.Inside(bx, by);
                    for (int c = 0; bgOk && c < 3; ++c)     // Color32 低字节为 R
                        bgOk = std::abs(static_cast<int>(ui.At(bx, by)[c]) - static_cast<int>((bg >> (8 * c)) & 0xFF)) <= 2;
                    check(bgOk, "文档窗口的绘图区空白处是窗口底色（与主窗口的绘图区相同）");
                    SaveBmp(CaptureWindow(hwnd), std::filesystem::temp_directory_path() / "minicad_selftest_docwindow_main.bmp");
                }

                // 文档窗口的布局标签：切的是 B 的布局，A 不变；切回模型（选择集会清掉，之后恢复）
                {
                    const auto selB = edB.GetSelection();
                    const int  layoutA = docA->ActiveLayout();
                    dw->chrome.layoutTabs->SetSelected(1);
                    paintF();
                    RenderFrame();
                    check(docB->ActiveLayout() == 1 && docA->ActiveLayout() == layoutA && m_docManager.GetActive() == docB &&
                          !dw->chrome.cube->IsVisible() && m_main.cube->IsVisible(),
                          "文档窗口的布局标签切换 B 的布局（布局页上它的视图方块隐藏），A 与主窗口的视图方块不变");
                    dw->chrome.layoutTabs->SetSelected(0);
                    paintF();
                    RenderFrame();
                    check(docB->ActiveLayout() == 0 && dw->chrome.cube->IsVisible(), "切回模型页");
                    edB.SetSelection(selB);
                }

                // 标题栏：文档名、主窗口的图标；空白处是系统标题区域（拖动、双击最大化、贴边），按钮不是
                {
                    MiniGUI::TitleBar* tb = dw->titleBar;
                    MiniGUI::UIContext& fui = dw->window->GetUI();
                    check(tb && tb->GetTitle().rfind(docB->GetName(), 0) == 0 && tb->GetIcon() != MiniGUI::InvalidTextureId,
                          "文档窗口的标题栏显示文档名与程序图标");
                    auto screenOf = [&](MiniGUI::Vec2 p)
                    {
                        POINT s{ static_cast<LONG>(std::lround(p.x * fui.GetPixelScale())), static_cast<LONG>(std::lround(p.y * fui.GetPixelScale())) };
                        ClientToScreen(fw, &s);
                        return s;
                    };
                    auto buttonAt = [&](int fromRight)      // 0 = 关闭，1 = 最大化，2 = 最小化
                    {
                        const MiniGUI::Rect r = tb->GetScreenBounds();
                        return MiniGUI::Vec2{ r.max.x - MiniGUI::TitleBar::kButtonWidth * (static_cast<float>(fromRight) + 0.5f), r.Center().y };
                    };
                    auto clickButton = [&](int fromRight)
                    {
                        const MiniGUI::Vec2 p = buttonAt(fromRight);
                        fui.PointerMove(p, 0);
                        fui.PointerDown(p, MiniGUI::MouseButton::Left, 0, 1);
                        fui.PointerUp(p, MiniGUI::MouseButton::Left, 0);
                        paintF();
                    };
                    const POINT blank = screenOf(MiniGUI::Vec2{ tb->GetScreenBounds().Center().x, tb->GetScreenBounds().Center().y });
                    const POINT maxButton = screenOf(buttonAt(1));
                    check(SendMessageW(fw, WM_NCHITTEST, 0, MAKELPARAM(blank.x, blank.y)) == HTCAPTION &&
                          SendMessageW(fw, WM_NCHITTEST, 0, MAKELPARAM(maxButton.x, maxButton.y)) == HTCLIENT,
                          "标题栏空白处交给系统拖动（HTCAPTION），按钮不是");
                    clickButton(1);
                    check(IsZoomed(fw) && dw->window->IsWindowMaximized(), "最大化按钮：文档窗口最大化");
                    clickButton(1);
                    check(!IsZoomed(fw), "再点：还原");
                    clickButton(2);
                    check(IsIconic(fw) != FALSE, "最小化按钮：文档窗口最小化");
                    ShowWindow(fw, SW_RESTORE);
                    paintF();
                    check(!IsIconic(fw) && m_docManager.IsDetached(docB) && DocWindowCount() == 1, "还原后文档仍在它的窗口里");
                }

                // 主窗口的视口：点一下 A 就成为当前文档
                send(WM_MOUSEMOVE, 0, cx, cy);
                send(WM_LBUTTONDOWN, MK_LBUTTON, cx, cy);
                send(WM_LBUTTONUP, 0, cx, cy);
                RenderFrame();
                check(m_docManager.GetActive() == docA && m_docManager.IsDetached(docB), "点主窗口的视口：A 成为当前文档，B 仍在它的窗口里");
                // 文档窗口被激活（点它、Alt+Tab）：B 成为当前文档
                SendMessageW(fw, WM_NCACTIVATE, TRUE, 0);
                check(m_docManager.GetActive() == docB && dw->titleBar->IsWindowActive(), "激活文档窗口：B 成为当前文档，标题栏按活动样式");

                // 拖回：系统的移动循环拖文档窗口的标题栏到主窗口的标签条上松开（光标相对窗口左上角不变）
                const CameraState bCam = vpB->GetCamera().GetState();
                const MiniGUI::Rect stripMain = m_docTabs->GetScreenBounds();
                const MiniGUI::Vec2 dropMain{ stripMain.min.x + 10.0f, stripMain.Center().y };     // 第一个标签的左半：插到最前
                POINT cursor{};
                RECT  wr{};
                GetCursorPos(&cursor);
                GetWindowRect(fw, &wr);
                const POINT grab{ cursor.x - wr.left, cursor.y - wr.top };
                POINT target{ static_cast<LONG>(std::lround(dropMain.x * m_dpiScale)), static_cast<LONG>(std::lround(dropMain.y * m_dpiScale)) };
                ClientToScreen(hwnd, &target);
                RECT moved{ target.x - grab.x, target.y - grab.y, target.x - grab.x + (wr.right - wr.left), target.y - grab.y + (wr.bottom - wr.top) };
                SendMessageW(fw, WM_ENTERSIZEMOVE, 0, 0);
                SendMessageW(fw, WM_MOVING, WMSZ_LEFT, reinterpret_cast<LPARAM>(&moved));
                check(m_docTabs->GetDropIndicator() == 0 && DocWindowCount() == 1, "标题栏拖到标签条上：标签条画出落点");
                SetWindowPos(fw, nullptr, moved.left, moved.top, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
                SendMessageW(fw, WM_EXITSIZEMOVE, 0, 0);
                RenderFrame();
                pump();
                check(DocWindowCount() == 0 && !m_docManager.IsDetached(docB) && m_docTabs->GetTabCount() == 2 && m_docTabs->GetDropIndicator() < 0,
                      "松开：文档窗口关闭，B 回到标签条");
                check(m_docTabs->GetTabData(0) == reinterpret_cast<uintptr_t>(docB) && m_docTabs->GetSelected() == 0, "插在落点处（最前）并选中");
                check(m_docManager.GetActive() == docB && m_docManager.GetMainDocument() == docB && vpMain.GetCamera().GetState().Zoom == bCam.Zoom,
                      "B 成为当前文档与主视口文档，相机是它在独立窗口里的视图");
                check(m_docManager.GetEditor(*docB).GetSelection().size() == 1, "选择集随文档保留");
            }

            // 关闭文档窗口 = 关闭文档：未保存时询问，取消后窗口还在；保存状态下直接关
            check(FloatDocument(docB, MiniGUI::Vec2{ 300.0f, 300.0f }), "右键菜单的“在独立窗口中打开”");
            RenderFrame();
            pump();
            if (DocWindow* w2 = FindDocWindow(docB))
            {
                const HWND fw = static_cast<Win32FloatingWindow*>(w2->window.get())->GetHwnd();
                check(GetWindow(fw, GW_OWNER) == nullptr && (GetWindowLongPtrW(fw, GWL_EXSTYLE) & WS_EX_APPWINDOW) != 0,
                      "文档窗口与主窗口平级（主窗口能盖在它上面，对话框不被挡住），任务栏里有按钮");
                SendMessageW(fw, WM_CLOSE, 0, 0);
                RenderFrame();
                check(m_ui->HasModal() && DocWindowCount() == 1 && m_modalRaised, "关闭未保存的文档窗口：主窗口询问是否保存，并置前");
                // 对话框开着：在文档窗口里点不会编辑，再关一次也不会叠出第二个对话框
                {
                    const MiniGUI::Rect hb2 = w2->host->GetScreenBounds();
                    const float fs2 = w2->window->GetUI().GetPixelScale();
                    const int px = static_cast<int>(std::lround(hb2.Center().x * fs2)), py = static_cast<int>(std::lround(hb2.Center().y * fs2));
                    const size_t selBefore = m_docManager.GetEditor(*docB).GetSelection().size();
                    SendMessageW(fw, WM_MOUSEMOVE, 0, MAKELPARAM(px + 200, py + 100));
                    SendMessageW(fw, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(px + 200, py + 100));
                    SendMessageW(fw, WM_MOUSEMOVE, MK_LBUTTON, MAKELPARAM(px - 200, py - 100));
                    SendMessageW(fw, WM_LBUTTONUP, 0, MAKELPARAM(px - 200, py - 100));
                    SendMessageW(fw, WM_CLOSE, 0, 0);
                    check(m_docManager.GetEditor(*docB).GetSelection().size() == selBefore && DocWindowCount() == 1 && m_ui->HasModal(),
                          "对话框开着时文档窗口不接受编辑（框选、再次关闭都不执行）");
                }
                // Esc 按在文档窗口里：转给主窗口的对话框
                SendMessageW(fw, WM_KEYDOWN, VK_ESCAPE, 0);
                SendMessageW(fw, WM_KEYUP, VK_ESCAPE, 0);
                RenderFrame();
                check(!m_ui->HasModal() && DocWindowCount() == 1 && m_docManager.GetAll().size() >= 2, "在文档窗口里按 Esc 取消对话框，文档与窗口都还在");
                docB->MarkSaved();
                SendMessageW(fw, WM_CLOSE, 0, 0);
                RenderFrame();
                pump();
                check(DocWindowCount() == 0 &&
                      std::none_of(m_docManager.GetAll().begin(), m_docManager.GetAll().end(), [docB](const auto& d) { return d.get() == docB; }),
                      "已保存的文档窗口关闭即关闭文档");
                check(m_docManager.GetActive() == docA && m_docManager.GetMainDocument() == docA, "关闭后当前文档回到主视口里的 A");
            }
            if (docA) { docA->MarkSaved(); CloseDocument(docA); }
            RenderFrame();
        }

        // ── 30. 多窗口：一个窗口放几个文档，标签在任意窗口之间拖动合并 ──────────
        section("30. 多窗口：一个窗口放几个文档，标签在任意窗口之间拖动合并");
        {
            using PaneId = DocumentManager::PaneId;
            constexpr PaneId kMain = DocumentManager::kMainPane;
            m_commands.Execute("file.new");
            Document* docA = m_docManager.GetActive();
            m_commands.Execute("file.new");
            Document* docB = m_docManager.GetActive();
            m_commands.Execute("file.new");
            Document* docC = m_docManager.GetActive();
            RenderFrame();
            auto key = [](const Document* d) { return reinterpret_cast<uintptr_t>(d); };
            // 某个窗口里某个标签的中心偏左（避开关闭按钮），那个窗口的逻辑坐标
            auto tabPoint = [&](PaneId pane, Document* d)
            {
                MiniGUI::TabView* tabs = TabsOfPane(pane);
                const MiniGUI::Rect r = tabs->GetTabRect(tabs->FindTabData(key(d)));
                return tabs->GetScreenBounds().min + MiniGUI::Vec2{ r.min.x + 12.0f, r.Center().y };
            };
            auto stripPoint = [&](PaneId pane, float fromLeft)      // 某个窗口的标签条上（主窗口坐标）
            {
                const MiniGUI::Rect r = TabsOfPane(pane)->GetScreenBounds();
                return PaneToMain(pane, MiniGUI::Vec2{ r.min.x + fromLeft, r.Center().y });
            };
            auto px = [&](float v) { return static_cast<int>(std::lround(v * m_dpiScale)); };
            // 在主窗口的标签条上按住某个标签拖到 mainTo（主窗口坐标），途中经过标签条下方，松开
            auto dragMainTab = [&](Document* d, MiniGUI::Vec2 mainTo)
            {
                const MiniGUI::Vec2 p = tabPoint(kMain, d);
                send(WM_MOUSEMOVE, 0, px(p.x), px(p.y));
                send(WM_LBUTTONDOWN, MK_LBUTTON, px(p.x), px(p.y));
                send(WM_MOUSEMOVE, MK_LBUTTON, px(p.x + 8.0f), px(p.y));
                send(WM_MOUSEMOVE, MK_LBUTTON, px(p.x + 8.0f), px(p.y + 200.0f));
                RenderFrame();
                pump();
                send(WM_MOUSEMOVE, MK_LBUTTON, px(mainTo.x), px(mainTo.y));
                send(WM_LBUTTONUP, 0, px(mainTo.x), px(mainTo.y));
                RenderFrame();
                pump();
            };
            // 在文档窗口的标签条上按住某个标签（那个窗口自己的界面），拖出标签条，再拖到 mainTo（主窗口坐标），松开。
            // 被拖的窗口跟着光标走：每一步都按它当时的位置换算
            auto dragWindowTab = [&](PaneId pane, Document* d, MiniGUI::Vec2 mainTo, bool release = true)
            {
                DocWindow* w = FindPaneWindow(pane);
                MiniGUI::UIContext& fui = w->window->GetUI();
                const MiniGUI::Vec2 p = tabPoint(pane, d);
                fui.PointerMove(p, 0);
                fui.PointerDown(p, MiniGUI::MouseButton::Left, 0, 100);
                fui.PointerMove(p + MiniGUI::Vec2{ 8.0f, 0.0f }, 0);
                fui.PointerMove(p + MiniGUI::Vec2{ 8.0f, 200.0f }, 0);
                RenderFrame();
                pump();
                fui.PointerMove(MainToPane(pane, mainTo), 0);
                if (release)
                {
                    fui.PointerUp(MainToPane(pane, mainTo), MiniGUI::MouseButton::Left, 0);
                    RenderFrame();
                    pump();
                }
            };

            // B 拖出成窗口 W1
            const MiniGUI::Rect mainStrip = m_docTabs->GetScreenBounds();
            dragMainTab(docB, MiniGUI::Vec2{ mainStrip.min.x + 200.0f, mainStrip.max.y + 300.0f });
            const PaneId w1 = m_docManager.PaneOf(docB);
            DocWindow* win1 = FindPaneWindow(w1);
            check(win1 && w1 != kMain && DocWindowCount() == 1 && win1->tabs && win1->tabs->GetTabCount() == 1,
                  "B 拖出成文档窗口 W1，W1 的标题栏里有它的标签条");

            // C 从主窗口的标签条拖到 W1 的标签条上：途中建的临时窗口在松开后关掉，C 进 W1
            dragMainTab(docC, stripPoint(w1, 200.0f));       // 标签右边的标题栏空白处：接到最后
            check(m_docManager.PaneOf(docC) == w1 && DocWindowCount() == 1 && TabOrder(w1) == std::vector<Document*>{ docB, docC } &&
                  m_docTabs->GetTabCount() == 1, "C 从主窗口拖到 W1 的标签条上：进 W1（排在 B 后面），途中的临时窗口关掉");
            check(m_docManager.GetShownDocument(w1) == docC && m_docManager.GetActive() == docC && m_docManager.GetMainDocument() == docA &&
                  win1->tabs->GetSelected() == win1->tabs->FindTabData(key(docC)), "W1 显示 C 并选中它的标签，C 是当前文档，主窗口仍是 A");

            // W1 里点 B 的标签：B 显示出来，相机各自保留；主窗口不动
            {
                MiniGUI::UIContext& fui = win1->window->GetUI();
                const MiniGUI::Vec2 p = tabPoint(w1, docB);
                fui.PointerMove(p, 0);
                fui.PointerDown(p, MiniGUI::MouseButton::Left, 0, 5000);
                fui.PointerUp(p, MiniGUI::MouseButton::Left, 0);
                RenderFrame();
                check(m_docManager.GetShownDocument(w1) == docB && m_docManager.GetActive() == docB && m_docManager.GetMainDocument() == docA &&
                      win1->chrome.console && DocOf(win1->chrome) == docB, "点 W1 里 B 的标签：W1 改显示 B，它的命令行等跟着换，主窗口不动");
            }

            // 当前文档在 W1 里：新建的文档进 W1；关掉它回到两个标签
            m_commands.Execute("file.new");
            Document* docD = m_docManager.GetActive();
            RenderFrame();
            check(m_docManager.PaneOf(docD) == w1 && win1->tabs->GetTabCount() == 3 && m_docTabs->GetTabCount() == 1,
                  "当前文档在 W1 里时新建：新文档进 W1");
            docD->MarkSaved();
            CloseDocument(docD);
            RenderFrame();
            check(win1->tabs->GetTabCount() == 2 && DocWindowCount() == 1, "关掉它：W1 剩两个标签");

            // C 从 W1 拖回主窗口的标签条（插到 A 前面）：W1 有两个文档，拖出时先建临时窗口，松开后关掉
            dragWindowTab(w1, docC, stripPoint(kMain, 6.0f));
            check(m_docManager.PaneOf(docC) == kMain && m_docTabs->GetTabData(0) == key(docC) && m_docTabs->GetTabCount() == 2 &&
                  DocWindowCount() == 1 && TabOrder(w1) == std::vector<Document*>{ docB },
                  "C 从 W1 拖到主窗口标签条的最前面：进主窗口，W1 只剩 B，临时窗口关掉");
            check(m_docManager.GetMainDocument() == docC && m_docManager.GetActive() == docC, "主窗口显示 C，它是当前文档");

            // W1 只有 B：拖它的标签就是拖整个窗口（不建新窗口）；经过主窗口的标签条时窗口让到标签条下面，松开即合并
            {
                const MiniGUI::Rect before = win1->window->GetRect();
                const MiniGUI::Vec2 away{ before.min.x + 300.0f, before.min.y + 420.0f };
                dragWindowTab(w1, docB, away, false);
                const MiniGUI::Rect moved = win1->window->GetRect();
                check(DocWindowCount() == 1 && m_torn.window == w1 && std::abs(moved.min.y - before.min.y) > 50.0f,
                      "只有一个标签的窗口：拖标签就是拖窗口，不建新窗口");
                MiniGUI::UIContext& fui = win1->window->GetUI();
                const MiniGUI::Vec2 onStrip = stripPoint(kMain, 300.0f);
                fui.PointerMove(MainToPane(w1, onStrip), 0);
                const float stripBottom = m_docTabs->GetScreenBounds().max.y;
                check(m_docTabs->GetDropIndicator() >= 0 && win1->window->GetRect().min.y >= stripBottom,
                      "拖到主窗口的标签条上：画出落点，被拖的窗口让到标签条下面");
                fui.PointerUp(MainToPane(w1, onStrip), MiniGUI::MouseButton::Left, 0);
                RenderFrame();
                pump();
                check(DocWindowCount() == 0 && m_docManager.PaneOf(docB) == kMain && !m_docManager.HasPane(w1) && m_docTabs->GetTabCount() == 3 &&
                      m_docTabs->GetDropIndicator() < 0, "松开：B 回主窗口，W1 空了随之关掉");
            }

            // 两个文档的窗口拖标题栏（系统的移动循环）到主窗口的标签条上：两个文档都移回去，窗口关掉；移动中窗口让开标签条
            check(FloatDocument(docB, MiniGUI::Vec2{ 300.0f, 300.0f }), "B 再拖出成窗口");
            RenderFrame();
            pump();
            const PaneId w2 = m_docManager.PaneOf(docB);
            MoveDocumentsTo({ docC }, docC, w2, -1);
            RenderFrame();
            pump();
            if (DocWindow* win2 = FindPaneWindow(w2))
            {
                check(TabOrder(w2) == std::vector<Document*>{ docB, docC } && m_docTabs->GetTabCount() == 1, "C 也移进这个窗口：它有两个标签");
                const HWND fw = static_cast<Win32FloatingWindow*>(win2->window.get())->GetHwnd();
                POINT cursor{};
                RECT  wr{};
                GetCursorPos(&cursor);
                GetWindowRect(fw, &wr);
                const POINT grab{ cursor.x - wr.left, cursor.y - wr.top };
                const MiniGUI::Vec2 dropMain = stripPoint(kMain, 300.0f);
                POINT target{ px(dropMain.x), px(dropMain.y) };
                ClientToScreen(hwnd, &target);
                RECT moved{ target.x - grab.x, target.y - grab.y, target.x - grab.x + (wr.right - wr.left), target.y - grab.y + (wr.bottom - wr.top) };
                SendMessageW(fw, WM_ENTERSIZEMOVE, 0, 0);
                SendMessageW(fw, WM_MOVING, WMSZ_LEFT, reinterpret_cast<LPARAM>(&moved));
                POINT stripBottom{ 0, px(m_docTabs->GetScreenBounds().max.y) };
                ClientToScreen(hwnd, &stripBottom);
                check(m_docTabs->GetDropIndicator() >= 0 && moved.top >= stripBottom.y,
                      "拖标题栏到主窗口的标签条上：画出落点，窗口让到标签条下面（系统给的位置被改）");
                SetWindowPos(fw, nullptr, moved.left, moved.top, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
                SendMessageW(fw, WM_EXITSIZEMOVE, 0, 0);
                RenderFrame();
                pump();
                check(DocWindowCount() == 0 && m_docManager.PaneOf(docB) == kMain && m_docManager.PaneOf(docC) == kMain && m_docTabs->GetTabCount() == 3,
                      "松开（光标还在标签条上，窗口位置已让开）：两个文档都回主窗口，窗口关掉");
            }

            // 主窗口的文档都移走：中间显示提示卡片，命令行、视图方块、导航条不显示；面板还在
            check(FloatDocument(docA, MiniGUI::Vec2{ 300.0f, 300.0f }), "A 拖出成窗口");
            RenderFrame();
            pump();
            const PaneId w3 = m_docManager.PaneOf(docA);
            MoveDocumentsTo({ docB, docC }, docC, w3, -1);
            RenderFrame();
            check(!m_docManager.GetMainDocument() && m_docTabs->GetTabCount() == 0 && m_docTabs->IsVisible() && m_mainEmptyCard && m_mainEmptyCard->IsVisible() &&
                  !m_startShown && m_viewport->IsVisible(), "主窗口的文档都移走：不显示起始页，中间是提示卡片，标签条还在（可以拖回来）");
            check(!IsCommandLineShown() && !m_main.cube->IsVisible() && !m_main.navBar->IsVisible(), "主窗口的命令行、视图方块、导航条不显示");
            // 在主窗口里操作（卡片上点“新建”）：新文档进主窗口
            UseChrome(m_main);
            m_commands.Execute("file.new");
            Document* docE = m_docManager.GetActive();
            RenderFrame();
            check(m_docManager.PaneOf(docE) == kMain && m_docManager.GetMainDocument() == docE && !m_mainEmptyCard->IsVisible() && IsCommandLineShown(),
                  "在空着的主窗口里新建：新文档进主窗口，提示卡片收起");

            // 关闭有三个文档的窗口（都已保存）：文档都关掉，窗口随之关掉
            for (Document* d : { docA, docB, docC })
                d->MarkSaved();
            CloseDocWindow(w3);
            RenderFrame();
            pump();
            check(DocWindowCount() == 0 && m_docManager.GetAll().size() == 1 && m_docManager.GetActive() == docE,
                  "关闭文档窗口：里面的三个文档都关掉，窗口随之关掉，当前文档回到主窗口");
            docE->MarkSaved();
            CloseDocument(docE);
            RenderFrame();
        }

        endSection();
        std::sort(sectionTimes.begin(), sectionTimes.end(),[](const auto& a, const auto& b) { return a.second > b.second; });
        std::printf("\n最耗时的测试项：\n");
        for (size_t i = 0; i < sectionTimes.size() && i < 5; ++i)
            std::printf("    %6.2f 秒  %s\n", sectionTimes[i].second, sectionTimes[i].first.c_str());
        std::printf("输入→呈现 最近 %.1f ms，最大 %.1f ms；界面 %llu 帧，视口 %llu 帧\n", m_latencyLast, m_latencyMax,
                    static_cast<unsigned long long>(m_frames), static_cast<unsigned long long>(m_viewportFrames));
        std::printf("%s：%d 项失败\n", failures == 0 ? "自测通过" : "自测失败", failures);
        return failures == 0 ? 0 : 1;
    }

    // =========================================================
    // 基准（--bench 图纸）：打开图纸、全部显示，然后在视口中心滚轮放大 / 缩小，打印每帧耗时、是否重新细分与内存
    // =========================================================
    int MainFrame::RunBenchmark(const std::string& path)
    {
        const HWND hwnd = static_cast<Win32Window*>(m_platform)->GetHwnd();
        auto memMb = []
        {
            PROCESS_MEMORY_COUNTERS_EX pmc = {};
            pmc.cb = sizeof(pmc);
            K32GetProcessMemoryInfo(GetCurrentProcess(), reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&pmc), sizeof(pmc));
            return std::pair<double, double>(static_cast<double>(pmc.PrivateUsage) / 1048576.0, static_cast<double>(pmc.WorkingSetSize) / 1048576.0);
        };
        auto msSince = [](int64_t from) { return static_cast<double>(NowTicks() - from) * 1000.0 / static_cast<double>(kTicksPerSecond); };
        MSG msg;
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) { TranslateMessage(&msg); DispatchMessageW(&msg); }
        RenderFrame();
        auto [m0, w0] = memMb();
        std::printf("启动：私有 %.0f MB，工作集 %.0f MB\n", m0, w0);

        int64_t t = NowTicks();
        Document* doc = m_docManager.Open(path);
        if (!doc) { std::printf("打开失败：%s\n", path.c_str()); return 1; }
        const double openMs = msSince(t);
        auto [m1, w1] = memMb();
        std::printf("打开 %.0f ms：私有 %.0f MB，工作集 %.0f MB，实体 %d\n", openMs, m1, w1, doc->GetScene().EntityCount());
        StateChanged();   // 离开起始页、显示视口（否则视口不渲染，下面量到的全是空帧）

        m_docManager.ZoomAll();
        m_syncGpu = true;
        m_viewport->RequestRender();
        t = NowTicks();
        RenderFrame();
        Editor& editor = m_docManager.GetEditor();
        {
            const auto st = editor.GetSceneStreamStats();
            auto [m2, w2] = memMb();
            std::printf("首帧 %.0f ms（视口 %.0f ms）：私有 %.0f MB，工作集 %.0f MB；顶点 线 %zu 填充 %zu 文字 %zu\n", msSince(t), m_timing.viewport,
                        m2, w2, st.lines, st.fills, st.texts);
        }
        // MINICAD_BENCH_SHOT=目录：模型页「全部显示」的视口也存一张 model.bmp，核对模型空间的显示（如外部参照）
        if (const char* shotDir = std::getenv("MINICAD_BENCH_SHOT"))
        {
            const std::filesystem::path dir(shotDir);
            std::error_code ec;
            std::filesystem::create_directories(dir, ec);
            RenderFrame();
            SaveBmp(ReadViewport(*m_platform), dir / "model.bmp");
            std::printf("截图：%s（模型页）\n", (dir / "model.bmp").string().c_str());
        }

        const MiniGUI::Rect vr = m_viewport->GetScreenBounds();
        POINT sp = { static_cast<LONG>(std::lround(vr.Center().x * m_dpiScale)), static_cast<LONG>(std::lround(vr.Center().y * m_dpiScale)) };
        SendMessageW(hwnd, WM_MOUSEMOVE, 0, MAKELPARAM(sp.x, sp.y));
        RenderFrame();

        // 新增实体：全图显示后在视口中心逐条加直线（走命令栈，与画线工具提交时相同），分别测命令本身、
        // 之后的首帧（场景流追加 / 重新拼接 + 上传）与再一次鼠标移动帧（拾取索引、悬停）；大图纸里这就是"画完卡一下"的那一下
        {
            Scene& scene = doc->GetScene();
            const Camera& cam = m_docManager.GetViewport().GetCamera();
            std::vector<Object::ObjectID> added;
            for (int i = 0; i < 3; ++i)
            {
                const Math::Point3 a = cam.ScreenToWorld(vr.Center().x - 100.0, vr.Center().y + 20.0 * i);
                const Math::Point3 b = cam.ScreenToWorld(vr.Center().x + 100.0, vr.Center().y + 20.0 * i);
                const auto st0 = editor.GetSceneStreamStats();
                int64_t f0 = NowTicks();
                added.push_back(scene.NextObjectID());
                doc->GetCommandStack().Execute(std::make_unique<AddEntityCommand>(std::make_unique<LineEntity>(added.back(), a, b)), scene);
                const double cmdMs = msSince(f0);
                m_viewport->RequestRender();
                f0 = NowTicks();
                RenderFrame();
                const double frameMs = msSince(f0), frameVp = m_timing.viewport;
                f0 = NowTicks();
                SendMessageW(hwnd, WM_MOUSEMOVE, 0, MAKELPARAM(sp.x + 3 + i, sp.y + 2));
                RenderFrame();
                const double moveMs = msSince(f0);
                auto [pm, wm] = memMb();
                std::printf("新增直线 %d：命令 %.1f ms，首帧 %.1f ms（视口 %.1f ms），再移动鼠标一帧 %.1f ms；线顶点 %zu → %zu，私有 %.0f MB\n",
                            i + 1, cmdMs, frameMs, frameVp, moveMs, st0.lines, editor.GetSceneStreamStats().lines, pm);
            }
            // 画线工具里移动鼠标（有捕捉与橡皮筋）：每帧耗时
            editor.ActivateToolById("Line");
            SendMessageW(hwnd, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(sp.x - 80, sp.y - 60));
            SendMessageW(hwnd, WM_LBUTTONUP, 0, MAKELPARAM(sp.x - 80, sp.y - 60));
            RenderFrame();
            double moveSum = 0, moveMax = 0;
            const int moves = 12;
            for (int i = 0; i < moves; ++i)
            {
                const int64_t f0 = NowTicks();
                SendMessageW(hwnd, WM_MOUSEMOVE, 0, MAKELPARAM(sp.x - 60 + 11 * i, sp.y - 40 + 7 * i));
                RenderFrame();
                const double ms = msSince(f0);
                moveSum += ms; moveMax = std::max(moveMax, ms);
            }
            SendMessageW(hwnd, WM_KEYDOWN, VK_ESCAPE, 0);
            SendMessageW(hwnd, WM_KEYUP, VK_ESCAPE, 0);
            RenderFrame();
            std::printf("画线工具里移动鼠标 %d 帧：平均 %.1f ms，最慢 %.1f ms\n", moves, moveSum / moves, moveMax);
            SendMessageW(hwnd, WM_MOUSEMOVE, 0, MAKELPARAM(sp.x, sp.y));
            RenderFrame();

            // 修改 / 删除 / 撤销：对刚加的 3 条直线做移动命令（夹点拖完提交、特性面板改值都是"已在缓存的实体标脏"这一条路）、
            // 夹点拖动（拖起时实体离开场景流、放下时回来）、删除、撤销，各测命令本身与之后的首帧；大图纸里这些原本都要重新拼接整张图
            auto frameAfter = [&](const char* what, auto&& act)
            {
                int64_t f0 = NowTicks();
                act();
                const double cmdMs = msSince(f0);
                m_viewport->RequestRender();
                f0 = NowTicks();
                RenderFrame();
                std::printf("%s：命令 %.1f ms，首帧 %.1f ms（视口 %.1f ms）\n", what, cmdMs, msSince(f0), m_timing.viewport);
            };
            const Math::Vec3 up5 = cam.ScreenToWorld(vr.Center().x, vr.Center().y - 5.0) - cam.ScreenToWorld(vr.Center().x, vr.Center().y);
            frameAfter("选中 3 条直线", [&] { editor.SetSelection({ added.begin(), added.end() }); });
            frameAfter("移动 3 条直线", [&] { doc->GetCommandStack().Execute(std::make_unique<MoveCommand>(added, up5, scene), scene); });
            {
                // 夹点拖动第 1 条直线的中点：点一下激活、抬起后跟随，移动两帧，再点一下放下
                editor.SetSelection({ added[0] });
                RenderFrame();
                // 相机的屏幕坐标是相对视口左上角的物理像素；鼠标消息要窗口客户区坐标
                const Math::Point2 mid = cam.WorldToScreen(cam.ScreenToWorld(vr.Center().x, vr.Center().y) + up5);
                const POINT gp = { static_cast<LONG>(std::lround(vr.min.x * m_dpiScale + mid.x)), static_cast<LONG>(std::lround(vr.min.y * m_dpiScale + mid.y)) };
                SendMessageW(hwnd, WM_MOUSEMOVE, 0, MAKELPARAM(gp.x, gp.y));
                RenderFrame();
                int64_t f0 = NowTicks();
                SendMessageW(hwnd, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(gp.x, gp.y));
                SendMessageW(hwnd, WM_LBUTTONUP, 0, MAKELPARAM(gp.x, gp.y));
                RenderFrame();
                const double pickMs = msSince(f0);
                const bool   dragged = editor.GetGripEditor().IsDragging();
                double dragMs[2] = {};
                for (int i = 0; i < 2; ++i)
                {
                    f0 = NowTicks();
                    SendMessageW(hwnd, WM_MOUSEMOVE, 0, MAKELPARAM(gp.x + 15 * (i + 1), gp.y + 15 * (i + 1)));
                    RenderFrame();
                    dragMs[i] = msSince(f0);
                }
                f0 = NowTicks();
                SendMessageW(hwnd, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(gp.x + 30, gp.y + 30));
                SendMessageW(hwnd, WM_LBUTTONUP, 0, MAKELPARAM(gp.x + 30, gp.y + 30));
                RenderFrame();
                const double dropMs = msSince(f0);
                std::printf("夹点拖动 1 条直线：拖起首帧 %.1f ms，拖动两帧 %.1f / %.1f ms，放下首帧 %.1f ms（%s）\n",
                            pickMs, dragMs[0], dragMs[1], dropMs, dragged ? "已拖动" : "夹点没点中");
            }
            frameAfter("撤销夹点拖动", [&] { m_docManager.Undo(); });
            editor.SetSelection({ added.begin(), added.end() });
            RenderFrame();
            frameAfter("删除 3 条直线", [&] { editor.DeleteSelected(); });
            frameAfter("撤销删除", [&] { m_docManager.Undo(); });
            frameAfter("撤销移动", [&] { m_docManager.Undo(); });
            editor.SetSelection({});
            SendMessageW(hwnd, WM_MOUSEMOVE, 0, MAKELPARAM(sp.x, sp.y));
            RenderFrame();
        }
        ClientToScreen(hwnd, &sp);
        const int steps = 12;
        double peak = 0, low = 1e30, total = 0, worst = 0;
        int rebuilds = 0;
        for (int pass = 0; pass < 2; ++pass)
            for (int i = 0; i < 2 * steps; ++i)
            {
                const bool zoomIn = i < steps;
                const uint64_t v = editor.BuildViewState().SceneVersion, dv = editor.BuildViewState().SceneDynVersion;
                const int64_t f0 = NowTicks();
                SendMessageW(hwnd, WM_MOUSEWHEEL, MAKEWPARAM(0, zoomIn ? WHEEL_DELTA : -WHEEL_DELTA), MAKELPARAM(sp.x, sp.y));
                RenderFrame();
                const double ms = msSince(f0);
                const bool rebuilt = editor.BuildViewState().SceneVersion != v;
                const bool dynRebuilt = editor.BuildViewState().SceneDynVersion != dv;
                rebuilds += rebuilt;
                auto [pm, wm] = memMb();
                peak = std::max(peak, pm); low = std::min(low, pm);
                total += ms; worst = std::max(worst, ms);
                if (pass == 0)
                    std::printf("  %s %2d：%7.1f ms（视口 %7.1f ms）%s 私有 %.0f MB 工作集 %.0f MB\n", zoomIn ? "放大" : "缩小", i % steps + 1, ms,
                                m_timing.viewport, rebuilt ? "整体重建" : dynRebuilt ? "动态流  " : "        ", pm, wm);
            }
        std::printf("滚轮 %d 帧：平均 %.1f ms，最慢 %.1f ms，整体重建 %d 帧，私有内存 %.0f～%.0f MB\n", 4 * steps, total / (4 * steps), worst, rebuilds, low, peak);

        // 平移：全图显示（缩小）时与放大 6 格后各平移 24 帧，打印每帧总耗时与各段耗时（布局 / 视口 / 界面 / 呈现）。
        // 平移不重建任何顶点，耗时全在视口的缓存层重画（GPU）与界面
        auto panPass = [&](const char* name)
        {
            double sum = 0, mx = 0, vp = 0, vpMax = 0, layout = 0, ui = 0, present = 0;
            const int n = 24;
            for (int i = 0; i < n; ++i)
            {
                const int64_t f0 = NowTicks();
                m_docManager.GetViewport().Pan((i & 1) ? 7.0f : -5.0f, (i & 2) ? 3.0f : -4.0f);
                m_viewport->RequestRender();
                RenderFrame();
                const double ms = msSince(f0);
                sum += ms; mx = std::max(mx, ms);
                vp += m_timing.viewport; vpMax = std::max(vpMax, m_timing.viewport);
                layout += m_timing.layout; ui += m_timing.ui; present += m_timing.present;
            }
            std::printf("平移（%s）%d 帧：平均 %.1f ms，最慢 %.1f ms；视口平均 %.1f ms（最慢 %.1f）、布局 %.1f、界面 %.1f、呈现 %.1f\n",
                        name, n, sum / n, mx, vp / n, vpMax, layout / n, ui / n, present / n);
        };
        m_docManager.ZoomAll();
        m_viewport->RequestRender();
        RenderFrame();
        panPass("全图");
        for (int i = 0; i < 6; ++i)
        {
            SendMessageW(hwnd, WM_MOUSEWHEEL, MAKEWPARAM(0, WHEEL_DELTA), MAKELPARAM(sp.x, sp.y));
            RenderFrame();
        }
        panPass("放大 6 格");

        // 框选全图：全部显示后从视口左上拖到右下（窗口选择），分别测抬起消息（框选判定 + 界面刷新）、
        // 选中后的首帧（选中流 + 夹点）、再一次界面刷新、带着选择集滚轮一格、Esc 取消选择后的首帧
        {
            m_docManager.ZoomAll();
            m_viewport->RequestRender();
            RenderFrame();
            const MiniGUI::Rect r = m_viewport->GetScreenBounds();
            const auto px = [&](double v) { return static_cast<int>(std::lround(v * m_dpiScale)); };
            const int x0 = px(r.min.x) + 4, y0 = px(r.min.y) + 4, x1 = px(r.max.x) - 4, y1 = px(r.max.y) - 4;
            SendMessageW(hwnd, WM_MOUSEMOVE, 0, MAKELPARAM(x0, y0));
            RenderFrame();
            SendMessageW(hwnd, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(x0, y0));
            SendMessageW(hwnd, WM_MOUSEMOVE, MK_LBUTTON, MAKELPARAM((x0 + x1) / 2, (y0 + y1) / 2));
            RenderFrame();
            SendMessageW(hwnd, WM_MOUSEMOVE, MK_LBUTTON, MAKELPARAM(x1, y1));
            int64_t f0 = NowTicks();
            RenderFrame();
            const double dragMs = msSince(f0);
            f0 = NowTicks();
            SendMessageW(hwnd, WM_LBUTTONUP, 0, MAKELPARAM(x1, y1));
            const double upMs = msSince(f0);
            f0 = NowTicks();
            RenderFrame();
            const double firstMs = msSince(f0), firstVp = m_timing.viewport, firstLayout = m_timing.layout, firstUi = m_timing.ui;
            f0 = NowTicks();
            StateChanged();
            const double refreshMs = msSince(f0);
            f0 = NowTicks();
            RenderFrame();
            const double idleMs = msSince(f0);
            std::printf("框选全图：选中 %zu 个；拖动一帧 %.1f ms，抬起（判定 + 界面刷新）%.1f ms，首帧 %.1f ms（视口 %.1f、布局 %.1f、界面 %.1f ms），"
                        "再刷新界面 %.1f ms，之后一帧 %.1f ms\n",
                        editor.GetSelection().size(), dragMs, upMs, firstMs, firstVp, firstLayout, firstUi, refreshMs, idleMs);
            double wheelSum = 0, wheelVp = 0;
            for (int i = 0; i < 4; ++i)
            {
                f0 = NowTicks();
                SendMessageW(hwnd, WM_MOUSEWHEEL, MAKEWPARAM(0, (i & 1) ? -WHEEL_DELTA : WHEEL_DELTA), MAKELPARAM(sp.x, sp.y));
                RenderFrame();
                wheelSum += msSince(f0);
                wheelVp  += m_timing.viewport;
            }
            f0 = NowTicks();
            SendMessageW(hwnd, WM_KEYDOWN, VK_ESCAPE, 0);
            SendMessageW(hwnd, WM_KEYUP, VK_ESCAPE, 0);
            const double escKeyMs = msSince(f0);   // 按键本身（取消选择 + 界面刷新）
            f0 = NowTicks();
            RenderFrame();
            const double escMs = msSince(f0), escVp = m_timing.viewport, escLayout = m_timing.layout, escUi = m_timing.ui;
            std::printf("  带着选择集滚轮 4 格：平均 %.1f ms（视口 %.1f ms）；Esc：按键 %.1f ms，之后首帧 %.1f ms（视口 %.1f、布局 %.1f、界面 %.1f ms，剩余选中 %zu）\n",
                        wheelSum / 4, wheelVp / 4, escKeyMs, escMs, escVp, escLayout, escUi, editor.GetSelection().size());
            std::printf("    Esc 帧的布局细分：状态同步 %.1f、第一次布局 %.1f、命令行同步 + 第二次布局 %.1f ms；图层 %zu 个\n",
                        m_timing.status, m_timing.layout1, m_timing.layout2, SortedLayerIds(*doc).size());
        }

        // 布局页：切到第一张图纸布局（进入时框住纸张），滚轮 24 帧、平移 24 帧。
        // 布局页的视口把模型空间内容合成进自己的缓存，这里看每格滚轮是否都在重新合成
        if (doc->GetScene().Layouts().size() > 1)
        {
            t = NowTicks();
            ActivateLayout(1);
            StateChanged();
            RenderFrame();
            std::printf("布局页「%s」首帧 %.0f ms（视口 %.0f ms）\n", doc->GetScene().Layouts()[1].Name.c_str(), msSince(t), m_timing.viewport);
            double sum = 0, mx = 0;
            int full = 0, dyn = 0;
            for (int i = 0; i < 2 * steps; ++i)
            {
                const bool zoomIn = i < steps;
                const uint64_t v = editor.BuildViewState().SceneVersion, dv = editor.BuildViewState().SceneDynVersion;
                const int64_t f0 = NowTicks();
                SendMessageW(hwnd, WM_MOUSEWHEEL, MAKEWPARAM(0, zoomIn ? WHEEL_DELTA : -WHEEL_DELTA), MAKELPARAM(sp.x, sp.y));
                RenderFrame();
                const double ms = msSince(f0);
                sum += ms; mx = std::max(mx, ms);
                full += editor.BuildViewState().SceneVersion != v;
                dyn += editor.BuildViewState().SceneDynVersion != dv;
            }
            std::printf("布局页滚轮 %d 帧：平均 %.1f ms，最慢 %.1f ms，整体重建 %d 帧，动态流重建 %d 帧\n", 2 * steps, sum / (2 * steps), mx, full, dyn);
            panPass("布局页");
            // MINICAD_BENCH_SHOT=目录：把布局页（框住纸张）与第一个 OLE 对象（放大到它）的视口读回存成 BMP，核对显示效果
            if (const char* shotDir = std::getenv("MINICAD_BENCH_SHOT"))
            {
                const std::filesystem::path dir(shotDir);
                std::error_code ec;
                std::filesystem::create_directories(dir, ec);
                Camera& cam = m_docManager.GetViewport().GetCamera();
                ZoomToLayout(doc->GetScene().Layouts()[1]);     // 与进布局时相同的视图
                m_viewport->RequestRender();
                RenderFrame();
                RenderFrame();
                SaveBmp(ReadViewport(*m_platform), dir / "layout.bmp");
                const OleFrameEntity* ole = nullptr;
                doc->GetScene().ForEachObject([&](const Object& o) { if (!ole) ole = dynamic_cast<const OleFrameEntity*>(&o); });
                if (ole)
                {
                    const AABB box = ole->GetBoundingBox();
                    cam.ZoomToBounds(box.Min.x, box.Min.y, box.Max.x, box.Max.y, 1.2);
                    m_viewport->RequestRender();
                    RenderFrame();
                    RenderFrame();
                    SaveBmp(ReadViewport(*m_platform), dir / "ole.bmp");
                    std::printf("截图：%s（布局页）、ole.bmp（OLE 对象 %.1f x %.1f）\n", (dir / "layout.bmp").string().c_str(),
                                box.Max.x - box.Min.x, box.Max.y - box.Min.y);
                }
            }
            // MINICAD_BENCH_PAUSE=秒：在布局页停留（照常处理消息与绘制），便于截图核对视口里的内容
            if (const char* pause = std::getenv("MINICAD_BENCH_PAUSE"))
            {
                const int64_t until = NowTicks() + static_cast<int64_t>(std::atof(pause) * static_cast<double>(kTicksPerSecond));
                while (NowTicks() < until)
                {
                    MSG m;
                    while (PeekMessageW(&m, nullptr, 0, 0, PM_REMOVE)) { TranslateMessage(&m); DispatchMessageW(&m); }
                    m_viewport->RequestRender();
                    RenderFrame();
                    Sleep(16);
                }
            }
            ActivateLayout(0);
            StateChanged();
            RenderFrame();
        }

        // 切换文档：切到新建的空白文档再切回，切回后的首帧要重新生成全部显示数据
        m_docManager.New();
        RenderFrame();
        t = NowTicks();
        m_docManager.SetActive(doc);
        StateChanged();
        RenderFrame();
        auto [m3, w3] = memMb();
        std::printf("切回文档首帧 %.0f ms（视口 %.0f ms）：私有 %.0f MB，工作集 %.0f MB\n", msSince(t), m_timing.viewport, m3, w3);
        {
            const auto st = editor.GetSceneStreamStats();
            const ViewState vs = editor.BuildViewState();
            std::printf("  顶点 线 %zu 填充 %zu 文字 %zu 动态线 %zu 动态填充 %zu 字形实例 %zu 字形网格 %zu\n", st.lines, st.fills, st.texts, vs.SceneDyn.size(), vs.SceneDynFill.size(), st.glyphs, st.glyphMeshes);
        }
        m_syncGpu = false;
        return 0;
    }
}
