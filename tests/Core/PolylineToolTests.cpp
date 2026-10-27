// ── 多段线工具（PLINE）：C 闭合 ─────────────────────────────────────────
//
// 用户报「多段线绘制时缺少 C 闭合命令」。命令行里单个字母作为选项键交给工具（CommandLine.cpp），
// 多段线工具原来只认 L / A。现在 C 用直线段（圆弧模式下给了弧上点时用经过该点的圆弧）连回起点并提交，
// 得到 MiniCAD 约定的闭合多段线（末点与起点重合），拉伸等命令能把它当作轮廓。
#include "Editor/Editor.h"
#include "Editor/Input/InputEvent.h"
#include "Document/Document.h"
#include "Viewport/Viewport.h"
#include "Scene/Scene.h"
#include "Core/Entity/PolylineEntity.hpp"
#include <cmath>
#include <cstdio>

using namespace MiniCAD;

namespace
{
    int g_failures = 0;
    void Check(bool ok, const char* what)
    {
        std::printf("[%s] %s\n", ok ? "通过" : "失败", what);
        if (!ok) ++g_failures;
    }

    void Click(Editor& editor, Viewport& vp, const Math::Point3& world)
    {
        const Math::Point2 sp = vp.GetCamera().WorldToScreen(world);
        InputEvent e = {};
        e.MouseX = static_cast<int>(sp.x);
        e.MouseY = static_cast<int>(sp.y);
        e.Button = MouseButton::Left;
        e.Type   = InputEventType::MouseButtonDown;
        e.MouseButtons = static_cast<uint8_t>(MouseButtonState::Left);
        editor.OnInput(e);
        e.Type   = InputEventType::MouseButtonUp;
        e.MouseButtons = 0;
        editor.OnInput(e);
        editor.Render();
    }

    void Key(Editor& editor, KeyCode key)
    {
        InputEvent e = {};
        e.Type = InputEventType::KeyDown;
        e.Key  = key;
        editor.OnInput(e);
    }

    const Polyline* OnlyPolyline(Scene& scene, int& count)
    {
        const Polyline* pl = nullptr;
        count = 0;
        for (auto id : scene.GetAllIDs())
            if (const Object* o = scene.GetEntity(id); o && o->IsKindOf<PolylineEntity>())
            {
                pl = &static_cast<const PolylineEntity*>(o)->GetPolyline();
                ++count;
            }
        return pl;
    }
}

int RunPolylineToolTests()
{
    g_failures = 0;

    // 命令行输入 AutoCAD 全名 / 缩写都能启动多段线（及其他全名与工具名不同的命令）
    {
        Document doc;
        Viewport viewport(1000.f, 800.f);
        Editor   editor;
        editor.Bind(doc, viewport);
        Check(editor.ResolveCommandAlias("PLINE") == "Polyline" && editor.ResolveCommandAlias("pl") == "Polyline" &&
              editor.ResolveCommandAlias("polyline") == "Polyline", "命令行：PLINE / PL / POLYLINE 都是多段线");
        Check(editor.ResolveCommandAlias("RECTANG") == "Rectangle" && editor.ResolveCommandAlias("DTEXT") == "Text" &&
              editor.ResolveCommandAlias("DIMLINEAR") == "Dimension" && editor.ResolveCommandAlias("DIMALIGNED") == "Dimension",
              "命令行：RECTANG、DTEXT、DIMLINEAR、DIMALIGNED");
        editor.RunCommand("PLINE");
        editor.Render();
        for (const Math::Point3& p : { Math::Point3{ 0, 0, 0 }, Math::Point3{ 100, 0, 0 } })
            Click(editor, viewport, p);
        Key(editor, KeyCode::Enter);
        int count = 0;
        Check(OnlyPolyline(doc.GetScene(), count) && count == 1, "命令行 PLINE：画出多段线");
    }

    // ① 三点 + C：闭合三角形（4 个顶点，末点与起点重合，闭合段是直线）
    {
        Document doc;
        Viewport viewport(1000.f, 800.f);
        Editor   editor;
        editor.Bind(doc, viewport);
        editor.ActivateToolById("Polyline");
        editor.Render();
        for (const Math::Point3& p : { Math::Point3{ 0, 0, 0 }, Math::Point3{ 100, 0, 0 }, Math::Point3{ 100, 100, 0 } })
            Click(editor, viewport, p);
        Key(editor, KeyCode::C);

        int count = 0;
        const Polyline* pl = OnlyPolyline(doc.GetScene(), count);
        Check(count == 1 && pl, "多段线 C：生成 1 条多段线");
        Check(pl && pl->Points.size() == 4 && pl->Points.front() == pl->Points.back(), "多段线 C：闭合（末点与起点重合）");
        Check(pl && pl->Bulges.size() == 3 && std::abs(pl->Bulges.back()) < 1e-12, "多段线 C：闭合段是直线");
    }

    // ② 两点 + C：顶点不足，不闭合、不提交，回车再提交为不闭合的多段线
    {
        Document doc;
        Viewport viewport(1000.f, 800.f);
        Editor   editor;
        editor.Bind(doc, viewport);
        editor.ActivateToolById("Polyline");
        editor.Render();
        Click(editor, viewport, { 0, 0, 0 });
        Click(editor, viewport, { 100, 0, 0 });
        Key(editor, KeyCode::C);
        int count = 0;
        OnlyPolyline(doc.GetScene(), count);
        Check(count == 0, "多段线 C：只有 2 个直线顶点时不闭合");
        Key(editor, KeyCode::Enter);
        const Polyline* pl = OnlyPolyline(doc.GetScene(), count);
        Check(count == 1 && pl && pl->Points.size() == 2, "多段线：回车提交不闭合的多段线");
    }

    // ③ 圆弧模式：起点、终点后切到 A，给出弧上点再按 C → 经过弧上点的圆弧连回起点
    {
        Document doc;
        Viewport viewport(1000.f, 800.f);
        Editor   editor;
        editor.Bind(doc, viewport);
        editor.ActivateToolById("Polyline");
        editor.Render();
        Click(editor, viewport, { 0, 0, 0 });
        Click(editor, viewport, { 100, 0, 0 });
        Key(editor, KeyCode::A);
        Click(editor, viewport, { 50, 50, 0 });
        Key(editor, KeyCode::C);
        int count = 0;
        const Polyline* pl = OnlyPolyline(doc.GetScene(), count);
        Check(count == 1 && pl && pl->Points.size() == 3 && pl->Points.front() == pl->Points.back(), "多段线 C（圆弧）：两点 + 弧上点闭合");
        Check(pl && pl->Bulges.size() == 2 && std::abs(std::abs(pl->Bulges.back()) - 1.0) < 1e-2, "多段线 C（圆弧）：闭合段是经过弧上点的半圆");
    }

    return g_failures;
}
