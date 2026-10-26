// DXF 写入：细分网格 MESH（AcDbSubDMesh），组码顺序与权威工具转出的样例一致：
//     AcDbSubDMesh  71 版本 / 72 混合折痕 / 91 细分级别
//                   92 顶点数 / 10,20,30 × 顶点
//                   93 面数据项数 / 90（每面：个数 + 索引）
//                   94 边数 / 90（边对）/ 95 折痕数 / 140 × 折痕
#include "Dxf/Write/DxfWriterImpl.h"
#include "Database/Mesh.h"

namespace MiniDWG::DxfWrite
{
    void Writer::WriteMesh(const Mesh& mesh)
    {
        WriteSubclass("AcDbSubDMesh");
        WriteInt(71, mesh.Version);
        WriteInt(72, mesh.BlendCrease ? 1 : 0);
        WriteInt(91, mesh.SubdivisionLevel);

        WriteInt(92, static_cast<std::int32_t>(mesh.Vertices.size()));
        for (const XYZ& v : mesh.Vertices)
            WriteXYZ(10, v);

        // 面数据项数 = Σ(每面顶点数 + 1)
        std::int32_t faceItems = 0;
        for (const auto& f : mesh.Faces)
            faceItems += static_cast<std::int32_t>(f.size()) + 1;
        WriteInt(93, faceItems);
        for (const auto& f : mesh.Faces)
        {
            WriteInt(90, static_cast<std::int32_t>(f.size()));
            for (int idx : f)
                WriteInt(90, idx);
        }

        WriteInt(94, static_cast<std::int32_t>(mesh.Edges.size()));
        for (const auto& e : mesh.Edges)
        {
            WriteInt(90, e.first);
            WriteInt(90, e.second);
        }

        WriteInt(95, static_cast<std::int32_t>(mesh.CreaseValues.size()));
        for (double c : mesh.CreaseValues)
            WriteReal(140, c);
    }
}
