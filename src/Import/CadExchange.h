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

    // 写出 DWG / DXF 的版本。AutoCAD 2013～2017 共用 2013 格式（AC1027），2018 及以后为 2018 格式（AC1032）
    enum class CadSaveVersion
    {
        R2018,      // AC1032
        R2013,      // AC1027：AutoCAD 2013 / 2014 / 2015 / 2016 / 2017
    };

    // 按扩展名（不区分大小写）判断是否 DWG / DXF 文件
    CadFileKind CadKindFromPath(const std::string& path);

    // 读取 DWG / DXF 内存数据到空场景。格式按文件内容识别，失败返回 false 并在 error 中说明原因
    // version：文件版本对应的写出版本（2013 格式 → R2013，其余 → R2018），保存时据此沿用原版本
    bool ImportCad(const std::vector<std::uint8_t>& data, Scene& scene, std::string* error = nullptr,
                   CadSaveVersion* version = nullptr);

    // 把场景写成 DWG 或 DXF（ASCII）。失败返回空数组
    std::vector<std::uint8_t> ExportCad(const Scene& scene, CadFileKind kind, CadSaveVersion version = CadSaveVersion::R2018,
                                        std::string* error = nullptr);
}
