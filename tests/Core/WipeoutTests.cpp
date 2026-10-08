// ── 区域覆盖测试：顶点流裁剪（线 / 填充 / 文字）、实体绘制、序列化往返 ─────────────
#include "Core/Draw/IDrawSink.hpp"
#include "Core/Entity/WipeoutEntity.hpp"
#include "Document/WipeClip.hpp"
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
