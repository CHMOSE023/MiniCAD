#include "Database/CadDatabase.h"
#include "Database/DxfMeta.h"
#include <algorithm>
#include <cctype>
#include <utility>

namespace MiniDWG
{
    namespace
    {
        bool EqualsIgnoreCase(std::string_view a, std::string_view b)
        {
            return a.size() == b.size()
                && std::equal(a.begin(), a.end(), b.begin(), [](char x, char y) {
                       return std::tolower(static_cast<unsigned char>(x)) == std::tolower(static_cast<unsigned char>(y));
                   });
        }

        // 根字典的默认条目，顺序与 ACadSharp CadDictionary.CreateDefaultEntries 一致
        constexpr std::string_view kRootEntries[] = {
            "ACAD_COLOR", "ACAD_GROUP", "ACAD_LAYOUT", "ACAD_MATERIAL", "ACAD_SORTENTS",
            "ACAD_MLEADERSTYLE", "ACAD_MLINESTYLE", "ACAD_TABLESTYLE", "ACAD_PLOTSETTINGS",
            "AcDbVariableDictionary", "ACAD_SCALELIST", "ACAD_VISUALSTYLE", "ACAD_IMAGE_DICT",
            "ACAD_FIELDLIST",
        };

        // ACAD_SCALELIST 的默认比例（ACadSharp ScaleCollection.CreateDefaults）
        struct DefaultScale
        {
            std::string_view name;
            double           paper;
            double           drawing;
        };

        constexpr DefaultScale kDefaultScales[] = {
            { "1:1", 1, 1 },    { "1:2", 1, 2 },    { "1:4", 1, 4 },     { "1:5", 1, 5 },
            { "1:8", 1, 8 },    { "1:10", 1, 10 },  { "1:16", 1, 16 },   { "1:20", 1, 20 },
            { "1:30", 1, 30 },  { "1:40", 1, 40 },  { "1:50", 1, 50 },   { "1:100", 1, 100 },
            { "2:1", 2, 1 },    { "4:1", 4, 1 },    { "8:1", 8, 1 },     { "10:1", 10, 1 },
            { "100:1", 100, 1 },
        };

        // AcDbVariableDictionary 的默认变量（ACadSharp DictionaryVariableCollection.CreateDefaults）
        constexpr std::pair<std::string_view, std::string_view> kDefaultVariables[] = {
            { "CMLEADERSTYLE", "Standard" },
            { "CANNOSCALE", "1:1" },
            { "CTABLESTYLE", "Standard" },
            { "WIPEOUTFRAME", "1" },
            { "CVIEWDETAILSTYLE", "Metric50" },
            { "CVIEWSECTIONSTYLE", "Metric50" },
        };
    }

    const DxfClassInfo& CadTable::GetClassInfo() const
    {
        static constexpr DxfSubclassInfo subclasses[] = { { "AcDbSymbolTable", {} } };
        static const DxfClassInfo info{ "CadTable", "TABLE", subclasses, nullptr };
        return info;
    }

    // ── 句柄与对象 ─────────────────────────────────────────────────

    Handle CadDatabase::AllocateHandle()
    {
        return m_handleSeed++;
    }

    void CadDatabase::SetHandleSeed(Handle seed)
    {
        // 句柄 0 表示空引用，不能分配出去
        m_handleSeed = seed == kNullHandle ? 1 : seed;
    }

    CadObject* CadDatabase::AddObject(std::unique_ptr<CadObject> object)
    {
        if (!object)
            return nullptr;

        if (object->ObjectHandle == kNullHandle)
            object->ObjectHandle = AllocateHandle();
        else if (object->ObjectHandle >= m_handleSeed)
            m_handleSeed = object->ObjectHandle + 1;

        auto [it, inserted] = m_objects.try_emplace(object->ObjectHandle, std::move(object));
        return inserted ? it->second.get() : nullptr;
    }

    std::unique_ptr<CadObject> CadDatabase::RemoveObject(Handle handle)
    {
        auto it = m_objects.find(handle);
        if (it == m_objects.end())
            return nullptr;
        std::unique_ptr<CadObject> object = std::move(it->second);
        m_objects.erase(it);
        return object;
    }

    CadObject* CadDatabase::Find(Handle handle) const
    {
        auto it = m_objects.find(handle);
        return it == m_objects.end() ? nullptr : it->second.get();
    }

    // ── 符号表与字典 ───────────────────────────────────────────────

    void CadDatabase::SetTable(CadObjectType controlType, Handle table)
    {
        switch (controlType)
        {
        case CadObjectType::BLOCK_CONTROL_OBJ:    m_blockRecords = table; break;
        case CadObjectType::LAYER_CONTROL_OBJ:    m_layers = table; break;
        case CadObjectType::DIMSTYLE_CONTROL_OBJ: m_dimensionStyles = table; break;
        case CadObjectType::STYLE_CONTROL_OBJ:    m_textStyles = table; break;
        case CadObjectType::LTYPE_CONTROL_OBJ:    m_lineTypes = table; break;
        case CadObjectType::VIEW_CONTROL_OBJ:     m_views = table; break;
        case CadObjectType::UCS_CONTROL_OBJ:      m_ucss = table; break;
        case CadObjectType::VPORT_CONTROL_OBJ:    m_vports = table; break;
        case CadObjectType::APPID_CONTROL_OBJ:    m_appIds = table; break;
        default: break;
        }
    }

    TableEntry* CadDatabase::FindTableEntry(const CadTable* table, std::string_view name) const
    {
        if (table == nullptr)
            return nullptr;
        for (Handle h : table->Entries)
        {
            auto* entry = FindAs<TableEntry>(h);
            if (entry != nullptr && EqualsIgnoreCase(entry->Name, name))
                return entry;
        }
        return nullptr;
    }

    BlockRecord* CadDatabase::ModelSpace() const
    {
        return FindTableEntry<BlockRecord>(BlockRecords(), "*Model_Space");
    }

    BlockRecord* CadDatabase::PaperSpace() const
    {
        return FindTableEntry<BlockRecord>(BlockRecords(), "*Paper_Space");
    }

    CadObject* CadDatabase::FindDictionaryEntry(const CadDictionary* dictionary, std::string_view name) const
    {
        if (dictionary == nullptr)
            return nullptr;
        const std::size_t count = std::min(dictionary->EntryNames.size(), dictionary->EntryHandles.size());
        for (std::size_t i = 0; i < count; ++i)
        {
            if (dictionary->EntryNames[i] == name)
                return Find(dictionary->EntryHandles[i]);
        }
        return nullptr;
    }

    CadDictionary* CadDatabase::FindNamedDictionary(std::string_view name) const
    {
        return dynamic_cast<CadDictionary*>(FindDictionaryEntry(RootDictionary(), name));
    }

    // ── 编辑辅助 ───────────────────────────────────────────────────

    void CadDatabase::LinkTableEntry(CadTable* table, TableEntry* entry)
    {
        if (table == nullptr)
            return;
        entry->OwnerHandle = table->ObjectHandle;
        table->Entries.push_back(entry->ObjectHandle);
    }

    void CadDatabase::LinkDictionaryEntry(CadDictionary* dictionary, std::string_view name, NonGraphicalObject* object)
    {
        object->Name = std::string(name);
        if (dictionary == nullptr)
            return;
        object->OwnerHandle = dictionary->ObjectHandle;
        dictionary->EntryNames.emplace_back(name);
        dictionary->EntryHandles.push_back(object->ObjectHandle);
    }

    void CadDatabase::LinkEntity(BlockRecord* block, Entity* entity)
    {
        if (entity->LayerHandle == kNullHandle)
        {
            if (auto* layer = FindTableEntry(Layers(), "0"))
                entity->LayerHandle = layer->ObjectHandle;
        }
        if (entity->LineTypeHandle == kNullHandle)
        {
            if (auto* lineType = FindTableEntry(LineTypes(), "ByLayer"))
                entity->LineTypeHandle = lineType->ObjectHandle;
        }
        if (block == nullptr)
            return;
        entity->OwnerHandle = block->ObjectHandle;
        block->Entities.push_back(entity->ObjectHandle);
    }

    BlockRecord* CadDatabase::CreateBlockRecord(std::string_view name)
    {
        auto record = std::make_unique<BlockRecord>();
        record->Name = std::string(name);
        BlockRecord* added = AddTableEntry(BlockRecords(), std::move(record));
        if (added == nullptr)
            return nullptr;

        // BLOCK / ENDBLK 不在块的实体列表中，由块记录单独引用
        auto begin = std::make_unique<Block>();
        begin->Name = std::string(name);
        Block* beginAdded = Add(std::move(begin));
        LinkEntity(nullptr, beginAdded);
        beginAdded->OwnerHandle = added->ObjectHandle;
        added->BlockEntityHandle = beginAdded->ObjectHandle;

        BlockEnd* endAdded = Add(std::make_unique<BlockEnd>());
        LinkEntity(nullptr, endAdded);
        endAdded->OwnerHandle = added->ObjectHandle;
        added->BlockEndHandle = endAdded->ObjectHandle;
        return added;
    }

    CadTable* CadDatabase::CreateTable(Handle& slot, CadObjectType controlType, std::string_view entryDxfName)
    {
        if (CadTable* existing = FindAs<CadTable>(slot))
            return existing;
        CadTable* table = Add(std::make_unique<CadTable>(controlType, entryDxfName));
        slot = table->ObjectHandle;
        return table;
    }

    Layout* CadDatabase::CreateLayout(BlockRecord* block, std::string_view name, int tabOrder)
    {
        auto layout = std::make_unique<Layout>();
        layout->TabOrder = tabOrder;
        layout->AssociatedBlockHandle = block->ObjectHandle;
        Layout* added = AddDictionaryEntry(FindNamedDictionary("ACAD_LAYOUT"), name, std::move(layout));
        block->LayoutHandle = added->ObjectHandle;
        return added;
    }

    // ── 默认内容 ───────────────────────────────────────────────────

    void CadDatabase::CreateDefaults()
    {
        // 顺序与 ACadSharp 一致：先符号表，再根字典，再默认表项，最后模型空间与图纸空间
        CreateTable(m_blockRecords, CadObjectType::BLOCK_CONTROL_OBJ, "BLOCK_RECORD");
        CreateTable(m_layers, CadObjectType::LAYER_CONTROL_OBJ, "LAYER");
        CreateTable(m_dimensionStyles, CadObjectType::DIMSTYLE_CONTROL_OBJ, "DIMSTYLE");
        CreateTable(m_textStyles, CadObjectType::STYLE_CONTROL_OBJ, "STYLE");
        CreateTable(m_lineTypes, CadObjectType::LTYPE_CONTROL_OBJ, "LTYPE");
        CreateTable(m_views, CadObjectType::VIEW_CONTROL_OBJ, "VIEW");
        CreateTable(m_ucss, CadObjectType::UCS_CONTROL_OBJ, "UCS");
        CreateTable(m_vports, CadObjectType::VPORT_CONTROL_OBJ, "VPORT");
        CreateTable(m_appIds, CadObjectType::APPID_CONTROL_OBJ, "APPID");

        // 根字典与默认命名字典
        if (RootDictionary() == nullptr)
        {
            auto root = std::make_unique<CadDictionary>();
            root->Name = "ROOT";
            m_rootDictionary = Add(std::move(root))->ObjectHandle;
        }
        for (std::string_view name : kRootEntries)
        {
            if (FindNamedDictionary(name) == nullptr)
                AddDictionaryEntry(RootDictionary(), name, std::make_unique<CadDictionary>());
        }

        if (CadDictionary* scales = FindNamedDictionary("ACAD_SCALELIST"); scales && scales->EntryNames.empty())
        {
            for (const DefaultScale& s : kDefaultScales)
            {
                auto scale = std::make_unique<Scale>();
                scale->PaperUnits = s.paper;
                scale->DrawingUnits = s.drawing;
                scale->IsUnitScale = s.name == "1:1";
                AddDictionaryEntry(scales, s.name, std::move(scale));
            }
        }

        if (CadDictionary* variables = FindNamedDictionary("AcDbVariableDictionary"); variables && variables->EntryNames.empty())
        {
            for (const auto& [name, value] : kDefaultVariables)
            {
                auto variable = std::make_unique<DictionaryVariable>();
                variable->Value = std::string(value);
                AddDictionaryEntry(variables, name, std::move(variable));
            }
        }

        // 默认表项
        if (!FindTableEntry(AppIds(), "ACAD"))
        {
            auto appId = std::make_unique<AppId>();
            appId->Name = "ACAD";
            AddTableEntry(AppIds(), std::move(appId));
        }
        for (std::string_view name : { "ByLayer", "ByBlock", "Continuous" })
        {
            if (FindTableEntry(LineTypes(), name))
                continue;
            auto lineType = std::make_unique<LineType>();
            lineType->Name = std::string(name);
            AddTableEntry(LineTypes(), std::move(lineType));
        }
        if (!FindTableEntry(Layers(), "0"))
        {
            auto layer = std::make_unique<Layer>();
            layer->Name = "0";
            layer->LineTypeHandle = FindTableEntry(LineTypes(), "Continuous")->ObjectHandle;
            AddTableEntry(Layers(), std::move(layer));
        }
        if (!FindTableEntry(TextStyles(), "Standard"))
        {
            auto style = std::make_unique<TextStyle>();
            style->Name = "Standard";
            AddTableEntry(TextStyles(), std::move(style));
        }
        if (!FindTableEntry(DimensionStyles(), "Standard"))
        {
            auto style = std::make_unique<DimensionStyle>();
            style->Name = "Standard";
            style->StyleHandle = FindTableEntry(TextStyles(), "Standard")->ObjectHandle;
            AddTableEntry(DimensionStyles(), std::move(style));
        }
        if (!FindTableEntry(VPorts(), "*Active"))
        {
            auto vport = std::make_unique<VPort>();
            vport->Name = "*Active";
            AddTableEntry(VPorts(), std::move(vport));
        }

        // 默认多线样式（需要 ByLayer 线型，放在线型之后）
        if (CadDictionary* mlineStyles = FindNamedDictionary("ACAD_MLINESTYLE"); mlineStyles && mlineStyles->EntryNames.empty())
        {
            const Handle byLayer = FindTableEntry(LineTypes(), "ByLayer")->ObjectHandle;
            auto style = std::make_unique<MLineStyle>();
            for (double offset : { 0.5, -0.5 })
            {
                MLineStyleElement element;
                element.LineTypeHandle = byLayer;
                element.Offset = offset;
                style->Elements.push_back(element);
            }
            AddDictionaryEntry(mlineStyles, "Standard", std::move(style));
        }

        // 默认多重引线样式（取值与 AutoCAD 新建图纸中的 Standard 一致）
        if (CadDictionary* mleaderStyles = FindNamedDictionary("ACAD_MLEADERSTYLE"); mleaderStyles && mleaderStyles->EntryNames.empty())
        {
            auto style = std::make_unique<MultiLeaderStyle>();
            style->ContentType = LeaderContentType::MText;
            style->MultiLeaderDrawOrder = static_cast<MultiLeaderDrawOrderType>(1);
            style->LeaderDrawOrder = static_cast<LeaderDrawOrderType>(0);
            style->MaxLeaderSegmentsPoints = 2;
            style->PathType = static_cast<MultiLeaderPathType>(1);      // 直线
            style->LineColor = Color::ByBlock();
            style->LeaderLineTypeHandle = FindTableEntry(LineTypes(), "ByBlock")->ObjectHandle;
            style->LeaderLineWeight = LineWeightType::ByBlock;
            style->EnableLanding = true;
            style->LandingGap = 0.09;
            style->EnableDogleg = true;
            style->LandingDistance = 0.36;
            style->Description = "Standard";
            style->ArrowheadSize = 0.18;
            style->TextStyleHandle = FindTableEntry(TextStyles(), "Standard")->ObjectHandle;
            style->TextLeftAttachment = static_cast<TextAttachmentType>(1);
            style->TextRightAttachment = static_cast<TextAttachmentType>(1);
            style->TextAngle = static_cast<TextAngleType>(1);
            style->TextColor = Color::ByBlock();
            style->TextHeight = 0.18;
            style->AlignSpace = 0.18;
            style->BlockContentColor = Color::ByBlock();
            style->BlockContentScaleX = style->BlockContentScaleY = style->BlockContentScaleZ = 1.0;
            style->EnableBlockContentScale = true;
            style->EnableBlockContentRotation = true;
            style->ScaleFactor = 1.0;
            style->BreakGapSize = 0.125;
            style->TextBottomAttachment = static_cast<TextAttachmentType>(9);
            style->TextTopAttachment = static_cast<TextAttachmentType>(9);
            AddDictionaryEntry(mleaderStyles, "Standard", std::move(style));
        }

        // 模型空间与图纸空间
        BlockRecord* model = ModelSpace();
        if (model == nullptr)
            model = CreateBlockRecord("*Model_Space");
        if (FindAs<Layout>(model->LayoutHandle) == nullptr)
            CreateLayout(model, "Model", 0);    // 读入的 R12 文件没有布局

        BlockRecord* paper = PaperSpace();
        if (paper == nullptr)
            paper = CreateBlockRecord("*Paper_Space");
        if (FindAs<Layout>(paper->LayoutHandle) == nullptr)
        {
            Layout* layout = CreateLayout(paper, "Layout1", 1);
            const bool hasViewport = std::any_of(paper->Entities.begin(), paper->Entities.end(),
                                                 [&](Handle h) { return FindAs<Viewport>(h) != nullptr; });
            if (!hasViewport)
            {
                // 图纸空间的整页视口（ACadSharp Layout.UpdatePaperViewport）
                auto viewport = std::make_unique<Viewport>();
                viewport->Width = layout->PaperWidth;
                viewport->Height = layout->PaperHeight;
                viewport->Center = XYZ{ layout->PaperWidth / 2, layout->PaperHeight / 2, 0 };
                AddEntity(paper, std::move(viewport));
            }
        }

        Header.HandleSeed = m_handleSeed;
    }
}
