// SMLib 冒烟测试：确认静态链接的内核（SM_STATIC，无 DllMain）能建模、布尔、求体积、离散化
// 几何取解析可算的情形：长方体减去贯穿的圆柱、长方体并上半露出的球
#include <SmApiBrep.h>
#include <SmApiGeneral.h>
#include <SmApiPolygons.h>
#include <SmApiPrimitives.h>
#include <SmApiQueries.h>
#include <SmBrep.h>
#include <SmPoly.h>
#include <SmVector3d.h>
#include <cmath>
#include <cstdio>
#include <memory>

namespace
{
    int g_failed = 0;
    void Check(bool ok, const char* label)
    {
        std::printf("[%s] %s\n", ok ? "PASS" : "FAIL", label);
        g_failed += ok ? 0 : 1;
    }

    constexpr double kPi = 3.14159265358979323846;

    bool NearRel(double a, double b, double rel) { return std::abs(a - b) <= std::abs(b) * rel; }

    using BrepPtr = std::unique_ptr<SmBrep>;
    using PolyBrepPtr = std::unique_ptr<SmPolyBrep>;

    BrepPtr MakeBox(double size)
    {
        SmBrep* raw = nullptr;
        const SmApiStatus status = SmApiCreateBox(SmVector3d(0, 0, 0), size, size, size, raw);
        BrepPtr brep(raw);
        return status == SM_SUCCESS ? std::move(brep) : nullptr;
    }

    double Volume(const SmBrep* brep)
    {
        double volume = -1.0;
        return SmApiBrepComputeVolume(brep, 1.0e-7, volume) == SM_SUCCESS ? volume : -1.0;
    }

    // 布尔运算成功时 B 被删除、A 原地改成结果；失败时两者都还在（可能已被修改）
    bool Boolean(SmApiStatus (*op)(SmBrep*, SmBrep*, SmBrep*&), BrepPtr& a, BrepPtr& b)
    {
        SmBrep* result = nullptr;
        const SmApiStatus status = op(a.get(), b.get(), result);
        if (status != SM_SUCCESS)
            return false;
        b.release();
        return result == a.get();
    }

    void TestBoxVolume()
    {
        BrepPtr box = MakeBox(10.0);
        Check(box != nullptr, "SmApiCreateBox");
        Check(box && NearRel(Volume(box.get()), 1000.0, 1e-6), "box volume = 1000");

        SmBoolean manifold = FALSE;
        Check(box && SmApiBrepIsManifoldSolid(box.get(), manifold) == SM_SUCCESS && manifold, "box is a manifold solid");
    }

    void TestDifference()
    {
        BrepPtr box = MakeBox(10.0);
        SmBrep* rawTool = nullptr;
        const SmApiStatus status = SmApiCreateCylinder(SmVector3d(5, 5, -1), 2.0, 12.0, rawTool);
        BrepPtr tool(rawTool);
        Check(box && status == SM_SUCCESS && tool, "SmApiCreateCylinder");
        if (!box || !tool)
            return;

        Check(Boolean(SmApiBooleanDifference, box, tool), "box - cylinder succeeds");
        const double expected = 1000.0 - kPi * 2.0 * 2.0 * 10.0;
        Check(NearRel(Volume(box.get()), expected, 1e-5), "difference volume = 1000 - 40π");
    }

    void TestUnion()
    {
        BrepPtr box = MakeBox(10.0);
        SmBrep* rawSphere = nullptr;
        const SmApiStatus status = SmApiCreateSphere(SmVector3d(10, 5, 5), 3.0, rawSphere);
        BrepPtr sphere(rawSphere);
        Check(box && status == SM_SUCCESS && sphere, "SmApiCreateSphere");
        if (!box || !sphere)
            return;

        // 球心在长方体的面上：并集多出半个球
        Check(Boolean(SmApiBooleanUnion, box, sphere), "box + sphere succeeds");
        const double expected = 1000.0 + 0.5 * 4.0 / 3.0 * kPi * 27.0;
        Check(NearRel(Volume(box.get()), expected, 1e-5), "union volume = 1000 + 18π");
    }

    void TestTessellate()
    {
        BrepPtr box = MakeBox(10.0);
        SmBrep* rawSphere = nullptr;
        SmApiCreateSphere(SmVector3d(10, 5, 5), 3.0, rawSphere);
        BrepPtr sphere(rawSphere);
        if (!box || !sphere || !Boolean(SmApiBooleanUnion, box, sphere))
        {
            Check(false, "tessellate: build input");
            return;
        }

        SmPolyBrep* rawMesh = nullptr;
        const SmApiStatus status = SmApiTessellate(box.get(), rawMesh);
        PolyBrepPtr mesh(rawMesh);
        Check(status == SM_SUCCESS && mesh, "SmApiTessellate");
        if (!mesh)
            return;

        double meshVolume = 0.0;
        Check(SmApiPolyBrepComputeVolume(mesh.get(), meshVolume) == SM_SUCCESS, "SmApiPolyBrepComputeVolume");
        // 网格是弦高近似，体积与精确值相差应在 1% 内
        Check(NearRel(meshVolume, Volume(box.get()), 1e-2), "mesh volume close to exact volume");

        SmBoolean manifold = FALSE;
        Check(SmApiPolyBrepIsManifoldSolid(mesh.get(), manifold) == SM_SUCCESS && manifold, "mesh is closed");
    }
}

int main()
{
    SmApiCreateContext();

    TestBoxVolume();
    TestDifference();
    TestUnion();
    TestTessellate();

    std::printf("\n%d failed\n", g_failed);
    return g_failed;
}
