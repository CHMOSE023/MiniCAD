// DWG 读取：建库——表、块记录的实体、子实体、字典、名称引用、默认内容
#include "Dwg/Read/DwgReaderImpl.h"
#include "Database/DxfMeta.h"
#include <algorithm>
#include <cctype>
#include <set>
#include <typeinfo>

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

        // 按动态类型缓存 dynamic_cast 的结果。建库要对十万个对象反复判断类型，dynamic_cast 每次要
        // 遍历类型层次（MSVC 上约 50 纳秒），而同一动态类型的结果总是相同；不同的动态类型只有几十种
        template <class T>
        class CastCache
        {
        public:
            T* operator()(CadObject* object)
            {
                if (object == nullptr)
                    return nullptr;
                const std::type_info* type = &typeid(*object);
                for (const auto& [known, is] : m_known)
                {
                    if (known == type)
                        return is ? static_cast<T*>(object) : nullptr;
                }
                T* result = dynamic_cast<T*>(object);
                m_known.emplace_back(type, result != nullptr);
                return result;
            }

        private:
            std::vector<std::pair<const std::type_info*, bool>> m_known;
        };

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
        BuildEntityReferences();
        BuildDictionaries();
        if (R13_14Only())
            ApplyR14RoundTrip();
        BuildExtendedData();

        // Objects() 按句柄升序，最后一个就是最大的
        const Handle maxHandle = m_db->Objects().empty() ? 0 : m_db->Objects().rbegin()->first;
        const Handle fileSeed = std::max<Handle>(maxHandle + 1, m_db->Header.HandleSeed);
        m_db->SetHandleSeed(fileSeed);

        if (m_options.CreateDefaults)
            m_db->CreateDefaults();
        FixDictionaryDefaults(FillDefaultReferences());
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
            entries.reserve(table->Entries.size());
            std::unordered_set<Handle> seen;
            seen.reserve(table->Entries.size());
            for (Handle h : table->Entries)
            {
                const auto* entry = m_db->FindAs<TableEntry>(h);
                if (entry != nullptr && entry->GetDxfName() == table->GetEntryDxfName()
                    && seen.insert(h).second)
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

        CastCache<Entity> asEntity;
        CastCache<Block> asBlock;
        CastCache<BlockEnd> asBlockEnd;
        CastCache<Seqend> asSeqend;
        CastCache<Vertex> asVertex;
        CastCache<AttributeEntity> asAttribute;
        CastCache<Viewport> asViewport;
        std::vector<Entity*> entities;
        std::vector<Handle> sorted;
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
            const bool linked = info->second.FirstChild != kNullHandle;
            const std::vector<Handle> chain = linked
                ? ChainEntities(info->second.FirstChild, info->second.LastChild)
                : std::vector<Handle>{};
            const auto& handles = linked ? chain : info->second.Owned;
            // 模型空间也是块记录；逐个 std::find 会对 N 个实体做 O(N²) 比较。
            // 集合只用于去重，实体列表仍保持文件中的顺序。通常没有重复：先排序检查，没有重复时
            // 不建哈希集合（几万个节点的分配比排序慢）
            sorted.assign(handles.begin(), handles.end());
            std::sort(sorted.begin(), sorted.end());
            const bool unique = record->Entities.empty() && std::adjacent_find(sorted.begin(), sorted.end()) == sorted.end();
            std::unordered_set<Handle> seen;
            if (!unique)
            {
                seen.reserve(record->Entities.size() + handles.size());
                seen.insert(record->Entities.begin(), record->Entities.end());
            }
            record->Entities.reserve(record->Entities.size() + handles.size());
            // 与 record->Entities 一一对应的实体指针，后面编视口序号时不必再按句柄查找
            entities.clear();
            for (Handle e : record->Entities)
                entities.push_back(asEntity(m_db->Find(e)));
            for (Handle e : handles)
            {
                CadObject* object = m_db->Find(e);
                auto* entity = asEntity(object);
                if (entity == nullptr || asBlock(object) || asBlockEnd(object) || asSeqend(object) || asVertex(object)
                    || asAttribute(object))
                    continue;
                if (!unique && !seen.insert(e).second)
                    continue;
                entity->OwnerHandle = h;
                record->Entities.push_back(e);
                entities.push_back(entity);
            }

            // 视口的叠放序号（DXF 68）不存于 DWG：AutoCAD 按布局中的顺序编号，总视口为 1，关闭的为 0
            std::int16_t stack = 0;
            for (Entity* entity : entities)
            {
                if (auto* vp = asViewport(entity))
                    vp->ActiveStatus = HasFlag(vp->Status, ViewportStatusFlags::ViewportOff) ? 0 : ++stack;
            }
        }

        // R14 的块记录没有布局句柄：由布局对象反查（R2000 起两边都有）
        // 只看读到的布局，按句柄升序（与遍历全部对象时的顺序相同）
        std::sort(m_layoutHandles.begin(), m_layoutHandles.end());
        m_layoutHandles.erase(std::unique(m_layoutHandles.begin(), m_layoutHandles.end()), m_layoutHandles.end());
        for (Handle h : m_layoutHandles)
        {
            if (const auto* layout = m_db->FindAs<Layout>(h))
            {
                auto* record = m_db->FindAs<BlockRecord>(layout->AssociatedBlockHandle);
                if (record != nullptr && m_db->FindAs<Layout>(record->LayoutHandle) == nullptr)
                    record->LayoutHandle = h;
            }
        }
    }

    // 实体的线型标志、所在空间、子对象；多线样式元素的线型序号。
    // 子对象与所在空间放在同一遍里：子对象的所有者由父对象直接改写，所在空间只在所有者为空时填写，
    // 两者的先后不影响结果
    void Reader::BuildEntityReferences()
    {
        // 材质 ByBlock 在 DWG 中只是标志，DXF 中写 ByBlock 材质对象的句柄（ByLayer 不写）
        Handle byBlockMaterial = kNullHandle;
        if (auto* materials = m_db->FindAs<CadDictionary>(m_headerHandles.DICTIONARY_MATERIALS))
        {
            if (CadObject* m = m_db->FindDictionaryEntry(materials, "ByBlock"))
                byBlockMaterial = m->ObjectHandle;
        }

        CastCache<Entity> asEntity;
        CastCache<MLineStyle> asMLineStyle;
        CastCache<Polyline> asPolyline;
        CastCache<Insert> asInsert;
        CastCache<Vertex> asVertex;
        CastCache<PolyfaceMesh> asMesh;
        CastCache<VertexFaceRecord> asFace;
        CastCache<AttributeEntity> asAttribute;
        CastCache<Seqend> asSeqend;

        // POLYLINE 的顶点、INSERT 的属性：R13～R2000 用首尾句柄，R2004 起用拥有对象列表
        auto ownedChildren = [&](Handle handle, const ObjectInfo& info, Polyline* pl, Insert* insert) {
            const bool linked = info.FirstChild != kNullHandle;
            const std::vector<Handle> chain = linked
                ? ChainEntities(info.FirstChild, info.LastChild)
                : std::vector<Handle>{};
            const auto& children = linked ? chain : info.Owned;
            for (Handle c : children)
            {
                CadObject* child = m_db->Find(c);
                if (child == nullptr)
                    continue;
                if (pl != nullptr && asVertex(child) != nullptr)
                {
                    child->OwnerHandle = handle;
                    auto* mesh = asMesh(pl);
                    (mesh != nullptr && asFace(child) != nullptr ? mesh->Faces : pl->Vertices).push_back(c);
                }
                else if (insert != nullptr && asAttribute(child) != nullptr)
                {
                    child->OwnerHandle = handle;
                    insert->Attributes.push_back(c);
                }
                else if (asSeqend(child) != nullptr)
                {
                    if (pl != nullptr)
                        pl->SeqendHandle = c;
                    else
                        insert->SeqendHandle = c;
                }
            }
            const Handle seqend = pl != nullptr ? pl->SeqendHandle : insert->SeqendHandle;
            if (CadObject* s = asSeqend(m_db->Find(seqend)))
                s->OwnerHandle = handle;
        };

        for (auto& [handle, info] : m_infos)
        {
            if (auto* entity = asEntity(info.Object))
            {
                auto* pl = asPolyline(entity);
                auto* insert = pl == nullptr ? asInsert(entity) : nullptr;
                if (pl != nullptr || insert != nullptr)
                    ownedChildren(handle, info, pl, insert);
                if (info.MaterialFlags == 1)
                    entity->MaterialHandle = byBlockMaterial;
                if (!m_bookColors.empty())
                {
                    if (auto book = m_bookColors.find(entity->BookColorHandle); book != m_bookColors.end())
                        entity->Color = book->second;
                }
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
            else if (auto* style = asMLineStyle(info.Object))
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
        // 只看读到的字典，按句柄升序（与遍历全部对象时的顺序相同：条目被多个字典引用时先到的命名）
        std::sort(m_dictionaryHandles.begin(), m_dictionaryHandles.end());
        m_dictionaryHandles.erase(std::unique(m_dictionaryHandles.begin(), m_dictionaryHandles.end()),
                                  m_dictionaryHandles.end());
        CastCache<CadDictionary> asDictionary;
        CastCache<NonGraphicalObject> asNonGraphical;
        for (Handle h : m_dictionaryHandles)
        {
            auto* dict = asDictionary(m_db->Find(h));
            if (dict == nullptr)
                continue;
            const std::size_t n = std::min(dict->EntryNames.size(), dict->EntryHandles.size());
            for (std::size_t k = 0; k < n; ++k)
            {
                auto* entry = asNonGraphical(m_db->Find(dict->EntryHandles[k]));
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
            for (Handle h : m_dictionaryHandles)
            {
                auto* dict = asDictionary(m_db->Find(h));
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
    // broken：默认项不在条目中的字典（FillDefaultReferences 遍历全部对象时顺带找出）
    void Reader::FixDictionaryDefaults(const std::vector<CadDictionaryWithDefault*>& broken)
    {
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
        // 只看读取时遇到过 1003 的对象（同一对象可能记了多次，第二次时值已是名称，不再处理）
        CastCache<Layer> asLayer;
        for (Handle h : m_layerXDataOwners)
        {
            CadObject* obj = m_db->Find(h);
            if (obj == nullptr)
                continue;
            for (ExtendedData& data : obj->ExtendedDataList)
            {
                for (ExtendedDataRecord& rec : data.Records)
                {
                    if (rec.Code != 1003)
                        continue;
                    if (const Handle* layer = std::get_if<Handle>(&rec.Value))
                    {
                        const auto* entry = asLayer(m_db->Find(*layer));
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

    // 省略的引用取默认值（与 DXF 读取相同）。同一遍里找出默认项不在条目中的带默认值字典并返回，
    // 由 FixDictionaryDefaults 补上（它只增加占位对象、改字典条目，与这里改的实体和图层无关）
    std::vector<CadDictionaryWithDefault*> Reader::FillDefaultReferences()
    {
        auto handleOf = [&](CadTable* table, std::string_view name) {
            TableEntry* e = m_db->FindTableEntry(table, name);
            return e != nullptr ? e->ObjectHandle : kNullHandle;
        };
        const Handle layer0 = handleOf(m_db->Layers(), "0");
        const Handle byLayer = handleOf(m_db->LineTypes(), "ByLayer");
        const Handle continuous = handleOf(m_db->LineTypes(), "Continuous");
        CastCache<Entity> asEntity;
        CastCache<Layer> asLayer;
        CastCache<CadDictionaryWithDefault> asDictionaryWithDefault;
        std::vector<CadDictionaryWithDefault*> broken;
        // 图层只有几十到几百个，而实体有几万个：记住查过的图层句柄是否有效
        std::unordered_map<Handle, bool> layerValid;
        for (const auto& [handle, obj] : m_db->Objects())
        {
            if (auto* entity = asEntity(obj.get()))
            {
                auto [it, inserted] = layerValid.try_emplace(entity->LayerHandle, false);
                if (inserted)
                    it->second = asLayer(m_db->Find(entity->LayerHandle)) != nullptr;
                if (!it->second)
                    entity->LayerHandle = layer0;
                if (entity->LineTypeHandle == kNullHandle)
                    entity->LineTypeHandle = byLayer;
            }
            else if (auto* layer = asLayer(obj.get()))
            {
                if (layer->LineTypeHandle == kNullHandle)
                    layer->LineTypeHandle = continuous;
            }
            else if (auto* dict = asDictionaryWithDefault(obj.get());
                     dict != nullptr
                     && std::find(dict->EntryHandles.begin(), dict->EntryHandles.end(), dict->DefaultEntryHandle) == dict->EntryHandles.end())
            {
                broken.push_back(dict);
            }
        }
        return broken;
    }
}
