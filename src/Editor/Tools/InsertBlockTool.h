#pragma once
#include "Editor/Tools/ITool.h"
#include "Editor/EditorContext.h"
#include "Core/Draw/IDrawSink.hpp"
#include "Core/Entity/BlockEntity.hpp"
#include "Core/Entity/InsertEntity.hpp"
#include "Document/Command/BatchAddCommand.h"
#include "Core/Log.h"
#include <memory>
#include <string>
#include <vector>

namespace MiniCAD
{
    // 插入块(AutoCAD INSERT):块定义跟随光标预览(插入点对齐基点),
    // 左键落场一个 InsertEntity 后结束;右键/ESC 取消。
    // 预览经临时 InsertEntity 绘制到 Overlay 适配 sink,块内任意实体类型
    // (含嵌套块)无需逐类型特化。
    class InsertBlockTool : public ITool
    {
    public:
        InsertBlockTool(const BlockEntity* block, uint32_t blockId)
            : m_block(block)
            , m_blockId(blockId)
            , m_preview(0, blockId, block->GetName(), block->GetBasePoint())
        {
            m_preview.SetBlock(block);
            LOG_INFO("[InsertBlockTool] %s: 左键指定插入点 | 右键/ESC 取消", block->GetName().c_str());
        }

        bool OnInput(const EditorContext& ctx) override
        {
            m_ctx = &ctx;
            m_overlay = &ctx.overlay;      // 事件之外只用它：m_ctx 指向的栈对象事件返回后即失效
            const auto& e = ctx.event;

            if (e.IsLeftClick())
            {
                Commit(GetPoint(e)); 
                return true;
            }

            if (e.IsRightClick() || e.IsCancel())
            {
                Cancel();
                return true;
            }

            if (e.Type == InputEventType::MouseMove)
            {
                RebuildPreview(GetPoint(e));
                return false;
            }

            return false;
        }

        void Cancel() override
        {
            // overlay 由 Editor 统一清理(同 PasteTool,见其 Cancel 注释)
            if (OnFinished) OnFinished();
        }

        void OnSceneChanged()  override { Cancel(); }
        void OnFocusLost()     override { if (m_overlay) m_overlay->Clear(); }
        void OnFocusRestored() override {}

        std::string GetPrompt() const override
        {
            return "指定插入点 [右键/ESC 取消]:";
        }

    private:
        // 把实体 Draw 输出的线段/三角形转为 Overlay 线框
        class OverlaySink : public IDrawSink
        {
        public:
            OverlaySink(Overlay& ov, const Math::Color4& color) : m_ov(ov), m_color(color) {}

            void DrawLine(const Math::Point3& a, const Math::Point3& b, const Math::Color4&, bool) override
            {
                m_ov.AddLine(a, b, m_color);
            }
            void FillTriangle(const Math::Point3& a, const Math::Point3& b, const Math::Point3& c, const Math::Color4&) override
            {
                m_ov.AddLine(a, b, m_color);
                m_ov.AddLine(b, c, m_color);
                m_ov.AddLine(c, a, m_color);
            }

        private:
            Overlay&     m_ov;
            Math::Color4 m_color;
        };

        Math::Point3 GetPoint(const InputEvent& e) const
        {
            if (e.HasSnap) return e.SnapWorld;
            return m_ctx->viewport.GetCamera().ScreenToWorld(e.MouseX, e.MouseY);
        }

        void RebuildPreview(const Math::Point3& p)
        {
            m_ctx->overlay.Clear();
            m_preview.SetPosition(p);

            OverlaySink sink(m_ctx->overlay,
                             m_ctx->scene.GetLayerManager().GetActiveLayer().GetColor());
            m_preview.Draw(sink, false, false);
        }

        void Commit(const Math::Point3& p)
        {
            Scene& scene = m_ctx->scene;

            auto ins = std::make_unique<InsertEntity>(scene.NextObjectID(), m_blockId,
                                                      m_block->GetName(), p);
            ins->SetBlock(m_block);

            std::vector<std::unique_ptr<Object>> batch;
            batch.push_back(std::move(ins));
            if (m_ctx->cmdStack.Execute(std::make_unique<BatchAddCommand>(std::move(batch)), scene))
            {
                LOG_DEBUG("[InsertBlockTool] 插入 %s at (%.3f, %.3f)",
                          m_block->GetName().c_str(), p.x, p.y);
            }
        }

    private:
        const BlockEntity*   m_block;       // 非拥有,块表持有
        uint32_t             m_blockId;
        const EditorContext* m_ctx = nullptr;
        Overlay*              m_overlay = nullptr;
        InsertEntity         m_preview;     // 复用 InsertEntity 的展开/变换逻辑做预览
    };
}
