// ── 标注测试：弧长、折弯半径的测量 / 文字 / 几何，镜像，包围盒，序列化往返 ─────────
#include "Core/Draw/IDrawSink.hpp"
#include "Core/Entity/DimensionEntity.hpp"
#include "Document/Command/EntityMirror.h"
#include "Serialization/EntityIO.h"
#include "Serialization/JsonSerializer.h"
#include <cmath>
#include <cstdio>
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

    bool Near(double a, double b, double eps = 1e-6) { return std::abs(a - b) <= eps; }
    bool Near(const Math::Point3& a, const Math::Point3& b, double eps = 1e-6) { return Near(a.x, b.x, eps) && Near(a.y, b.y, eps); }

    struct Seg { Math::Point3 a, b; };

    class RecordingSink : public IDrawSink
    {
    public:
        std::vector<Seg> lines;
        std::string      text;
        void DrawLine(const Math::Point3& a, const Math::Point3& b, const Math::Color4&, bool) override { lines.push_back({ a, b }); }
        void EmitMText(const Math::Point3&, const std::string& t, uint32_t, double, double, double, const Math::Color4&) override { text = t; }
    };

    RecordingSink Draw(const DimensionEntity& d)
    {
        RecordingSink s;
        d.Draw(s, false, false);
        return s;
    }

    bool HasSeg(const RecordingSink& s, const Math::Point3& a, const Math::Point3& b)
    {
        for (const auto& l : s.lines)
            if ((Near(l.a, a) && Near(l.b, b)) || (Near(l.a, b) && Near(l.b, a)))
                return true;
        return false;
    }

    std::unique_ptr<DimensionEntity> RoundTrip(const DimensionEntity& d)
    {
        JsonSerializer w;
        EntityIO::Write(w, d);
        JsonSerializer r;
        auto e = r.Parse(w.Dump()) ? EntityIO::Read(r) : nullptr;
        if (!e || !e->IsKindOf<DimensionEntity>()) return nullptr;
        return std::unique_ptr<DimensionEntity>(static_cast<DimensionEntity*>(e.release()));
    }
}

int RunDimensionTests()
{
    g_failures = 0;
    std::printf("── 标注 ──\n");
    const double s45 = std::sqrt(0.5);

    // ── 1. 弧长 ─────────────────────────────────────────────────
    {
        // 圆心原点、半径 10，从 0° 到 90° 的弧
        auto out = DimensionEntity::MakeArcLength(1, { 0, 0, 0 }, { 10, 0, 0 }, { 0, 10, 0 }, { 15 * s45, 15 * s45, 0 });
        Check(Near(out->Measurement(), 10.0 * Math::HalfPI), "弧长：90° 弧、半径 10，测量值 5π");

        const RecordingSink s = Draw(*out);
        Check(s.text == "\xE2\x8C\x92" "16", "弧长文字带 ⌒ 前缀，按精度取整为 16");

        // 标注弧在外侧（r=15）：尺寸界线沿径向从 10+偏移 引到 15+超出
        const DimStyle st;
        Check(HasSeg(s, { 10 + st.ExtLineOffset, 0, 0 }, { 15 + st.ExtLineExtend, 0, 0 }), "外侧标注：尺寸界线沿径向向外引出");

        auto in = DimensionEntity::MakeArcLength(2, { 0, 0, 0 }, { 10, 0, 0 }, { 0, 10, 0 }, { 5 * s45, 5 * s45, 0 });
        const RecordingSink si = Draw(*in);
        Check(Near(in->Measurement(), out->Measurement()) && HasSeg(si, { 10 - st.ExtLineOffset, 0, 0 }, { 5 - st.ExtLineExtend, 0, 0 }),
              "内侧标注：测量值不变，尺寸界线向内引出");

        auto reflex = DimensionEntity::MakeArcLength(3, { 0, 0, 0 }, { 10, 0, 0 }, { 0, 10, 0 }, { -15 * s45, -15 * s45, 0 });
        Check(Near(reflex->Measurement(), 10.0 * 1.5 * Math::PI), "标注弧点在另一侧：标注 270° 那段弧");

        // 镜像：p1→p2 的方向反转，但被测弧由标注弧点确定，测量值不变
        MirrorAxis axis{ { 0, 0, 0 }, { 0, 1, 0 } };
        MirrorEntityInPlace(*out, axis);
        Check(Near(out->Measurement(), 10.0 * Math::HalfPI), "镜像后弧长测量值不变");

        auto back = RoundTrip(*reflex);
        Check(back && back->GetType() == DimType::ArcLength && Near(back->Measurement(), reflex->Measurement()), "弧长 JSON 往返");
    }

    // ── 2. 折弯半径 ─────────────────────────────────────────────
    {
        // 真实圆心远在左侧 (-1000,0)，弧上箭头点 (100,0)，替代圆心 (60,20)，折弯在 x=90
        auto d = DimensionEntity::MakeJoggedRadius(10, { -1000, 0, 0 }, { 100, 0, 0 }, { 60, 20, 0 }, { 90, 5, 0 });
        Check(Near(d->Measurement(), 1100.0), "折弯半径：测量值为真实半径 1100");

        const RecordingSink s = Draw(*d);
        Check(s.text == "R1100", "折弯半径文字带 R 前缀");
        Check(HasSeg(s, { 60, 20, 0 }, { 70, 20, 0 }) && HasSeg(s, { 70, 20, 0 }, { 90, 0, 0 }) && HasSeg(s, { 90, 0, 0 }, { 100, 0, 0 }),
              "替代圆心 → 平行段 → 45° 折弯段 → 径向段到箭头");

        const AABB box = d->GetBoundingBox();
        Check(box.Min.x > -100.0, "包围盒不包含远处的真实圆心");

        // 折弯点被夹在替代圆心与弧之间
        auto clamp = DimensionEntity::MakeJoggedRadius(11, { -1000, 0, 0 }, { 100, 0, 0 }, { 60, 20, 0 }, { 500, 0, 0 });
        Check(Near(clamp->DimLineP1(), { 100, 0, 0 }), "折弯位置超出弧时夹到弧上");

        auto back = RoundTrip(*d);
        Check(back && back->GetType() == DimType::JoggedRadius && Near(back->Measurement(), 1100.0)
                   && Near(back->GetP2(), { 60, 20, 0 }), "折弯半径 JSON 往返");
    }

    // ── 3. 已有类型不受影响 ─────────────────────────────────────
    {
        auto ang = DimensionEntity::MakeAngular(20, { 0, 0, 0 }, { 10, 0, 0 }, { 0, 10, 0 }, { 5, 5, 0 });
        Check(Near(ang->Measurement(), Math::HalfPI) && Draw(*ang).text == "90\xC2\xB0", "角度标注：90°");
        auto rad = DimensionEntity::MakeRadius(21, { 0, 0, 0 }, { 3, 4, 0 });
        Check(Near(rad->Measurement(), 5.0) && Draw(*rad).text == "R5", "半径标注：R5");
    }

    return g_failures;
}
