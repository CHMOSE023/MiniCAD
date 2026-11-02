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

        size_t LoadText(std::string_view text, std::string* error = nullptr);
        size_t LoadFile(const std::string& utf8Path, std::string* error = nullptr);

        const HatchPattern*              Find(std::string_view name) const;   // 不区分大小写
        const std::vector<HatchPattern>& All() const { return m_patterns; }
        const HatchPattern*              FindTianzheng(std::string_view name) const;

        HatchPattern Get(std::string_view name) const;

        static bool Parse(std::string_view text, std::vector<HatchPattern>& out, std::string* error = nullptr);

    private:
        HatchPatternLibrary();
        std::vector<HatchPattern> m_patterns;
        std::vector<HatchPattern> m_tianzheng;       
    };
}
