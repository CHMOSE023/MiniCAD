#pragma once
#include "../GeomKernel/ICurve.hpp"
#include <memory>

namespace MiniCAD
{
    // =========================================================================
    // ICurveEntity —— 「可作为参数曲线编辑」的实体混入接口
    //
    // 由线形实体（Line / Ray / XLine / Arc / Circle / Ellipse Entity）实现，
    // 通过 MakeCurve() 暴露其几何的 ICurve 视图，使 Trim / Extend / Intersect /
    // Snap 等统一算法面向 ICurve 编程，而不必识别具体实体类型。
    //
    // Entity 基类提供 AsCurveEntity() 默认返回 nullptr，曲线实体覆写为返回 this，
    // 这样调用方无需依赖 dynamic_cast（与本工程自有 RTTI 风格一致）即可判定与取用。
    // =========================================================================
    class ICurveEntity
    {
    public:
        virtual ~ICurveEntity() = default;

        // 构建当前几何的参数曲线视图（按值快照，独立于实体生命周期）。
        virtual std::unique_ptr<ICurve> MakeCurve() const = 0;
    };
}
