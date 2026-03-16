// ============================================================
// MiniCAD — render/Viewport/Camera.h
// 职责：正交 / 透视相机，提供 ViewMatrix / ProjMatrix / ScreenToRay
// 依赖：math/MathDefs.hpp, math/Matrix.hpp, math/Vector.hpp, math/Point.hpp
// 约束：不依赖 D3D11 API，不依赖 core / app / ui
// ============================================================
#pragma once

#include "math/MathDefs.hpp"
#include "math/Matrix.hpp"
#include "math/Vector.hpp"
#include "math/Point.hpp"
#include <functional>

namespace MiniCAD {

    struct Ray {
        Point3 origin;
        Vec3   direction;   // 已归一化
    };

    class Camera {
    public:
        enum class ProjectionMode : uint8_t {
            ORTHOGRAPHIC = 0,
            PERSPECTIVE = 1,
        };

        Camera();
        ~Camera() = default;

        // --- 投影模式 ---
        void           SetProjectionMode(ProjectionMode mode);
        ProjectionMode GetProjectionMode() const { return m_projMode; }

        // --- 视图变换 ---
        void LookAt(const Point3& eye, const Point3& target, const Vec3& up);
        void SetPosition(const Point3& pos);
        void SetTarget(const Point3& target);
        void SetUp(const Vec3& up);

        const Point3& GetPosition() const { return m_eye; }
        const Point3& GetTarget()   const { return m_target; }
        const Vec3& GetUp()       const { return m_up; }

        // --- 正交投影参数（全部 Real） ---
        void SetOrthoExtent(Real left, Real right, Real bottom, Real top,
            Real nearZ, Real farZ);
        void SetOrthoFromViewport(Real viewportWidth, Real viewportHeight,
            Real zoom, Real nearZ, Real farZ);

        Real GetZoom() const { return m_zoom; }
        void SetZoom(Real zoom);

        // --- 透视投影参数（全部 Real） ---
        void SetPerspective(Real fovRad,   // radians
            Real aspect,
            Real nearZ,
            Real farZ);

        // --- 矩阵访问 ---
        const Mat4& GetViewMatrix()  const;
        const Mat4& GetProjMatrix()  const;
        Mat4        GetViewProjMatrix() const;

        // --- 坐标转换（视口尺寸用 Real，彻底消除混算） ---
        Ray    ScreenToRay(const Vec2& screenPos,
            Real viewportWidth, Real viewportHeight) const;

        Point3 ScreenToWorld(const Vec2& screenPos, Real depth,
            Real viewportWidth, Real viewportHeight) const;

        Vec2   WorldToScreen(const Point3& worldPos,
            Real viewportWidth, Real viewportHeight) const;

        // Picker 注入回调
        using ScreenToRayFn = std::function<Ray(const Vec2&)>;
        ScreenToRayFn MakeScreenToRayCallback(Real viewportWidth,
            Real viewportHeight) const;

    private:
        void RebuildViewMatrix() const;
        void RebuildProjMatrix() const;

        // 视图参数（Real）
        Point3 m_eye = { Real(0), Real(0), Real(10) };
        Point3 m_target = { Real(0), Real(0), Real(0) };
        Vec3   m_up = { Real(0), Real(1), Real(0) };

        // 正交参数（Real）
        Real m_orthoLeft = Real(-1);
        Real m_orthoRight = Real(1);
        Real m_orthoBottom = Real(-1);
        Real m_orthoTop = Real(1);
        Real m_orthoNear = Real(-100);
        Real m_orthoFar = Real(100);
        Real m_zoom = Real(1);

        // 透视参数（Real）
        Real m_fovRad = Real(1.0471975511965977);  // 60°，精确 double 值
        Real m_aspect = Real(1.7777777777777777);  // 16:9
        Real m_nearZ = Real(0.1);
        Real m_farZ = Real(1000);

        ProjectionMode m_projMode = ProjectionMode::ORTHOGRAPHIC;

        mutable Mat4 m_viewMatrix;
        mutable Mat4 m_projMatrix;
        mutable bool m_viewDirty = true;
        mutable bool m_projDirty = true;
    };

} // namespace MiniCAD