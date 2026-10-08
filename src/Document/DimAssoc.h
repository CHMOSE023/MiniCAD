#pragma once
#include "Core/Entity/DimensionEntity.hpp"
#include "Core/Object/Object.hpp"
#include "Core/Math/Point3.hpp"
#include "Document/FeaturePoints.h"
#include <memory>
#include <optional>
#include <unordered_map>
#include <utility>
#include <vector>

namespace MiniCAD
{
    class Scene;
    class Entity;
    class ICommand;

    // =========================================================================
    // 关联标注（同 AutoCAD DIMASSOC = 2）
    //
    // 标注工具创建标注时，把用对象捕捉拾取的定义点记成 DimAssocRef（某对象的第几个端点、
    // 曲线上的某个参数、两条曲线的交点……）。之后每条命令执行完，CommandStack 调用 Sync：
    //   · 几何变了、标注没动     → 定义点跟到特征点的新位置，尺寸线 / 文字位置随之调整
    //   · 标注与几何一起被变换   → 定义点不动，按新位置重新挂接（如一起移动 / 旋转 / 镜像）
    //   · 只有标注被改（移走、拖夹点）、或被关联的对象没了 → 该点解除关联
    // 这些改动打包成一条附属命令，与原命令一起撤销 / 重做。
    // =========================================================================
    namespace DimAssoc
    {
        // 以下各函数建立关联引用。pt 是工具实际采用的点：对象上没有与它重合的对应点
        // （如被正交约束、键入坐标改写过）时返回无效引用，该定义点就不关联。

        // 对象的第几个端点 / 中点 / 象限点 / 圆心：取与 pt 重合的那个
        DimAssocRef Feature(const Scene& scene, Object::ObjectID id, FeatureKind kind, const Math::Point3& pt);
        // 圆 / 圆弧 / 椭圆的圆心
        DimAssocRef Center(const Scene& scene, Object::ObjectID id);
        // 曲线上的点（按曲线参数记录）
        DimAssocRef Nearest(const Scene& scene, Object::ObjectID id, const Math::Point3& pt);
        // 两条曲线的交点；extended = 两条直线按无限长求交（角度标注的顶点）
        DimAssocRef Intersection(const Scene& scene, Object::ObjectID a, Object::ObjectID b,
                                 const Math::Point3& pt, bool extended = false);

        // 关联点当前位置；对象不存在或已不具备该特征点时返回空
        std::optional<Math::Point3> Evaluate(const Scene& scene, const DimAssocRef& ref);

        // 让场景里的关联标注跟上几何。返回已应用的改动（供撤销），无改动返回空
        std::unique_ptr<ICommand> Sync(Scene& scene);

        // 复制出的标注：关联对象也一起复制了的改指向副本，否则解除关联
        void RemapCopy(Entity& copy, const std::unordered_map<Object::ObjectID, Object::ObjectID>& srcToNew);
        // 一批副本（源 ID → 副本 ID，副本已在场景中）：对其中的标注调用 RemapCopy
        void RemapCopies(Scene& scene, const std::vector<std::pair<Object::ObjectID, Object::ObjectID>>& srcToNew);
        // 同上，源与副本按下标一一对应
        void RemapCopies(Scene& scene, const std::vector<Object::ObjectID>& src, const std::vector<Object::ObjectID>& copies);
        // 解除全部关联（如分解块后得到的标注）
        void Strip(Entity& e);
    }
}
