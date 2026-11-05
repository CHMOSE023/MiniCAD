#pragma once
#include "Solid/BrepData.h"
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

// ACIS（SAT 文本 / SAB 二进制）→ 中性 B-rep 与边线框（docs/Solid/PLAN.md 第 16 节）
namespace MiniCAD::Import
{
    struct AcisResult
    {
        Solid::BrepModel                       Brep;
        std::vector<std::vector<Math::Point3>> Wire;
        int                                    Version     = 0;
        double                                 Resolution  = 1e-6;   // 文件头的 resabs
        int                                    FailedFaces = 0;      // 曲面或边曲线不认识，没放进 Brep
    };

    // 自动识别 SAT / SAB，读到 End-of-ACIS-data / End-of-ASM-data 为止（后面的数据不管）。
    // 一个面都没读出来、也没有边时返回 false
    bool ReadAcis(const std::uint8_t* data, std::size_t size, AcisResult& out, std::string* error = nullptr);
}
