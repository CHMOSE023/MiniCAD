// ── 区域覆盖测试：顶点流裁剪（线 / 填充 / 文字）、实体绘制、序列化往返 ─────────────
#include "Core/Draw/IDrawSink.hpp"
#include "Core/Entity/InsertEntity.hpp"
#include "Core/Entity/LineEntity.hpp"
#include "Core/Entity/WipeoutEntity.hpp"
#include "Document/DrawContext.hpp"
#include "Document/WipeClip.hpp"
#include "Scene/Scene.h"
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

    Vertex_P3_C4 V(float x, float y) { return { { x, y, 0 }, { 1, 1, 1, 1 } }; }

    class WipeSink : public IDrawSink
    {
    public:
        int wipes = 0;
        int lines = 0;
        void DrawLine(const Math::Point3&, const Math::Point3&, const Math::Color4&, bool) override { ++lines; }
        void Wipe(const std::vector<Math::Point3>&) override { ++wipes; }
    };

    // 10×10 的方形区域
    const std::vector<Math::Point3> kSquare = { { 0, 0, 0 }, { 10, 0, 0 }, { 10, 10, 0 }, { 0, 10, 0 } };

    double Area(const std::vector<Vertex_P3_C4>& t)
    {
        double a = 0;
        for (size_t k = 0; k + 2 < t.size(); k += 3)
            a += std::abs((t[k + 1].pos.x - t[k].pos.x) * (t[k + 2].pos.y - t[k].pos.y)
                        - (t[k + 2].pos.x - t[k].pos.x) * (t[k + 1].pos.y - t[k].pos.y)) * 0.5;
        return a;
    }
}

int RunWipeoutTests()
{
    g_failures = 0;
    const WipeClip::Poly poly(kSquare);

    // ── 线 ────────────────────────────────────────────────────────────
    {
        std::vector<Vertex_P3_C4> v = { V(-5, 5), V(15, 5) };            // 穿过区域
        WipeClip::ClipLines(v, poly);
        Check(v.size() == 4 && std::abs(v[1].pos.x) < 1e-4 && std::abs(v[2].pos.x - 10) < 1e-4, "穿过区域的线被截成两段");
    }
    {
        std::vector<Vertex_P3_C4> v = { V(2, 2), V(8, 8) };              // 完全在内
        WipeClip::ClipLines(v, poly);
        Check(v.empty(), "完全在区域内的线被删除");
    }
    {
        std::vector<Vertex_P3_C4> v = { V(-5, -5), V(-1, -1), V(20, 20), V(30, 20) };   // 完全在外
        WipeClip::ClipLines(v, poly);
        Check(v.size() == 4, "区域外的线保持不变");
    }

    // ── 填充 ──────────────────────────────────────────────────────────
    {
        std::vector<Vertex_P3_C4> v = { V(2, 2), V(8, 2), V(5, 8) };     // 完全在内
        WipeClip::ClipFills(v, poly);
        Check(v.empty(), "完全在区域内的三角形被删除");
    }
    {
        std::vector<Vertex_P3_C4> v = { V(20, 0), V(30, 0), V(25, 10) }; // 完全在外
        WipeClip::ClipFills(v, poly);
        Check(v.size() == 3, "区域外的三角形保持不变");
    }
    {
        // 大三角形盖住整个方形：剩余面积 = 总面积 − 100，允许细分带来的边缘误差
        std::vector<Vertex_P3_C4> v = { V(-20, -20), V(40, -20), V(-20, 40) };
        const double total = Area(v);
        WipeClip::ClipFills(v, poly, 7);
        const double left = Area(v);
        Check(std::abs((total - left) - 100.0) < 8.0, "跨区域的三角形剩余面积 ≈ 总面积 − 区域面积");
    }

    // ── 文字 ──────────────────────────────────────────────────────────
    {
        std::vector<Vertex_P3_C4_UV> v;
        auto quad = [&](float cx, float cy)
        {
            for (int i = 0; i < 6; ++i)
                v.push_back({ { cx + (i % 2), cy + (i % 3), 0 }, { 1, 1, 1, 1 }, { 0, 0 } });
        };
        quad(3, 3);      // 在内
        quad(30, 30);    // 在外
        WipeClip::ClipTexts(v, poly);
        Check(v.size() == 6 && v[0].pos.x > 20, "区域内的字符被删除，区域外的保留");
    }

    // ── 实体 ──────────────────────────────────────────────────────────
    WipeoutEntity w(1, kSquare);
    Check(w.GetPolyline().Points.size() == 5, "首尾不重合时自动补上闭合点");

    WipeSink s;
    w.Draw(s, false, false);
    Check(s.wipes == 1 && s.lines == 4, "绘制：通知一次区域覆盖并画 4 条边框");

    w.SetShowFrame(false);
    WipeSink s2;
    w.Draw(s2, false, false);
    Check(s2.wipes == 1 && s2.lines == 0, "关闭边框后只剩遮挡");

    auto clone = w.Clone(2);
    Check(clone && clone->IsKindOf<WipeoutEntity>() && !static_cast<WipeoutEntity*>(clone.get())->GetShowFrame(), "克隆保持类型与边框设置");

    // ── 块内的区域覆盖：经插入变换，并盖住同一块里先画的图形（后画的不受影响）────────
    {
        Scene scene;
        const BlockID blk = scene.GetBlockTable().Create("KTTG", { 0, 0, 0 });
        BlockEntity* def = scene.GetBlockTable().Find(blk);
        def->AddEntity(std::make_unique<LineEntity>(scene.NextObjectID(), Math::Point3{ -5, 5, 0 }, Math::Point3{ 15, 5, 0 }));
        auto mask = std::make_unique<WipeoutEntity>(scene.NextObjectID(), kSquare);
        mask->SetShowFrame(false);
        def->AddEntity(std::move(mask));
        def->AddEntity(std::make_unique<LineEntity>(scene.NextObjectID(), Math::Point3{ 5, -5, 0 }, Math::Point3{ 5, 15, 0 }));
        InsertEntity ins(scene.NextObjectID(), blk, "KTTG", Math::Point3{ 100, 0, 0 });
        scene.ResolveInsert(ins);

        std::vector<Vertex_P3_C4> lines, fills;
        std::vector<Vertex_P3_C4_UV> texts;
        Overlay overlay;
        DrawContext ctx(lines, fills, texts, overlay, scene.GetLayerManager(), scene.GetLineTypeTable());
        lines.push_back(V(0, 0));   // 之前实体的顶点：不归本实体裁剪
        lines.push_back(V(200, 0));
        ctx.BeginEntity(&ins.GetAttr());
        ins.Draw(ctx, false, false);
        ctx.BeginEntity(nullptr);
        const auto wipes = ctx.TakeWipes();

        double horizontal = 0, vertical = 0;
        for (size_t k = 2; k + 1 < lines.size(); k += 2)
        {
            const auto& a = lines[k].pos;
            const auto& b = lines[k + 1].pos;
            if (a.y == b.y) horizontal += std::abs(b.x - a.x);
            else vertical += std::abs(b.y - a.y);
        }
        Check(lines[0].pos.x == 0 && lines[1].pos.x == 200, "块内遮罩不裁剪之前实体的顶点（由拼接顶点流时处理）");
        Check(std::abs(horizontal - 10) < 1e-4, "块内遮罩盖住块里先画的线（经插入变换到 x = 100～110）");
        Check(std::abs(vertical - 20) < 1e-4, "块里后画的线不受遮罩影响");
        Check(wipes.size() == 1 && std::abs(wipes[0][0].x - 100) < 1e-9, "遮罩边界经插入变换后交给场景拼接");
    }

    // ── 批量裁剪：每个遮罩只裁剪它出现之前的顶点，结果与逐个裁剪一致 ──────────
    {
        // 线：第一条在两个遮罩之前，第二条只在第二个遮罩之前
        std::vector<Vertex_P3_C4> v = { V(-5, 5), V(35, 5), V(-5, 6), V(35, 6) };
        WipeClip::Batch b(1);
        b.Add(kSquare, { 2 });
        b.Add({ { 20, 0, 0 }, { 30, 0, 0 }, { 30, 10, 0 }, { 20, 10, 0 } }, { 4 });
        b.ClipLines(v, 0);
        double len5 = 0, len6 = 0;
        for (size_t k = 0; k + 1 < v.size(); k += 2)
            (v[k].pos.y == 5 ? len5 : len6) += std::abs(v[k + 1].pos.x - v[k].pos.x);
        Check(std::abs(len5 - 20) < 1e-4, "Batch::ClipLines：前面的线被两个遮罩都裁剪");
        Check(std::abs(len6 - 30) < 1e-4, "Batch::ClipLines：后面的线只被之后的遮罩裁剪");
    }
    {
        // 填充：20×20 的两个三角形，被两个相交的凸遮罩精确扣掉并集（面积 400 - 100 - 100 + 25）
        std::vector<Vertex_P3_C4> v = { V(-5, -5), V(15, -5), V(15, 15), V(-5, -5), V(15, 15), V(-5, 15) };
        WipeClip::Batch b(1);
        b.Add(kSquare, { 6 });
        b.Add({ { 5, 5, 0 }, { 15, 5, 0 }, { 15, 15, 0 }, { 5, 15, 0 } }, { 6 });
        b.ClipFills(v, 0);
        Check(std::abs(Area(v) - 225) < 1e-3, "Batch::ClipFills：凸遮罩精确扣除并集");
        Check(v.size() < 200, "Batch::ClipFills：凸遮罩不细分三角形");

        // 顺时针给出的遮罩同样按凸多边形处理
        std::vector<Vertex_P3_C4> w = { V(-5, -5), V(15, -5), V(15, 15) };
        WipeClip::Batch cw(1);
        cw.Add({ { 0, 10, 0 }, { 10, 10, 0 }, { 10, 0, 0 }, { 0, 0, 0 } }, { 3 });
        cw.ClipFills(w, 0);
        Check(std::abs(Area(w) - 150) < 1e-3, "Batch::ClipFills：顺时针遮罩");

        // 非凸（L 形）遮罩走细分，面积误差在细分精度内
        std::vector<Vertex_P3_C4> u = { V(0, 0), V(20, 0), V(20, 20), V(0, 0), V(20, 20), V(0, 20) };
        WipeClip::Batch nc(1);
        nc.Add({ { 0, 0, 0 }, { 10, 0, 0 }, { 10, 5, 0 }, { 5, 5, 0 }, { 5, 10, 0 }, { 0, 10, 0 } }, { 6 });
        nc.ClipFills(u, 0, 7);
        Check(std::abs(Area(u) - 325) < 2.0, "Batch::ClipFills：非凸遮罩细分裁剪");
    }
    {
        std::vector<Vertex_P3_C4_UV> t(12);
        for (int i = 0; i < 6; ++i) { t[i].pos = { 5, 5, 0 }; t[6 + i].pos = { 50, 5, 0 }; }
        WipeClip::Batch b(1);
        b.Add(kSquare, { 12 });
        b.ClipTexts(t, 0);
        Check(t.size() == 6 && t[0].pos.x == 50, "Batch::ClipTexts：删去中心在遮罩内的字符");
    }

    // 只裁剪 from 之后的顶点
    {
        std::vector<Vertex_P3_C4> v = { V(-5, 5), V(15, 5), V(-5, 6), V(15, 6) };
        WipeClip::ClipLines(v, WipeClip::Poly(kSquare), 2);
        Check(v.size() == 6 && v[0].pos.x == -5 && v[1].pos.x == 15, "ClipLines：from 之前的线段保持不变");
    }

    JsonSerializer ws;
    EntityIO::Write(ws, w);
    JsonSerializer rs;
    auto e = rs.Parse(ws.Dump()) ? EntityIO::Read(rs) : nullptr;
    Check(e && e->IsKindOf<WipeoutEntity>(), "序列化往返保持 WipeoutEntity 类型");
    if (e && e->IsKindOf<WipeoutEntity>())
    {
        auto* r = static_cast<WipeoutEntity*>(e.get());
        Check(r->GetPolyline().Points.size() == 5 && !r->GetShowFrame(), "序列化往返顶点与边框设置一致");
    }

    return g_failures;
}
