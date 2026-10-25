// NodeGraphTests（节点图核心测试，不需要实体内核）的公共部分：检查与比较函数、读取节点输出，
// 以及各组测试的声明（main 在 NodeGraphTests.cpp，按下面的顺序调用）。
// 新增测试放进对应的文件：图 / 连线 / 读写 / 搜索 → Core，数据与曲线节点 → Data，图纸 / 参数 / 预览 / 后台 → Document，
// 组节点 → Group；需要实体内核的放进 NodeSolidTests（NodeSolidCommon.h）
#pragma once
#include "NodeGraph/Expression.h"
#include "NodeGraph/Graph.h"
#include "NodeGraph/GraphHistory.h"
#include "NodeGraph/Preview.h"
#include "Document/Document.h"
#include "Core/Entity/CircleEntity.hpp"
#include "Core/Entity/EllipseEntity.hpp"
#include "Core/Entity/LineEntity.hpp"
#include "Core/Entity/PointEntity.hpp"
#include "Core/Entity/PolylineEntity.hpp"
#include "Core/Entity/SplineEntity.hpp"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <map>
#include <set>
#include <numbers>
#include <string>
#include <thread>

using namespace MiniCAD;
using namespace MiniCAD::Nodes;

namespace NodeGraphTest
{
    inline int g_failed = 0;
    inline void Check(bool ok, const char* label)
    {
        std::printf("[%s] %s\n", ok ? "PASS" : "FAIL", label);
        g_failed += ok ? 0 : 1;
    }

    inline bool Near(double a, double b, double tol = 1e-9) { return std::abs(a - b) <= tol; }

    inline const DataList& Out(const Graph& g, NodeId id, int port = 0)
    {
        static const DataList kEmpty;
        const Node* n = g.GetNode(id);
        return n && port < static_cast<int>(n->Outputs.size()) ? n->Outputs[port] : kEmpty;
    }

    inline double Num(const Graph& g, NodeId id, int port = 0, size_t i = 0)
    {
        const auto& l = Out(g, id, port);
        Value v;
        return i < l.size() && Convert(l[i], DataType::Number, v) ? v.AsNumber() : NAN;
    }

    inline NodeId Add(Graph& g, const char* type) { Node* n = g.AddNode(type); return n ? n->Id : kInvalidNode; }

    // 各组测试（main 的调用顺序）
    void TestRegistry();                      // NodeGraphCoreTests.cpp
    void TestCategoryOrder();                 // NodeGraphCoreTests.cpp
    void TestSearchForPort();                 // NodeGraphCoreTests.cpp
    void TestBasicsAndDirty();                // NodeGraphCoreTests.cpp
    void TestLinksRules();                    // NodeGraphCoreTests.cpp
    void TestLongestList();                   // NodeGraphCoreTests.cpp
    void TestSerialization();                 // NodeGraphCoreTests.cpp
    void TestHistoryAndDuplicate();           // NodeGraphCoreTests.cpp
    void TestCopyPaste();                     // NodeGraphCoreTests.cpp
    void TestSearch();                        // NodeGraphCoreTests.cpp
    void TestExpression();                    // NodeGraphDataTests.cpp
    void TestDataNodes();                     // NodeGraphDataTests.cpp
    void TestCurvesWithoutKernel();           // NodeGraphDataTests.cpp
    void TestReferenceAndDocument();          // NodeGraphDocumentTests.cpp
    void TestPreviewAndBake();                // NodeGraphDocumentTests.cpp
    void TestParameters();                    // NodeGraphDocumentTests.cpp
    void TestBackground();                    // NodeGraphDocumentTests.cpp
    void TestGroups();                        // NodeGraphGroupTests.cpp
}
