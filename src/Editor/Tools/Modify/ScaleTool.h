#pragma once
#include "Scene/Scene.h"
#include "Editor/Tools/ITool.h"
#include "Editor/EditorContext.h"
#include "Editor/Input/KeyCode.h"
#include "Editor/Overlay/EntityOverlay.h"
#include "Document/Command/EntityScale.h"
#include "Document/Command/GeometryEditCommand.h"
#include "Document/DimAssoc.h"
#include "Core/Math/Point3.hpp"
#include "Core/Object/Object.hpp"
#include "Core/Entity/Entity.hpp"
#include <cmath>
#include <unordered_map>
#include <vector>
#include "Core/Log.h"

namespace MiniCAD
{
    // =========================================================================
    // ScaleTool —— 缩放（预选对象）
    //
    // 基点 → 是否保留源对象 [Y/N] → 比例因子。
    // 比例因子 = 光标到基点的距离（与 AutoCAD 一致：以 1 个图形单位为参照）。
    // 因此键入数字（动态输入 / 命令行输入长度）就是直接指定比例因子，例如输入 2 放大一倍，
    // 输入 0.5 缩小一半；用鼠标拖动时因子随距离连续变化，左键确定。
    //
    // 不支持缩放的实体类型（ScaleEntityInPlace 返回 false）保持不变；全部不支持时不入撤销栈。
    // =========================================================================
    class ScaleTool : public ITool
    {
    public:
        explicit ScaleTool(std::vector<Object*> targets)
        {
            for (Object* o : targets)
                if (o) m_sourceIds.push_back(o->GetID());
            LOG_INFO("[ScaleTool] 左键指定缩放基点");
        }

        ~ScaleTool() { LOG_DEBUG("[ScaleTool] 退出"); }

        bool OnInput(const EditorContext& ctx) override
        {
            m_ctx     = &ctx;
            m_overlay = &ctx.overlay;
            const auto& e = ctx.event;

            if (e.IsRightClick() || e.IsCancel())
            {
                Cancel();
                return true;
            }

            switch (m_phase)
            {
            case Phase::Base:
                if (e.IsLeftClick())
                {
                    m_base  = GetPoint(e);
                    m_phase = Phase::AskCopy;
                    return true;
                }
                break;

            case Phase::AskCopy:
                if (e.Type == InputEventType::KeyDown)
                {
                    if (e.Key == KeyCode::Y)
                    {
                        m_keepSource = true;
                        m_phase = Phase::Factor;
                        return true;
                    }
                    if (e.Key == KeyCode::N || e.Key == KeyCode::Enter || e.Key == KeyCode::Space)
                    {
                        m_keepSource = false;
                        m_phase = Phase::Factor;
                        return true;
                    }
                }
                break;

            case Phase::Factor:
                if (e.IsLeftClick())
                {
                    Commit(Factor(GetPoint(e)));
                    return true;
                }
                if (e.Type == InputEventType::MouseMove)
                {
                    RebuildPreview(GetPoint(e));
                    return false;
                }
                break;
            }
            return false;
        }

        // 注意：m_ctx 指向 OnInput 期间的栈对象，事件之间已失效（撤销 / 重做会在两次事件之间调用
        // OnSceneChanged），所以事件之外只能用 m_overlay（编辑器成员，生命周期更长）
        void Cancel() override
        {
            if (m_overlay) m_overlay->Clear();
            if (OnFinished) OnFinished();
        }

        void OnSceneChanged()  override { Cancel(); }
        void OnFocusLost()     override { if (m_overlay) m_overlay->Clear(); }

        bool         HasAnchor() const override { return m_phase == Phase::Factor; }
        Math::Point3 GetAnchor() const override { return m_base; }

        std::string GetPrompt() const override
        {
            switch (m_phase)
            {
                case Phase::AskCopy: return "是否保留源对象? [是(Y)/否(N)]:";
                case Phase::Factor:  return "指定比例因子（键入数字，或移动光标，距基点的距离即比例）:";
                default:             return "指定基点:";
            }
        }

    private:
        enum class Phase { Base, AskCopy, Factor };

        Math::Point3 GetPoint(const InputEvent& e) const
        {
            if (e.HasSnap) return e.SnapWorld;
            return m_ctx->viewport.GetCamera().ScreenToWorld(e.MouseX, e.MouseY);
        }

        double Factor(const Math::Point3& cur) const
        {
            return std::hypot(cur.x - m_base.x, cur.y - m_base.y);
        }

        void RebuildPreview(const Math::Point3& cur)
        {
            m_ctx->overlay.Clear();
            const double k = Factor(cur);
            if (!(k > 1e-9))
                return;

            static const Math::Color4 guide = { 0.6, 0.6, 0.6, 0.6 };
            m_ctx->overlay.AddLine(m_base, cur, guide);

            const auto& color = m_ctx->scene.GetLayerManager().GetActiveLayer().GetColor();
            for (auto id : m_sourceIds)
            {
                const auto* src = dynamic_cast<const Entity*>(m_ctx->scene.GetEntity(id));
                if (!src) continue;
                auto temp = src->Clone(0);
                if (ScaleEntityInPlace(*temp, m_base, k))
                    DrawEntityToOverlay(m_ctx->overlay, *temp, color);
            }
        }

        void Commit(double k)
        {
            if (!(k > 1e-9))
            {
                LOG_WARN("[ScaleTool] 比例因子必须大于 0");
                return;                         // 留在工具里，让用户重新指定
            }

            std::vector<GeometryEditCommand::Item> items;
            std::unordered_map<Object::ObjectID, Object::ObjectID> srcToNew;   // 保留源时：源 ID → 副本 ID
            size_t skipped = 0;
            for (auto id : m_sourceIds)
            {
                const auto* src = dynamic_cast<const Entity*>(m_ctx->scene.GetEntity(id));
                if (!src) continue;

                GeometryEditCommand::Item item;
                if (m_keepSource)
                {
                    item.id    = m_ctx->scene.NextObjectID();
                    item.after = src->Clone(item.id);
                }
                else
                {
                    item.id     = id;
                    item.before = src->Clone(id);
                    item.after  = src->Clone(id);
                }
                if (!ScaleEntityInPlace(*item.after, m_base, k))
                {
                    ++skipped;
                    continue;
                }
                if (m_keepSource) srcToNew[id] = item.id;
                items.push_back(std::move(item));
            }

            // 保留源时生成的是副本：一起复制的标注改为关联到副本
            if (m_keepSource)
                for (auto& it : items) DimAssoc::RemapCopy(*it.after, srcToNew);

            if (skipped > 0)
                LOG_WARN("[ScaleTool] %zu 个对象的类型暂不支持缩放，已跳过", skipped);

            if (!items.empty())
            {
                m_ctx->cmdStack.Execute(std::make_unique<GeometryEditCommand>("缩放", std::move(items)), m_ctx->scene);
                LOG_DEBUG("[ScaleTool] 缩放 %zu 个对象，比例 %.4f%s", m_sourceIds.size() - skipped, k, m_keepSource ? "（保留源）" : "");
            }

            m_ctx->overlay.Clear();
            if (OnFinished) OnFinished();
        }

        std::vector<Object::ObjectID> m_sourceIds;
        const EditorContext*          m_ctx = nullptr;          // 仅在 OnInput 内有效
        Overlay*                      m_overlay = nullptr;
        Phase                         m_phase = Phase::Base;
        Math::Point3                  m_base{};
        bool                          m_keepSource = false;
    };
}
