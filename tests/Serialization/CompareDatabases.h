#pragma once
// 测试用：逐对象比较两个数据库（元数据中的全部属性，加上元数据不覆盖的集合）
#include "Database/CadDatabase.h"
#include "Database/DxfMeta.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <functional>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace MiniDWG::Test
{
    inline std::string HandleText(Handle h)
    {
        return DxfValue(HandleValue{ h }).AsString();
    }

    inline bool SameValue(const DxfValue& a, const DxfValue& b)
    {
        if (a.Is<XYZ>() && b.Is<XYZ>())
        {
            const XYZ x = a.AsXYZ(), y = b.AsXYZ();
            return SameValue(DxfValue(x.X), DxfValue(y.X)) && SameValue(DxfValue(x.Y), DxfValue(y.Y))
                && SameValue(DxfValue(x.Z), DxfValue(y.Z));
        }
        if (a.Is<double>() && b.Is<double>())
        {
            const double x = a.AsDouble(), y = b.AsDouble();
            // 角度经过 弧度→度→弧度 的换算，差一两个 ulp
            return x == y || std::abs(x - y) <= 1e-12 * std::max(1.0, std::abs(x));
        }
        return a == b;
    }

    inline bool Near(double x, double y)
    {
        return x == y || std::abs(x - y) <= 1e-12 * std::max(1.0, std::abs(x));
    }

    inline bool Near(const XYZ& a, const XYZ& b) { return Near(a.X, b.X) && Near(a.Y, b.Y) && Near(a.Z, b.Z); }
    inline bool Near(const XY& a, const XY& b) { return Near(a.X, b.X) && Near(a.Y, b.Y); }

    // 逐项近似相等（ASCII DXF 中的实数只有 16 位有效数字，与 DWG 中的二进制值差一两个 ulp）
    template <class T>
    bool Near(const std::vector<T>& a, const std::vector<T>& b)
    {
        if (a.size() != b.size())
            return false;
        for (std::size_t i = 0; i < a.size(); ++i)
        {
            if (!Near(a[i], b[i]))
                return false;
        }
        return true;
    }

    // 写 DXF 时有意不往返的属性（DXF 写入测试与 DWG→DXF 测试共用）
    inline bool SkipNotRoundTripped(const CadObject& a, const DxfPropertyInfo& p, bool noMeasurement)
    {
        // R12 与 R14 DWG 没有测量值，写出时由定义点计算
        if (noMeasurement && p.Name == "Measurement")
            return true;
        // 形文件样式写出时名称为空（与 AutoCAD 一致）
        if (auto* style = dynamic_cast<const TextStyle*>(&a); style && (static_cast<int>(style->Flags) & 1) && p.Name == "Name")
            return true;
        // 标注类型码由类决定，块参照位在有块时总是置上
        if (dynamic_cast<const Dimension*>(&a) && p.Name == "Flags")
            return true;
        // 多段线、顶点的 3D 标志由类决定
        if ((dynamic_cast<const Polyline*>(&a) || dynamic_cast<const Vertex*>(&a)) && p.Name == "Flags")
            return true;
        // DEFPOINTS 图层总是写为不打印（R12 文件没有打印标志，读入为默认的 1）
        if (auto* layer = dynamic_cast<const Layer*>(&a); layer && p.Name == "PlotFlag"
            && (layer->Name == "DEFPOINTS" || layer->Name == "Defpoints" || layer->Name == "defpoints"))
            return true;
        // R12/R14 的图写为 R2000：补出的默认多重引线样式中 R2010 起才有的属性写不出
        if (noMeasurement && dynamic_cast<const MultiLeaderStyle*>(&a)
            && (p.Name == "TextTopAttachment" || p.Name == "TextBottomAttachment" || p.Name == "TextAttachmentDirection"
                || p.Name == "UnknownFlag298"))
            return true;
        // 多行属性的内嵌 MTEXT 尚未建模，按单行写出
        if (dynamic_cast<const AttributeBase*>(&a) && p.Name == "AttributeType")
            return true;
        return false;
    }

    // 逐对象比较两个数据库：元数据中的全部属性，加上元数据不覆盖的集合。
    // 返回差异描述（最多 maxDiffs 条）
    struct Comparer
    {
        const CadDatabase& A;   // 基准
        const CadDatabase& B;   // 被比较的（写出后读回、或另一种格式读入）
        std::vector<std::string> Diffs;
        std::map<std::string, int> Missing;     // A 中有、B 中没有的对象（按类名）
        std::function<bool(const CadObject&, const DxfPropertyInfo&)> SkipRule;
        // 表项标志的第 64 位（"被引用"）：R2004 及以前的 DWG 有，AutoCAD 写 DXF 时不写
        bool IgnoreReferencedFlag = false;
        // 只比较两边都有的对象：两份文件各有一些句柄不同的对象（同一张图另存时重新分配），
        // 引用、集合、扩展数据中指向它们的部分两边都忽略
        bool CommonOnly = false;
        // 写成另一种格式或另一版本：A 中原样保留的未建模对象不会写出，它们及其拥有的对象不算丢失
        bool RawNotWritten = false;
        std::size_t MaxDiffs = 30;
        // 只比较句柄小于它的对象（另存时程序新建的对象句柄在 HANDSEED 之后，两边可能互相冲突）
        Handle Below = ~Handle(0);
        // 已知的差异：参数为两侧对象与差异描述（以属性名开头），返回 true 时不记录
        std::function<bool(const CadObject&, const CadObject&, std::string_view)> KnownDiff;
        // 扩展数据的规整（两侧都用）：参数为所在数据库与扩展数据组，去掉不比较的记录；
        // 去掉后为空的组整组不比较
        std::function<ExtendedData(const CadDatabase&, const ExtendedData&)> NormalizeXData;
        const CadObject* m_current = nullptr;   // 正在比较的 B 侧对象

        // 句柄在比较范围内：B 中存在（CommonOnly 时还要求 A 中存在）
        bool Known(Handle h) const
        {
            return B.Find(h) != nullptr && (!CommonOnly || A.Find(h) != nullptr);
        }

        // 集合中只留比较范围内的句柄
        std::vector<Handle> KnownOnly(const std::vector<Handle>& hs) const
        {
            std::vector<Handle> out;
            for (Handle h : hs)
            {
                if (Known(h))
                    out.push_back(h);
            }
            return out;
        }

        // B 一侧的集合：CommonOnly 时同样过滤
        std::vector<Handle> KnownB(const std::vector<Handle>& hs) const
        {
            return CommonOnly ? KnownOnly(hs) : hs;
        }

        void Diff(const CadObject& o, std::string_view what)
        {
            if (KnownDiff && m_current != nullptr && KnownDiff(o, *m_current, what))
                return;
            if (Diffs.size() < MaxDiffs)
                Diffs.push_back(HandleText(o.ObjectHandle) + " " + std::string(o.GetClassInfo().ClassName) + "." +
                                std::string(what));
            else if (Diffs.size() == MaxDiffs)
                Diffs.push_back("……");
        }

        void Run()
        {
            for (const auto& [handle, a] : A.Objects())
            {
                if (handle >= Below)
                    continue;
                const CadObject* b = B.Find(handle);
                if (b == nullptr)
                {
                    if (!CommonOnly && !IsOrphan(*a))
                        ++Missing[std::string(a->GetClassInfo().ClassName)];
                    continue;
                }
                m_current = b;
                // 写成别的格式或版本时表格写为块参照：比较块参照部分
                if (const auto* ta = dynamic_cast<const TableEntity*>(a.get());
                    ta != nullptr && RawNotWritten && dynamic_cast<const Insert*>(b) != nullptr)
                {
                    CompareTableInsert(*ta, static_cast<const Insert&>(*b));
                    continue;
                }
                if (a->GetClassInfo().ClassName != b->GetClassInfo().ClassName)
                {
                    Diff(*a, "类型不同：" + std::string(b->GetClassInfo().ClassName));
                    continue;
                }
                CompareCommon(*a, *b);
                CompareProperties(*a, *b);
                CompareCollections(*a, *b);
                CompareRaw(*a, *b);
            }
        }

        // 所有者链断在不存在的对象上（未建模对象的扩展字典及其条目）：不会写出
        bool IsOrphan(const CadObject& o) const
        {
            if (RawNotWritten && RawDataOf(o) != nullptr)
                return true;
            Handle h = o.OwnerHandle;
            for (int depth = 0; h != kNullHandle && depth < 64; ++depth)
            {
                const CadObject* owner = A.Find(h);
                if (owner == nullptr || (RawNotWritten && RawDataOf(*owner) != nullptr))
                    return true;
                h = owner->OwnerHandle;
            }
            return false;
        }

        // 原样保留的数据：同一格式、同一版本时必须完全相同
        void CompareRaw(const CadObject& a, const CadObject& b)
        {
            const RawObjectData* ra = RawDataOf(a);
            const RawObjectData* rb = RawDataOf(b);
            if (ra == nullptr || rb == nullptr)
                return;
            if (ra->DxfName != rb->DxfName)
                Diff(a, "Raw.DxfName " + ra->DxfName + " → " + rb->DxfName);
            if (ra->Dwg.Version != CadVersion::Unknown && ra->Dwg.Version == rb->Dwg.Version
                && (ra->Dwg.Main != rb->Dwg.Main || ra->Dwg.MainBits != rb->Dwg.MainBits || ra->Dwg.Text != rb->Dwg.Text
                    || ra->Dwg.Handles != rb->Dwg.Handles || ra->Dwg.HasDataStore != rb->Dwg.HasDataStore))
                Diff(a, "Raw.Dwg");
            if (ra->Dxf.Version != CadVersion::Unknown && ra->Dxf.Version == rb->Dxf.Version)
            {
                bool same = ra->Dxf.Groups.size() == rb->Dxf.Groups.size();
                for (std::size_t i = 0; same && i < ra->Dxf.Groups.size(); ++i)
                    same = ra->Dxf.Groups[i].Code == rb->Dxf.Groups[i].Code
                        && ra->Dxf.Groups[i].Value.AsString() == rb->Dxf.Groups[i].Value.AsString();
                if (!same)
                    Diff(a, "Raw.Dxf");
            }
            const auto* ea = dynamic_cast<const UnknownEntity*>(&a);
            const auto* eb = dynamic_cast<const UnknownEntity*>(&b);
            if (ea != nullptr && eb != nullptr && ea->Raw.Dwg.Version == eb->Raw.Dwg.Version && ea->ProxyGraphics != eb->ProxyGraphics)
                Diff(a, "ProxyGraphics");
        }

        void CompareCommon(const CadObject& a, const CadObject& b)
        {
            if (a.OwnerHandle != b.OwnerHandle && Known(a.OwnerHandle))
                Diff(a, "OwnerHandle");
            if (a.XDictionaryHandle != b.XDictionaryHandle && Known(a.XDictionaryHandle))
                Diff(a, "XDictionaryHandle");
            if (KnownOnly(a.Reactors) != KnownB(b.Reactors))
                Diff(a, "Reactors");
            // CommonOnly 时只比较应用名两边都有的扩展数据
            auto collect = [&](const CadDatabase& db, const CadObject& o) {
                std::vector<ExtendedData> out;
                for (const ExtendedData& x : o.ExtendedDataList)
                {
                    if (CommonOnly && !Known(x.AppIdHandle))
                        continue;
                    if (!NormalizeXData)
                        out.push_back(x);
                    else if (ExtendedData n = NormalizeXData(db, x); !n.Records.empty())
                        out.push_back(std::move(n));
                }
                return out;
            };
            const std::vector<ExtendedData> xa = collect(A, a);
            const std::vector<ExtendedData> xb = collect(B, b);
            if (xa.size() != xb.size())
            {
                Diff(a, "ExtendedDataList.size");
                return;
            }
            for (std::size_t i = 0; i < xa.size(); ++i)
            {
                // 单独的 Y/Z 分量（1020～1033）不写出
                std::vector<ExtendedDataRecord> ra;
                for (const ExtendedDataRecord& r : xa[i].Records)
                {
                    if (r.Code < 1020 || r.Code > 1033)
                        ra.push_back(r);
                }
                const auto& rb = xb[i].Records;
                if (xa[i].AppIdHandle != xb[i].AppIdHandle || ra.size() != rb.size())
                {
                    Diff(a, "ExtendedData");
                    continue;
                }
                for (std::size_t k = 0; k < ra.size(); ++k)
                {
                    bool same = ra[k].Code == rb[k].Code && ra[k].Value.index() == rb[k].Value.index();
                    if (same)
                    {
                        if (const auto* d = std::get_if<double>(&ra[k].Value))
                            same = Near(*d, std::get<double>(rb[k].Value));
                        else if (const auto* p = std::get_if<XYZ>(&ra[k].Value))
                            same = Near(*p, std::get<XYZ>(rb[k].Value));
                        else if (const auto* h = std::get_if<Handle>(&ra[k].Value))
                            same = *h == std::get<Handle>(rb[k].Value) || (!Known(*h) && (std::get<Handle>(rb[k].Value) == 0 || !Known(std::get<Handle>(rb[k].Value))));
                        else
                            same = ra[k].Value == rb[k].Value;
                    }
                    if (!same)
                        Diff(a, "ExtendedData[" + std::to_string(k) + "] 组码 " + std::to_string(ra[k].Code));
                }
            }
        }

        void CompareProperties(const CadObject& a, const CadObject& b)
        {
            const DxfClassInfo& info = a.GetClassInfo();
            std::set<std::string_view> seen;
            for (const DxfSubclassInfo& sub : info.Subclasses)
            {
                for (const DxfPropertyInfo& p : sub.Properties)
                {
                    if (p.Get == nullptr || p.Computed || !seen.insert(p.Name).second)
                        continue;
                    if (Skip(a, p))
                        continue;
                    for (int k = 0; k < p.CodeCount; ++k)
                    {
                        DxfValue va = p.Get(a, p.Codes[k]);
                        DxfValue vb = p.Get(b, p.Codes[k]);
                        if (IgnoreReferencedFlag && p.Name == "Flags" && dynamic_cast<const TableEntry*>(&a) != nullptr)
                        {
                            va = DxfValue(va.AsInt() & ~std::int64_t(64));
                            vb = DxfValue(vb.AsInt() & ~std::int64_t(64));
                        }
                        // 悬空引用写成 0 或省略
                        const bool isRef = p.Kind == DxfValueKind::Handle
                            || GroupCodeTypeOf(p.Codes[k]) == GroupCodeType::Handle;
                        if (isRef && va.AsHandle() != 0 && !Known(va.AsHandle()))
                            va = DxfValue(HandleValue{ 0 });
                        // B 中指向未建模对象的引用（读 DWG 时原样保留）：A 一侧没有该引用时不算差异
                        if (isRef && CommonOnly && vb.AsHandle() != 0 && !Known(vb.AsHandle()))
                            vb = DxfValue(HandleValue{ 0 });
                        if (isRef && va.AsHandle() == 0 && vb.AsHandle() == 0)
                            continue;
                        if (!SameValue(va, vb))
                        {
                            Diff(a, std::string(p.Name) + "（组码 " + std::to_string(p.Codes[k]) + "）" + va.AsString() +
                                        " → " + vb.AsString());
                            break;
                        }
                    }
                }
            }
        }

        // 有意不比较的属性（由调用方给出规则）
        bool Skip(const CadObject& a, const DxfPropertyInfo& p) const
        {
            return SkipRule && SkipRule(a, p);
        }

        template <class T>
        bool Is(const CadObject& o) const { return dynamic_cast<const T*>(&o) != nullptr; }

        void CompareCollections(const CadObject& a, const CadObject& b)
        {
            auto existing = [&](const std::vector<Handle>& hs) { return KnownOnly(hs); };

            if (auto* ra = dynamic_cast<const BlockRecord*>(&a))
            {
                auto* rb = static_cast<const BlockRecord*>(&b);
                if (existing(ra->Entities) != KnownB(rb->Entities))
                    Diff(a, "Entities");
            }
            else if (auto* ta = dynamic_cast<const TableEntity*>(&a))
            {
                CompareTableInsert(*ta, *MakeTableInsert(static_cast<const TableEntity&>(b)));
            }
            else if (auto* ma = dynamic_cast<const MultiLeader*>(&a))
            {
                CompareMultiLeader(*ma, static_cast<const MultiLeader&>(b));
            }
            else if (auto* aa = dynamic_cast<const DimensionAssociation*>(&a))
            {
                auto* ab = static_cast<const DimensionAssociation*>(&b);
                for (int i = 0; i < 4; ++i)
                {
                    if ((static_cast<int>(aa->AssociativityFlags) & (1 << i)) == 0)
                        continue;
                    const auto& x = PointRefAt(*aa, i);
                    const auto& y = PointRefAt(*ab, i);
                    const bool sameGeometry = x.GeometryHandle == y.GeometryHandle || !Known(x.GeometryHandle);
                    if (x.ObjectOsnapType != y.ObjectOsnapType || !sameGeometry || x.SubentType != y.SubentType
                        || x.GsMarker != y.GsMarker || !Near(x.GeometryParameter, y.GeometryParameter)
                        || !Near(x.OsnapPoint, y.OsnapPoint) || x.HasLastPointRef != y.HasLastPointRef)
                        Diff(a, "PointRef[" + std::to_string(i) + "]");
                }
            }
            else if (auto* ta = dynamic_cast<const CadTable*>(&a))
            {
                if (existing(ta->Entries) != KnownB(static_cast<const CadTable*>(&b)->Entries))
                    Diff(a, "Entries");
            }
            else if (auto* da = dynamic_cast<const CadDictionary*>(&a))
            {
                auto* db = static_cast<const CadDictionary*>(&b);
                std::vector<std::pair<std::string, Handle>> ea, eb;
                for (std::size_t i = 0; i < da->EntryNames.size() && i < da->EntryHandles.size(); ++i)
                {
                    if (Known(da->EntryHandles[i]))
                        ea.emplace_back(da->EntryNames[i], da->EntryHandles[i]);
                }
                for (std::size_t i = 0; i < db->EntryNames.size() && i < db->EntryHandles.size(); ++i)
                {
                    if (!CommonOnly || Known(db->EntryHandles[i]))
                        eb.emplace_back(db->EntryNames[i], db->EntryHandles[i]);
                }
                if (ea != eb)
                    Diff(a, "Entries");
            }
            else if (auto* pa = dynamic_cast<const Polyline*>(&a))
            {
                if (pa->Vertices != static_cast<const Polyline*>(&b)->Vertices)
                    Diff(a, "Vertices");
            }
            else if (auto* ia = dynamic_cast<const Insert*>(&a))
            {
                if (existing(ia->Attributes) != KnownB(static_cast<const Insert*>(&b)->Attributes))
                    Diff(a, "Attributes");
            }
            else if (auto* la = dynamic_cast<const LwPolyline*>(&a))
            {
                auto* lb = static_cast<const LwPolyline*>(&b);
                bool same = la->Vertices.size() == lb->Vertices.size();
                for (std::size_t i = 0; same && i < la->Vertices.size(); ++i)
                {
                    const auto& x = la->Vertices[i];
                    const auto& y = lb->Vertices[i];
                    same = Near(x.Location, y.Location) && Near(x.Bulge, y.Bulge) && Near(x.StartWidth, y.StartWidth)
                        && Near(x.EndWidth, y.EndWidth);
                }
                if (!same)
                    Diff(a, "Vertices");
            }
            else if (auto* sa = dynamic_cast<const Spline*>(&a))
            {
                auto* sb = static_cast<const Spline*>(&b);
                if (!Near(sa->ControlPoints, sb->ControlPoints) || !Near(sa->Knots, sb->Knots) || !Near(sa->Weights, sb->Weights))
                    Diff(a, "ControlPoints/Knots/Weights");
                if (!Near(sa->FitPoints, sb->FitPoints))
                    Diff(a, "FitPoints");
            }
            else if (auto* ha = dynamic_cast<const Hatch*>(&a))
                CompareHatch(*ha, *static_cast<const Hatch*>(&b));
            else if (auto* lta = dynamic_cast<const LineType*>(&a))
            {
                auto* ltb = static_cast<const LineType*>(&b);
                bool same = lta->Segments.size() == ltb->Segments.size();
                for (std::size_t i = 0; same && i < lta->Segments.size(); ++i)
                {
                    const auto& x = lta->Segments[i];
                    const auto& y = ltb->Segments[i];
                    same = Near(x.Length, y.Length) && x.Flags == y.Flags && Near(x.Rotation, y.Rotation) && Near(x.Scale, y.Scale)
                        && x.Text == y.Text && Near(x.Offset, y.Offset);
                    if (!same)
                        Diff(a, "Segments[" + std::to_string(i) + "] " + std::to_string(x.Length) + "/" + std::to_string(x.Rotation) + "/" +
                                    std::to_string(x.Scale) + "/" + std::to_string(x.Offset.X) + "," + std::to_string(x.Offset.Y) + "/" + std::to_string(static_cast<int>(x.Flags)) + "/[" + x.Text + "]" + std::to_string(x.Text.size()) + " → " +
                                    std::to_string(y.Length) + "/" + std::to_string(y.Rotation) + "/" + std::to_string(y.Scale) + "/" +
                                    std::to_string(y.Offset.X) + "," + std::to_string(y.Offset.Y) + "/" + std::to_string(static_cast<int>(y.Flags)) + "/[" + y.Text + "]" + std::to_string(y.Text.size()));
                }
                if (lta->Segments.size() != ltb->Segments.size())
                    Diff(a, "Segments.size");
            }
            else if (auto* xa = dynamic_cast<const XRecord*>(&a))
            {
                auto* xb = static_cast<const XRecord*>(&b);
                if (xa->Entries.size() != xb->Entries.size())
                    Diff(a, "Entries.size " + std::to_string(xa->Entries.size()) + " → " + std::to_string(xb->Entries.size()));
                for (std::size_t i = 0; i < xa->Entries.size() && i < xb->Entries.size(); ++i)
                {
                    DxfValue va = xa->Entries[i].Value;
                    DxfValue vb = xb->Entries[i].Value;
                    if (GroupCodeTypeOf(xa->Entries[i].Code) == GroupCodeType::Handle && !Known(va.AsHandle()))
                        va = DxfValue(HandleValue{ 0 });
                    if (CommonOnly && GroupCodeTypeOf(xb->Entries[i].Code) == GroupCodeType::Handle && !Known(vb.AsHandle()))
                        vb = DxfValue(HandleValue{ 0 });
                    if (xa->Entries[i].Code != xb->Entries[i].Code || !SameValue(va, vb))
                    {
                        Diff(a, "Entries[" + std::to_string(i) + "] 组码 " + std::to_string(xa->Entries[i].Code) + " " +
                                    va.AsString() + " → 组码 " + std::to_string(xb->Entries[i].Code) + " " + vb.AsString());
                        break;
                    }
                }
            }
            else if (auto* ma = dynamic_cast<const MLine*>(&a))
            {
                auto* mb = static_cast<const MLine*>(&b);
                bool same = ma->Vertices.size() == mb->Vertices.size();
                for (std::size_t i = 0; same && i < ma->Vertices.size(); ++i)
                {
                    same = Near(ma->Vertices[i].Position, mb->Vertices[i].Position)
                        && ma->Vertices[i].Segments.size() == mb->Vertices[i].Segments.size();
                }
                if (!same)
                    Diff(a, "Vertices");
            }
            else if (auto* wa = dynamic_cast<const CadWipeoutBase*>(&a))
            {
                if (!Near(wa->ClipBoundaryVertices, static_cast<const CadWipeoutBase*>(&b)->ClipBoundaryVertices))
                    Diff(a, "ClipBoundaryVertices");
            }
            else if (auto* lea = dynamic_cast<const Leader*>(&a))
            {
                if (!Near(lea->Vertices, static_cast<const Leader*>(&b)->Vertices))
                    Diff(a, "Vertices");
            }
            else if (auto* vpa = dynamic_cast<const Viewport*>(&a))
            {
                if (existing(vpa->FrozenLayers) != KnownB(static_cast<const Viewport*>(&b)->FrozenLayers))
                    Diff(a, "FrozenLayers");
            }
            else if (auto* ga = dynamic_cast<const Group*>(&a))
            {
                const auto ea = existing(ga->Entities);
                const auto eb = KnownB(static_cast<const Group*>(&b)->Entities);
                if (ea != eb)
                {
                    std::string text = "Entities";
                    for (Handle h : ea)
                        text += " " + HandleText(h);
                    text += " →";
                    for (Handle h : eb)
                        text += " " + HandleText(h);
                    Diff(a, text);
                }
            }
            else if (auto* mta = dynamic_cast<const MText*>(&a))
            {
                const auto& ca = mta->ColumnData;
                const auto& cb = static_cast<const MText*>(&b)->ColumnData;
                if (ca.ColumnType != cb.ColumnType || ca.ColumnCount != cb.ColumnCount || ca.Heights != cb.Heights)
                    Diff(a, "ColumnData");
            }
            else if (auto* msa = dynamic_cast<const MLineStyle*>(&a))
            {
                auto* msb = static_cast<const MLineStyle*>(&b);
                bool same = msa->Elements.size() == msb->Elements.size();
                for (std::size_t i = 0; same && i < msa->Elements.size(); ++i)
                    same = Near(msa->Elements[i].Offset, msb->Elements[i].Offset)
                        && msa->Elements[i].LineTypeHandle == msb->Elements[i].LineTypeHandle;
                if (!same)
                    Diff(a, "Elements");
            }
            else if (auto* sta = dynamic_cast<const SortEntitiesTable*>(&a))
            {
                auto* stb = static_cast<const SortEntitiesTable*>(&b);
                if (sta->Entries.size() != stb->Entries.size())
                    Diff(a, "Entries");
            }
        }

        void CompareTableInsert(const TableEntity& a, const Insert& b)
        {
            if ((a.BlockHandle != b.BlockHandle && Known(a.BlockHandle)) || !Near(a.InsertPoint, b.InsertPoint)
                || !Near(a.XScale, b.XScale) || !Near(a.YScale, b.YScale) || !Near(a.ZScale, b.ZScale)
                || std::abs(a.Rotation - b.Rotation) > 1e-9 || !Near(a.Normal, b.Normal))
                Diff(a, "块参照部分");
        }

        // 多重引线：上下文数据按元数据逐项比较，引线、块属性逐项比较
        void CompareMultiLeader(const MultiLeader& a, const MultiLeader& b)
        {
            const CadObject* saved = m_current;
            m_current = &b.ContextData;
            CompareProperties(a.ContextData, b.ContextData);
            m_current = saved;

            const auto& ra = a.ContextData.LeaderRoots;
            const auto& rb = b.ContextData.LeaderRoots;
            if (ra.size() != rb.size())
            {
                Diff(a, "LeaderRoots.size");
                return;
            }
            for (std::size_t i = 0; i < ra.size(); ++i)
            {
                const std::string tag = "LeaderRoots[" + std::to_string(i) + "]";
                if (ra[i].TextAttachmentDirection != rb[i].TextAttachmentDirection)
                    Diff(a, tag + ".TextAttachmentDirection");
                if (!Near(ra[i].ConnectionPoint, rb[i].ConnectionPoint) || !Near(ra[i].Direction, rb[i].Direction)
                    || ra[i].LeaderIndex != rb[i].LeaderIndex || !Near(ra[i].LandingDistance, rb[i].LandingDistance)
                    || ra[i].ContentValid != rb[i].ContentValid || ra[i].Unknown != rb[i].Unknown
                    || ra[i].BreakStartEndPointsPairs.size() != rb[i].BreakStartEndPointsPairs.size()
                    || ra[i].Lines.size() != rb[i].Lines.size())
                {
                    Diff(a, tag);
                    continue;
                }
                for (std::size_t k = 0; k < ra[i].Lines.size(); ++k)
                {
                    const auto& la = ra[i].Lines[k];
                    const auto& lb = rb[i].Lines[k];
                    const std::string line = tag + ".Lines[" + std::to_string(k) + "]";
                    if (!Near(la.Points, lb.Points) || la.Index != lb.Index || la.BreakInfoEntries.size() != lb.BreakInfoEntries.size())
                        Diff(a, line);
                    // 没有替代时引线的样式属性不起作用（DXF 中也不写）
                    if (la.OverrideFlags != lb.OverrideFlags
                        || (la.OverrideFlags != LeaderLinePropertOverrideFlags{}
                            && (la.PathType != lb.PathType || la.LineColor != lb.LineColor || la.LineWeight != lb.LineWeight
                                || !Near(la.ArrowheadSize, lb.ArrowheadSize))))
                        Diff(a, line + ".Override");
                }
            }
            bool sameAttributes = a.BlockAttributes.size() == b.BlockAttributes.size();
            for (std::size_t i = 0; sameAttributes && i < a.BlockAttributes.size(); ++i)
                sameAttributes = a.BlockAttributes[i].Text == b.BlockAttributes[i].Text
                    && a.BlockAttributes[i].Index == b.BlockAttributes[i].Index
                    && Near(a.BlockAttributes[i].Width, b.BlockAttributes[i].Width);
            if (!sameAttributes)
                Diff(a, "BlockAttributes");
        }

        void CompareHatch(const Hatch& a, const Hatch& b)
        {
            if (a.Paths.size() != b.Paths.size() || a.SeedPoints.size() != b.SeedPoints.size()
                || a.Pattern.Lines.size() != b.Pattern.Lines.size() || a.Pattern.Name != b.Pattern.Name
                || a.GradientColor.Enabled != b.GradientColor.Enabled
                // 渐变未启用时 DWG 中仍有颜色，AutoCAD 写 DXF 时不写
                || (a.GradientColor.Enabled && a.GradientColor.Colors.size() != b.GradientColor.Colors.size()))
            {
                Diff(a, "Paths/Pattern/Gradient");
                return;
            }
            for (std::size_t i = 0; i < a.Paths.size(); ++i)
            {
                const auto& pa = a.Paths[i];
                const auto& pb = b.Paths[i];
                if (pa.Flags != pb.Flags || pa.Edges.size() != pb.Edges.size())
                {
                    Diff(a, "Paths[" + std::to_string(i) + "]");
                    continue;
                }
                for (std::size_t k = 0; k < pa.Edges.size(); ++k)
                {
                    if (pa.Edges[k]->GetType() != pb.Edges[k]->GetType())
                        Diff(a, "Paths[" + std::to_string(i) + "].Edges");
                    else if (auto* ea = dynamic_cast<const HatchBoundaryPathPolyline*>(pa.Edges[k].get()))
                    {
                        auto* eb = static_cast<const HatchBoundaryPathPolyline*>(pb.Edges[k].get());
                        if (!Near(ea->Vertices, eb->Vertices) || ea->IsClosed != eb->IsClosed)
                            Diff(a, "Paths[" + std::to_string(i) + "] 多段线边界");
                    }
                    else if (auto* aa = dynamic_cast<const HatchBoundaryPathArc*>(pa.Edges[k].get()))
                    {
                        auto* ab = static_cast<const HatchBoundaryPathArc*>(pb.Edges[k].get());
                        if (!Near(aa->StartAngle, ab->StartAngle) || !Near(aa->EndAngle, ab->EndAngle) || !Near(aa->Radius, ab->Radius))
                            Diff(a, "Paths[" + std::to_string(i) + "] 圆弧边");
                    }
                }
            }
        }
    };

}
