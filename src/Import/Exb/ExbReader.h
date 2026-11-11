#pragma once
// EXB 导入的顶层入口。与 src/Import/CadExchange.h 的 ImportCad 对齐，
// 但刻意不进 CadFileKind：那一套枚举和 CadSaveVersion / ExportCad / 界面另存表强绑定，
// EXB 目前是只读格式，混进去会让「能否写出」的语义到处出现特例。
#include "Import/Exb/ExbTypes.h"
#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace MiniCAD
{
    class Scene;
}

namespace MiniCAD::Exb
{
    // 按扩展名判断（大小写不敏感），供 Document 与各宿主的文件过滤器共用
    bool IsExbPath(const std::string& path);

    // 内容嗅探：CFB 签名。扩展名可能被改错，打开时以内容为准再确认一次
    bool IsExbData(const std::uint8_t* data, std::size_t size);

    // 复合文档之前的老格式：文件头是 CaxaEbForWindows。还不支持，上层据此给出明确的提示
    bool IsLegacyExbData(const std::uint8_t* data, std::size_t size);

    // 返回 true 表示「文件被正确识别、容器完整、且几何已尽量还原」。
    // 空图纸（新建的 blank 类文件）也会返回 true，此时 result.EntityCount == 0。
    // error 里是英文原因，用户可见文字由 Document 层包 Tr。
    // sourcePath 预留给阶段 4 的外部参照解析，当前未使用。
    bool ImportExb(const std::vector<std::uint8_t>& data, Scene& scene,
                   std::string* error = nullptr,
                   ExbImportResult* result = nullptr,
                   const std::function<void(float)>& progress = nullptr,
                   const std::string& sourcePath = {});
}
