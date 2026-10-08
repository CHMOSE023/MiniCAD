// DXF 读取：建库——补句柄、关联表/块/子实体/字典、解析名称引用、补齐默认内容
#include "Dxf/Read/DxfReaderImpl.h"
#include <algorithm>
#include <unordered_set>

namespace MiniDWG::DxfRead
{
    void Reader::Build()
    {
        AssignMissingHandles();

        // 对象移入数据库；按下标保留指针供后续关联
        m_ptrs.resize(m_items.size());
        for (std::size_t i = 0; i < m_items.size(); ++i)
        {
            m_ptrs[i] = m_items[i].Object.get();
            m_db->AddObject(std::move(m_items[i].Object));
        }

        BuildTables();
        BuildBlocks();
        BuildEntities();
        BuildChildren();
        BuildDictionaries();

        if (m_options.CreateDefaults)
            m_db->CreateDefaults();

        ResolveNameRefs();
        ResolveXData();
        FillDefaultReferences();

        const Handle seed = std::max(m_db->GetHandleSeed(), static_cast<Handle>(m_db->Header.HandleSeed));
        m_db->SetHandleSeed(seed);
        m_db->Header.HandleSeed = seed;

        if (m_options.Report != nullptr)
        {
            m_options.Report->FileHandleSeed = m_fileHandleSeed;
            m_options.Report->Skipped.assign(m_skipped.begin(), m_skipped.end());
            m_options.Report->Preserved.assign(m_preserved.begin(), m_preserved.end());
        }
    }

    // 没有句柄（R12、手写的 DXF）或句柄重复的对象分配新句柄
    void Reader::AssignMissingHandles()
    {
        Handle maxHandle = 0;
        for (const ReadItem& item : m_items)
            maxHandle = std::max(maxHandle, item.Object->ObjectHandle);
        Handle next = std::max<Handle>(maxHandle + 1, m_db->Header.HandleSeed);
        m_fileHandleSeed = next;

        std::unordered_set<Handle> used;
        for (ReadItem& item : m_items)
        {
            Handle& h = item.Object->ObjectHandle;
            if (h != kNullHandle && used.insert(h).second)
                continue;
            if (h != kNullHandle)
                Notify(NotificationType::Warning, "重复的句柄 " + DxfValue(HandleValue{ h }).AsString() + "，已重新分配");
            h = next++;
            used.insert(h);
        }
        m_db->SetHandleSeed(next);
    }

    CadTable* Reader::EnsureTable(CadObjectType controlType, std::string_view entryDxfName)
    {
        for (CadTable* t : { m_db->BlockRecords(), m_db->Layers(), m_db->DimensionStyles(), m_db->TextStyles(),
                             m_db->LineTypes(), m_db->Views(), m_db->UCSs(), m_db->VPorts(), m_db->AppIds() })
        {
            if (t != nullptr && t->GetObjectType() == controlType)
                return t;
        }
        CadTable* table = m_db->Add(std::make_unique<CadTable>(controlType, entryDxfName));
        m_db->SetTable(controlType, table->ObjectHandle);
        return table;
    }

    BlockRecord* Reader::EnsureBlockRecord(const std::string& name)
    {
        if (auto* record = m_db->FindTableEntry<BlockRecord>(m_db->BlockRecords(), name))
            return record;
        EnsureTable(CadObjectType::BLOCK_CONTROL_OBJ, "BLOCK_RECORD");
        return m_db->CreateBlockRecord(name);
    }

    void Reader::BuildTables()
    {
        for (const TableDef& def : m_tables)
        {
            auto* table = static_cast<CadTable*>(m_ptrs[def.Item]);
            for (CadTable* existing : { m_db->BlockRecords(), m_db->Layers(), m_db->DimensionStyles(),
                                        m_db->TextStyles(), m_db->LineTypes(), m_db->Views(), m_db->UCSs(),
                                        m_db->VPorts(), m_db->AppIds() })
            {
                if (existing != nullptr && existing->GetObjectType() == table->GetObjectType())
                    Notify(NotificationType::Warning, "重复的表 " + def.Name + "，以后出现的为准");
            }
            m_db->SetTable(table->GetObjectType(), table->ObjectHandle);
            for (std::size_t entry : def.Entries)
            {
                CadObject* obj = m_ptrs[entry];
                obj->OwnerHandle = table->ObjectHandle;
                table->Entries.push_back(obj->ObjectHandle);
            }
        }
    }

    void Reader::BuildBlocks()
    {
        for (const BlockDef& def : m_blocks)
        {
            auto* block = static_cast<Block*>(m_ptrs[def.Begin]);
            BlockRecord* record = def.RecordHandle != kNullHandle ? m_db->FindAs<BlockRecord>(def.RecordHandle) : nullptr;
            if (record == nullptr)
                record = m_db->FindTableEntry<BlockRecord>(m_db->BlockRecords(), def.Name);
            if (record == nullptr)
            {
                // R12 没有块记录表：按 BLOCK 新建块记录
                auto created = std::make_unique<BlockRecord>();
                created->Name = def.Name;
                record = m_db->AddTableEntry(EnsureTable(CadObjectType::BLOCK_CONTROL_OBJ, "BLOCK_RECORD"),
                                             std::move(created));
            }

            block->OwnerHandle = record->ObjectHandle;
            record->BlockEntityHandle = block->ObjectHandle;
            if (def.End)
            {
                m_ptrs[*def.End]->OwnerHandle = record->ObjectHandle;
                record->BlockEndHandle = m_ptrs[*def.End]->ObjectHandle;
            }
            else
            {
                BlockEnd* end = m_db->Add(std::make_unique<BlockEnd>());
                end->OwnerHandle = record->ObjectHandle;
                record->BlockEndHandle = end->ObjectHandle;
            }
            for (std::size_t e : def.Entities)
            {
                CadObject* obj = m_ptrs[e];
                obj->OwnerHandle = record->ObjectHandle;
                record->Entities.push_back(obj->ObjectHandle);
            }
        }

        // 表中有、BLOCKS 段里没有的块记录：补上 BLOCK / ENDBLK
        if (CadTable* records = m_db->BlockRecords())
        {
            for (Handle h : records->Entries)
            {
                auto* record = m_db->FindAs<BlockRecord>(h);
                if (record == nullptr)
                    continue;
                if (m_db->FindAs<Block>(record->BlockEntityHandle) == nullptr)
                {
                    auto block = std::make_unique<Block>();
                    block->Name = record->Name;
                    block->OwnerHandle = record->ObjectHandle;
                    record->BlockEntityHandle = m_db->Add(std::move(block))->ObjectHandle;
                }
                if (m_db->FindAs<BlockEnd>(record->BlockEndHandle) == nullptr)
                {
                    auto end = std::make_unique<BlockEnd>();
                    end->OwnerHandle = record->ObjectHandle;
                    record->BlockEndHandle = m_db->Add(std::move(end))->ObjectHandle;
                }
            }
        }
    }

    // ENTITIES 段：有所有者（块记录）的放入所有者；没有的按 67 放入模型空间或图纸空间
    void Reader::BuildEntities()
    {
        std::unordered_set<Handle> placed;
        if (CadTable* records = m_db->BlockRecords())
        {
            for (Handle h : records->Entries)
            {
                if (auto* record = m_db->FindAs<BlockRecord>(h))
                    placed.insert(record->Entities.begin(), record->Entities.end());
            }
        }

        BlockRecord* model = nullptr;
        BlockRecord* paper = nullptr;
        for (std::size_t e : m_entities)
        {
            CadObject* obj = m_ptrs[e];
            auto* record = obj->OwnerHandle != kNullHandle ? m_db->FindAs<BlockRecord>(obj->OwnerHandle) : nullptr;
            if (record == nullptr)
            {
                if (m_items[e].PaperSpace)
                    record = paper != nullptr ? paper : (paper = EnsureBlockRecord("*Paper_Space"));
                else
                    record = model != nullptr ? model : (model = EnsureBlockRecord("*Model_Space"));
            }
            obj->OwnerHandle = record->ObjectHandle;
            if (placed.insert(obj->ObjectHandle).second)
                record->Entities.push_back(obj->ObjectHandle);
        }
    }

    void Reader::BuildChildren()
    {
        for (std::size_t i = 0; i < m_items.size(); ++i)
        {
            const ReadItem& item = m_items[i];
            if (item.Children.empty() && !item.Seqend)
                continue;
            CadObject* parent = m_ptrs[i];
            std::vector<Handle> children;
            for (std::size_t c : item.Children)
            {
                m_ptrs[c]->OwnerHandle = parent->ObjectHandle;
                children.push_back(m_ptrs[c]->ObjectHandle);
            }
            Handle seqend = kNullHandle;
            if (item.Seqend)
            {
                m_ptrs[*item.Seqend]->OwnerHandle = parent->ObjectHandle;
                seqend = m_ptrs[*item.Seqend]->ObjectHandle;
            }
            if (auto* pl = dynamic_cast<Polyline*>(parent))
            {
                // 多面网格：顶点之后是面（VERTEX 中的面记录）
                if (auto* mesh = dynamic_cast<PolyfaceMesh*>(pl))
                {
                    for (Handle c : children)
                        (m_db->FindAs<VertexFaceRecord>(c) != nullptr ? mesh->Faces : mesh->Vertices).push_back(c);
                }
                else
                {
                    pl->Vertices = std::move(children);
                }
                pl->SeqendHandle = seqend;
            }
            else if (auto* insert = dynamic_cast<Insert*>(parent))
            {
                insert->Attributes = std::move(children);
                insert->SeqendHandle = seqend;
            }
        }
    }

    void Reader::BuildDictionaries()
    {
        for (std::size_t o : m_objects)
        {
            auto* dict = dynamic_cast<CadDictionary*>(m_ptrs[o]);
            if (dict == nullptr)
                continue;
            if (m_db->RootDictionary() == nullptr && dict->OwnerHandle == kNullHandle)
                m_db->SetRootDictionary(dict->ObjectHandle);

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
        // 根字典的所有者句柄可能是 0 以外的值（个别程序写成自身），退而取 OBJECTS 段的第一个字典
        if (m_db->RootDictionary() == nullptr)
        {
            for (std::size_t o : m_objects)
            {
                if (auto* dict = dynamic_cast<CadDictionary*>(m_ptrs[o]))
                {
                    m_db->SetRootDictionary(dict->ObjectHandle);
                    break;
                }
            }
        }
    }

    Handle Reader::FindByName(std::string_view target, std::string_view name)
    {
        CadTable* table = nullptr;
        if (target == "Layer") table = m_db->Layers();
        else if (target == "LineType") table = m_db->LineTypes();
        else if (target == "TextStyle") table = m_db->TextStyles();
        else if (target == "DimensionStyle") table = m_db->DimensionStyles();
        else if (target == "BlockRecord") table = m_db->BlockRecords();
        else if (target == "AppId") table = m_db->AppIds();
        else if (target == "UCS") table = m_db->UCSs();
        else if (target == "View") table = m_db->Views();
        else if (target == "VPort") table = m_db->VPorts();

        if (table != nullptr)
        {
            TableEntry* entry = m_db->FindTableEntry(table, name);
            return entry != nullptr ? entry->ObjectHandle : kNullHandle;
        }

        std::string_view dictionary;
        if (target == "MLineStyle") dictionary = "ACAD_MLINESTYLE";
        else if (target == "Material") dictionary = "ACAD_MATERIAL";
        else if (target == "Scale") dictionary = "ACAD_SCALELIST";
        else if (target == "BookColor") dictionary = "ACAD_COLOR";     // 430：颜色簿$颜色名
        if (!dictionary.empty())
        {
            if (CadDictionary* dict = m_db->FindNamedDictionary(dictionary))
            {
                const std::size_t n = std::min(dict->EntryNames.size(), dict->EntryHandles.size());
                for (std::size_t k = 0; k < n; ++k)
                {
                    if (EqualsIgnoreCaseAscii(dict->EntryNames[k], name))
                        return dict->EntryHandles[k];
                }
            }
        }
        return kNullHandle;
    }

    void Reader::ResolveNameRefs()
    {
        for (std::size_t i = 0; i < m_items.size(); ++i)
        {
            CadObject* obj = m_ptrs[i];
            if (auto* table = dynamic_cast<TableEntity*>(obj); table != nullptr && !table->BlockName.empty())
            {
                if (m_db->FindAs<BlockRecord>(table->BlockHandle) == nullptr)
                    table->BlockHandle = FindByName("BlockRecord", table->BlockName);
                table->BlockName.clear();
            }
            for (const NameRef& ref : m_items[i].NameRefs)
            {
                if (ref.Name.empty() || ref.Property == nullptr || ref.Property->Set == nullptr)
                    continue;
                const std::string_view target = ref.Property->RefTarget;
                Handle h = FindByName(target, ref.Name);
                if (h == kNullHandle && target == "Layer")
                {
                    // 图层表中没有的图层（常见于手写或 R12 的 DXF）：按名称新建
                    auto layer = std::make_unique<Layer>();
                    layer->Name = ref.Name;
                    if (auto* continuous = m_db->FindTableEntry(m_db->LineTypes(), "Continuous"))
                        layer->LineTypeHandle = continuous->ObjectHandle;
                    h = m_db->AddTableEntry(EnsureTable(CadObjectType::LAYER_CONTROL_OBJ, "LAYER"), std::move(layer))
                            ->ObjectHandle;
                }
                if (h == kNullHandle)
                {
                    if (target != "BookColor")
                        NotifyOnce("ref:" + std::string(target) + ":" + ref.Name, NotificationType::Warning,
                                   "找不到 " + std::string(target) + " \"" + ref.Name + "\"");
                    continue;
                }
                ref.Property->Set(*obj, ref.Property->Codes[0], DxfValue(HandleValue{ h }));
            }

            if (auto* style = dynamic_cast<MLineStyle*>(obj))
            {
                for (const auto& [element, name] : m_items[i].ElementLineTypes)
                {
                    if (element < style->Elements.size())
                        style->Elements[element].LineTypeHandle = FindByName("LineType", name);
                }
            }
        }
    }

    void Reader::ResolveXData()
    {
        for (std::size_t i = 0; i < m_items.size(); ++i)
        {
            CadObject* obj = m_ptrs[i];
            const auto& names = m_items[i].XDataAppNames;
            for (std::size_t k = 0; k < names.size() && k < obj->ExtendedDataList.size(); ++k)
            {
                Handle h = FindByName("AppId", names[k]);
                if (h == kNullHandle)
                {
                    auto appId = std::make_unique<AppId>();
                    appId->Name = names[k];
                    h = m_db->AddTableEntry(EnsureTable(CadObjectType::APPID_CONTROL_OBJ, "APPID"), std::move(appId))
                            ->ObjectHandle;
                }
                obj->ExtendedDataList[k].AppIdHandle = h;
            }
        }
    }

    // 文件中省略的引用取默认值：图层 0、线型 ByLayer / Continuous、文字样式与标注样式 Standard
    void Reader::FillDefaultReferences()
    {
        auto handleOf = [&](CadTable* table, std::string_view name) {
            TableEntry* e = m_db->FindTableEntry(table, name);
            return e != nullptr ? e->ObjectHandle : kNullHandle;
        };
        const Handle layer0 = handleOf(m_db->Layers(), "0");
        const Handle byLayer = handleOf(m_db->LineTypes(), "ByLayer");
        const Handle continuous = handleOf(m_db->LineTypes(), "Continuous");
        const Handle standardText = handleOf(m_db->TextStyles(), "Standard");
        const Handle standardDim = handleOf(m_db->DimensionStyles(), "Standard");

        for (const auto& [handle, obj] : m_db->Objects())
        {
            if (auto* entity = dynamic_cast<Entity*>(obj.get()))
            {
                if (entity->LayerHandle == kNullHandle)
                    entity->LayerHandle = layer0;
                if (entity->LineTypeHandle == kNullHandle)
                    entity->LineTypeHandle = byLayer;
            }
            if (auto* layer = dynamic_cast<Layer*>(obj.get()); layer && layer->LineTypeHandle == kNullHandle)
                layer->LineTypeHandle = continuous;
            if (auto* text = dynamic_cast<TextEntity*>(obj.get()); text && text->StyleHandle == kNullHandle)
                text->StyleHandle = standardText;
            if (auto* mtext = dynamic_cast<MText*>(obj.get()); mtext && mtext->StyleHandle == kNullHandle)
                mtext->StyleHandle = standardText;
            if (auto* dim = dynamic_cast<Dimension*>(obj.get()); dim && dim->StyleHandle == kNullHandle)
                dim->StyleHandle = standardDim;
            if (auto* style = dynamic_cast<DimensionStyle*>(obj.get()); style && style->StyleHandle == kNullHandle)
                style->StyleHandle = standardText;
        }
    }
}
