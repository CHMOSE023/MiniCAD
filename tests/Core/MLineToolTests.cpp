// ── 多线工具（MLINE）：激活后不崩、能画出实体 ────────────────────────────
//
// 用户报「点『多线』直接崩溃」。
//
// 根因：`MLineTool::GetPrompt()` 里读 `m_ctx->scene`（取样式名），
// 而 `GetPrompt` 由 `Editor::Render()` **每帧**调用 —— `m_ctx` 指向
// `OnInput` 的栈参数，事件返回后即失效。于是**激活工具后的第一帧渲染**
// 就解引用野指针，程序立刻崩溃（还没点任何点）。
//
// 修法：样式名在 `OnInput` 里缓存到成员 `m_styleName`，`GetPrompt` 只读缓存。
//
// 本文件守住两条：
//   ① 激活后**未经 OnInput** 就 Render（第一帧）不崩，且提示文本可用
//   ② 正常点击几点后能生成 MLineEntity，且能进撤销栈
#include "Editor/Editor.h"
#include "Editor/Input/InputEvent.h"
#include "Document/Document.h"
#include "Viewport/Viewport.h"
#include "Scene/Scene.h"
#include "Core/Entity/MLineEntity.hpp"
#include <cmath>
#include <cstdio>
#include <string>

using namespace MiniCAD;

namespace
{
    int g_failures = 0;
    void Check(bool ok, const char* what)
    {
        std::printf("[%s] %s\n", ok ? "通过" : "失败", what);
        if (!ok) ++g_failures;
    }

    InputEvent MakeClick(Viewport& vp, const Math::Point3& world, bool up)
    {
        const Math::Point2 sp = vp.GetCamera().WorldToScreen(world);
        InputEvent e = {};
        e.Type   = up ? InputEventType::MouseButtonUp : InputEventType::MouseButtonDown;
        e.Button = MouseButton::Left;
        e.MouseX = static_cast<int>(sp.x);
        e.MouseY = static_cast<int>(sp.y);
        e.MouseButtons = up ? 0 : static_cast<uint8_t>(MouseButtonState::Left);
        return e;
    }

}

int RunMLineToolTests()
{
    g_failures = 0;

    // ══ ① 激活后直接渲染（第一帧）不崩 ═══════════════════════════════════
    //
    // 这一步在修复前必然崩溃：GetPrompt 读失效的 m_ctx。
    {
        Document doc;
        Viewport viewport(1000.f, 800.f);
        Editor   editor;
        editor.Bind(doc, viewport);

        editor.ActivateToolById("MLine");
        Check(editor.IsActiveTool(), "多线：工具能激活");
        Check(editor.GetLastCommand() == "MLine", "多线：LastCommand 记录为 MLine");

        // 关键：没有经过任何 OnInput，直接渲染。
        // 修复前这里解引用野指针 → 崩溃；修复后应安全返回。
        editor.Render();
        Check(true, "多线：激活后**未经 OnInput** 直接渲染不崩溃（GetPrompt 不碰失效的 m_ctx）");

        editor.Render();
        editor.Render();
        Check(true, "多线：连续多帧渲染仍然安全（提示文本只读缓存）");
    }

    // ══ ② 正常交互：点 3 个点 + 回车 → 生成一条多线 ═════════════════════
    {
        Document doc;
        Viewport viewport(1000.f, 800.f);
        Editor   editor;
        editor.Bind(doc, viewport);

        editor.ActivateToolById("MLine");
        editor.Render();      // 第一帧

        const Math::Point3 pts[] = { { 0, 0, 0 }, { 100, 0, 0 }, { 100, 100, 0 } };
        for (const auto& p : pts)
        {
            editor.OnInput(MakeClick(viewport, p, /*up*/false));
            editor.OnInput(MakeClick(viewport, p, /*up*/true));
            editor.Render();     // 每步都渲染一次，模拟真实逐帧
        }

        InputEvent enter = {};
        enter.Type = InputEventType::KeyDown;
        enter.Key  = KeyCode::Enter;
        editor.OnInput(enter);

        Scene& scene = doc.GetScene();
        int mlines = 0;
        for (auto id : scene.GetAllIDs())
            if (const Object* o = scene.GetEntity(id); o && o->IsKindOf<MLineEntity>())
                ++mlines;

        std::printf("    生成多线 %d 条\n", mlines);
        Check(mlines == 1, "多线：点 3 点后回车生成 1 条 MLineEntity");
        Check(doc.GetCommandStack().CanUndo(), "多线：生成走命令栈，可撤销");

        // 几何合理：Standard 样式是 ±0.5 偏移的两条线，
        // 路径 (0,0)-(100,0)-(100,100) 的包围盒应覆盖 Y 的 -0.5 与 X 的 +0.5
        bool geoOk = false;
        for (auto id : scene.GetAllIDs())
        {
            const Object* o = scene.GetEntity(id);
            if (!o || !o->IsKindOf<MLineEntity>()) continue;
            const AABB box = static_cast<const MLineEntity*>(o)->GetBoundingBox();
            std::printf("    包围盒 X[%.2f, %.2f] Y[%.2f, %.2f]\n",
                        box.Min.x, box.Max.x, box.Min.y, box.Max.y);
            geoOk = box.Min.x <= 0.01 && box.Max.x >= 100.49 &&
                    box.Min.y <= -0.49 && box.Max.y >= 99.99;
        }
        Check(geoOk, "多线：几何按样式偏移展开（包围盒覆盖两条平行线）");

        // 撤销后应消失
        doc.Undo();
        int afterUndo = 0;
        for (auto id : doc.GetScene().GetAllIDs())
            if (const Object* o = doc.GetScene().GetEntity(id); o && o->IsKindOf<MLineEntity>())
                ++afterUndo;
        Check(afterUndo == 0, "多线：撤销后实体消失");
    }

    // ══ ③ 渲染与输入交替（模拟真实主循环）不崩 ═══════════════════════════
    {
        Document doc;
        Viewport viewport(1000.f, 800.f);
        Editor   editor;
        editor.Bind(doc, viewport);

        editor.ActivateToolById("MLine");
        for (int i = 0; i < 20; ++i)
        {
            editor.Render();
            // 鼠标移动也走一遍（会触发 Refresh → overlay）
            const Math::Point2 sp = viewport.GetCamera().WorldToScreen(Math::Point3{ 10.0 * i, 5.0 * i, 0 });
            InputEvent mv = {};
            mv.Type   = InputEventType::MouseMove;
            mv.MouseX = static_cast<int>(sp.x);
            mv.MouseY = static_cast<int>(sp.y);
            editor.OnInput(mv);
        }
        Check(true, "多线：渲染与鼠标移动交替 20 轮不崩溃");
    }

    std::printf("多线工具用例失败：%d\n", g_failures);
    return g_failures;
}
