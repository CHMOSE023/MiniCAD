// DWG 读取：建库——表、块记录的实体、子实体、字典、名称引用、默认内容
#include "Dwg/Read/DwgReaderImpl.h"
#include "Database/DxfMeta.h"
#include <algorithm>
#include <cctype>
#include <set>

namespace MiniDWG::DwgRead
{
    namespace
    {
        bool EqualsIgnoreCase(std::string_view a, std::string_view b)
        {
            return a.size() == b.size() && std::equal(a.begin(), a.end(), b.begin(), [](char x, char y) {
                       auto up = [](char c) { return c >= 'a' && c <= 'z' ? static_cast<char>(c - 'a' + 'A') : c; };
                       return up(x) == up(y);
                   });
        }

        bool StartsWithIgnoreCase(std::string_view s, std::string_view prefix)
        {
            return s.size() >= prefix.size() && EqualsIgnoreCase(s.substr(0, prefix.size()), prefix);
        }

        // 完整的匿名块名：*U12、*D3、*X7、*T2 ……
        bool IsAnonymousName(std::string_view name)
        {
            if (name.size() < 3 || name[0] != '*' || !std::isalpha(static_cast<unsigned char>(name[1])))
                return false;
            return std::all_of(name.begin() + 2, name.end(), [](char c) { return c >= '0' && c <= '9'; });
        }
    }

    void Reader::Build()
    {
        BuildTables();
        BuildBlockRecords();
        BuildOwnedChildren();
        BuildEntityReferences();
        BuildDictionaries();
        if (R13_14Only())
            ApplyR14RoundTrip();
        BuildExtendedData();

        Handle maxHandle = 0;
        for (const auto& [h, obj] : m_db->Objects())
            maxHandle = std::max(maxHandle, h);
        const Handle fileSeed = std::max<Handle>(maxHandle + 1, m_db->Header.HandleSeed);
        m_db->SetHandleSeed(fileSeed);

        if (m_options.CreateDefaults)
            m_db->CreateDefaults();
        FixDictionaryDefaults();
        FillDefaultReferences();
        BuildHeaderNames();

        const Handle seed = std::max(m_db->GetHandleSeed(), static_cast<Handle>(m_db->Header.HandleSeed));
        m_db->SetHandleSeed(seed);
        m_db->Header.HandleSeed = seed;

        if (m_options.Report != nullptr)
        {
            m_options.Report->FileHandleSeed = fileSeed;
            m_options.Report->Skipped.assign(m_skipped.begin(), m_skipped.end());
            m_options.Report->Preserved.assign(m_preserved.begin(), m_preserved.end());
        }
    }

    // 符号表：以头段中的控制对象为准；表项列表只保留类型正确的对象
    void Reader::BuildTables()
    {
        const std::pair<CadObjectType, Handle> controls[] = {
            { CadObjectType::BLOCK_CONTROL_OBJ, m_headerHandles.BLOCK_CONTROL_OBJECT },
            { CadObjectType::LAYER_CONTROL_OBJ, m_headerHandles.LAYER_CONTROL_OBJECT },
            { CadObjectType::STYLE_CONTROL_OBJ, m_headerHandles.STYLE_CONTROL_OBJECT },
            { CadObjectType::LTYPE_CONTROL_OBJ, m_headerHandles.LINETYPE_CONTROL_OBJECT },
            { CadObjectType::VIEW_CONTROL_OBJ, m_headerHandles.VIEW_CONTROL_OBJECT },
            { CadObjectType::UCS_CONTROL_OBJ, m_headerHandles.UCS_CONTROL_OBJECT },
            { CadObjectType::VPORT_CONTROL_OBJ, m_headerHandles.VPORT_CONTROL_OBJECT },
            { CadObjectType::APPID_CONTROL_OBJ, m_headerHandles.APPID_CONTROL_OBJECT },
            { CadObjectType::DIMSTYLE_CONTROL_OBJ, m_headerHandles.DIMSTYLE_CONTROL_OBJECT },
        };
        for (const auto& [type, handle] : controls)
        {
            auto* table = m_db->FindAs<CadTable>(handle);
            if (table == nullptr || table->GetObjectType() != type)
            {
                Notify(NotificationType::Warning, "找不到符号表 " + std::string(type == CadObjectType::BLOCK_CONTROL_OBJ ? "BLOCK_RECORD" : "") +
                                                      "（类型 " + std::to_string(static_cast<int>(type)) + "）");
                continue;
            }
            m_db->SetTable(type, handle);
            std::vector<Handle> entries;
            for (Handle h : table->Entries)
            {
                const auto* entry = m_db->FindAs<TableEntry>(h);
                if (entry != nullptr && entry->GetDxfName() == table->GetEntryDxfName()
                    && std::find(entries.begin(), entries.end(), h) == entries.end())
                    entries.push_back(h);
            }
            table->Entries = std::move(entries);
        }
        if (m_db->FindAs<CadDictionary>(m_headerHandles.DICTIONARY_NAMED_OBJECTS) != nullptr)
            m_db->SetRootDictionary(m_headerHandles.DICTIONARY_NAMED_OBJECTS);
    }

    // R13～R2000：沿前后实体句柄（没有时为句柄 + 1）从 first 走到 last；未建模的实体也在链上
    std::vector<Handle> Reader::ChainEntities(Handle first, Handle last) const
    {
        std::vector<Handle> out;
        std::unordered_set<Handle> seen;
        Handle current = first;
        while (current != kNullHandle && seen.insert(current).second)
        {
            auto it = m_infos.find(current);
            if (it == m_infos.end())
                break;
            out.push_back(current);
            if (current == last)
                break;
            current = it->second.HasLinks ? it->second.NextEntity : current + 1;
        }
        return out;
    }

    std::string Reader::MakeAnonymousName(char letter)
    {
        letter = static_cast<char>(std::toupper(static_cast<unsigned char>(letter)));
        for (int n = 1;; ++n)
        {
            std::string name = std::string("*") + letter + std::to_string(n);
            if (m_blockNames.insert(name).second)
                return name;
        }
    }

    void Reader::BuildBlockRecords()
    {
        const CadTable* records = m_db->BlockRecords();
        if (records == nullptr)
            return;
        for (Handle h : records->Entries)
        {
            const auto* record = m_db->FindAs<BlockRecord>(h);
            const auto* block = record != nullptr ? m_db->FindAs<Block>(record->BlockEntityHandle) : nullptr;
            for (const std::string* n : { record ? &record->Name : nullptr, block ? &block->Name : nullptr })
            {
                if (n != nullptr && IsAnonymousName(*n))
                    m_blockNames.insert(*n);
            }
        }

        for (Handle h : records->Entries)
        {
            auto* record = m_db->FindAs<BlockRecord>(h);
            if (record == nullptr)
                continue;

            // 块记录中的数据合并到 BLOCK 实体；匿名块的完整名称以 BLOCK 为准
            if (auto* block = m_db->FindAs<Block>(record->BlockEntityHandle))
            {
                if (auto data = m_blockData.find(h); data != m_blockData.end())
                {
                    block->Flags = data->second->Flags;
                    block->BasePoint = data->second->BasePoint;
                    block->XRefPath = data->second->XRefPath;
                    block->Comments = data->second->Comments;
                    block->IsUnloaded = data->second->IsUnloaded;
                }
                // R2004 及以前块记录只存名称的前缀（*Paper_Space、*U），完整名称在 BLOCK 中（*Paper_Space0）
                std::string name = record->Name;
                if (block->Name.size() > name.size() && StartsWithIgnoreCase(block->Name, name))
                    name = block->Name;
                if (name.empty())
                    name = block->Name;
                // 匿名块的编号不存在 DWG 中（AutoCAD 打开时重新编号），动态块的匿名实例甚至存原块名：补一个不重复的编号
                if (HasFlag(block->Flags, BlockTypeFlags::Anonymous) && !IsAnonymousName(name))
                    name = MakeAnonymousName(name.size() >= 2 && name[0] == '*' ? name[1] : 'U');
                record->Name = name;
                block->Name = name;
                block->OwnerHandle = h;
            }
            if (auto* end = m_db->FindAs<BlockEnd>(record->BlockEndHandle))
                end->OwnerHandle = h;
            if (h == m_headerHandles.MODEL_SPACE && !EqualsIgnoreCase(record->Name, "*Model_Space"))
                record->Name = "*Model_Space";
            if (h == m_headerHandles.PAPER_SPACE && !EqualsIgnoreCase(record->Name, "*Paper_Space"))
                record->Name = "*Paper_Space";

            // 块中的实体
            auto info = m_infos.find(h);
            if (info == m_infos.end())
                continue;
            const std::vector<Handle> handles = info->second.FirstChild != kNullHandle
                ? ChainEntities(info->second.FirstChild, info->second.LastChild)
                : info->second.Owned;
            for (Handle e : handles)
            {
                auto* entity = m_db->FindAs<Entity>(e);
                if (entity == nullptr || dynamic_cast<Block*>(entity) || dynamic_cast<BlockEnd*>(entity)
                    || dynamic_cast<Seqend*>(entity) || dynamic_cast<Vertex*>(entity)
                    || dynamic_cast<AttributeEntity*>(entity))
                    continue;
                if (std::find(record->Entities.begin(), record->Entities.end(), e) != record->Entities.end())
                    continue;
                entity->OwnerHandle = h;
                record->Entities.push_back(e);
            }

            // 视口的叠放序号（DXF 68）不存于 DWG：AutoCAD 按布局中的顺序编号，总视口为 1，关闭的为 0
            std::int16_t stack = 0;
            for (Handle e : record->Entities)
            {
                if (auto* vp = m_db->FindAs<Viewport>(e))
                    vp->ActiveStatus = HasFlag(vp->Status, ViewportStatusFlags::ViewportOff) ? 0 : ++stack;
            }
        }

        // R14 的块记录没有布局句柄：由布局对象反查（R2000 起两边都有）
        for (const auto& [h, obj] : m_db->Objects())
        {
            if (const auto* layout = dynamic_cast<const Layout*>(obj.get()))
            {
                auto* record = m_db->FindAs<BlockRecord>(layout->AssociatedBlockHandle);
                if (record != nullptr && m_db->FindAs<Layout>(record->LayoutHandle) == nullptr)
                    record->LayoutHandle = h;
            }
        }
    }

    // POLYLINE 的顶点、INSERT 的属性：R13～R2000 用首尾句柄，R2004 起用拥有对象列表
    void Reader::BuildOwnedChildren()
    {
        for (auto& [handle, info] : m_infos)
        {
            auto* pl = dynamic_cast<Polyline*>(info.Object);
            auto* insert = dynamic_cast<Insert*>(info.Object);
            if (pl == nullptr && insert == nullptr)
                continue;
            const std::vector<Handle> children = info.FirstChild != kNullHandle
                ? ChainEntities(info.FirstChild, info.LastChild)
                : info.Owned;
            for (Handle c : children)
            {
                CadObject* child = m_db->Find(c);
                if (child == nullptr)
                    continue;
                if (pl != nullptr && dynamic_cast<Vertex*>(child) != nullptr)
                {
                    child->OwnerHandle = handle;
                    auto* mesh = dynamic_cast<PolyfaceMesh*>(pl);
                    (mesh != nullptr && dynamic_cast<VertexFaceRecord*>(child) != nullptr ? mesh->Faces : pl->Vertices).push_back(c);
                }
                else if (insert != nullptr && dynamic_cast<AttributeEntity*>(child) != nullptr)
                {
                    child->OwnerHandle = handle;
                    insert->Attributes.push_back(c);
                }
                else if (dynamic_cast<Seqend*>(child) != nullptr)
                {
                    if (pl != nullptr)
                        pl->SeqendHandle = c;
                    else
                        insert->SeqendHandle = c;
                }
            }
            const Handle seqend = pl != nullptr ? pl->SeqendHandle : insert->SeqendHandle;
            if (CadObject* s = m_db->FindAs<Seqend>(seqend))
                s->OwnerHandle = handle;
        }
    }

    // 实体的线型标志、所在空间；多线样式元素的线型序号
    void Reader::BuildEntityReferences()
    {
        // 材质 ByBlock 在 DWG 中只是标志，DXF 中写 ByBlock 材质对象的句柄（ByLayer 不写）
        Handle byBlockMaterial = kNullHandle;
        if (auto* materials = m_db->FindAs<CadDictionary>(m_headerHandles.DICTIONARY_MATERIALS))
        {
            if (CadObject* m = m_db->FindDictionaryEntry(materials, "ByBlock"))
                byBlockMaterial = m->ObjectHandle;
        }

        for (auto& [handle, info] : m_infos)
        {
            if (auto* entity = dynamic_cast<Entity*>(info.Object))
            {
                if (info.MaterialFlags == 1)
                    entity->MaterialHandle = byBlockMaterial;
                if (auto book = m_bookColors.find(entity->BookColorHandle); book != m_bookColors.end())
                    entity->Color = book->second;
                switch (info.LtypeFlags)
                {
                case 0: entity->LineTypeHandle = m_headerHandles.BYLAYER; break;
                case 1: entity->LineTypeHandle = m_headerHandles.BYBLOCK; break;
                case 2: entity->LineTypeHandle = m_headerHandles.CONTINUOUS; break;
                default: break;
                }
                if (entity->OwnerHandle == kNullHandle)
                {
                    if (info.EntityMode == 1)
                        entity->OwnerHandle = m_headerHandles.PAPER_SPACE;
                    else if (info.EntityMode == 2)
                        entity->OwnerHandle = m_headerHandles.MODEL_SPACE;
                }
            }
            else if (auto* style = dynamic_cast<MLineStyle*>(info.Object))
            {
                for (std::size_t i = 0; i < style->Elements.size() && i < info.ElementLineTypeIndex.size(); ++i)
                {
                    const int index = info.ElementLineTypeIndex[i];
                    if (index < 0)
                        continue;
                    if (index == 0x7FFF)
                        style->Elements[i].LineTypeHandle = m_headerHandles.BYLAYER;
                    else if (index == 0x7FFE)
                        style->Elements[i].LineTypeHandle = m_headerHandles.BYBLOCK;
                    else if (static_cast<std::size_t>(index) < m_lineTypeIndex.size())
                        style->Elements[i].LineTypeHandle = m_lineTypeIndex[index];
                }
            }
        }
    }

    // 字典条目的名称与所有者（与 DXF 读取一致）
    void Reader::BuildDictionaries()
    {
        for (const auto& [h, obj] : m_db->Objects())
        {
            auto* dict = dynamic_cast<CadDictionary*>(obj.get());
            if (dict == nullptr)
                continue;
            const std::size_t n = std::min(dict->EntryNames.size(), dict->EntryHandles.size());
            for (std::size_t k = 0; k < n; ++k)
            {
                auto* entry = m_db->FindAs<NonGraphicalObject>(dict->EntryHandles[k]);
                if (entry == nullptr)
                    continue;
                if (entry->Name.empty())
                    entry->Name = dict->EntryNames[k];
                if (entry->OwnerHandle == kNullHandle)
                    entry->OwnerHandle = dict->ObjectHandle;
            }
        }
        if (m_db->RootDictionary() == nullptr)
        {
            for (const auto& [h, obj] : m_db->Objects())
            {
                auto* dict = dynamic_cast<CadDictionary*>(obj.get());
                if (dict != nullptr && dict->OwnerHandle == kNullHandle)
                {
                    m_db->SetRootDictionary(h);
                    break;
                }
            }
        }
    }

    // 带默认值的字典，默认项必须是字典中的条目。AutoCAD 存为 R14 的打印样式字典（ACAD_PLOTSTYLENAME）
    // 没有条目、只有默认项句柄，写成 DXF 后 AUDIT 报"Default Name Is Missing"：与 AUDIT 的修复相同，补上条目
    void Reader::FixDictionaryDefaults()
    {
        std::vector<CadDictionaryWithDefault*> broken;
        for (const auto& [h, obj] : m_db->Objects())
        {
            auto* dict = dynamic_cast<CadDictionaryWithDefault*>(obj.get());
            if (dict != nullptr
                && std::find(dict->EntryHandles.begin(), dict->EntryHandles.end(), dict->DefaultEntryHandle) == dict->EntryHandles.end())
                broken.push_back(dict);
        }
        for (CadDictionaryWithDefault* dict : broken)
        {
            auto* entry = m_db->FindAs<NonGraphicalObject>(dict->DefaultEntryHandle);
            if (entry == nullptr)
            {
                auto placeholder = std::make_unique<AcdbPlaceHolder>();
                placeholder->Reactors.push_back(dict->ObjectHandle);
                entry = m_db->Add(std::move(placeholder));
            }
            if (entry->Name.empty())
                entry->Name = "Normal";
            entry->OwnerHandle = dict->ObjectHandle;
            dict->EntryNames.push_back(entry->Name);
            dict->EntryHandles.push_back(entry->ObjectHandle);
            dict->DefaultEntryHandle = entry->ObjectHandle;
            Notify(NotificationType::Info, "带默认值的字典 " + DxfValue(HandleValue{ dict->ObjectHandle }).AsString() +
                                               " 的默认项不在条目中，已补为 " + entry->Name);
        }
    }

    // 扩展数据中 1003 存的是图层句柄，换成图层名（与 DXF 一致）
    void Reader::BuildExtendedData()
    {
        for (const auto& [h, obj] : m_db->Objects())
        {
            for (ExtendedData& data : obj->ExtendedDataList)
            {
                for (ExtendedDataRecord& rec : data.Records)
                {
                    if (rec.Code != 1003)
                        continue;
                    if (const Handle* layer = std::get_if<Handle>(&rec.Value))
                    {
                        const auto* entry = m_db->FindAs<Layer>(*layer);
                        rec.Value = entry != nullptr ? entry->Name : std::string("0");
                    }
                }
            }
        }
    }

    // AutoCAD 另存为 R14 时，R14 表达不了而 R2000 起有的数据存在对象扩展字典的 ACAD_XREC_ROUNDTRIP 中，
    // 打开时应用并删除（与 AutoCAD 打开 R14 文件的做法一致）：
    //   EXTNAMES：（1 大写名称、2 原名称）…… —— R14 的名称都是大写；表项改名，字典改条目名
    //   ACADR14ROUNDTRIP：1000 对象类型、1002 {、（1070 DXF 组码、该组码的值）……、1002 } —— 如标注样式的 DIMDSEP、DIMBLK
    //   LINEWEIGHT：280 线宽序号；PLOTBIT：280 打印标志（图层）
    // 其他记录（真彩色、着色打印等 R2000 也表达不了的）原样保留
    void Reader::ApplyR14RoundTrip()
    {
        auto sameName = [](std::string_view a, std::string_view b) {
            return a.size() == b.size() && std::equal(a.begin(), a.end(), b.begin(), [](char x, char y) {
                       return std::toupper(static_cast<unsigned char>(x)) == std::toupper(static_cast<unsigned char>(y));
                   });
        };
        // 按 DXF 组码给对象的属性赋值
        auto assign = [](CadObject& object, int code, const DxfValue& value) {
            for (const DxfSubclassInfo& sub : object.GetClassInfo().Subclasses)
            {
                for (const DxfPropertyInfo& p : sub.Properties)
                {
                    if (p.Set == nullptr)
                        continue;
                    for (int k = 0; k < p.CodeCount; ++k)
                    {
                        if (p.Codes[k] == code)
                        {
                            p.Set(object, code, value);
                            return true;
                        }
                    }
                }
            }
            return false;
        };

        std::vector<Handle> owners;
        for (const auto& [h, obj] : m_db->Objects())
        {
            if (m_db->FindAs<CadDictionary>(obj->XDictionaryHandle) != nullptr)
                owners.push_back(h);
        }
        int applied = 0;
        for (Handle ownerHandle : owners)
        {
            // 前面处理时可能已删除（扩展字典自己也有扩展字典的情况）
            CadObject* owner = m_db->Find(ownerHandle);
            auto* xdict = owner != nullptr ? m_db->FindAs<CadDictionary>(owner->XDictionaryHandle) : nullptr;
            if (xdict == nullptr)
                continue;
            const CadObject* entry = m_db->FindDictionaryEntry(xdict, "ACAD_XREC_ROUNDTRIP");
            auto* record = entry != nullptr ? m_db->FindAs<XRecord>(entry->ObjectHandle) : nullptr;
            if (record == nullptr)
                continue;

            std::vector<XRecordEntry> rest;
            const auto& es = record->Entries;
            for (std::size_t i = 0; i < es.size();)
            {
                const std::string tag = es[i].Code == 102 ? es[i].Value.AsString() : std::string();
                std::size_t next = i + 1;
                while (next < es.size() && es[next].Code != 102)
                    ++next;
                if (tag == "EXTNAMES")
                {
                    for (std::size_t k = i + 1; k + 1 < next; k += 2)
                    {
                        const std::string from = es[k].Value.AsString();
                        const std::string to = es[k + 1].Value.AsString();
                        if (auto* tableEntry = dynamic_cast<TableEntry*>(owner); tableEntry && sameName(tableEntry->Name, from))
                        {
                            tableEntry->Name = to;
                            if (auto* br = dynamic_cast<BlockRecord*>(owner))
                            {
                                if (auto* block = m_db->FindAs<Block>(br->BlockEntityHandle))
                                    block->Name = to;
                            }
                        }
                        else if (auto* dict = dynamic_cast<CadDictionary*>(owner))
                        {
                            for (std::size_t n = 0; n < dict->EntryNames.size() && n < dict->EntryHandles.size(); ++n)
                            {
                                if (dict->EntryNames[n] != from)
                                    continue;
                                dict->EntryNames[n] = to;
                                if (auto* named = m_db->FindAs<NonGraphicalObject>(dict->EntryHandles[n]); named && named->Name == from)
                                    named->Name = to;
                            }
                        }
                    }
                    ++applied;
                }
                else if (tag == "ACADR14ROUNDTRIP")
                {
                    // 1000 类型、1002 {、（1070 组码、值）……、1002 }
                    for (std::size_t k = i + 1; k + 1 < next; ++k)
                    {
                        if (es[k].Code == 1070)
                        {
                            assign(*owner, static_cast<int>(es[k].Value.AsInt()), es[k + 1].Value);
                            ++k;
                        }
                    }
                    ++applied;
                }
                else if ((tag == "LINEWEIGHT" || tag == "PLOTBIT") && next == i + 2 && dynamic_cast<Layer*>(owner) != nullptr)
                {
                    auto* layer = static_cast<Layer*>(owner);
                    if (tag == "LINEWEIGHT")
                        layer->LineWeight = LineWeightFromIndex(static_cast<std::uint8_t>(es[i + 1].Value.AsInt()));
                    else
                        layer->PlotFlag = es[i + 1].Value.AsInt() != 0;
                    ++applied;
                }
                else
                {
                    rest.insert(rest.end(), es.begin() + static_cast<std::ptrdiff_t>(i), es.begin() + static_cast<std::ptrdiff_t>(next));
                }
                i = next;
            }
            if (rest.size() == record->Entries.size())
                continue;
            record->Entries = std::move(rest);

            // 用完的记录从扩展字典中去掉；扩展字典空了也去掉
            if (record->Entries.empty())
            {
                for (std::size_t n = 0; n < xdict->EntryHandles.size(); ++n)
                {
                    if (xdict->EntryHandles[n] != record->ObjectHandle)
                        continue;
                    xdict->EntryHandles.erase(xdict->EntryHandles.begin() + static_cast<std::ptrdiff_t>(n));
                    if (n < xdict->EntryNames.size())
                        xdict->EntryNames.erase(xdict->EntryNames.begin() + static_cast<std::ptrdiff_t>(n));
                    break;
                }
                m_db->RemoveObject(record->ObjectHandle);
                if (xdict->EntryHandles.empty())
                {
                    owner->XDictionaryHandle = kNullHandle;
                    m_db->RemoveObject(xdict->ObjectHandle);
                }
            }
        }
        if (applied > 0)
            Notify(NotificationType::Info, "已应用 R14 文件中的往返数据（名称大小写、标注样式与图层的 R2000 属性）" + std::to_string(applied) + " 处");
    }

    // 头段中按句柄引用的当前图层、线型、样式 ……：换成名称
    void Reader::BuildHeaderNames()
    {
        CadHeader& h = m_db->Header;
        auto nameOf = [&](Handle handle, std::string& out) {
            if (const auto* entry = m_db->FindAs<TableEntry>(handle))
                out = entry->Name;
            else if (const auto* named = m_db->FindAs<NonGraphicalObject>(handle); named && !named->Name.empty())
                out = named->Name;
        };
        nameOf(m_headerHandles.CLAYER, h.CurrentLayerName);
        nameOf(m_headerHandles.CELTYPE, h.CurrentLineTypeName);
        nameOf(m_headerHandles.CMLSTYLE, h.CurrentMLineStyleName);
        nameOf(m_headerHandles.TEXTSTYLE, h.CurrentTextStyleName);
        nameOf(m_headerHandles.DIMTXSTY, h.DimensionTextStyleName);
        nameOf(m_headerHandles.DIMSTYLE, h.CurrentDimensionStyleName);
        nameOf(m_headerHandles.DIMBLK, h.DimensionBlockName);
        nameOf(m_headerHandles.DIMLDRBLK, h.ArrowBlockName);
        nameOf(m_headerHandles.DIMBLK1, h.DimensionBlockNameFirst);
        nameOf(m_headerHandles.DIMBLK2, h.DimensionBlockNameSecond);
        nameOf(m_headerHandles.DIMLTYPE, h.DimensionLineType);
        nameOf(m_headerHandles.DIMLTEX1, h.DimensionTex1);
        nameOf(m_headerHandles.DIMLTEX2, h.DimensionTex2);
    }

    // 省略的引用取默认值（与 DXF 读取相同）
    void Reader::FillDefaultReferences()
    {
        auto handleOf = [&](CadTable* table, std::string_view name) {
            TableEntry* e = m_db->FindTableEntry(table, name);
            return e != nullptr ? e->ObjectHandle : kNullHandle;
        };
        const Handle layer0 = handleOf(m_db->Layers(), "0");
        const Handle byLayer = handleOf(m_db->LineTypes(), "ByLayer");
        const Handle continuous = handleOf(m_db->LineTypes(), "Continuous");
        for (const auto& [handle, obj] : m_db->Objects())
        {
            if (auto* entity = dynamic_cast<Entity*>(obj.get()))
            {
                if (m_db->FindAs<Layer>(entity->LayerHandle) == nullptr)
                    entity->LayerHandle = layer0;
                if (entity->LineTypeHandle == kNullHandle)
                    entity->LineTypeHandle = byLayer;
            }
            if (auto* layer = dynamic_cast<Layer*>(obj.get()); layer && layer->LineTypeHandle == kNullHandle)
                layer->LineTypeHandle = continuous;
        }
    }
}
