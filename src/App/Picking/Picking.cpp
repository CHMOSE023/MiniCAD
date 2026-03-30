#include "Picking.h"
#include "Render/Viewport/Camera.h"
#include "App/Scene/Scene.h"

using namespace DirectX;

namespace MiniCAD
{
    // 将屏幕坐标转成世界空间
    Ray Picking::ScreenPointToRay(const Camera& camera, float mouseX, float mouseY) const
    {
        float width  = camera.GetWidth();
        float height = camera.GetHeight();

        // 屏幕 → NDC
        float ndcX = (2.0f * mouseX / width - 1.0f);
        float ndcY = (1.0f - 2.0f * mouseY / height);

        XMMATRIX invViewProj = XMMatrixInverse(nullptr, camera.GetViewProj());

        XMVECTOR nearPoint = XMVector3TransformCoord(XMVectorSet(ndcX, ndcY, 0.0f, 1.0f), invViewProj);
        XMVECTOR farPoint  = XMVector3TransformCoord(XMVectorSet(ndcX, ndcY, 1.0f, 1.0f), invViewProj);

        Ray ray;
        ray.origin = nearPoint;
        ray.direction = XMVector3Normalize(farPoint - nearPoint);

        return ray;
    }

    // 点选：返回离相机最近的对象ID
    Object::ObjectID Picking::PickPoint(const Scene& scene, const Camera& camera, float mouseX, float mouseY) const
    {
        Ray ray = ScreenPointToRay(camera, mouseX, mouseY);

        float closestDist = FLT_MAX;
        Object::ObjectID picked = Object::InvalidID;

        // 选择
        const float threshold = 0.1f; // 世界单位阈值

        for (const auto& pair : scene.GetEntities())
        {
            Object* obj = pair.second.get();
            if (!obj) continue;

            if (obj->IsKindOf<LineEntity>())
            {
                auto line = static_cast<LineEntity*>(obj);
                XMVECTOR p0 = XMLoadFloat3(&line->GetLine().Start);
                XMVECTOR p1 = XMLoadFloat3(&line->GetLine().End);

                XMVECTOR u = p1 - p0;
                XMVECTOR d = ray.direction;
                XMVECTOR w0 = ray.origin - p0;

                float a = XMVectorGetX(XMVector3Dot(d, d));
                float b = XMVectorGetX(XMVector3Dot(d, u));
                float c = XMVectorGetX(XMVector3Dot(u, u));
                float d_ = XMVectorGetX(XMVector3Dot(d, w0));
                float e = XMVectorGetX(XMVector3Dot(u, w0));

                float denom = a * c - b * b;
                if (fabs(denom) < 1e-6f) continue; // 平行，忽略

                float s = (b * d_ - a * e) / denom;
                float t = (b * e - c * d_) / denom;

                s = std::clamp(s, 0.0f, 1.0f);
                if (t < 0) t = 0.0f;

                XMVECTOR closestLinePoint = p0 + s * u;
                XMVECTOR closestRayPoint = ray.origin + t * d;
                float dist = XMVectorGetX(XMVector3Length(closestLinePoint - closestRayPoint));

                if (dist < threshold && t < closestDist)
                {
                    closestDist = t;
                    picked = pair.first;
                }
            }
        }

        return picked;
    }
}