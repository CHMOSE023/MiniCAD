#pragma once
#include "Document/CommandStack/ICommand.h"
#include "Scene/Scene.h"
#include "Scene/Layer.h"
#include "Scene/LayerManager.h"
#include "Scene/LayerPath.h"
#include "Core/Entity/Entity.hpp"
#include <algorithm>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
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
        std::optional<MiniCAD::Lineweight> Lineweight;

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

    // =========================================================================
    // 图层层级（Scene/LayerPath.h）：对上级改名、开关、锁定、删除时下级图层一起改，合成一步撤销。
    // 下级不继承上级的状态：上级关闭时把下级也逐个关闭，存进文件的就是各图层实际的开关，AutoCAD 里看到的相同。
    // =========================================================================

    // 一组图层命令合成一步撤销。与 BatchCommand 不同，没有生效的子命令（例如本来就关着的下级图层）跳过，
    // 不让整批失败；一个都没生效时失败
    class LayerBatchCommand : public ICommand
    {
    public:
        explicit LayerBatchCommand(std::vector<std::unique_ptr<ICommand>> cmds) : m_commands(std::move(cmds)) {}

        bool Execute(Scene& scene) override
        {
            if (m_executed)
            {
                for (auto& c : m_commands)              // 重做：都是上次生效过的
                    c->Execute(scene);
                return true;
            }
            std::vector<std::unique_ptr<ICommand>> done;
            for (auto& c : m_commands)
                if (c->Execute(scene))
                    done.push_back(std::move(c));
            m_commands = std::move(done);
            m_executed = true;
            return !m_commands.empty();
        }

        void Undo(Scene& scene) override
        {
            for (auto it = m_commands.rbegin(); it != m_commands.rend(); ++it)
                (*it)->Undo(scene);
        }

        std::string GetName() const override { return m_commands.empty() ? "修改图层" : m_commands.front()->GetName(); }

    private:
        std::vector<std::unique_ptr<ICommand>> m_commands;
        bool                                   m_executed = false;
    };

    namespace LayerOps
    {
        // 名为 path 的图层与它的全部下级图层（path 可以没有对应的图层：只有 "A$B" 时的 "A"）
        inline std::vector<LayerID> Subtree(const LayerManager& lm, std::string_view path)
        {
            std::vector<LayerID> ids;
            for (LayerID id : lm.GetAllLayerIDs())
                if (LayerPath::InSubtree(lm.GetLayer(id)->GetName(), path))
                    ids.push_back(id);
            return ids;
        }

        // 上级与全部下级都改成 change（开关、锁定等；不用来改名，改名用 RenameLayerTreeCommand）
        inline std::unique_ptr<ICommand> ChangeTree(const LayerManager& lm, std::string_view path, const LayerChange& change)
        {
            std::vector<std::unique_ptr<ICommand>> cmds;
            for (LayerID id : Subtree(lm, path))
                cmds.push_back(std::make_unique<ChangeLayerCommand>(id, change));
            return std::make_unique<LayerBatchCommand>(std::move(cmds));
        }

        // 删除上级与全部下级图层（0 层除外），上面的对象移到 0 层
        inline std::unique_ptr<ICommand> DeleteTree(const LayerManager& lm, std::string_view path)
        {
            std::vector<std::unique_ptr<ICommand>> cmds;
            for (LayerID id : Subtree(lm, path))
                if (id != Layer::DefaultLayerID)
                    cmds.push_back(std::make_unique<DeleteLayerCommand>(id));
            return std::make_unique<LayerBatchCommand>(std::move(cmds));
        }
    }

    // ── 上级改名：自己与全部下级图层一起改（"A" 改成 "B" 时 "A$x" 成为 "B$x"）──────────────
    // from 可以没有对应的图层（只用来分组的上级）。to 里带 '$' 即移到别的上级下面（"A$x" 改成 "B$x" 是从 A 移到 B）。
    // 失败：没有这样的图层、0 层在其中、移进自己的下级、改后与这组以外的图层重名、分级的名字里有空的一级
    class RenameLayerTreeCommand : public ICommand
    {
    public:
        RenameLayerTreeCommand(std::string from, std::string to) : m_from(std::move(from)), m_to(std::move(to)) {}

        bool Execute(Scene& scene) override
        {
            LayerManager& lm = scene.GetLayerManager();
            if (m_renames.empty())
            {
                const std::vector<LayerID> ids = LayerOps::Subtree(lm, m_from);
                if (ids.empty() || m_to.empty() || m_to == m_from)
                    return false;
                // 没有上下级的普通图层照旧可以取任意名字；有上下级的，新路径的每一级都不能空
                const bool plain = ids.size() == 1 && lm.GetLayer(ids[0])->GetName() == m_from && LayerPath::Split(m_from).size() == 1;
                if (!plain && !LayerPath::IsValidRelative(m_to))
                    return false;
                if (LayerPath::InSubtree(m_to, m_from) && LayerPath::Split(m_to).size() > LayerPath::Split(m_from).size())
                    return false;

                std::vector<Rename> renames;
                for (LayerID id : ids)
                {
                    if (id == Layer::DefaultLayerID)
                        return false;
                    const std::string& name = lm.GetLayer(id)->GetName();
                    renames.push_back({ id, name, LayerPath::Rebase(name, m_from, m_to) });
                }
                std::vector<std::string> taken;
                for (LayerID other : lm.GetAllLayerIDs())
                    if (std::find(ids.begin(), ids.end(), other) == ids.end())
                        taken.push_back(lm.GetLayer(other)->GetName());
                for (const Rename& r : renames)
                    taken.push_back(r.after);
                std::sort(taken.begin(), taken.end());
                if (std::adjacent_find(taken.begin(), taken.end()) != taken.end())
                    return false;
                m_renames = std::move(renames);
            }
            for (const Rename& r : m_renames)
                if (!lm.GetLayer(r.id))
                    return false;
            for (const Rename& r : m_renames)
                lm.GetLayer(r.id)->SetName(r.after);
            scene.MarkDisplayDirty();
            return true;
        }

        void Undo(Scene& scene) override
        {
            for (const Rename& r : m_renames)
                if (Layer* l = scene.GetLayerManager().GetLayer(r.id))
                    l->SetName(r.before);
            scene.MarkDisplayDirty();
        }

        std::string GetName() const override { return "图层改名"; }
        size_t      RenamedCount() const     { return m_renames.size(); }

    private:
        struct Rename { LayerID id; std::string before, after; };
        std::string         m_from, m_to;
        std::vector<Rename> m_renames;
    };
}
