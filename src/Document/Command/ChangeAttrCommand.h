#pragma once
#include "Document/CommandStack/ICommand.h"
#include "Scene/Scene.h"
#include "Core/Entity/Entity.hpp"
#include "Core/Entity/EntityAttr.hpp"
#include <optional>
#include <string>
#include <vector>

namespace MiniCAD
{
    // 一次属性修改：每个字段可选，只修改有值的字段（其余保持各实体原值）。
    // 对应特性面板 / MatchProp / 命令行里改图层、颜色、线型、线宽、透明度的操作。
    struct AttrChange
    {
        std::optional<LayerID>       Layer;
        std::optional<EntityColor>   Color;
        std::optional<LineTypeID>    LineType;
        std::optional<double>        LinetypeScale;     // 必须 > 0
        std::optional<Lineweight>    Lineweight;
        std::optional<Transparency>  Transparency;
        std::optional<bool>          Visible;

        bool Empty() const
        {
            return !Layer && !Color && !LineType && !LinetypeScale && !Lineweight && !Transparency && !Visible;
        }

        // 从一个属性集里取出全部常规属性（MatchProp：把源对象的属性刷给其他对象）
        static AttrChange FromAttr(const EntityAttr& a)
        {
            AttrChange c;
            c.Layer         = a.LayerId;
            c.Color         = a.Color;
            c.LineType      = a.LineType;
            c.LinetypeScale = a.LinetypeScale;
            c.Lineweight    = a.Lineweight;
            c.Transparency  = a.Transparency;
            return c;
        }
    };

    // =========================================================================
    // ChangeAttrCommand —— 批量修改实体的常规属性（图层、颜色、线型、线型比例、线宽、透明度、可见性）
    //
    //   · 只对「值真的变了」的实体生效；一个都没变（或没有有效的实体 / 修改）时 Execute 返回 false，
    //     不进撤销栈，也不触发重画。
    //   · 图层 / 线型 ID 在场景里不存在时，忽略该字段（不会把实体改到不存在的图层上）。
    //   · 保存每个被修改实体改前的完整属性，Undo 逐个还原；一次 Undo 撤回整批。
    //   · 实体级脏标记：只重新细分被修改的实体，不触发全场景重建。
    // =========================================================================
    class ChangeAttrCommand : public ICommand
    {
    public:
        ChangeAttrCommand(std::vector<Object::ObjectID> ids, AttrChange change)
            : m_ids(std::move(ids)), m_change(std::move(change)) {}

        bool Execute(Scene& scene) override
        {
            m_items.clear();
            AttrChange c = Sanitized(scene, m_change);
            if (c.Empty()) return false;

            for (Object::ObjectID id : m_ids)
            {
                Object* obj = scene.GetEntity(id);
                if (!obj || !obj->IsKindOf<Entity>()) continue;
                Entity& e = static_cast<Entity&>(*obj);

                EntityAttr after = e.GetAttr();
                Apply(c, after);
                if (Same(after, e.GetAttr())) continue;

                m_items.push_back({ id, e.GetAttr() });
                e.SetAttr(after);
                scene.MarkEntityDirty(id);
            }
            return !m_items.empty();
        }

        void Undo(Scene& scene) override
        {
            for (auto it = m_items.rbegin(); it != m_items.rend(); ++it)
            {
                Object* obj = scene.GetEntity(it->id);
                if (!obj || !obj->IsKindOf<Entity>()) continue;
                static_cast<Entity*>(obj)->SetAttr(it->before);
                scene.MarkEntityDirty(it->id);
            }
        }

        std::string GetName() const override
        {
            const AttrChange& c = m_change;
            const int fields = int(c.Layer.has_value()) + int(c.Color.has_value()) + int(c.LineType.has_value())
                             + int(c.LinetypeScale.has_value()) + int(c.Lineweight.has_value())
                             + int(c.Transparency.has_value()) + int(c.Visible.has_value());
            if (fields == 1)
            {
                if (c.Layer)         return "修改图层";
                if (c.Color)         return "修改颜色";
                if (c.LineType)      return "修改线型";
                if (c.LinetypeScale) return "修改线型比例";
                if (c.Lineweight)    return "修改线宽";
                if (c.Transparency)  return "修改透明度";
                return "修改可见性";
            }
            return "修改特性";
        }

        // 实际被修改的实体数（Execute 之后有效）
        size_t ChangedCount() const { return m_items.size(); }

    private:
        struct Item { Object::ObjectID id; EntityAttr before; };

        // 去掉场景里不存在的图层 / 线型，以及非法的线型比例
        static AttrChange Sanitized(const Scene& scene, AttrChange c)
        {
            if (c.Layer && !scene.GetLayerManager().GetLayer(*c.Layer))
                c.Layer.reset();
            if (c.LineType)
            {
                const LineTypeID id = *c.LineType;
                if (id != LineTypeTable::ByLayerID && id != LineTypeTable::ByBlockID && !scene.GetLineTypeTable().Find(id))
                    c.LineType.reset();
            }
            if (c.LinetypeScale && !(*c.LinetypeScale > 0.0))
                c.LinetypeScale.reset();
            return c;
        }

        static void Apply(const AttrChange& c, EntityAttr& a)
        {
            if (c.Layer)         a.LayerId       = *c.Layer;
            if (c.Color)         a.Color         = *c.Color;
            if (c.LineType)      a.LineType      = *c.LineType;
            if (c.LinetypeScale) a.LinetypeScale = *c.LinetypeScale;
            if (c.Lineweight)    a.Lineweight    = *c.Lineweight;
            if (c.Transparency)  a.Transparency  = *c.Transparency;
            if (c.Visible)       a.Visible       = *c.Visible;
        }

        static bool SameColor(const EntityColor& x, const EntityColor& y)
        {
            if (x.Method != y.Method) return false;
            switch (x.Method)
            {
            case ColorMethod::ByLayer:
            case ColorMethod::ByBlock: return true;
            case ColorMethod::ByAci:   return x.Aci == y.Aci;
            case ColorMethod::ByRgb:   return x.Rgba.r == y.Rgba.r && x.Rgba.g == y.Rgba.g && x.Rgba.b == y.Rgba.b && x.Rgba.a == y.Rgba.a;
            }
            return true;
        }

        static bool Same(const EntityAttr& x, const EntityAttr& y)
        {
            return x.LayerId == y.LayerId && SameColor(x.Color, y.Color) && x.LineType == y.LineType
                && x.LinetypeScale == y.LinetypeScale && x.Lineweight == y.Lineweight
                && x.Transparency.ByLayer == y.Transparency.ByLayer
                && (x.Transparency.ByLayer || x.Transparency.Alpha == y.Transparency.Alpha)
                && x.Visible == y.Visible;
        }

        std::vector<Object::ObjectID> m_ids;
        AttrChange                    m_change;
        std::vector<Item>             m_items;
    };
}
