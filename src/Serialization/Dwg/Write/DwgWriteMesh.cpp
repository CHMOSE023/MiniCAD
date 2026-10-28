// DWG 写入：细分网格 MESH，字段顺序与 DwgReadMesh.cpp 逐项对应（即 ODA 规范的顺序）。
#include "Dwg/Write/DwgWriterImpl.h"
#include "Database/Mesh.h"

namespace MiniDWG::DwgWrite
{
    void Writer::WriteMesh(const Mesh& mesh)
    {
        DwgBitWriter& m = M();
        m.WriteBitShort(mesh.Version);
        m.WriteBit(mesh.BlendCrease);
        m.WriteBitLong(mesh.SubdivisionLevel);

        m.WriteBitLong(static_cast<std::int32_t>(mesh.Vertices.size()));
        for (const XYZ& v : mesh.Vertices)
            m.Write3BitDouble(v);

        // 面数据的"项数" = Σ(每个面的顶点数 + 1)（每个面自己的"个数"也占一项）
        std::int32_t faceData = 0;
        for (const auto& face : mesh.Faces)
            faceData += static_cast<std::int32_t>(face.size()) + 1;
        m.WriteBitLong(faceData);
        for (const auto& face : mesh.Faces)
        {
            m.WriteBitLong(static_cast<std::int32_t>(face.size()));
            for (int idx : face)
                m.WriteBitLong(idx);
        }

        m.WriteBitLong(static_cast<std::int32_t>(mesh.Edges.size()));
        for (const auto& e : mesh.Edges)
        {
            m.WriteBitLong(e.first);
            m.WriteBitLong(e.second);
        }

        m.WriteBitLong(static_cast<std::int32_t>(mesh.CreaseValues.size()));
        for (double c : mesh.CreaseValues)
            m.WriteBitDouble(c);

        m.WriteBitLong(mesh.OverrideOption);
    }
}
