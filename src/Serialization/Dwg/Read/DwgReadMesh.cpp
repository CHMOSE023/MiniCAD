// DWG 读取：细分网格 MESH（AcDbSubDMesh）。
//
// 位流顺序与 ODA 规范一致（对照 WkViewer/tools/cadcodec 的
// io/dwg/dwg_stream_readers/object_reader/entities.rs::read_mesh 逐项核实）：
//
//     BS   版本
//     B    混合折痕
//     BL   细分级别
//     BL   顶点数
//     顶点数 × 3BD            顶点
//     BL   面数据项数
//       —— 循环直到消耗完这些"项"：
//          BL n                 本面顶点数（占 1 项）
//          n  × BL              顶点索引（占 n 项）
//     BL   边数
//     边数 × (BL, BL)         边对
//     BL   折痕数
//     折痕数 × BD             折痕值
//     BL   覆盖选项
//
// 【关键】"面数据项数"是**项数**（每个面的"个数"那项 + 它的索引项），不是面数：
//   项数 = Σ(每个面的顶点数 + 1)。
//
// 【实测 · `sample_AC1032.dwg`】
//   句柄 343：顶点 62 / 面数据 336 / 面 72（24 三角 + 48 四边形）/ 边 132 / 折痕 132
//   句柄 380：顶点 64 / 面数据 320 / 面 64（全四边形）/ 边 128 / 折痕 128
// 七个样例 AC1014~AC1032 解出的结果完全一致（规范与版本无关）。
#include "Dwg/Read/DwgReaderImpl.h"
#include "Database/Mesh.h"

namespace MiniDWG::DwgRead
{
    namespace
    {
        // 计数护栏：损坏或与规范不符的数据不能让读取器吃满内存
        constexpr std::int32_t kMaxMeshVertices = 5'000'000;
        constexpr std::int32_t kMaxMeshFaceData = 50'000'000;
        constexpr std::int32_t kMaxMeshEdges    = 10'000'000;
        constexpr std::int32_t kMaxMeshCreases  = 10'000'000;
    }

    // 返回 nullptr 表示数据不合规范：调用方回退到"原样保留"（与 MULTILEADER 的处理一致）
    std::unique_ptr<CadObject> Reader::ReadMesh(ObjectInfo& info)
    {
        auto mesh = std::make_unique<Mesh>();
        ReadCommonEntityData(*mesh, info);

        DwgBitReader& r = m_objReader;
        mesh->Version     = r.ReadBitShort();
        mesh->BlendCrease = r.ReadBit();
        mesh->SubdivisionLevel = r.ReadBitLong();
        const std::int32_t numVerts = r.ReadBitLong();

        if (r.Failed() || numVerts < 0 || numVerts > kMaxMeshVertices)
            return nullptr;
        if (!r.CheckCount(numVerts, 24))         // 每个顶点 3 个 double = 24 字节，够做合理性检查
            return nullptr;

        mesh->Vertices.reserve(static_cast<size_t>(numVerts));
        for (std::int32_t i = 0; i < numVerts; ++i)
            mesh->Vertices.push_back(r.Read3BitDouble());
        if (r.Failed())
            return nullptr;

        // ── 面：先读"面数据项数"，再按「个数 + 索引」循环消耗完它 ──
        const std::int32_t faceData = r.ReadBitLong();
        if (r.Failed() || faceData < 0 || faceData > kMaxMeshFaceData)
            return nullptr;

        std::int32_t consumed = 0;
        while (consumed < faceData)
        {
            const std::int32_t n = r.ReadBitLong();
            ++consumed;
            if (r.Failed())
                return nullptr;
            // 护栏：n 要不是个合理的面顶点数，或超出剩余项数，说明数据不合规范
            if (n < 3 || n > numVerts || n > faceData - consumed)
                return nullptr;

            std::vector<int> face;
            face.reserve(static_cast<size_t>(n));
            for (std::int32_t k = 0; k < n; ++k)
            {
                const std::int32_t idx = r.ReadBitLong();
                if (r.Failed())
                    return nullptr;
                if (idx < 0 || idx >= numVerts)
                    return nullptr;
                face.push_back(static_cast<int>(idx));
            }
            consumed += n;
            mesh->Faces.push_back(std::move(face));
        }

        // ── 边 ──
        const std::int32_t numEdges = r.ReadBitLong();
        if (r.Failed() || numEdges < 0 || numEdges > kMaxMeshEdges)
            return nullptr;
        mesh->Edges.reserve(static_cast<size_t>(numEdges));
        for (std::int32_t i = 0; i < numEdges; ++i)
        {
            const std::int32_t a = r.ReadBitLong();
            const std::int32_t b = r.ReadBitLong();
            if (r.Failed())
                return nullptr;
            mesh->Edges.emplace_back(static_cast<int>(a), static_cast<int>(b));
        }

        // ── 折痕 ──
        const std::int32_t numCreases = r.ReadBitLong();
        if (r.Failed() || numCreases < 0 || numCreases > kMaxMeshCreases)
            return nullptr;
        mesh->CreaseValues.reserve(static_cast<size_t>(numCreases));
        for (std::int32_t i = 0; i < numCreases; ++i)
        {
            mesh->CreaseValues.push_back(r.ReadBitDouble());
            if (r.Failed())
                return nullptr;
        }

        mesh->OverrideOption = r.ReadBitLong();
        if (r.Failed())
            return nullptr;
        return mesh;
    }
}
