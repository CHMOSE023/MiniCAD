// ── 多线测试：样式表、元素线几何（对正 / 比例 / 斜接 / 闭合）、封口与连接线、序列化往返 ────────
#include "Core/Draw/IDrawSink.hpp"
#include "Core/Entity/MLineEntity.hpp"
#include "Scene/MLineStyleTable.h"
#include "Serialization/EntityIO.h"
#include "Serialization/JsonSerializer.h"
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
        if (!ok)
            ++g_failures;
    }

    bool Near(double a, double b) { return std::abs(a - b) < 1e-9; }
    bool Near(const Math::Point3& p, double x, double y) { return Near(p.x, x) && Near(p.y, y); }

    class CountSink : public IDrawSink
    {
    public:
        int lines = 0;
        void DrawLine(const Math::Point3&, const Math::Point3&, const Math::Color4&, bool) override { ++lines; }
    };

    MLineStyleRecord Standard() { return MLineStyleTable().Resolve(MLineStyleTable::StandardID); }
}

int RunMLineTests()
{
    g_failures = 0;

    // ── 样式表 ────────────────────────────────────────────────────────
    {
        MLineStyleTable t;
        Check(t.Records().size() == 1 && t.Find(0) && t.Find(0)->Elements.size() == 2, "预置 Standard：两条元素线");

        MLineStyleRecord wall;
        wall.Name     = "Wall";
        wall.Elements = { { -1.0, false, {} }, { 1.0, false, {} }, { 0.0, true, { 1, 0, 0, 1 } } };   // 乱序
        wall.StartCap = wall.EndCap = true;
        const MLineStyleID id = t.Add(wall);
        Check(id != MLineStyleTable::InvalidID, "新增样式");
        Check(t.Find(id)->Elements[0].Offset == 1.0 && t.Find(id)->Elements[2].Offset == -1.0, "元素线按偏移从大到小排序");
        Check(t.Add(wall) == MLineStyleTable::InvalidID, "重名（不区分大小写）被拒绝");
        wall.Name = "empty"; wall.Elements.clear();
        Check(t.Add(wall) == MLineStyleTable::InvalidID, "没有元素线的样式被拒绝");
        Check(!t.Remove(MLineStyleTable::StandardID) && !t.Rename(MLineStyleTable::StandardID, "X"), "Standard 不能删除 / 改名");
        Check(&t.Resolve(12345) == t.Find(0), "找不到的 ID 退回 Standard");

        JsonSerializer w;
        t.Serialize(w);
        MLineStyleTable back;
        JsonSerializer r;
        const bool parsed = r.Parse(w.Dump());
        if (parsed) back.Deserialize(r);
        const MLineStyleRecord* b = back.Find(id);
        Check(parsed && b && b->Name == "Wall" && b->Elements.size() == 3 && b->StartCap && b->EndCap && !b->Joints
              && b->Elements[1].UseColor && b->Elements[1].Color.r == 1.0, "样式表序列化往返");

        MLineStyleTable old;
        JsonSerializer empty;
        empty.Parse("{}");
        old.Deserialize(empty);
        Check(old.Records().size() == 1, "旧文件没有样式表时保留预置项");
    }

    // ── 几何 ──────────────────────────────────────────────────────────
    const std::vector<Math::Point3> straight = { { 0, 0, 0 }, { 10, 0, 0 } };
    {
        auto g = MLineEntity::Compute(straight, Standard(), MLineJustify::Zero, 1.0, false);
        Check(g.Elements.size() == 2 && Near(g.Elements[0][0], 0, 0.5) && Near(g.Elements[1][1], 10, -0.5), "无对正：元素线在 y = ±0.5");
    }
    {
        auto g = MLineEntity::Compute(straight, Standard(), MLineJustify::Top, 1.0, false);
        Check(Near(g.Elements[0][0], 0, 0) && Near(g.Elements[1][0], 0, -1), "上对正：偏移最大的线在路径上");
    }
    {
        auto g = MLineEntity::Compute(straight, Standard(), MLineJustify::Bottom, 1.0, false);
        Check(Near(g.Elements[0][0], 0, 1) && Near(g.Elements[1][0], 0, 0), "下对正：偏移最小的线在路径上");
    }
    {
        auto g = MLineEntity::Compute(straight, Standard(), MLineJustify::Zero, 20.0, false);
        Check(Near(g.Elements[0][0], 0, 10) && Near(g.Elements[1][0], 0, -10), "比例 20：线间距放大 20 倍");
    }
    {
        // L 形：(0,0)→(10,0)→(10,10)，左转，内角在左侧
        auto g = MLineEntity::Compute({ { 0, 0, 0 }, { 10, 0, 0 }, { 10, 10, 0 } }, Standard(), MLineJustify::Zero, 1.0, false);
        Check(Near(g.Elements[0][1], 9.5, 0.5) && Near(g.Elements[1][1], 10.5, -0.5), "90° 拐角做斜接：内角 (9.5,0.5)，外角 (10.5,-0.5)");
    }
    {
        // 闭合正方形：四个角各自斜接
        auto g = MLineEntity::Compute({ { 0, 0, 0 }, { 10, 0, 0 }, { 10, 10, 0 }, { 0, 10, 0 } }, Standard(), MLineJustify::Zero, 1.0, true);
        Check(g.Elements[0].size() == 4 && Near(g.Elements[0][0], 0.5, 0.5) && Near(g.Elements[1][0], -0.5, -0.5), "闭合：首顶点也斜接");
    }
    {
        auto g = MLineEntity::Compute({ { 0, 0, 0 }, { 0, 0, 0 }, { 10, 0, 0 }, { 10, 0, 0 } }, Standard(), MLineJustify::Zero, 1.0, false);
        Check(g.Path.size() == 2, "连续重复点被去掉");
        auto d = MLineEntity::Compute({ { 0, 0, 0 } }, Standard(), MLineJustify::Zero, 1.0, false);
        Check(d.Elements.empty(), "少于两个顶点没有几何");
    }

    // ── 实体：绘制 / 封口 / 连接线 / 包围盒 ─────────────────────────────
    {
        MLineEntity e(1, { { 0, 0, 0 }, { 10, 0, 0 }, { 10, 10, 0 } }, Standard(), MLineJustify::Zero, 1.0, false);
        CountSink s; e.Draw(s, false, false);
        Check(s.lines == 4, "两条元素线 × 两段 = 4 条线（无封口无连接线）");

        auto st = Standard();
        st.StartCap = st.EndCap = st.Joints = true;
        MLineEntity c(2, { { 0, 0, 0 }, { 10, 0, 0 }, { 10, 10, 0 } }, st, MLineJustify::Zero, 1.0, false);
        CountSink sc; c.Draw(sc, false, false);
        Check(sc.lines == 4 + 2 + 1, "加上起止封口 2 条 + 内部顶点连接线 1 条");

        MLineEntity closed(3, { { 0, 0, 0 }, { 10, 0, 0 }, { 10, 10, 0 }, { 0, 10, 0 } }, st, MLineJustify::Zero, 1.0, true);
        CountSink cl; closed.Draw(cl, false, false);
        Check(cl.lines == 8 + 4, "闭合：2 × 4 段 + 4 条连接线，没有封口");

        const auto bb = e.GetBoundingBox();
        Check(Near(bb.Min.y, -0.5) && Near(bb.Max.x, 10.5) && Near(bb.Max.y, 10.0), "包围盒包含全部元素线");
    }

    // ── 序列化 / 克隆 ─────────────────────────────────────────────────
    {
        auto st = Standard();
        st.Elements.push_back({ 0.0, true, { 0, 1, 0, 1 } });
        st.Joints = true;
        MLineEntity e(7, { { 0, 0, 0 }, { 10, 0, 0 }, { 10, 10, 0 } }, st, MLineJustify::Top, 5.0, false);

        auto clone = e.Clone(8);
        Check(clone && clone->IsKindOf<MLineEntity>() && static_cast<MLineEntity*>(clone.get())->GetScale() == 5.0, "克隆保持类型与比例");

        JsonSerializer w;
        EntityIO::Write(w, e);
        JsonSerializer r;
        auto back = r.Parse(w.Dump()) ? EntityIO::Read(r) : nullptr;
        Check(back && back->IsKindOf<MLineEntity>(), "序列化往返保持 MLineEntity 类型");
        if (back && back->IsKindOf<MLineEntity>())
        {
            auto* b = static_cast<MLineEntity*>(back.get());
            Check(b->GetVertices().size() == 3 && b->GetJustify() == MLineJustify::Top && b->GetScale() == 5.0 && !b->IsClosed()
                  && b->GetMLineStyle().Elements.size() == 3 && b->GetMLineStyle().Joints, "往返：顶点、对正、比例、样式快照一致");
        }
    }

    return g_failures;
}
