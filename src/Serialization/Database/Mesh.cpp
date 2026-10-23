#include "Database/Mesh.h"
#include "Database/DxfMeta.h"

// MESH 的类元数据。属性表为空：DXF 侧的属性（10/20/30、90、140 …）不是"一个属性一个组码"，
// 顶点与面是**变长重复组**，通用属性表表达不了，所以 DXF 读写都用手写实现
// （DxfReadMesh / DxfWriteMesh），这里只提供类元数据供 DXF 的类表使用。
//
// Create 必须非空：DxfReader::ReadObject 把它为空视为"未建模对象"，
// 会走 UnknownObject 分支、绕开手写读取（同 Database/SpatialFilter.cpp 的说明）。
namespace MiniDWG
{
    const DxfClassInfo& Mesh::ClassInfo()
    {
        static const DxfClassInfo info{
            "Mesh", "MESH", {},
            [] { return std::unique_ptr<CadObject>(new Mesh()); }
        };
        return info;
    }
}
