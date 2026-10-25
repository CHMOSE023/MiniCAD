// NodeSolidTests（节点图的三维实体节点测试，MINICAD_WITH_SOLID）的公共部分：检查与比较函数、单个内核对象、求值夹具，
// 以及各组测试的声明（main 在 NodeSolidTests.cpp，按下面的顺序调用）。
// 新增节点的测试放进对应的文件：基础实体 / 引用 / 后台 → Basic，形体 → Form，曲线修改 → Curve，
// 空间曲线与曲面 → Surface，网格与运算转换 → Mesh；文件超过约 600 行时再按节点大类拆
#pragma once
#include "NodeGraph/Examples.h"
#include "NodeGraph/Graph.h"
#include "NodeGraph/Preview.h"
#include "Document/Document.h"
#include "Editor/Editor.h"
#include "Viewport/Viewport.h"
#include "Core/Entity/Solid3DEntity.hpp"
#include "Core/Entity/SplineEntity.hpp"
#include "Core/Entity/ArcEntity.hpp"
#include "Core/Entity/CircleEntity.hpp"
#include "Core/Entity/EllipseEntity.hpp"
#include "Core/Entity/LineEntity.hpp"
#include "Core/Entity/PolylineEntity.hpp"
#include "Core/GeomKernel/PolylineOps.hpp"
#include "NodeGraph/CurveOps.h"
#include "NodeGraph/MeshOps.h"
#include "Core/Entity/MeshEntity.hpp"
#include "NodeGraph/GeometryOps.h"
#include "Solid/SMLib/SMLibKernel.h"
#include "Dwg/Read/DwgReader.h"
#include "Dxf/Read/DxfReader.h"
#include "Database/CadDatabase.h"
#include "Database/Generated/Model.g.h"
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <numbers>
#include <thread>
#include <type_traits>

using namespace MiniCAD;
using namespace MiniCAD::Nodes;

namespace NodeSolidTest
{
    inline int g_failed = 0;
    inline void Check(bool ok, const char* label)
    {
        std::printf("[%s] %s\n", ok ? "PASS" : "FAIL", label);
        g_failed += ok ? 0 : 1;
    }

    inline constexpr double kPi = std::numbers::pi;
    inline bool Near(double a, double b, double rel = 1e-3) { return std::abs(a - b) <= rel * std::max(1.0, std::abs(b)); }

    inline NodeId Add(Graph& g, const char* type) { Node* n = g.AddNode(type); return n ? n->Id : kInvalidNode; }

    // SMLib 静态库整个进程只能用一个内核对象（见 SMLibKernel.h）
    inline Solid::ISolidKernel* g_kernel = nullptr;

    struct Fixture
    {
        Solid::ISolidKernel* kernel = g_kernel;
        Graph        g;
        SolveContext ctx;
        Fixture() { ctx.Kernel = kernel; }

        void Eval() { g.Evaluate(ctx); }
        // 节点 id 第一个输出的实体体积之和
        double Volume(NodeId id, int port = 0)
        {
            const Node* n = g.GetNode(id);
            double v = 0.0;
            for (const auto& item : n->Outputs[port])
                v += kernel->Volume(item.AsBody());
            return v;
        }
        size_t Count(NodeId id, int port = 0) { return g.GetNode(id)->Outputs[port].size(); }
        // 节点 id 第 port 个输出第一项的数值 / 实体
        double Number(NodeId id, int port = 0) { const auto& o = g.GetNode(id)->Outputs[port]; return o.empty() ? -1.0 : o[0].AsNumber(); }
        Solid::BodyPtr Body(NodeId id, int port = 0) { const auto& o = g.GetNode(id)->Outputs[port]; return o.empty() ? nullptr : o[0].AsBody(); }
        NodeState State(NodeId id) { return g.GetNode(id)->State; }
        bool   Ok(NodeId id) { return g.GetNode(id)->State == NodeState::Ok; }
    };

    // 各组测试（main 的调用顺序）
    void TestPrimitives();                    // NodeSolidBasicTests.cpp
    void TestExtrudeRevolveSweep();           // NodeSolidBasicTests.cpp
    void TestArraysAndBooleans();             // NodeSolidBasicTests.cpp
    void TestFilletAndSection();              // NodeSolidBasicTests.cpp
    void TestPreviewAndBake();                // NodeSolidBasicTests.cpp
    void TestCurve3D();                       // NodeSolidSurfaceTests.cpp
    void TestSurfaceNodes();                  // NodeSolidSurfaceTests.cpp
    void TestSurfacePreviewBakeAndFiles();    // NodeSolidSurfaceTests.cpp
    void TestN6WithKernel();                  // NodeSolidBasicTests.cpp
    void TestN7Forms();                       // NodeSolidFormTests.cpp
    void TestMatchLoops();                    // NodeSolidFormTests.cpp
    void TestN8Surfaces();                    // NodeSolidSurfaceTests.cpp
    void TestN6bCurves();                     // NodeSolidCurveTests.cpp
    void TestN9Meshes();                      // NodeSolidMeshTests.cpp
    void TestN10();                           // NodeSolidMeshTests.cpp
    void TestN11Background();                 // NodeSolidBasicTests.cpp
}
