#pragma once
// DWG / DXF 文件交换：经 MiniDWG 的 CadDatabase 与 Scene 互相转换。
// 公开头文件不暴露 MiniDWG 类型。
#include <cstdint>
#include <string>
#include <vector>

namespace MiniCAD
{
    class Scene;

    enum class CadFileKind
    {
        None,   // 不是 DWG / DXF（按扩展名判断）
        Dwg,
        Dxf,
    };

    // 按扩展名（不区分大小写）判断是否 DWG / DXF 文件
    CadFileKind CadKindFromPath(const std::string& path);

    // 读取 DWG / DXF 内存数据到空场景。格式按文件内容识别，失败返回 false 并在 error 中说明原因
    bool ImportCad(const std::vector<std::uint8_t>& data, Scene& scene, std::string* error = nullptr);

    // 把场景写成 DWG 或 DXF（ASCII）。失败返回空数组
    std::vector<std::uint8_t> ExportCad(const Scene& scene, CadFileKind kind, std::string* error = nullptr);
}
