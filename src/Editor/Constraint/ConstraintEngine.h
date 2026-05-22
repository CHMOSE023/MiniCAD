#pragma once
#include "OrthoConstraint.h"
#include "PolarConstraint.h"
#include "Core/GeomKernel/Line.hpp"

namespace MiniCAD
{
    // 约束引擎：管理正交约束和极轴约束，两者互斥。
    // 调用 Apply() 后通过 GetGuideLine() 获取约束辅助线用于 Overlay 绘制。
    class ConstraintEngine
    {
    public:
        // 尝试应用已启用的约束，返回 true 表示有约束生效。
        // 结果可通过 GetConstrainedPoint() / GetGuideLine() 读取。
        bool Apply(const ConstraintContext& ctx);

        Math::Point3 GetConstrainedPoint() const { return m_resultPoint; }
        const Line&  GetGuideLine()        const { return m_guideLine;   }
        bool         HasResult()           const { return m_hasResult;   }
        bool         IsAnyActive()         const { return m_ortho.IsEnabled() || m_polar.IsEnabled(); }

        // 各约束直接访问（供 UI 读写状态）
        OrthoConstraint&       GetOrtho()       { return m_ortho; }
        const OrthoConstraint& GetOrtho() const { return m_ortho; }
        PolarConstraint&       GetPolar()       { return m_polar; }
        const PolarConstraint& GetPolar() const { return m_polar; }

        // 互斥开关：启用任一约束时自动关闭另一个
        void EnableOrtho(bool on);
        void EnablePolar(bool on);

        bool IsOrthoEnabled() const { return m_ortho.IsEnabled(); }
        bool IsPolarEnabled() const { return m_polar.IsEnabled(); }

    private:
        OrthoConstraint m_ortho;
        PolarConstraint m_polar;

        Math::Point3 m_resultPoint;
        Line         m_guideLine;
        bool         m_hasResult = false;
    };
}
