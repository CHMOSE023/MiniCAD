// NodeSolidTests 的入口：创建唯一的实体内核（SMLib 静态库整个进程只能有一个内核对象），按顺序运行各组测试。
// 测试分在 NodeSolid*Tests.cpp，公共部分与分组见 NodeSolidCommon.h
#include "NodeSolidCommon.h"

using namespace NodeSolidTest;

int main()
{
    auto kernel = Solid::CreateSMLibKernel();
    g_kernel = kernel.get();
    TestPrimitives();
    TestExtrudeRevolveSweep();
    TestArraysAndBooleans();
    TestFilletAndSection();
    TestPreviewAndBake();
    TestCurve3D();
    TestSurfaceNodes();
    TestSurfacePreviewBakeAndFiles();
    TestN6WithKernel();
    TestN7Forms();
    TestMatchLoops();
    TestN8Surfaces();
    TestN6bCurves();
    TestN9Meshes();
    TestN10();
    TestN11Background();
    std::printf("\n%s：%d 项失败\n", g_failed == 0 ? "全部通过" : "有失败", g_failed);
    return g_failed;
}
