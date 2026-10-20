#include "NodeGraph/Preview.h"
#include "NodeGraph/GeometryOps.h"
#include "Core/Entity/PointEntity.hpp"
#include "Core/Entity/Solid3DEntity.hpp"
#include "Scene/Scene.h"
#include "Document/Command/BatchAddCommand.h"
#include "Document/CommandStack/CommandStack.h"

namespace MiniCAD::Nodes
{
    namespace
    {
        void Flatten(const Node& n, int port, bool highlight, std::vector<PreviewItem>& out)
        {
            if (port < 0 || port >= static_cast<int>(n.Outputs.size()))
                return;
            for (const auto& v : n.Outputs[port])
                if (IsGeometryType(v.Type()))
                    out.push_back({ v, highlight });
        }
    }

    std::vector<PreviewItem> CollectPreview(const Graph& g, const std::set<NodeId>& selected)
    {
        std::vector<PreviewItem> out;
        for (const auto& n : g.GetNodes())
        {
            if (!n->Type)
                continue;
            const bool sel = selected.count(n->Id) > 0;
            for (int p = 0; p < static_cast<int>(n->Type->Outputs.size()); ++p)
            {
                const DataType t = n->Type->Outputs[p].Type;
                if (!IsGeometryType(t) && t != DataType::Any)
                    continue;
                if (sel)
                    Flatten(*n, p, true, out);
                else if (n->Preview && !g.IsOutputConnected(n->Id, p))
                    Flatten(*n, p, false, out);
            }
        }
        return out;
    }

    PreviewSet PreviewBuilder::Build(const Graph& g, const std::set<NodeId>& selected, Solid::ISolidKernel* kernel)
    {
        PreviewSet set;
        decltype(m_bodyCache) used;
        decltype(m_voidCache) usedVoids;
        for (const PreviewItem& item : CollectPreview(g, selected))
        {
            const Math::Color4& color = item.Highlight ? Highlight : Normal;
            EntityAttr attr;
            attr.Color = EntityColor::FromRgb(color.r, color.g, color.b);
            switch (item.v.Type())
            {
            case DataType::Vector:
            {
                const auto& p = item.v.AsVector();
                (item.Highlight ? set.HighlightPoints : set.Points).push_back({ p.x, p.y, p.z });
                break;
            }
            case DataType::Plane:
            {
                // 平面：以原点为中心、边长 10 的方框 + X（红）/ Y（绿）/ Z（蓝）轴，只预览不烘焙
                const Plane& pl = item.v.AsPlane();
                auto pt = [&](double u, double v, double w = 0.0) { return Geo::P(pl.PointAt(u, v, w)); };
                const Math::Point3 corners[4] = { pt(-5, -5), pt(5, -5), pt(5, 5), pt(-5, 5) };
                for (int i = 0; i < 4; ++i)
                {
                    std::shared_ptr<Entity> e = std::const_pointer_cast<Entity>(Geo::MakeLine(corners[i], corners[(i + 1) % 4]));
                    e->SetAttr(attr);
                    set.Entities.push_back(std::move(e));
                }
                const Math::Point3 axisEnds[3] = { pt(4, 0), pt(0, 4), pt(0, 0, 4) };
                const double axisColors[3][3] = { { 0.90, 0.24, 0.24 }, { 0.24, 0.78, 0.31 }, { 0.27, 0.47, 0.94 } };
                for (int i = 0; i < 3; ++i)
                {
                    std::shared_ptr<Entity> e = std::const_pointer_cast<Entity>(Geo::MakeLine(pt(0, 0), axisEnds[i]));
                    EntityAttr axisAttr = attr;
                    axisAttr.Color = EntityColor::FromRgb(axisColors[i][0], axisColors[i][1], axisColors[i][2]);
                    e->SetAttr(axisAttr);
                    set.Entities.push_back(std::move(e));
                }
                break;
            }
            case DataType::Curve:
                if (item.v.AsCurve())
                {
                    std::shared_ptr<Entity> c = item.v.AsCurve()->Clone(Object::InvalidID);
                    attr.Extrusion = c->GetAttr().Extrusion;
                    c->SetAttr(attr);
                    set.Entities.push_back(std::move(c));
                }
                break;
            case DataType::Curve3D:
                if (item.v.AsCurve3D() && item.v.AsCurve3D()->IsValid())
                {
                    std::shared_ptr<Entity> c = Geo::MakeSplineEntity(*item.v.AsCurve3D(), Object::InvalidID);
                    c->SetAttr(attr);
                    set.Entities.push_back(std::move(c));
                }
                break;
            case DataType::Body:
            {
                const Solid::BodyPtr& body = item.v.AsBody();
                if (!body || !kernel)
                    break;
                if (item.v.Void)
                {
                    // 空心：各条边画成折线（不着色、不写深度），与 Revit 族编辑器里空心的显示相同
                    auto vit = usedVoids.find(body.get());
                    if (vit == usedVoids.end())
                    {
                        auto cached = m_voidCache.find(body.get());
                        if (cached != m_voidCache.end() && cached->second.first == body)
                            vit = usedVoids.insert(*cached).first;
                        else
                        {
                            std::vector<std::shared_ptr<Entity>> wires;
                            for (const auto& line : kernel->Edges(body).Lines)
                                for (size_t i = 1; i < line.size(); ++i)
                                    wires.push_back(std::const_pointer_cast<Entity>(Geo::MakeLine(line[i - 1], line[i])));
                            vit = usedVoids.insert({ body.get(), { body, std::move(wires) } }).first;
                        }
                    }
                    const Math::Color4& vc = item.Highlight ? VoidHighlight : Void;
                    EntityAttr voidAttr;
                    voidAttr.Color = EntityColor::FromRgb(vc.r, vc.g, vc.b);
                    for (const auto& w : vit->second.second)
                    {
                        std::shared_ptr<Entity> e = w->Clone(Object::InvalidID);
                        e->SetAttr(voidAttr);
                        set.Entities.push_back(std::move(e));
                    }
                    break;
                }
                auto it = used.find(body.get());
                if (it == used.end())
                {
                    auto cached = m_bodyCache.find(body.get());
                    if (cached != m_bodyCache.end() && cached->second.first == body)
                        it = used.insert(*cached).first;
                    else if (std::shared_ptr<Entity> e = Solid3DEntity::Create(Object::InvalidID, *kernel, body))
                        it = used.insert({ body.get(), { body, std::move(e) } }).first;
                    else
                        break;
                }
                std::shared_ptr<Entity> e = it->second.second->Clone(Object::InvalidID);   // 克隆共享内核数据与网格
                e->SetAttr(attr);
                set.Entities.push_back(std::move(e));
                break;
            }
            default:
                break;
            }
        }
        m_bodyCache = std::move(used);
        m_voidCache = std::move(usedVoids);
        return set;
    }

    int Bake(const Graph& g, const std::set<NodeId>& selected, Scene& scene, CommandStack& stack, Solid::ISolidKernel* kernel, int* skippedVoids)
    {
        if (skippedVoids)
            *skippedVoids = 0;
        std::vector<PreviewItem> items;
        if (selected.empty())
            items = CollectPreview(g, {});
        else
            for (const PreviewItem& it : CollectPreview(g, selected))
                if (it.Highlight)
                    items.push_back(it);

        EntityAttr attr;
        attr.LayerId    = scene.GetLayerManager().GetActiveLayerID();
        attr.LineType   = scene.GetCurrentLineType();
        attr.Lineweight = scene.GetCurrentLineweight();

        std::vector<std::unique_ptr<Object>> entities;
        for (const PreviewItem& item : items)
        {
            std::unique_ptr<Entity> e;
            switch (item.v.Type())
            {
            case DataType::Vector:
            {
                const auto& p = item.v.AsVector();
                e = std::make_unique<PointEntity>(scene.NextObjectID(), Math::Point3{ p.x, p.y, p.z });
                break;
            }
            case DataType::Curve:
                if (item.v.AsCurve())
                    e = item.v.AsCurve()->Clone(scene.NextObjectID());
                break;
            case DataType::Curve3D:
                if (item.v.AsCurve3D() && item.v.AsCurve3D()->IsValid())
                    e = Geo::MakeSplineEntity(*item.v.AsCurve3D(), scene.NextObjectID());   // 样条（控制点形式）
                break;
            case DataType::Body:
                if (item.v.Void)
                {
                    if (skippedVoids)
                        ++*skippedVoids;   // 空心只用来切除，不烘焙
                }
                else if (kernel && item.v.AsBody())
                    e = Solid3DEntity::Create(scene.NextObjectID(), *kernel, item.v.AsBody());
                break;
            default:
                break;
            }
            if (!e)
                continue;
            EntityAttr a = attr;
            a.Extrusion  = e->GetAttr().Extrusion;
            e->SetAttr(a);
            entities.push_back(std::move(e));
        }
        const int count = static_cast<int>(entities.size());
        if (count > 0)
            stack.Execute(std::make_unique<BatchAddCommand>(std::move(entities)), scene);
        return count;
    }
}
