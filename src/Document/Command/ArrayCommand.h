#pragma once
#include "Document/CommandStack/ICommand.h"
#include "Core/Object/Object.hpp"
#include "Core/Math/Point3.hpp"
#include "Core/Math/Vec3.hpp"
#include <vector>
#include <string>

namespace MiniCAD
{
    class Scene;
    class Entity;

    // 阵列类型(AutoCAD ARRAY)
    enum class ArrayType
    {
        Rectangular,   // 矩形阵列(行 × 列)
        Polar          // 环形阵列(绕中心点)
    };

    // 阵列参数 —— 由对话框/命令行填写,供命令与预览共用
    struct ArrayParams
    {
        ArrayType type = ArrayType::Rectangular;

        // ── 矩形阵列 ──────────────────────────────────────────
        int    rows       = 3;       // 行数(>=1)
        int    cols       = 4;       // 列数(>=1)
        double rowSpacing = 20.0;    // 行间距(沿阵列 Y 轴,可为负)
        double colSpacing = 20.0;    // 列间距(沿阵列 X 轴,可为负)
        double angleDeg   = 0.0;     // 阵列旋转角(度):旋转行/列网格方向

        // ── 环形阵列 ──────────────────────────────────────────
        Math::Point3 center{};       // 阵列中心
        int    count        = 6;     // 项目总数(含源,>=2)
        double fillAngleDeg = 360.0; // 填充角(度,正=CCW,负=CW)
        bool   rotateItems  = true;  // 是否旋转每个项目

        // 总数有效性:矩形需 rows*cols>1,环形需 count>=2
        bool ProducesCopies() const
        {
            if (type == ArrayType::Rectangular)
                return rows >= 1 && cols >= 1 && (rows * cols) > 1;
            return count >= 2;
        }
    };

    // 单个副本位置的刚体变换:旋转(绕 center)或平移(delta)
    struct ArrayPlacement
    {
        bool         rotate = false;
        Math::Point3 center{};
        double       angle  = 0.0;   // 弧度
        Math::Vec3   delta{};

        void Apply(Entity& e) const;
    };

    // 源对象组的参考基点(合并包围盒中心),供「环形不旋转」整组刚体搬运
    Math::Point3 ComputeArrayGroupBase(Scene& scene, const std::vector<Object::ObjectID>& ids);

    // 枚举除源原位以外的全部副本位置(矩形/环形通用),供命令与预览共用
    std::vector<ArrayPlacement> ComputeArrayPlacements(const ArrayParams& p, const Math::Point3& groupBase);

    // 阵列命令:把若干源对象按矩形/环形规则克隆出多份副本(源对象保留)。
    // - Execute(首次):为每个阵列位置(跳过源所在的原位)克隆全部源对象,
    //                 分配新 ID 入场,记录新 ID 供 Undo/Redo。
    // - Undo:        删除全部新对象。
    // - Redo:        沿用首次分配的 ID 重新克隆入场,保持引用稳定。
    class ArrayCommand : public ICommand
    {
    public:
        ArrayCommand(std::vector<Object::ObjectID> sourceIds, ArrayParams params);

        bool        Execute(Scene& scene) override;
        void        Undo(Scene& scene) override;
        std::string GetName() const override { return "Array"; }

        const std::vector<Object::ObjectID>& GetNewIds() const { return m_newIds; }

    private:
        std::vector<Object::ObjectID> m_sourceIds;
        ArrayParams                   m_params;
        std::vector<Object::ObjectID> m_newIds;
        bool                          m_executed = false;
    };
}
