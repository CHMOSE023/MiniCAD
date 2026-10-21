// ── 双击中键 = 缩放全图（ZOOM E / ZOOM A）────────────────────────────────
//
// 用户要求：「新增双击鼠标中键，缩放全图的功能。」
//
// 连击计数不在 Editor 里算 —— UI 层 `UIContext::PointerDown` 已经按
// 「同一按键 + 双击时间窗内 + 位置差 ≤ 4 逻辑像素」算好 `clickCount`，
// 经 `ViewportHost` → `ViewportPointerEvent` → `MainFrame::OnViewportPointer`
// 填进 `InputEvent::ClickCount`。本文件守住这条链路的**消费端**：
//   · Editor 收到 ClickCount >= 2 的中键按下 → 调用 ZoomAll
//   · 单击中键仍然是平移的准备（不得缩放）
//   · 双击中键不得把「平移」也执行一遍
//
// 判据用**相机状态**（缩放后视野应覆盖全部图形），不是"函数被调了"。
#include "Editor/Editor.h"
#include "Editor/Input/InputEvent.h"
#include "Document/Document.h"
#include "Viewport/Viewport.h"
#include "Scene/Scene.h"
#include "Core/Entity/LineEntity.hpp"
#include <cmath>
#include <cstdio>
#include <memory>

using namespace MiniCAD;

namespace
{
    int g_failures = 0;
    void Check(bool ok, const char* what)
    {
        std::printf("[%s] %s\n", ok ? "通过" : "失败", what);
        if (!ok) ++g_failures;
    }

    // 屏幕坐标 → 世界坐标（用于判定"图形是否落在视野内"）
    Math::Point3 ToWorld(Viewport& vp, double sx, double sy)
    {
        return vp.GetCamera().ScreenToWorld(sx, sy);
    }
}

int RunMiddleDoubleClickZoomTests()
{
    g_failures = 0;

    Document doc;
    Viewport viewport(1000.f, 800.f);
    Editor   editor;
    editor.Bind(doc, viewport);

    Scene& scene = doc.GetScene();
    // 一块远离原点、尺寸很大的图形：初始视图（默认相机）绝对看不全
    scene.AddEntity(std::make_unique<LineEntity>(
        1, Math::Point3{ 10000, 20000, 0 }, Math::Point3{ 30000, 60000, 0 }));

    // ── 1. 前置：初始视野看不全 ─────────────────────────────────
    {
        const Math::Point3 a = ToWorld(viewport, 0, 0);
        const Math::Point3 b = ToWorld(viewport, 1000, 800);
        const double wx = std::abs(b.x - a.x), wy = std::abs(b.y - a.y);
        std::printf("    初始视野 %.1f × %.1f（图形 20000 × 40000）\n", wx, wy);
        Check(wx < 20000.0 || wy < 40000.0,
              "初始视野看不全图形（否则本组是假通过）");
    }

    // ── 2. 单击中键：不缩放 ─────────────────────────────────────
    {
        const CameraState before = viewport.GetCamera().GetState();

        InputEvent down = {};
        down.Type   = InputEventType::MouseButtonDown;
        down.Button = MouseButton::Middle;
        down.ClickCount = 1;
        down.MouseX = 500;
        down.MouseY = 400;
        down.MouseButtons = static_cast<uint8_t>(MouseButtonState::Middle);
        editor.OnInput(down);

        const CameraState after = viewport.GetCamera().GetState();
        const bool same = std::abs(before.Zoom - after.Zoom) < 1e-9 &&
                          std::abs(before.Target.x - after.Target.x) < 1e-9 &&
                          std::abs(before.Target.y - after.Target.y) < 1e-9;
        Check(same, "单击中键不缩放（只准备平移）");

        // 补一个抬起，恢复正常状态
        InputEvent up = down;
        up.Type = InputEventType::MouseButtonUp;
        up.MouseButtons = 0;
        editor.OnInput(up);
    }

    // ── 3. 双击中键：缩放全图 ───────────────────────────────────
    {
        InputEvent down = {};
        down.Type   = InputEventType::MouseButtonDown;
        down.Button = MouseButton::Middle;
        down.ClickCount = 2;              // ← 关键：UI 层算出的连击计数
        down.MouseX = 500;
        down.MouseY = 400;
        down.MouseButtons = static_cast<uint8_t>(MouseButtonState::Middle);

        const CameraState before = viewport.GetCamera().GetState();
        const bool handled = editor.OnInput(down);
        const CameraState after = viewport.GetCamera().GetState();

        Check(handled, "双击中键事件被 Editor 消费");

        const bool changed = std::abs(before.Zoom - after.Zoom) > 1e-9 ||
                             std::abs(before.Target.x - after.Target.x) > 1e-9 ||
                             std::abs(before.Target.y - after.Target.y) > 1e-9;
        Check(changed, "双击中键改变了相机（确实执行了缩放）");

        // 判据：缩放后视野必须**包含**图形的两个端点
        const Math::Point3 a = ToWorld(viewport, 0, 0);
        const Math::Point3 b = ToWorld(viewport, 1000, 800);
        const double x0 = std::min(a.x, b.x), x1 = std::max(a.x, b.x);
        const double y0 = std::min(a.y, b.y), y1 = std::max(a.y, b.y);
        std::printf("    缩放后视野 X[%.1f, %.1f] Y[%.1f, %.1f]\n", x0, x1, y0, y1);

        Check(x0 <= 10000.0 && x1 >= 30000.0,
              "缩放后横向包含整个图形（10000 ~ 30000）");
        Check(y0 <= 20000.0 && y1 >= 60000.0,
              "缩放后纵向包含整个图形（20000 ~ 60000）");

        // 视图操作不得进撤销栈：否则用户按 Ctrl+Z 想回上一视图，
        // 实际撤销掉的会是几何编辑。中键平移与滚轮缩放本来就不进栈。
        Check(!doc.GetCommandStack().CanUndo(),
              "双击中键缩放**不进撤销栈**（与平移 / 滚轮缩放同类）");

        // 补一个抬起
        InputEvent up = down;
        up.Type = InputEventType::MouseButtonUp;
        up.MouseButtons = 0;
        editor.OnInput(up);
    }

    // ── 4. 双击之后，再单击中键仍不缩放（状态没被搞坏）──────────
    {
        const CameraState before = viewport.GetCamera().GetState();

        InputEvent down = {};
        down.Type   = InputEventType::MouseButtonDown;
        down.Button = MouseButton::Middle;
        down.ClickCount = 1;
        down.MouseX = 500;
        down.MouseY = 400;
        down.MouseButtons = static_cast<uint8_t>(MouseButtonState::Middle);
        editor.OnInput(down);

        const CameraState after = viewport.GetCamera().GetState();
        Check(std::abs(before.Zoom - after.Zoom) < 1e-9 &&
              std::abs(before.Target.x - after.Target.x) < 1e-9,
              "双击之后再单击中键仍不缩放");

        InputEvent up = down;
        up.Type = InputEventType::MouseButtonUp;
        up.MouseButtons = 0;
        editor.OnInput(up);
    }

    // ── 5. 左键双击不受影响（不能被中键逻辑误吃）────────────────
    {
        const CameraState before = viewport.GetCamera().GetState();

        InputEvent down = {};
        down.Type   = InputEventType::MouseButtonDown;
        down.Button = MouseButton::Left;
        down.ClickCount = 2;
        down.MouseX = 500;
        down.MouseY = 400;
        down.MouseButtons = static_cast<uint8_t>(MouseButtonState::Left);
        editor.OnInput(down);

        const CameraState after = viewport.GetCamera().GetState();
        Check(std::abs(before.Zoom - after.Zoom) < 1e-9 &&
              std::abs(before.Target.x - after.Target.x) < 1e-9,
              "左键双击不触发缩放全图（只认中键）");
    }

    std::printf("双击中键缩放全图用例失败：%d\n", g_failures);
    return g_failures;
}
