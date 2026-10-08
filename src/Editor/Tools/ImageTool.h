#pragma once
#include "Scene/Scene.h"
#include "Editor/Tools/ITool.h"
#include "Editor/EditorContext.h"
#include "Document/Command/AddEntityCommand.h"
#include "Core/Math/Point3.hpp"
#include "Core/Entity/ImageEntity.hpp"
#include "Core/Log.h"
#include <cmath>
#include <string>

namespace MiniCAD
{
    // 光栅图像放置工具（IMAGE）：
    //   第 1 点 = 图像左下角；第 2 点 = 决定宽度与旋转（高度按图像比例），回车 / 右键 = 1 像素 1 单位、不旋转。
    class ImageTool : public ITool
    {
    public:
        ImageTool(std::string path, uint32_t pixelW, uint32_t pixelH)
            : m_path(std::move(path)), m_pw(pixelW ? pixelW : 1), m_ph(pixelH ? pixelH : 1)
        {
            LOG_DEBUG("[ImageTool] 左键指定左下角 | 再指定宽度 / 旋转（回车 = 原始大小）| ESC 退出");
        }

        bool OnInput(const EditorContext& ctx) override
        {
            m_ctx = &ctx;
            const auto& e = ctx.event;

            if (e.IsLeftClick())
            {
                const auto pt = GetPoint(e);
                if (!m_hasBase)
                {
                    m_base    = pt;
                    m_hasBase = true;
                    Preview(pt);
                }
                else
                {
                    const double dx = pt.x - m_base.x, dy = pt.y - m_base.y;
                    const double w  = std::sqrt(dx * dx + dy * dy);
                    if (w > 1e-9)
                        Commit(w, std::atan2(dy, dx));
                }
                return true;
            }

            if (e.Type == InputEventType::MouseMove)
            {
                Preview(GetPoint(e));
                return false;
            }

            if (m_hasBase && (e.IsKeyPressed(KeyCode::Enter) || e.IsRightClick()))
            {
                Commit(static_cast<double>(m_pw), 0.0);
                return true;
            }

            if (e.IsRightClick() || e.IsCancel())
            {
                m_ctx->overlay.Clear();
                if (OnFinished) OnFinished();
                return true;
            }

            return false;
        }

        bool HasAnchor() const override { return m_hasBase; }
        Math::Point3 GetAnchor() const override { return m_base; }

        std::string GetPrompt() const override
        {
            return m_hasBase ? "指定宽度与旋转 [回车=原始大小]:" : "指定图像左下角:";
        }

    private:
        Math::Point3 GetPoint(const InputEvent& e) const
        {
            if (e.HasSnap) return e.SnapWorld;
            return m_ctx->viewport.GetCamera().ScreenToWorld(e.MouseX, e.MouseY);
        }

        // 四角：宽度 w、旋转 a，高度按像素比例
        void Corners(const Math::Point3& base, double w, double a, Math::Point3 out[4]) const
        {
            const double h  = w * static_cast<double>(m_ph) / static_cast<double>(m_pw);
            const double ux = std::cos(a), uy = std::sin(a);
            out[0] = base;
            out[1] = { base.x + ux * w,            base.y + uy * w,            base.z };
            out[2] = { base.x + ux * w - uy * h,   base.y + uy * w + ux * h,   base.z };
            out[3] = { base.x - uy * h,            base.y + ux * h,            base.z };
        }

        // 未点第 1 点：原始大小的预览跟着光标；点了之后：按光标距离定宽度和旋转
        void Preview(const Math::Point3& cursor)
        {
            m_ctx->overlay.Clear();
            Math::Point3 c[4];
            if (!m_hasBase)
                Corners(cursor, static_cast<double>(m_pw), 0.0, c);
            else
            {
                const double dx = cursor.x - m_base.x, dy = cursor.y - m_base.y;
                const double w  = std::sqrt(dx * dx + dy * dy);
                if (w < 1e-9) return;
                Corners(m_base, w, std::atan2(dy, dx), c);
            }
            m_ctx->overlay.AddRect(c[0], c[1], c[2], c[3], { 1.0, 1.0, 1.0, 1.0 });
        }

        void Commit(double width, double angle)
        {
            Math::Point3 c[4];
            Corners(m_base, width, angle, c);

            auto id  = m_ctx->scene.NextObjectID();
            auto img = std::make_unique<ImageEntity>(id, m_path, c[0], c[1], c[2], c[3]);
            m_ctx->ApplyCurrentAttr(*img);
            m_ctx->cmdStack.Execute(std::make_unique<AddEntityCommand>(std::move(img)), m_ctx->scene);
            LOG_DEBUG("[ImageTool] 图像 Id=%d 宽 %.3f 旋转 %.3f", static_cast<int>(id), width, angle);

            m_ctx->overlay.Clear();
            if (OnFinished) OnFinished();
        }

    private:
        const EditorContext* m_ctx = nullptr;
        std::string          m_path;
        uint32_t             m_pw = 1, m_ph = 1;
        bool                 m_hasBase = false;
        Math::Point3         m_base{};
    };
}
