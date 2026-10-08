// ── 二维填充测试：三角形 / 四边形的填充三角形、克隆、序列化往返 ─────────────────
#include "Core/Draw/IDrawSink.hpp"
#include "Core/Entity/SolidEntity.hpp"
#include "Serialization/EntityIO.h"
#include "Serialization/JsonSerializer.h"
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

    class CountSink : public IDrawSink
    {
    public:
        int triangles = 0;
        int lines     = 0;
        void DrawLine(const Math::Point3&, const Math::Point3&, const Math::Color4&, bool) override { ++lines; }
        void FillTriangle(const Math::Point3&, const Math::Point3&, const Math::Point3&, const Math::Color4&) override { ++triangles; }
    };
}

int RunSolidTests()
{
    g_failures = 0;

    SolidEntity quad(1, { 0, 0, 0 }, { 10, 0, 0 }, { 12, 8, 0 }, { 0, 8, 0 });
    SolidEntity tri (2, { 0, 0, 0 }, { 10, 0, 0 }, { 5, 8, 0 });

    Check(!quad.IsTriangle() && tri.IsTriangle(), "P4 == P3 即为三角形");

    CountSink sq; quad.Draw(sq, false, false);
    CountSink st; tri .Draw(st, false, false);
    Check(sq.triangles == 2 && sq.lines == 0, "四边形填 2 个三角形，不画轮廓线");
    Check(st.triangles == 1 && st.lines == 0, "三角形填 1 个三角形");

    auto bb = quad.GetBoundingBox();
    Check(bb.Max.x == 12 && bb.Max.y == 8, "包围盒覆盖四个顶点");

    auto clone = quad.Clone(9);
    Check(clone && clone->IsKindOf<SolidEntity>() && clone->IsKindOf<RectangleEntity>(), "克隆保持 SolidEntity 类型");

    JsonSerializer w;
    EntityIO::Write(w, quad);
    JsonSerializer r;
    auto e = r.Parse(w.Dump()) ? EntityIO::Read(r) : nullptr;
    Check(e && e->IsKindOf<SolidEntity>(), "序列化往返保持 SolidEntity 类型");
    if (e && e->IsKindOf<SolidEntity>())
    {
        const auto& rc = static_cast<SolidEntity*>(e.get())->GetRectangle();
        Check(rc.P3.x == 12 && rc.P4.y == 8, "序列化往返顶点一致");
    }

    return g_failures;
}
