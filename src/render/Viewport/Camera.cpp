// ============================================================
// MiniCAD — render/Viewport/Camera.cpp
// ============================================================

#include "render/Viewport/Camera.h"
#include <cassert>
#include <cmath>

namespace MiniCAD {

    Camera::Camera() = default;

    void Camera::SetProjectionMode(ProjectionMode mode) {
        if (m_projMode != mode) { m_projMode = mode; m_projDirty = true; }
    }

    void Camera::LookAt(const Point3& eye, const Point3& target, const Vec3& up) {
        m_eye = eye; m_target = target; m_up = up;
        m_viewDirty = true;
    }

    void Camera::SetPosition(const Point3& pos) { m_eye = pos;    m_viewDirty = true; }
    void Camera::SetTarget(const Point3& t) { m_target = t;      m_viewDirty = true; }
    void Camera::SetUp(const Vec3& up) { m_up = up;     m_viewDirty = true; }

    // ============================================================
    // 正交参数
    // ============================================================

    void Camera::SetOrthoExtent(Real left, Real right, Real bottom, Real top,
        Real nearZ, Real farZ) {
        m_orthoLeft = left; m_orthoRight = right;
        m_orthoBottom = bottom; m_orthoTop = top;
        m_orthoNear = nearZ; m_orthoFar = farZ;
        m_projDirty = true;
    }

    void Camera::SetOrthoFromViewport(Real viewportWidth, Real viewportHeight,
        Real zoom, Real nearZ, Real farZ) {
        assert(FLOAT_LT(Real(0), zoom) && "Camera: zoom must be positive");
        const Real halfW = (viewportWidth * Real(0.5)) / zoom;
        const Real halfH = (viewportHeight * Real(0.5)) / zoom;
        SetOrthoExtent(-halfW, halfW, -halfH, halfH, nearZ, farZ);
    }

    void Camera::SetZoom(Real zoom) {
        assert(FLOAT_LT(Real(0), zoom) && "Camera: zoom must be positive");
        m_zoom = zoom;
        m_projDirty = true;
    }

    // ============================================================
    // 透视参数
    // ============================================================

    void Camera::SetPerspective(Real fovRad, Real aspect, Real nearZ, Real farZ) {
        m_fovRad = fovRad;  // radians
        m_aspect = aspect;
        m_nearZ = nearZ;
        m_farZ = farZ;
        m_projDirty = true;
    }

    // ============================================================
    // 矩阵构建（全程 Real，无 float 混算）
    // ============================================================

    void Camera::RebuildViewMatrix() const {
        Vec3 forward = Vec3{ m_target.x - m_eye.x,
                              m_target.y - m_eye.y,
                              m_target.z - m_eye.z }.Normalized();
        Vec3 right = forward.Cross(m_up).Normalized();
        Vec3 up = right.Cross(forward);

        Vec3 eye{ m_eye.x, m_eye.y, m_eye.z };

        m_viewMatrix = Mat4::FromRows(
            right.x, right.y, right.z, -right.Dot(eye),
            up.x, up.y, up.z, -up.Dot(eye),
            -forward.x, -forward.y, -forward.z, forward.Dot(eye),
            Real(0), Real(0), Real(0), Real(1)
        );
        m_viewDirty = false;
    }

    void Camera::RebuildProjMatrix() const {
        if (m_projMode == ProjectionMode::ORTHOGRAPHIC) {
            const Real rml = m_orthoRight - m_orthoLeft;
            const Real tmb = m_orthoTop - m_orthoBottom;
            const Real fmn = m_orthoFar - m_orthoNear;

            assert(!FLOAT_EQ(rml, Real(0)));
            assert(!FLOAT_EQ(tmb, Real(0)));
            assert(!FLOAT_EQ(fmn, Real(0)));

            // D3D11 NDC: z ∈ [0, 1]
            m_projMatrix = Mat4::FromRows(
                Real(2) / rml, Real(0), Real(0), -(m_orthoRight + m_orthoLeft) / rml,
                Real(0), Real(2) / tmb, Real(0), -(m_orthoTop + m_orthoBottom) / tmb,
                Real(0), Real(0), Real(-1) / fmn, -m_orthoNear / fmn,
                Real(0), Real(0), Real(0), Real(1)
            );
        }
        else {
            assert(FLOAT_LT(Real(0), m_fovRad));
            assert(FLOAT_LT(Real(0), m_aspect));

            const Real tanHalfFov = std::tan(m_fovRad * Real(0.5));
            const Real fmn = m_farZ - m_nearZ;

            assert(FLOAT_LT(Real(0), tanHalfFov));
            assert(!FLOAT_EQ(fmn, Real(0)));

            m_projMatrix = Mat4::FromRows(
                Real(1) / (m_aspect * tanHalfFov), Real(0), Real(0), Real(0),
                Real(0), Real(1) / tanHalfFov, Real(0), Real(0),
                Real(0), Real(0), m_farZ / fmn, -m_nearZ * m_farZ / fmn,
                Real(0), Real(0), Real(1), Real(0)
            );
        }
        m_projDirty = false;
    }

    const Mat4& Camera::GetViewMatrix()  const { if (m_viewDirty) RebuildViewMatrix(); return m_viewMatrix; }
    const Mat4& Camera::GetProjMatrix()  const { if (m_projDirty) RebuildProjMatrix(); return m_projMatrix; }
    Mat4        Camera::GetViewProjMatrix() const { return GetProjMatrix() * GetViewMatrix(); }

    // ============================================================
    // 坐标转换（全程 Real，最后转 float 只发生在 DrawPrimitives）
    // ============================================================

    Ray Camera::ScreenToRay(const Vec2& screenPos,
        Real viewportWidth, Real viewportHeight) const {
        // 屏幕 → NDC（D3D11: y 朝下 → NDC y 朝上）
        const Real ndcX = (Real(2) * screenPos.x / viewportWidth) - Real(1);
        const Real ndcY = -(Real(2) * screenPos.y / viewportHeight) + Real(1);

        const Mat4& proj = GetProjMatrix();
        const Mat4& view = GetViewMatrix();

        if (m_projMode == ProjectionMode::ORTHOGRAPHIC) {
            Vec3 forward = Vec3{ m_target.x - m_eye.x,
                                  m_target.y - m_eye.y,
                                  m_target.z - m_eye.z }.Normalized();

            const Real viewX = ndcX / proj.At(0, 0);   // proj[0][0]
            const Real viewY = ndcY / proj.At(1, 1);   // proj[1][1]

            const Mat4  invView = view.Inverse();
            const Vec4  originV{ viewX, viewY, Real(0), Real(1) };
            const Vec4  originW = invView * originV;

            Ray ray;
            ray.origin = Point3{ originW.x, originW.y, originW.z };
            ray.direction = forward;
            return ray;
        }
        else {
            const Real viewX = ndcX / proj.At(0, 0);
            const Real viewY = ndcY / proj.At(1, 1);

            const Mat4 invView = view.Inverse();
            const Vec4 dirV{ viewX, viewY, Real(-1), Real(0) };
            const Vec4 dirW = invView * dirV;

            Ray ray;
            ray.origin = m_eye;
            ray.direction = Vec3{ dirW.x, dirW.y, dirW.z }.Normalized();
            return ray;
        }
    }

    Point3 Camera::ScreenToWorld(const Vec2& screenPos, Real depth,
        Real viewportWidth, Real viewportHeight) const {
        const Real ndcX = (Real(2) * screenPos.x / viewportWidth) - Real(1);
        const Real ndcY = -(Real(2) * screenPos.y / viewportHeight) + Real(1);

        const Mat4 invVP = GetViewProjMatrix().Inverse();
        const Vec4 ndc{ ndcX, ndcY, depth, Real(1) };
        const Vec4 world = invVP * ndc;

        assert(!FLOAT_EQ(world.w, Real(0)));
        const Real invW = Real(1) / world.w;
        return Point3{ world.x * invW, world.y * invW, world.z * invW };
    }

    Vec2 Camera::WorldToScreen(const Point3& worldPos,
        Real viewportWidth, Real viewportHeight) const {
        const Vec4 clip = GetViewProjMatrix()
            * Vec4 {
            worldPos.x, worldPos.y, worldPos.z, Real(1)
        };

        assert(!FLOAT_EQ(clip.w, Real(0)));
        const Real ndcX = clip.x / clip.w;
        const Real ndcY = clip.y / clip.w;

        return Vec2{
            (ndcX + Real(1)) * Real(0.5) * viewportWidth,
            (Real(1) - ndcY) * Real(0.5) * viewportHeight
        };
    }

    Camera::ScreenToRayFn Camera::MakeScreenToRayCallback(Real viewportWidth,
        Real viewportHeight) const {
        return [this, viewportWidth, viewportHeight](const Vec2& screenPos) -> Ray {
            return this->ScreenToRay(screenPos, viewportWidth, viewportHeight);
            };
    }

} // namespace MiniCAD