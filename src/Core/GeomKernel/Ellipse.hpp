#pragma once
#include "../Math/Point3.hpp"
#include "../Math/Constants.hpp"
#include "AABB.hpp"
#include <cmath>
#include <algorithm>

namespace MiniCAD
{
    // 椭圆：圆心 + 半长轴 / 半短轴 + 旋转角（弧度，逆时针）
    // 参数方程：
    //   P(t) = Center
    //        + RadiusX * cos(t) * [cos(Rotation), sin(Rotation)]
    //        + RadiusY * sin(t) * [-sin(Rotation), cos(Rotation)]
    struct Ellipse
    {
        Math::Point3 Center;
        double       RadiusX  = 0.0;   // 沿 Rotation 方向的半轴
        double       RadiusY  = 0.0;   // 垂直于 Rotation 方向的半轴
        double       Rotation = 0.0;   // 长轴相对 +X 轴的旋转角（弧度）

        Ellipse() = default;

        constexpr Ellipse(const Math::Point3& center,
                          double rx, double ry, double rotation = 0.0)
            : Center(center)
            , RadiusX(rx)
            , RadiusY(ry)
            , Rotation(rotation)
        {
        }

        // ── 有效性 ──────────────────────────────────────────────────────
        bool IsValid() const
        {
            return RadiusX > Math::LengthEPS && RadiusY > Math::LengthEPS;
        }

        // 是否近似为圆
        bool IsCircular() const
        {
            return std::abs(RadiusX - RadiusY) < Math::LengthEPS;
        }

        // ── 参数点 ──────────────────────────────────────────────────────
        // 参数 t ∈ [0, TwoPI)，逆时针
        Math::Point3 PointAt(double t) const
        {
            double cosR = std::cos(Rotation);
            double sinR = std::sin(Rotation);
            double cosT = std::cos(t);
            double sinT = std::sin(t);
            return
            {
                Center.x + RadiusX * cosT * cosR - RadiusY * sinT * sinR,
                Center.y + RadiusX * cosT * sinR + RadiusY * sinT * cosR,
                Center.z
            };
        }

        // 四个特征点（对应参数 t = 0, PI/2, PI, 3PI/2）
        Math::Point3 VertexE()  const { return PointAt(0.0);            } // 长轴正端
        Math::Point3 VertexN()  const { return PointAt(Math::PI * 0.5); } // 短轴正端
        Math::Point3 VertexW()  const { return PointAt(Math::PI);       } // 长轴负端
        Math::Point3 VertexS()  const { return PointAt(Math::PI * 1.5); } // 短轴负端

        // ── 最近点（迭代法，牛顿法 + 参数化） ──────────────────────────
        // 将点 p 变换到椭圆局部坐标系，然后在局部坐标系中做 1D 参数搜索
        Math::Point3 ClosestPoint(const Math::Point3& p) const
        {
            // 变换到局部坐标（去旋转）
            double cosR = std::cos(Rotation);
            double sinR = std::sin(Rotation);
            double dx   = p.x - Center.x;
            double dy   = p.y - Center.y;
            double lx   =  dx * cosR + dy * sinR;   // 局部 x
            double ly   = -dx * sinR + dy * cosR;   // 局部 y

            // 初始猜测：直接用 atan2
            double t = std::atan2(ly / (RadiusY + Math::LengthEPS),  lx / (RadiusX + Math::LengthEPS));

            // 牛顿迭代（最多 8 次，通常 3~4 次收敛）
            for (int i = 0; i < 8; ++i)
            {
                double cosT = std::cos(t);
                double sinT = std::sin(t);
                // 椭圆上的点（局部坐标）
                double ex   = RadiusX * cosT;
                double ey   = RadiusY * sinT;
                // 椭圆的切向量（局部坐标）
                double tx_  = -RadiusX * sinT;
                double ty_  =  RadiusY * cosT;
                // 残差：(P-E) · T = 0
                double f    = (lx - ex) * tx_ + (ly - ey) * ty_;
                // 导数
                double df   = (lx - ex) * (-RadiusX * cosT)  + (-tx_)   * tx_  + (ly - ey) * (-RadiusY * sinT)  + (-ty_)   * ty_;
                if (std::abs(df) < 1e-14) break;
                double dt = f / df;
                t -= dt;
                if (std::abs(dt) < 1e-9) break;
            }
            return PointAt(t);
        }

        // 点 p 到椭圆周的距离
        double DistanceToPoint(const Math::Point3& p) const
        {
            Math::Point3 cp = ClosestPoint(p);
            double dx = p.x - cp.x, dy = p.y - cp.y;
            return std::sqrt(dx * dx + dy * dy);
        }

        // ── 轴对齐包围盒（解析解）────────────────────────────────────────
        // 对旋转椭圆的 AABB 有精确公式：
        //   half_width  = sqrt((rx*cosR)^2 + (ry*sinR)^2)
        //   half_height = sqrt((rx*sinR)^2 + (ry*cosR)^2)
        AABB GetBounds() const
        {
            double cosR = std::cos(Rotation);
            double sinR = std::sin(Rotation);
            double hw   = std::sqrt(RadiusX * RadiusX * cosR * cosR +  RadiusY * RadiusY * sinR * sinR);
            double hh   = std::sqrt(RadiusX * RadiusX * sinR * sinR +  RadiusY * RadiusY * cosR * cosR);
            return
            {
                { Center.x - hw, Center.y - hh, Center.z },
                { Center.x + hw, Center.y + hh, Center.z }
            };
        }
    };
}