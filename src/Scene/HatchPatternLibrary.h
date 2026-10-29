#pragma once
#include "Core/Entity/HatchEntity.hpp"
#include <string>
#include <string_view>
#include <vector>

namespace MiniCAD
{
    // =========================================================================
    // HatchPatternLibrary —— 填充图案库（AutoCAD .pat 格式）
    //
    // .pat 格式：
    //   ; 注释
    //   *NAME[, 说明]
    //   angle, x-origin, y-origin, delta-x, delta-y [, dash1, dash2, ...]
    //   ...（一行一个线族）
    // 名为 SOLID 的图案视为实心填充（忽略其线族）。
    //
    // 首次访问时载入内置的公制图案（单位 mm，与 acadiso.pat 的量级一致）与（钢筋混凝土、加气混凝土等）；
    // 图案数据都在 HatchPatternLibrary.inc。之后可用 LoadFile / LoadText 追加，同名图案（不区分大小写）以后载入的为准。
    // =========================================================================
    class HatchPatternLibrary
    {
    public:
        static HatchPatternLibrary& Instance();

        // 解析 .pat 文本（UTF-8），追加 / 覆盖到库中。返回成功载入的图案数；
        // 有无法解析的行时写 error（行号 + 内容），已解析的图案仍然载入。
        size_t LoadText(std::string_view text, std::string* error = nullptr);
        // 读 .pat 文件：不是合法 UTF-8 的按 GBK 解码（中文版 AutoCAD、天正自带的都是 GBK）
        size_t LoadFile(const std::string& utf8Path, std::string* error = nullptr);

        const HatchPattern*              Find(std::string_view name) const;   // 不区分大小写
        const std::vector<HatchPattern>& All() const { return m_patterns; }
        // 天正对象的墙柱填充按名称取：先找天正 acad.pat 里的（与 AutoCAD 同名的标准图案天正改了比例，如 ANSI31 是
        // acadiso 的一半，只在这里用），没有再找库里的（patterns 目录里另加的图案）
        const HatchPattern*              FindTianzheng(std::string_view name) const;

        // 按名称取图案；找不到时返回实心填充
        HatchPattern Get(std::string_view name) const;

        // 只解析不入库（测试、预览用）
        static bool Parse(std::string_view text, std::vector<HatchPattern>& out, std::string* error = nullptr);

    private:
        HatchPatternLibrary();
        std::vector<HatchPattern> m_patterns;
        std::vector<HatchPattern> m_tianzheng;      // 天正 acad.pat 的全部图案（FindTianzheng）
    };
}
