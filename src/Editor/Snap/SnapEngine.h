#pragma once
#include "SnapResult.h"
#include "Scene/Scene.h"
#include "Core/Object/Object.hpp"
#include "Core/Math/Point2.hpp"
#include "Viewport/Camera.h"
#include <algorithm>
#include <cstdint>
#include <unordered_set>
#include <vector>

namespace MiniCAD
{
    // 对象捕捉模式位掩码（同 AutoCAD OSMODE 思路，可按位组合）
    enum class SnapMode : uint32_t
    {
        None          = 0,
        Endpoint      = 1u << 0,
        Midpoint      = 1u << 1,
        Nearest       = 1u << 2,
        Quadrant      = 1u << 3,   // 圆/弧/椭圆象限点
        Intersection  = 1u << 4,   // 两曲线交点
        Perpendicular = 1u << 5,   // 垂足（需 fromPoint）
        Grid          = 1u << 6,   // 网格（兜底，仅在其余均未命中时生效）

        Default       = Endpoint | Midpoint | Nearest | Quadrant
                      | Intersection | Perpendicular,
    };

	// 捕捉引擎：提供捕捉查询接口，支持多种捕捉类型（端点、中心点、最近点、网格等）
    class SnapEngine
    {
    public:
        // ─── 主接口 ─────────────────────────────
        // exclude:    需要跳过的对象（夹点拖拽时传入当前选中集合，避免捕捉自身）
        // fromPoint:  当前橡皮筋/拖拽的基点（工具锚点或活动夹点）。仅垂足捕捉需要；
        //             为空时不计算垂足。
        // candidates: 光标附近的候选实体 ID（经空间索引粗筛，见
        //             Picking::CollectSnapCandidates）。非空时各捕捉模式只遍历
        //             候选集而非全场景；传 nullptr 回退为全场景遍历。
        SnapResult Query(const Math::Point2&                             screenPt,
                         const Scene&                                    scene,
                         const Camera&                                   cam,
                         const std::unordered_set<Object::ObjectID>&     exclude    = {},
                         const Math::Point3*                             fromPoint  = nullptr,
                         const std::vector<Object::ObjectID>*            candidates = nullptr) const;

        bool  IsEnabled() const { return m_enableEngine; }
        void  SetEnableSnap(bool enable) { m_enableEngine = enable; }

        // ─── 捕捉模式（按位组合 SnapMode）────────
        uint32_t GetSnapModes() const               { return m_modes; }
        void     SetSnapModes(uint32_t modes)       { m_modes = modes; }
        bool     IsModeEnabled(SnapMode m) const    { return (m_modes & static_cast<uint32_t>(m)) != 0; }
        void     SetModeEnabled(SnapMode m, bool on)
        {
            if (on) m_modes |=  static_cast<uint32_t>(m);
            else    m_modes &= ~static_cast<uint32_t>(m);
        }

        // ─── 捕捉孔径（屏幕像素）────────────────
        double GetSnapRadiusPx() const         { return m_snapRadiusPx; }
        void   SetSnapRadiusPx(double px)      { m_snapRadiusPx = std::clamp(px, 1.0, 50.0); }

        double GetGridSize() const             { return m_gridSize; }
        void   SetGridSize(double size)        { if (size > 0.0) m_gridSize = size; }

    private:
        SnapResult TryEndpoint     (const Math::Point2&, const Scene&, const Camera&, const std::unordered_set<Object::ObjectID>&) const;
        SnapResult TryMidpoint     (const Math::Point2&, const Scene&, const Camera&, const std::unordered_set<Object::ObjectID>& ) const;
        SnapResult TryNearest      (const Math::Point2&, const Scene&, const Camera&, const std::unordered_set<Object::ObjectID>&) const;
        SnapResult TryQuadrant     (const Math::Point2&, const Scene&, const Camera&, const std::unordered_set<Object::ObjectID>&) const; // ← 新增
        SnapResult TryIntersection (const Math::Point2&, const Scene&, const Camera&, const std::unordered_set<Object::ObjectID>& ) const;
        SnapResult TryPerpendicular(const Math::Point2&, const Scene&, const Camera&, const std::unordered_set<Object::ObjectID>&, const Math::Point3& ) const;
        SnapResult TryGrid         (const Math::Point2&, const Camera&) const;
        // 端点 / 中点 / 象限点的共同实现：kind 为 FeatureKind
        SnapResult TryFeature      (const Math::Point2&, const Scene&, const Camera&, const std::unordered_set<Object::ObjectID>&, int kind, SnapResult::Type type) const;

        // 遍历捕捉对象：Query 设置了候选集时只遍历候选，否则全场景。
        template<typename Fn>
        void ForEachSnapObject(const Scene& scene, Fn&& fn) const
        {
            if (m_queryCandidates)
            {
                for (auto id : *m_queryCandidates)
                    if (const Object* o = scene.GetEntity(id))
                        fn(*o);
            }
            else
            {
                scene.ForEachObject(std::forward<Fn>(fn));
            }
        }

    private:
        // ─── 开关 ───────────────────────────────
        bool     m_enableEngine = true;
        uint32_t m_modes        = static_cast<uint32_t>(SnapMode::Default);

        double m_gridSize       = 1.0;    // 世界单位
        double m_snapRadiusPx   = 10.0;   // 屏幕像素捕捉孔径(同 AutoCAD APERTURE 默认值)

        // 本次 Query 的候选集（仅在 Query 执行期间有效；nullptr = 全场景遍历）
        mutable const std::vector<Object::ObjectID>* m_queryCandidates = nullptr;
    };
}
