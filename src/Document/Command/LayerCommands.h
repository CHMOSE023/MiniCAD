#pragma once
#include "Document/CommandStack/ICommand.h"
#include "Scene/Scene.h"
#include "Scene/Layer.h"
#include "Scene/LayerManager.h"
#include "Core/Entity/Entity.hpp"
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace MiniCAD
{
    // =========================================================================
    // 图层操作命令：新建 / 删除 / 修改（改名、颜色、开关、锁定、线型、线宽），全部可撤销。
    //
    //   · 图层 ID 稳定：撤销删除 / 重做新建都用原来的 ID，实体对图层的引用不会错位。
    //   · 图层特性变化影响所有引用它的实体的显示（颜色 / 线型 / 线宽 / 可见性随层），
    //     所以统一用 Scene::MarkDisplayDirty()：实体显示缓存全部失效，但几何不变，拾取索引不动。
    //   · 当前图层（CLAYER）的切换不是文档编辑，不走命令；但新建图层时“置为当前”、
    //     删除当前图层时回退到 0 层，会随命令一起撤销。
    // =========================================================================

    namespace LayerOps
    {
        inline bool NameTaken(const LayerManager& lm, const std::string& name, LayerID except = 0xFFFFFFFFu)
        {
            for (LayerID id : lm.GetAllLayerIDs())
                if (id != except && lm.GetLayer(id)->GetName() == name)
                    return true;
            return false;
        }

        // 不重名的“图层N”
        inline std::string UniqueName(const LayerManager& lm, const std::string& prefix = "图层")
        {
            int n = static_cast<int>(lm.GetAllLayerIDs().size());
            while (NameTaken(lm, prefix + std::to_string(n)))
                ++n;
            return prefix + std::to_string(n);
        }
    }

    // ── 新建图层 ────────────────────────────────────────────────────────────
    class AddLayerCommand : public ICommand
    {
    public:
        // makeCurrent：新建后置为当前图层（面板“新建”的行为）
        explicit AddLayerCommand(std::string name, bool makeCurrent = true)
            : m_name(std::move(name)), m_makeCurrent(makeCurrent) {}

        bool Execute(Scene& scene) override
        {
            LayerManager& lm = scene.GetLayerManager();
            if (!m_layer)
            {
                // 首次执行：分配 ID。名称为空 / 重名则失败
                if (m_name.empty() || LayerOps::NameTaken(lm, m_name))
                    return false;
                m_id    = lm.AddLayer(m_name);
                m_layer = std::make_unique<Layer>(*lm.GetLayer(m_id));
            }
            else
            {
                // 重做：用同一个 ID 把图层放回去
                if (lm.GetLayer(m_id) || LayerOps::NameTaken(lm, m_layer->GetName()))
                    return false;
                lm.AddLayer(std::make_unique<Layer>(*m_layer));
            }

            m_prevActive = lm.GetActiveLayerID();
            if (m_makeCurrent)
                lm.SetActiveLayerID(m_id);
            scene.MarkDisplayDirty();
            return true;
        }

        void Undo(Scene& scene) override
        {
            LayerManager& lm = scene.GetLayerManager();
            if (const Layer* l = lm.GetLayer(m_id))
                m_layer = std::make_unique<Layer>(*l);          // 保存撤销时的状态（可能被后续命令改过后又还原）
            lm.SetActiveLayerID(m_prevActive);
            lm.RemoveLayer(m_id);
            scene.MarkDisplayDirty();
        }

        std::string GetName() const override { return "新建图层"; }
        LayerID     GetLayerID() const       { return m_id; }       // Execute 成功后有效

    private:
        std::string            m_name;
        bool                   m_makeCurrent;
        LayerID                m_id         = Layer::DefaultLayerID;
        LayerID                m_prevActive = Layer::DefaultLayerID;
        std::unique_ptr<Layer> m_layer;                             // 图层快照（重做用）
    };

    // ── 删除图层 ────────────────────────────────────────────────────────────
    // 引用该图层的实体改到 0 层（避免悬空引用）；0 层不能删除。撤销时图层和这些实体的归属都还原。
    class DeleteLayerCommand : public ICommand
    {
    public:
        explicit DeleteLayerCommand(LayerID id) : m_id(id) {}

        bool Execute(Scene& scene) override
        {
            LayerManager& lm = scene.GetLayerManager();
            const Layer* layer = lm.GetLayer(m_id);
            if (m_id == Layer::DefaultLayerID || !layer)
                return false;

            m_layer = std::make_unique<Layer>(*layer);
            m_moved.clear();
            for (auto eid : scene.GetAllIDs())
            {
                Object* obj = scene.GetEntity(eid);
                if (!obj || !obj->IsKindOf<Entity>()) continue;
                Entity& ent = static_cast<Entity&>(*obj);
                if (ent.GetLayerID() != m_id) continue;
                m_moved.push_back(eid);
                ent.SetLayerId(Layer::DefaultLayerID);
            }
            m_wasActive = lm.GetActiveLayerID() == m_id;
            if (m_wasActive)
                lm.SetActiveLayerID(Layer::DefaultLayerID);
            lm.RemoveLayer(m_id);
            scene.MarkDisplayDirty();
            return true;
        }

        void Undo(Scene& scene) override
        {
            LayerManager& lm = scene.GetLayerManager();
            if (!m_layer) return;
            lm.AddLayer(std::make_unique<Layer>(*m_layer));
            for (auto eid : m_moved)
                if (Object* obj = scene.GetEntity(eid); obj && obj->IsKindOf<Entity>())
                    static_cast<Entity*>(obj)->SetLayerId(m_id);
            if (m_wasActive)
                lm.SetActiveLayerID(m_id);
            scene.MarkDisplayDirty();
        }

        std::string GetName() const override { return "删除图层"; }
        size_t      MovedCount() const       { return m_moved.size(); }

    private:
        LayerID                       m_id;
        std::unique_ptr<Layer>        m_layer;
        std::vector<Object::ObjectID> m_moved;
        bool                          m_wasActive = false;
    };

    // ── 修改图层特性 ────────────────────────────────────────────────────────
    struct LayerChange
    {
        std::optional<std::string>   Name;
        std::optional<Math::Color4>  Color;
        std::optional<bool>          Visible;
        std::optional<bool>          Locked;
        std::optional<LineTypeID>    LineType;       // 图层线型只能是具名线型（不能 ByLayer / ByBlock）
        std::optional<Lineweight>    Lineweight;

        bool Empty() const { return !Name && !Color && !Visible && !Locked && !LineType && !Lineweight; }
    };

    class ChangeLayerCommand : public ICommand
    {
    public:
        ChangeLayerCommand(LayerID id, LayerChange change) : m_id(id), m_change(std::move(change)) {}

        bool Execute(Scene& scene) override
        {
            LayerManager& lm = scene.GetLayerManager();
            Layer* l = lm.GetLayer(m_id);
            if (!l) return false;

            // 去掉非法的字段：0 层不能改名；名称不能为空 / 重名；线型必须是场景里存在的具名线型
            LayerChange c = m_change;
            if (c.Name && (m_id == Layer::DefaultLayerID || c.Name->empty() || LayerOps::NameTaken(lm, *c.Name, m_id)))
                c.Name.reset();
            if (c.LineType)
            {
                const LineTypeID lt = *c.LineType;
                if (lt < LineTypeTable::ContinuousID || !scene.GetLineTypeTable().Find(lt))
                    c.LineType.reset();
            }

            auto sameColor = [](const Math::Color4& a, const Math::Color4& b) { return a.r == b.r && a.g == b.g && a.b == b.b && a.a == b.a; };
            if (c.Name       && *c.Name == l->GetName())                 c.Name.reset();
            if (c.Color      && sameColor(*c.Color, l->GetColor()))      c.Color.reset();
            if (c.Visible    && *c.Visible == l->IsVisible())            c.Visible.reset();
            if (c.Locked     && *c.Locked == l->IsLocked())              c.Locked.reset();
            if (c.LineType   && *c.LineType == l->GetLineType())         c.LineType.reset();
            if (c.Lineweight && *c.Lineweight == l->GetLineweight())     c.Lineweight.reset();
            if (c.Empty()) return false;

            m_before = std::make_unique<Layer>(*l);
            if (c.Name)       l->SetName(*c.Name);
            if (c.Color)      l->SetColor(*c.Color);
            if (c.Visible)    l->SetVisible(*c.Visible);
            if (c.Locked)     l->SetLocked(*c.Locked);
            if (c.LineType)   l->SetLineType(*c.LineType);
            if (c.Lineweight) l->SetLineweight(*c.Lineweight);
            scene.MarkDisplayDirty();
            return true;
        }

        void Undo(Scene& scene) override
        {
            Layer* l = scene.GetLayerManager().GetLayer(m_id);
            if (!l || !m_before) return;
            *l = *m_before;
            scene.MarkDisplayDirty();
        }

        std::string GetName() const override
        {
            const LayerChange& c = m_change;
            const int fields = int(c.Name.has_value()) + int(c.Color.has_value()) + int(c.Visible.has_value())
                             + int(c.Locked.has_value()) + int(c.LineType.has_value()) + int(c.Lineweight.has_value());
            if (fields == 1)
            {
                if (c.Name)       return "图层改名";
                if (c.Color)      return "修改图层颜色";
                if (c.Visible)    return (*c.Visible) ? "打开图层" : "关闭图层";
                if (c.Locked)     return (*c.Locked) ? "锁定图层" : "解锁图层";
                if (c.LineType)   return "修改图层线型";
                return "修改图层线宽";
            }
            return "修改图层特性";
        }

    private:
        LayerID                m_id;
        LayerChange            m_change;
        std::unique_ptr<Layer> m_before;
    };
}
