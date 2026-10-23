#pragma once
#include "Entity.hpp"
#include "Core/GeomKernel/AABB.hpp"
#include "Core/Math/Point3.hpp"
#include <cmath>
#include <memory>

namespace MiniCAD
{
    // 图纸空间视口（DXF `VIEWPORT`）。
    //
    // 为什么需要：AutoCAD 的布局页里，**图形内容是通过视口显示的** ——
    // 图纸空间本身通常是空的，只有一个或多个 VIEWPORT，每个视口把**模型空间**
    // 的几何按自己的缩放 / 平移画进一个矩形框。
    //
    // 实测 `test0.dwg`：`*Paper_Space` 块**完全为空**，图纸空间只有 2 个 VIEWPORT。
    // 不支持 VIEWPORT 时，切到布局页只有一张白纸、框里什么都没有。
    //
    // 坐标语义（DXF 组码）：
    //   · 10/20  视口中心（**图纸空间**坐标，即纸张上的位置）
    //   · 40/41  视口宽 / 高（图纸空间单位，纸张上的框大小）
    //   · 12/22  视图中心（**模型空间**坐标，视口「看向」哪里）
    //   · 45     视图高度（模型空间单位，视口竖向能容纳多少模型长度）
    //   · 50     视图旋转角（弧度）
    //   · 68     视口 id
    //
    // 由此得到模型 → 图纸的变换（挤出方向为 +Z 时）：
    //     scale   = 41 / 45
    //     paperPt = center + Rotate(modelPt - viewCenter) * scale
    class ViewportEntity : public Entity
    {
    public:
        ViewportEntity(ObjectID id,
                       const Math::Point3& center,
                       double width,
                       double height)
            : Entity(id)
            , m_center(center)
            , m_width(width)
            , m_height(height)
        {
        }

        // ── 纸张上的框 ──────────────────────────────────────────────────
        const Math::Point3& GetCenter() const { return m_center; }
        void SetCenter(const Math::Point3& c) { m_center = c; }

        double GetWidth()  const { return m_width; }
        double GetHeight() const { return m_height; }
        void SetSize(double w, double h) { m_width = w; m_height = h; }

        // ── 视口看向的模型范围 ──────────────────────────────────────────
        //
        // ⚠ 存储的始终是 **WCS（世界坐标）**，不是 DWG 组码 12/22 的原始值。
        //    组码 12/22（ViewCenter）按 DWG 规范是 **DCS（显示坐标系）** 坐标：
        //    DCS 是把 WCS 绕视线方向旋转 +Twist 得到的坐标系，所以
        //        DCS → WCS 必须反向旋转 -Twist
        //    导入时用 SetViewCenterDcs() 一次性转正；导出时反向转回。
        //
        //    实测 `01-曲陵路.dwg`：布局上有 5 个浮动视口 Twist=1.6701 rad(95.7°)，
        //    ViewCenter 原始值 (-457894..-457114, 725685.4) —— 离模型 45 万单位。
        //    按 R(-Twist) 转过之后是 (767492..767415, 383720..382944)，
        //    正好落在模型包围盒内。之前把原始值当 WCS 用，这 5 个视口的内容
        //    就被画到几十万单位外，表现为「布局里有几个视口是空白框」。
        const Math::Point3& GetViewCenter() const { return m_viewCenter; }
        void SetViewCenter(const Math::Point3& c) { m_viewCenter = c; }

        // 用 DWG 组码 12/22 的原始（DCS）值设置视图中心：内部转成 WCS 存。
        // twist 用同一个视口的 TwistAngle（组码 51）。
        void SetViewCenterDcs(const Math::Point3& dcsCenter, double twist)
        {
            const double c  = std::cos(-twist);
            const double sn = std::sin(-twist);
            const double rx = dcsCenter.x * c - dcsCenter.y * sn;
            const double ry = dcsCenter.x * sn + dcsCenter.y * c;
            m_viewCenter = Math::Point3{ rx, ry, dcsCenter.z };
        }

        // 取回组码 12/22 应写入的（DCS）值 —— SetViewCenterDcs 的逆变换。
        Math::Point3 GetViewCenterDcs(double twist) const
        {
            const double c  = std::cos(twist);
            const double sn = std::sin(twist);
            const double rx = m_viewCenter.x * c - m_viewCenter.y * sn;
            const double ry = m_viewCenter.x * sn + m_viewCenter.y * c;
            return Math::Point3{ rx, ry, m_viewCenter.z };
        }

        double GetViewHeight() const { return m_viewHeight; }
        void SetViewHeight(double h) { m_viewHeight = h; }

        double GetTwist() const { return m_twist; }
        void SetTwist(double a) { m_twist = a; }

        int GetViewportId() const { return m_id; }
        void SetViewportId(int id) { m_id = id; }

        // 是否应当把**模型内容**合成进来。
        //
        // 判据用**几何特征**而不是视口 id，因为实测 id 不可靠：
        //   · `test0.dxf`：主视口 id=1、浮动视口 id=2 —— 符合规范
        //   · `1#楼-结构-202104.dxf`：45 个视口里 44 个 **id=0**、1 个 id=1，
        //     但那些 id=0 的视图中心在模型坐标（如 (285989, 67673)）、
        //     视图高 59400 —— 显然是**真正显示模型内容**的浮动视口。
        //     若按「id > 1」判断会把这 44 个全部排除，布局依然空白。
        //
        // 可靠的区分：**图纸空间主视口代表纸张本身**，它的视图中心与框中心重合、
        // 且视图高等于框高（1:1）。浮动视口看向模型，三个值必然不同。
        bool ShowsModel() const
        {
            if (!(m_width > 0.0) || !(m_height > 0.0) || !(m_viewHeight > 0.0))
                return false;

            constexpr double kEps = 1.0e-6;
            const bool looksLikePaper =
                std::abs(m_viewCenter.x - m_center.x) <= kEps &&
                std::abs(m_viewCenter.y - m_center.y) <= kEps &&
                std::abs(m_viewHeight - m_height) <= kEps;
            return !looksLikePaper;
        }

        // ⚠ 不要用「scale 太小 ⇒ 异常视口」来排除视口。
        //
        // 曾这样做过，依据是某张图的视口视图高 3 千万、scale≈5e-6，
        // 把 4.5 万实体全合成了一遍（2100 万顶点 / 单帧 45 秒）。
        // 但核对数据发现判据是错的：
        //     模型包围盒 7.29e6 × 2.88e7，布局纸张 205.6 × 156
        //     按宽装入 scale = 2.82e-5
        //     按高装入 scale = 5.42e-6   ← 与实测的 5.14e-6 吻合
        //   ⇒ 那个视口正是「把整张图装进这张纸」的，是**正确**的；
        //     scale 小只是因为该图本身极高（Y 跨 2800 万单位）。
        // 性能问题应靠包围盒预剔除解决（见 ModelVisibleBox），不要靠排除视口。

        // 视口在**模型空间**里能看到的矩形（用于按包围盒预剔除实体）。
        //
        // 模型可见宽 = 视图高 × (框宽 / 框高)。这个矩形是绕 viewCenter
        // **旋转过 Twist** 的（纸面横向对上 UCS 横向，见 ModelToPaper），
        // 所以取它的**外接轴对齐盒** —— 外接盒 ⊇ 真实可见区域，
        // 预剔除只会多保留实体、不会漏内容（漏了就又是「视口空白」）。
        //
        // 不旋转（直接按 viewCenter ± hw/hh）时，Twist≠0 的外接盒偏小，
        // 会把本该显示的实体挡掉；实测 01-曲陵路.dwg 的 Twist=95.69°
        // 视口就差在这里。
        AABB ModelVisibleBox() const
        {
            const double hh = m_viewHeight * 0.5;
            const double aspect = (m_height > 0.0) ? (m_width / m_height) : 1.0;
            const double hw = hh * aspect;
            if (m_twist == 0.0)
                return AABB{ { m_viewCenter.x - hw, m_viewCenter.y - hh, 0.0 },
                             { m_viewCenter.x + hw, m_viewCenter.y + hh, 0.0 } };

            // 旋转矩形四角的外接盒。R(θ) 后 |x| 的最大值是
            //   hw·|cos θ| + hh·|sin θ|，|y| 的最大值是 hw·|sin θ| + hh·|cos θ|
            const double c = std::abs(std::cos(m_twist));
            const double s = std::abs(std::sin(m_twist));
            const double ex = hw * c + hh * s;
            const double ey = hw * s + hh * c;
            return AABB{ { m_viewCenter.x - ex, m_viewCenter.y - ey, 0.0 },
                         { m_viewCenter.x + ex, m_viewCenter.y + ey, 0.0 } };
        }

        // 模型 → 图纸的缩放系数。
        double ModelToPaperScale() const
        {
            return (m_viewHeight > 0.0) ? (m_height / m_viewHeight) : 1.0;
        }

        // 把一个模型空间点变换到图纸空间。
        //
        // ⚠ 必须与 `Editor::ComposeModelIntoViewport` 的合成矩阵逐字一致
        //    （平移 -viewCenter → 缩放 s → 旋转 **+Twist** → 平移 +center）。
        //    两者算的地方不同（拾取 / 包围盒走这里，渲染走矩阵），差一点
        //    就表现为「能选中但点下的位置不对」。
        //
        // 旋转方向依据见 Editor.cpp 同一处的说明：
        //   R(+Twist) 才把视口的 UcsXAxis 映射到纸面 +X；用 R(-Twist)
        //   会让内容整体转错约 90°（实测 01-曲陵路.dwg 5 个 Twist=95.69° 视口）。
        Math::Point3 ModelToPaper(const Math::Point3& p) const
        {
            const double s = ModelToPaperScale();
            double dx = p.x - m_viewCenter.x;
            double dy = p.y - m_viewCenter.y;
            if (m_twist != 0.0)
            {
                // 视图旋转：把内容按 +Twist 转正，使 UCS 横向对上纸张横向
                const double c  = std::cos(m_twist);
                const double sn = std::sin(m_twist);
                const double rx = dx * c - dy * sn;
                const double ry = dx * sn + dy * c;
                dx = rx;
                dy = ry;
            }
            return Math::Point3{ m_center.x + dx * s, m_center.y + dy * s, m_center.z };
        }

        // 纸张空间下的矩形（用于裁剪与包围盒）。
        AABB GetBoundingBox() const override
        {
            const double hw = m_width * 0.5;
            const double hh = m_height * 0.5;
            return AABB{ { m_center.x - hw, m_center.y - hh, m_center.z },
                         { m_center.x + hw, m_center.y + hh, m_center.z } };
        }

        bool ContainsPaper(const Math::Point3& p) const
        {
            const double hw = m_width * 0.5;
            const double hh = m_height * 0.5;
            return p.x >= m_center.x - hw && p.x <= m_center.x + hw
                && p.y >= m_center.y - hh && p.y <= m_center.y + hh;
        }

        std::unique_ptr<Entity> Clone(ObjectID newId) const override
        {
            auto e = std::make_unique<ViewportEntity>(newId, m_center, m_width, m_height);
            e->SetAttr(GetAttr());
            e->m_viewCenter = m_viewCenter;
            e->m_viewHeight = m_viewHeight;
            e->m_twist      = m_twist;
            e->m_id         = m_id;
            return e;
        }

        // 只画视口边框；模型内容由渲染层按视口变换合成（见 Editor 的视口合成路径）。
        void Draw(IDrawSink& sink, bool isSelected, bool isHovered) const override
        {
            const Math::Color4& color = isSelected ? IDrawSink::kSelectionColor
                                      : isHovered  ? IDrawSink::kHoverColor
                                                   : ResolveDrawColor(sink);

            const double hw = m_width * 0.5;
            const double hh = m_height * 0.5;
            const Math::Point3 a{ m_center.x - hw, m_center.y - hh, m_center.z };
            const Math::Point3 b{ m_center.x + hw, m_center.y - hh, m_center.z };
            const Math::Point3 c{ m_center.x + hw, m_center.y + hh, m_center.z };
            const Math::Point3 d{ m_center.x - hw, m_center.y + hh, m_center.z };

            sink.DrawLine(a, b, color, false);
            sink.DrawLine(b, c, color, false);
            sink.DrawLine(c, d, color, false);
            sink.DrawLine(d, a, color, false);
        }

        DECLARE_RUNTIME_TYPE(ViewportEntity, Entity)

    private:
        Math::Point3 m_center{ 0.0, 0.0, 0.0 };      // 纸张上的中心（10/20）
        double       m_width  = 0.0;                 // 纸张上的宽（40）
        double       m_height = 0.0;                 // 纸张上的高（41）

        Math::Point3 m_viewCenter{ 0.0, 0.0, 0.0 };  // 视图中心（12/22，模型坐标）
        double       m_viewHeight = 0.0;             // 视图高度（45，模型单位）
        double       m_twist      = 0.0;             // 视图旋转（50）
        int          m_id         = 0;               // 视口 id（68）
    };
}
