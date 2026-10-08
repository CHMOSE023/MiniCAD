// ── 表格测试：几何（含旋转）、命中单元格、绘制、夹点拖拽、编辑命令撤销、序列化往返 ──────────
#include "Core/Draw/IDrawSink.hpp"
#include "Core/Entity/TableEntity.hpp"
#include "Document/Command/EditTextCommand.h"
#include "Editor/Grip/TableGripHandler.h"
#include "Scene/Scene.h"
#include "Serialization/EntityIO.h"
#include "Serialization/JsonSerializer.h"
#include <cmath>
#include <cstdio>
#include <memory>
#include <string>
#include <vector>

using namespace MiniCAD;

namespace
{
    int g_failures = 0;

    void Check(bool ok, const char* what)
    {
        std::printf("[%s] %s\n", ok ? "通过" : "失败", what);
        if (!ok)
            ++g_failures;
    }

    bool Near(double a, double b) { return std::abs(a - b) < 1e-9; }

    class CountSink : public IDrawSink
    {
    public:
        int lines = 0;
        std::vector<std::string> texts;
        std::vector<double>      boxWidths;
        void DrawLine(const Math::Point3&, const Math::Point3&, const Math::Color4&, bool) override { ++lines; }
        void EmitMText(const Math::Point3&, const std::string& t, uint32_t, double, double, double bw, const Math::Color4&) override
        {
            texts.push_back(t);
            boxWidths.push_back(bw);
        }
    };

    // 3 行 × 2 列：列宽 10 / 20，行高 5 / 6 / 7，字高 2，左上角 (100, 200)
    std::unique_ptr<TableEntity> Make()
    {
        return std::make_unique<TableEntity>(1, Math::Point3{ 100, 200, 0 }, std::vector<double>{ 10, 20 },
                                             std::vector<double>{ 5, 6, 7 }, 2.0);
    }
}

int RunTableTests()
{
    g_failures = 0;

    auto t = Make();
    Check(t->RowCount() == 3 && t->ColCount() == 2, "行列数");
    Check(Near(t->TotalWidth(), 30) && Near(t->TotalHeight(), 18), "总宽 30、总高 18");
    Check(Near(t->ColEdge(1), 10) && Near(t->ColEdge(2), 30) && Near(t->RowEdge(2), 11), "列 / 行边界");

    const auto bb = t->GetBoundingBox();
    Check(Near(bb.Min.x, 100) && Near(bb.Max.x, 130) && Near(bb.Min.y, 182) && Near(bb.Max.y, 200), "包围盒：向右向下延伸");

    // ── 命中单元格 ─────────────────────────────────────────────────────
    size_t r = 9, c = 9;
    Check(t->HitCell({ 105, 197, 0 }, r, c) && r == 0 && c == 0, "命中 (0,0)");
    Check(t->HitCell({ 125, 190, 0 }, r, c) && r == 1 && c == 1, "命中 (1,1)");
    Check(t->HitCell({ 110.5, 183, 0 }, r, c) && r == 2 && c == 1, "列边界上取右侧列，最后一行");
    Check(!t->HitCell({ 99, 190, 0 }, r, c) && !t->HitCell({ 105, 201, 0 }, r, c), "表格外不命中");

    // ── 旋转 90°（逆时针）：表格向上、向右延伸 ──────────────────────────
    {
        auto rot = Make();
        rot->SetRotation(3.14159265358979323846 / 2);
        const auto p = rot->ToWorld(30, 0);          // 局部右上角
        Check(std::abs(p.x - 100) < 1e-9 && std::abs(p.y - 230) < 1e-9, "旋转 90°：局部 x 轴朝世界 +y");
        Check(rot->HitCell({ 105, 205, 0 }, r, c) && r == 0 && c == 0, "旋转后命中 (0,0)");
        const auto b = rot->GetBoundingBox();
        Check(std::abs(b.Min.x - 100) < 1e-9 && std::abs(b.Max.x - 118) < 1e-9 && std::abs(b.Min.y - 200) < 1e-9 && std::abs(b.Max.y - 230) < 1e-9, "旋转后的包围盒");
    }

    // ── 绘制 ───────────────────────────────────────────────────────────
    t->SetCellText(0, 0, "A");
    t->SetCellText(1, 1, "第一行\\P第二行");
    t->SetCellText(2, 0, "right");
    t->SetCellAlign(2, 0, TableAlign::Right);
    {
        CountSink s;
        t->Draw(s, false, false);
        Check(s.lines == (3 + 1) + (2 + 1), "网格：行线 4 + 列线 3");
        Check(s.texts.size() == 3 && s.texts[0] == "A" && s.texts[1] == "第一行\n第二行", "只输出非空单元格，内联换行码已处理");
        Check(Near(s.boxWidths[0], 10 - 2 * t->Margin()) && Near(s.boxWidths[2], 0), "左对齐按单元格宽折行，靠右不折行");
    }

    // ── 夹点：移动 / 拖列宽 / 拖行高 ────────────────────────────────────
    {
        TableGripHandler h;
        std::vector<Grip> grips;
        h.BuildGrips(t.get(), grips);
        Check(grips.size() == 1 + 2 + 3, "夹点数 = 1 + 列数 + 行数");

        Grip colGrip{};
        for (const auto& g : grips) if (g.GripType == Grip::Type::Mid && g.SubIndex == 0) colGrip = g;
        Check(Near(colGrip.WorldPos.x, 110) && Near(colGrip.WorldPos.y, 200), "第 0 列夹点在列右边界的顶边上");

        auto st = h.BeginDrag(t.get(), colGrip);
        h.UpdateDrag(t.get(), st.get(), colGrip, { 118, 150, 0 }, grips);
        Check(Near(t->ColWidths()[0], 18) && Near(t->ColWidths()[1], 20), "拖列宽：只改该列，其余不变");
        Check(Near(t->TotalWidth(), 38), "表格总宽随之变化");

        DragEntityEntry entry{};
        Check(h.EndDrag(t.get(), st.get(), entry) && entry.Kind == DragEntityEntry::Kind::Table
              && Near(entry.BeforeTable.ColWidths[0], 10) && Near(entry.AfterTable.ColWidths[0], 18), "EndDrag：生成带前后快照的条目");

        h.UpdateDrag(t.get(), st.get(), colGrip, { 100.0001, 150, 0 }, grips);
        Check(t->ColWidths()[0] > 0.0, "拖到边界之外时列宽有下限，不会为零或为负");
        h.CancelDrag(t.get(), st.get());
        Check(Near(t->ColWidths()[0], 10), "CancelDrag 还原");

        Grip rowGrip{};
        for (const auto& g : grips) if (g.GripType == Grip::Type::End && g.SubIndex == 1) rowGrip = g;
        auto st2 = h.BeginDrag(t.get(), rowGrip);
        h.UpdateDrag(t.get(), st2.get(), rowGrip, { 100, 185, 0 }, grips);       // 第 1 行下边界拖到 y=185：5 + h1 = 15
        Check(Near(t->RowHeights()[1], 10) && Near(t->RowHeights()[2], 7), "拖行高：只改该行");
        h.CancelDrag(t.get(), st2.get());

        Grip startGrip{};
        for (const auto& g : grips) if (g.GripType == Grip::Type::Start) startGrip = g;
        auto st3 = h.BeginDrag(t.get(), startGrip);
        h.UpdateDrag(t.get(), st3.get(), startGrip, { 50, 60, 0 }, grips);
        Check(Near(t->GetPosition().x, 50) && Near(t->GetPosition().y, 60), "Start 夹点整体移动");
        h.CancelDrag(t.get(), st3.get());
    }

    // ── 编辑单元格命令 + 撤销 ──────────────────────────────────────────
    {
        Scene scene;
        const Object::ObjectID id = scene.NextObjectID();
        auto e = std::make_unique<TableEntity>(id, Math::Point3{ 0, 0, 0 }, std::vector<double>{ 10, 10 },
                                               std::vector<double>{ 5, 5 }, 2.0);
        scene.AddEntity(std::move(e));
        auto* tb = static_cast<TableEntity*>(scene.GetEntity(id));

        EditTableCellCommand cmd(id, 1, 0, "", "hello");
        Check(cmd.Execute(scene) && tb->Cell(1, 0).Text == "hello", "编辑单元格");
        cmd.Undo(scene);
        Check(tb->Cell(1, 0).Text.empty(), "撤销还原");
        EditTableCellCommand bad(id, 9, 9, "", "x");
        Check(!bad.Execute(scene), "越界的单元格编辑失败");
    }

    // ── 克隆 / 序列化 ──────────────────────────────────────────────────
    {
        t->SetRotation(0.5);
        auto clone = t->Clone(5);
        Check(clone && clone->IsKindOf<TableEntity>() && static_cast<TableEntity*>(clone.get())->Cell(1, 1).Text == t->Cell(1, 1).Text,
              "克隆保持类型与单元格内容");

        JsonSerializer w;
        EntityIO::Write(w, *t);
        JsonSerializer rd;
        auto back = rd.Parse(w.Dump()) ? EntityIO::Read(rd) : nullptr;
        Check(back && back->IsKindOf<TableEntity>(), "序列化往返保持 TableEntity 类型");
        if (back && back->IsKindOf<TableEntity>())
        {
            auto* b = static_cast<TableEntity*>(back.get());
            Check(b->RowCount() == 3 && b->ColCount() == 2 && Near(b->ColWidths()[1], 20) && Near(b->RowHeights()[2], 7)
                  && Near(b->GetRotation(), 0.5) && Near(b->GetHeight(), 2.0) && Near(b->GetPosition().x, 100),
                  "往返：行列尺寸、旋转、字高、位置一致");
            Check(b->Cell(0, 0).Text == "A" && b->Cell(1, 1).Text == "第一行\\P第二行"
                  && b->Cell(2, 0).Align == TableAlign::Right && b->Cell(0, 1).Text.empty(), "往返：单元格文字与对齐一致");
        }
    }

    return g_failures;
}
