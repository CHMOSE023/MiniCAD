#pragma once
#include "Database/Generated/Model.g.h"
#include "Database/Types.hpp"
#include <cstdint>
#include <utility>
#include <vector>

// MESH（AcDbSubDMesh）的模型。**手写**，不放进生成的 Model.g.h ——
// 生成器（已归档仓库里的 tools/dxfgen）的输入 ACadSharp 源码不在此仓库，
// 手改生成文件会在重新生成时丢失。做法与 Database/SpatialFilter.h 一致：
// 类元数据（ClassInfo）在这里手写，DXF 侧在 DxfReader.cpp 里补登。
namespace MiniDWG
{
    // 网格顶点（与 XYZ 同形，但单独留名便于将来带权重）
    // 面 = 顶点索引序列（三角形 / 四边形，DXF 也允许更多边）
    class Mesh : public Entity
    {
    public:
        static constexpr std::string_view kDxfName = "MESH";

        std::string_view GetDxfName() const override { return kDxfName; }
        // MESH 是按类名注册的实体（类型码 ≥ 500），没有固定类型码
        CadObjectType GetObjectType() const override { return CadObjectType::UNLISTED; }
        static const DxfClassInfo& ClassInfo();
        const DxfClassInfo& GetClassInfo() const override { return ClassInfo(); }
        std::string_view GetSubclassMarker() const override { return "AcDbSubDMesh"; }

        std::int16_t Version = 2;              // 71
        bool         BlendCrease = false;      // 72
        std::int32_t SubdivisionLevel = 0;     // 91
        std::vector<XYZ>                Vertices;   // 10/20/30
        std::vector<std::vector<int>>   Faces;      // 90（先个数再索引）
        std::vector<std::pair<int, int>> Edges;     // 90（边对）
        std::vector<double>             CreaseValues;  // 140（每个 crease 对应同下标的边）
        std::int32_t OverrideOption = 0;       // 尾部的 BL
    };
}
