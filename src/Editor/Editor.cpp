#include "Editor.h"
#include "Editor/EditorContext.h"
#include "Document/Document.h"
#include "Document/DrawContext.hpp"
#include "Document/CommandStack/CommandStack.h"
#include "Document/Command/BatchDeleteCommand.h"
#include "Document/TextStyleResolver.h"
#include "Document/Command/AddEntityCommand.h"
#include "Viewport/Viewport.h"
#include "Editor/Overlay/Overlay.h"
#include "Editor/Picking/Picking.h"
#include "Editor/Snap/SnapResult.h"
#include "Editor/Snap/SnapEngine.h"
#include "Editor/Input/KeyCode.h"
#include "Scene/Scene.h"
#include "Text/FontSystem.h"
#include "Core/Math/Point3.hpp"
#include "Core/Math/Constants.hpp"
#include "Core/Entity/Entity.hpp"
#include "Core/Entity/TextEntity.hpp"
#include "Core/Entity/MTextEntity.hpp"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include "Core/Log.h"

#ifdef MINICAD_WEB
#include <emscripten.h>
#endif

// ── 绘制工具 ──────────────────────────────────────────────────
#include "Editor/Tools/LineTool.h"
#include "Editor/Tools/ZoomWindowTool.h"
#include "Document/Command/ZoomViewCommand.h"
#include "Editor/Tools/PointTool.h"
#include "Editor/Tools/SolidTool.h"
#include "Editor/Tools/WipeoutTool.h"
#include "Editor/Tools/MLineTool.h"
#include "Editor/Tools/TableTool.h"
#include "Core/Entity/TableEntity.hpp"
#include "Core/Entity/RegionEntity.hpp"
#include "Document/RegionBuilder.hpp"
#include "Document/RegionBoolean.hpp"
#include "Document/Command/GeometryEditCommand.h"
#include "Document/Command/EntityExplode.h"
#include "Document/Command/BatchAddCommand.h"
#include <map>
#include <cstdio>
#include "Document/WipeClip.hpp"
#include "Editor/Tools/ImageTool.h"
#include <filesystem>
#include "Editor/Tools/CircleTool.h"
#include "Editor/Tools/RectangleTool.h"
#include "Editor/Tools/ArcTool.h"
#include "Editor/Tools/EllipseTool.h"
#include "Editor/Tools/PolylineTool.h"
#include "Editor/Tools/HatchTool.h"
#include "Editor/Tools/SplineTool.h"
#include "Editor/Tools/TextTool.h"
#include "Editor/Tools/MTextTool.h"
#include "Editor/Tools/XLineTool.h"
#include "Editor/Tools/DimensionTool.h"
#include "Editor/Tools/DimensionTools.h"
#include "Editor/Tools/LeaderTool.h"
#include "Editor/Tools/MLeaderTool.h"
#include "Editor/Tools/PickPointTool.h"
#include "Editor/Tools/InsertBlockTool.h"
#include "Document/Command/DefineBlockCommand.h"
#include "Document/Command/EditTextCommand.h"
#include "Document/Command/ArrayCommand.h"
#include "Editor/Overlay/EntityOverlay.h"

// ── 编辑工具 ──────────────────────────────────────────────────
#include "Editor/Tools/Modify/MoveTool.h"
#include "Editor/Tools/Modify/CopyTool.h"
#include "Editor/Tools/Modify/PasteTool.h"
#include "Editor/Tools/Modify/MirrorTool.h"
#include "Editor/Tools/Modify/RotateTool.h"
#include "Editor/Tools/Modify/ScaleTool.h"
#include "Editor/Tools/Modify/StretchTool.h"
#include "Editor/Tools/Modify/FilletTool.h"
#include "Editor/Tools/Modify/ChamferTool.h"
#include "Editor/Tools/Modify/TrimTool.h"
#include "Editor/Tools/Modify/ExtendTool.h"
#include "Editor/Tools/Modify/BreakTool.h"
#include "Editor/Tools/Modify/OffsetTool.h"

#include <cstdio>
#include <memory>
#include <algorithm>

namespace MiniCAD
{
    namespace
    {
        // 悬停高亮收集器：把实体 Draw() 输出的几何以固定高亮色直接写入 overlay 顶点缓冲。
        // 用于每帧重建“当前悬停实体”的高亮，无需触碰整场景顶点。
        class HoverHighlightSink : public IDrawSink
        {
        public:
            HoverHighlightSink(std::vector<Vertex_P3_C4>& out, const Math::Color4& color)
                : m_out(out), m_color(color) {}

            void DrawLine(const Math::Point3& a, const Math::Point3& b,
                          const Math::Color4&, bool) override
            {
                m_out.push_back(V(a));
                m_out.push_back(V(b));
            }

            void FillTriangle(const Math::Point3& a, const Math::Point3& b,
                              const Math::Point3& c, const Math::Color4&) override
            {
                // 三角形以三条边描边（overlay 走 LINE 图元）
                m_out.push_back(V(a)); m_out.push_back(V(b));
                m_out.push_back(V(b)); m_out.push_back(V(c));
                m_out.push_back(V(c)); m_out.push_back(V(a));
            }
            // EmitText / EmitMText: 文字字形不经线段输出，悬停反馈由包围盒勾边补充。

        private:
            Vertex_P3_C4 V(const Math::Point3& p) const
            {
                return {
                    { static_cast<float>(p.x), static_cast<float>(p.y), static_cast<float>(p.z) },
                    { static_cast<float>(m_color.r), static_cast<float>(m_color.g),
                      static_cast<float>(m_color.b), static_cast<float>(m_color.a) }
                };
            }

            std::vector<Vertex_P3_C4>& m_out;
            Math::Color4               m_color;
        };
    }

    // ─────────────────────────────────────────────────────────────
    //  构造
    // ─────────────────────────────────────────────────────────────
    Editor::Editor()
    {
        RegisterBuiltinTools();
    }

    // ─────────────────────────────────────────────────────────────
    //  Bind / Unbind
    // ─────────────────────────────────────────────────────────────
    void Editor::Bind(Document& doc, Viewport& viewport)
    {
        Unbind();

        m_doc      = &doc;
        m_viewport = &viewport;

        auto& scene    = doc.GetScene();
        auto& cmdStack = doc.GetCommandStack();
         
        m_overlay.Bind(viewport);
        m_picking.Bind(scene, viewport);
        m_gripEditor.Bind(viewport, scene, cmdStack, m_picking, m_overlay);

        scene.MarkDirty();
		m_picking.MarkDirty();
        m_picking.ClearSelection();
        m_gripEditor.MarkDirty();
        m_overlay.Clear();

    }

    void Editor::Unbind()
    {
        if (m_tool)
        {
            m_tool->Cancel();
            m_tool.reset();
        }
        m_toolSuspended    = false;
        m_pendingToolReset = false;
        m_overlay.Clear();
        m_currentSnap = {};

        m_doc      = nullptr;
        m_viewport = nullptr;
    }

    // ─────────────────────────────────────────────────────────────
    //  RegisterBuiltinTools
    // ─────────────────────────────────────────────────────────────
    void Editor::RegisterBuiltinTools()
    {
        // ── 绘制工具 ──────────────────────────────────────────
        RegisterTool("Line",      [] { return std::make_unique<LineTool>();      });
        RegisterTool("ZoomWindow", [] { return std::make_unique<ZoomWindowTool>(); });   // ZOOM E：窗口缩放
        RegisterTool("Point",     [] { return std::make_unique<PointTool>();     });
        RegisterTool("Circle",    [] { return std::make_unique<CircleTool>();    });
        RegisterTool("Rectangle", [] { return std::make_unique<RectangleTool>(); });
        RegisterTool("Arc",       [] { return std::make_unique<ArcTool>();       });
        RegisterTool("Ellipse",   [] { return std::make_unique<EllipseTool>();   });
        RegisterTool("EllipseArc",[] { return std::make_unique<EllipseTool>(/*arcMode=*/true); });
        RegisterTool("Polyline",  [] { return std::make_unique<PolylineTool>();  });
        RegisterTool("Solid",     [] { return std::make_unique<SolidTool>();     });
        // Image 工具需要先选文件，不进注册表：见 ActivateToolById / SubmitImagePath
        RegisterTool("MLine",     [this] { return std::make_unique<MLineTool>(m_mlineSettings); });
        RegisterTool("Table",     [this] { return std::make_unique<TableTool>(CurrentTextHeight()); });
        RegisterTool("Wipeout",   [] { return std::make_unique<WipeoutTool>();   });
        RegisterTool("Hatch",     [this] { return std::make_unique<HatchTool>(m_hatchSettings); });
        RegisterTool("Spline",    [] { return std::make_unique<SplineTool>();    });
        RegisterTool("XLine",     [] { return std::make_unique<XLineTool>();     });
        RegisterTool("Ray",       [] { return std::make_unique<RayTool>();       });
        RegisterTool("Dimension", [] { return std::make_unique<DimensionTool>(); });
        RegisterTool("DimAngular",  [] { return std::make_unique<DimAngularTool>(); });
        RegisterTool("DimRadius",   [] { return std::make_unique<DimRadialTool>(DimRadialTool::Kind::Radius); });
        RegisterTool("DimDiameter", [] { return std::make_unique<DimRadialTool>(DimRadialTool::Kind::Diameter); });
        RegisterTool("DimJogged",   [] { return std::make_unique<DimRadialTool>(DimRadialTool::Kind::Jogged); });
        RegisterTool("DimArc",      [] { return std::make_unique<DimArcLengthTool>(); });
        RegisterTool("DimOrdinate", [] { return std::make_unique<DimOrdinateTool>(); });
        RegisterTool("Leader",    [] { return std::make_unique<LeaderTool>();    });
        RegisterTool("MLeader",   [] { return std::make_unique<MLeaderTool>();   });
        RegisterTool("Text",      [this]
        {
            auto tool = std::make_unique<TextTool>();
            tool->OnInsertPointPicked = [this](Math::Point3 pos)
            {
                m_textRequest.Active    = true;
                m_textRequest.InsertPos = pos;
                m_textRequest.Height    = static_cast<float>(CurrentTextHeight());
#ifdef MINICAD_WEB
                EM_ASM({ if (typeof window._minicadShowTextInput === 'function') window._minicadShowTextInput(); });
#endif
            };
            return tool;
        });

        RegisterTool("MText", [this]
        {
            auto tool = std::make_unique<MTextTool>();
            tool->OnInsertPointPicked = [this](Math::Point3 pos)
            {
                m_mtextRequest.Active    = true;
                m_mtextRequest.InsertPos = pos;
                m_mtextRequest.Height    = CurrentTextHeight();
#ifdef MINICAD_WEB
                EM_ASM({ if (typeof window._minicadShowMTextInput === 'function') window._minicadShowMTextInput(); });
#endif
            };
            return tool;
        });

        // ── 编辑工具 ──────────────────────────────────────────
        RegisterTool("Move", [this]() -> std::unique_ptr<ITool> {
            auto targets = GetEditableSelectedObjects();
            if (targets.empty())
            {
                LOG_WARN("[Editor] Move: no selection");
                return nullptr;
            }
            return std::make_unique<MoveTool>(std::move(targets));
        });

        RegisterTool("Copy", [this]() -> std::unique_ptr<ITool> {
            auto targets = GetSelectedObjects();
            if (targets.empty())
            {
                LOG_WARN("[Editor] Copy: no selection");
                return nullptr;
            }
            return std::make_unique<CopyTool>(std::move(targets));
        });

        RegisterTool("Mirror", [this]() -> std::unique_ptr<ITool> {
            auto targets = GetEditableSelectedObjects();
            if (targets.empty())
            {
                LOG_WARN("[Editor] Mirror: no selection");
                return nullptr;
            }
            return std::make_unique<MirrorTool>(std::move(targets));
        });

        RegisterTool("Rotate", [this]() -> std::unique_ptr<ITool> {
            auto targets = GetEditableSelectedObjects();
            if (targets.empty())
            {
                LOG_WARN("[Editor] Rotate: no selection");
                return nullptr;
            }
            return std::make_unique<RotateTool>(std::move(targets));
        });

        RegisterTool("Scale", [this]() -> std::unique_ptr<ITool> {
            auto targets = GetEditableSelectedObjects();
            if (targets.empty())
            {
                LOG_WARN("[Editor] Scale: no selection");
                return nullptr;
            }
            return std::make_unique<ScaleTool>(std::move(targets));
        });

        // 定义块(AutoCAD BLOCK):预选实体 → 拾取基点 → 输入块名 → 收进新块并原位替换为块引用
        RegisterTool("Block", [this]() -> std::unique_ptr<ITool> {
            std::vector<Object::ObjectID> ids = EditableSelectionIds();     // 块定义会把原对象换成块引用
            if (ids.empty())
            {
                LOG_WARN("[Editor] Block: no (editable) selection");
                return nullptr;
            }
            return std::make_unique<PickPointTool>("指定基点:",
                [this, ids = std::move(ids)](const Math::Point3& p)
                {
#ifdef MINICAD_WEB
                    DefineBlock(ids, p, {});                  // Web 暂无块名对话框,自动命名
#else
                    m_blockNameRequest.Active      = true;    // 弹块名对话框(BlockPopups.cpp)
                    m_blockNameRequest.DefaultName = GenerateBlockName();
                    m_blockNameRequest.Ids         = ids;
                    m_blockNameRequest.BasePoint   = p;
#endif
                });
        });

        // ── 几何编辑工具（无需预选）──────────────────────────
        RegisterTool("Trim",   [] { return std::make_unique<TrimTool>();   });
        RegisterTool("Extend", [] { return std::make_unique<ExtendTool>(); });
        RegisterTool("Offset", [] { return std::make_unique<OffsetTool>(); });
        RegisterTool("Fillet", [this] { return std::make_unique<FilletTool>(&m_filletRadius); });      // 自带两次点选，无需预选
        RegisterTool("Chamfer", [this] { return std::make_unique<ChamferTool>(&m_chamferD1, &m_chamferD2); });
        RegisterTool("Stretch", [] { return std::make_unique<StretchTool>(); });     // 自带交叉窗口选择，无需预选
        RegisterTool("Break",  [] { return std::make_unique<BreakTool>();  });

        // ── 快捷键绑定 ────────────────────────────────────────
        RegisterAlias("P",      "Previous");
        RegisterAlias("L",      "Line");
        RegisterAlias("LI",     "Line");
        RegisterAlias("REC",    "Rectangle");
        RegisterAlias("PL",     "Polyline");
        RegisterAlias("MI",     "Mirror");
        RegisterAlias("RO",     "Rotate");
        RegisterAlias("SC",     "Scale");
        RegisterAlias("S",      "Stretch");
        RegisterAlias("F",      "Fillet");
        RegisterAlias("CHA",    "Chamfer");
        RegisterAlias("PT",     "Point");
        RegisterAlias("SO",     "Solid");
        RegisterAlias("WI",     "Wipeout");
        RegisterAlias("ML",     "MLine");
        RegisterAlias("TB",     "Table");
        RegisterAlias("REG",    "Region");
        RegisterAlias("REGION", "Region");   // Region / Area / Union / Subtract / Intersect 是特例命令(非工具),全名也走别名表
        RegisterAlias("AREA",   "Area");
        RegisterAlias("UNION",  "Union");
        RegisterAlias("SUBTRACT",  "Subtract");
        RegisterAlias("INTERSECT", "Intersect");
        RegisterAlias("AA",     "Area");
        RegisterAlias("UNI",    "Union");
        RegisterAlias("SU",     "Subtract");
        RegisterAlias("IN",     "Intersect");
        RegisterAlias("Z",      "Zoom");
        RegisterAlias("ZOOMALL", "ZoomAll");
        RegisterAlias("IM",     "Image");   // Image 是特例命令(非工具),全名也走别名表
        RegisterAlias("IMAGE",  "Image");
        RegisterAlias("C",      "Circle");
        RegisterAlias("ARC",    "Arc");
        RegisterAlias("EL",     "Ellipse");
        RegisterAlias("SP",     "Spline");
        RegisterAlias("RAY",    "Ray");
        RegisterAlias("ELA",    "EllipseArc");
        RegisterAlias("E",      "Erase");
        RegisterAlias("ERASE",  "Erase");
        RegisterAlias("X",      "Explode");
        RegisterAlias("EXPLODE", "Explode");         // 特例命令(非工具)，全名走别名表
        RegisterAlias("DELETE", "Erase");
        RegisterAlias("SELECTALL", "SelectAll");     // 特例命令(非工具)，全名走别名表
        RegisterAlias("XL",     "XLine");
        RegisterAlias("M",      "Move");
        RegisterAlias("CO",     "Copy");
        RegisterAlias("T",      "Text");
        RegisterAlias("DT",     "Text");
        RegisterAlias("MT",     "MText");
        RegisterAlias("TR",     "Trim");
        RegisterAlias("EX",     "Extend");
        RegisterAlias("O",      "Offset");
        RegisterAlias("BR",     "Break");
        RegisterAlias("H",      "Hatch");
        RegisterAlias("ST",     "Style");
        RegisterAlias("STYLE",  "Style");    // Style 是特例命令(非工具),全名也走别名表
        RegisterAlias("B",      "Block");
        RegisterAlias("I",      "Insert");
        RegisterAlias("INSERT", "Insert");   // Insert 是特例命令(非工具),不在工具注册表,全名也走别名表
        RegisterAlias("AR",     "Array");
        RegisterAlias("ARRAY",  "Array");    // Array 同为特例命令,全名走别名表
        RegisterAlias("DIM",    "Dimension");
        RegisterAlias("DLI",    "Dimension");      // 同 AutoCAD 的标注别名
        RegisterAlias("DAL",    "Dimension");
        RegisterAlias("DAN",    "DimAngular");
        RegisterAlias("DRA",    "DimRadius");
        RegisterAlias("DDI",    "DimDiameter");
        RegisterAlias("DJO",    "DimJogged");
        RegisterAlias("DAR",    "DimArc");
        RegisterAlias("DOR",    "DimOrdinate");
        RegisterAlias("Le",     "Leader");
        RegisterAlias("MLe",    "MLeader");
    }

    // ─────────────────────────────────────────────────────────────
    //  工具注册表 — 对外接口
    // ─────────────────────────────────────────────────────────────
    void Editor::RegisterTool(const std::string& toolId, std::function<std::unique_ptr<ITool>()> factory)
    {
        m_toolRegistry[toolId] = std::move(factory);
    }

    void Editor::RegisterAlias(const std::string& alias, const std::string& toolId)
    {
        // 键统一为大写:查找侧(ActivateToolByAlias/ResolveCommandAlias)按大写查表,
        // 否则 "Le"/"MLe" 这类混合大小写注册的别名永远查不到
        std::string key = alias;
        for (auto& c : key) c = (char)std::toupper((unsigned char)c);
        m_aliasRegistry[key] = toolId;
    }

    void Editor::ActivateToolByAlias(const std::string& alias)
    {
        auto iequals = [](const std::string& a, const std::string& b)
        {
            if (a.size() != b.size()) return false;
            for (size_t i = 0; i < a.size(); ++i)
                if (std::toupper((unsigned char)a[i]) != std::toupper((unsigned char)b[i]))
                    return false;
            return true;
        };

        if (iequals(alias, "Previous"))
        {
            m_picking.RestoreLastSelection();
            m_gripEditor.MarkDirty();
            return;
        }

        // 别名表（键统一为大写）：L → Line
        std::string upper = alias;
        for (auto& c : upper) c = (char)std::toupper((unsigned char)c);
        if (auto it = m_aliasRegistry.find(upper); it != m_aliasRegistry.end())
        {
            ActivateToolById(it->second);
            return;
        }

        // 工具全名（大小写不敏感）：line / LINE / Line → Line
        for (const auto& [id, factory] : m_toolRegistry)
        {
            if (iequals(id, alias))
            {
                ActivateToolById(id);
                return;
            }
        }

        m_commandLine.Echo("未知命令: " + alias);
        LOG_WARN("[Editor] Unknown command: %s", alias.c_str());
    }

    std::string Editor::ResolveCommandAlias(const std::string& word) const
    {
        if (word.empty())
            return {};

        // 别名精确匹配（键统一为大写）：L → Line
        std::string upper = word;
        for (auto& c : upper) c = (char)std::toupper((unsigned char)c);
        if (auto it = m_aliasRegistry.find(upper); it != m_aliasRegistry.end())
            return it->second;

        // 工具全名精确匹配（大小写不敏感）
        for (const auto& [id, factory] : m_toolRegistry)
        {
            if (id.size() != word.size())
                continue;
            bool eq = true;
            for (size_t i = 0; i < id.size(); ++i)
                if (std::toupper((unsigned char)id[i]) != std::toupper((unsigned char)word[i])) { eq = false; break; }
            if (eq)
                return id;
        }
        return {};
    }

    std::vector<std::string> Editor::GetCommandNames() const
    {
        // 仅返回工具全名用于补全：别名是缩写、本就用于快速输入，无需补全；
        // 混入会污染候选、使公共前缀补全几乎失效。补全到全名后直接回车即可。
        std::vector<std::string> names;
        names.reserve(m_toolRegistry.size() + 1);
        for (const auto& [id, factory] : m_toolRegistry)
            names.push_back(id);
        names.push_back("Insert");   // 特例命令(非工具):插入块
        names.push_back("Region");   // 特例命令(非工具):生成面域
        names.push_back("Union");
        names.push_back("Subtract");
        names.push_back("Intersect");
        names.push_back("Area");     // 特例命令(非工具):面积查询
        names.push_back("Image");    // 特例命令(非工具):插入光栅图像
        names.push_back("Array");    // 特例命令(非工具):阵列
        names.push_back("Style");    // 特例命令(非工具):文字样式
        names.push_back("Erase");    // 特例命令(非工具):删除选择集
        names.push_back("SelectAll");   // 特例命令(非工具):全选
        names.push_back("Zoom");        // 特例命令(非工具):视图缩放（A 全部 / E 窗口）
        names.push_back("Explode");     // 特例命令(非工具):分解
        std::sort(names.begin(), names.end());
        return names;
    }

    void Editor::ZoomAll(bool undoable)
    {
        if (!m_doc || !m_viewport)
            return;
        AABB box = AABB::Empty();
        if (!m_doc->GetScene().GetExtents(box))
        {
            m_commandLine.Echo("图形中没有可显示的对象");
            return;
        }
        Camera& cam = m_viewport->GetCamera();
        const CameraState before = cam.GetState();
        cam.ZoomToBounds(box.Min.x, box.Min.y, box.Max.x, box.Max.y);
        const CameraState after = cam.GetState();
        if (undoable)
        {
            cam.SetState(before);   // 由命令来应用，撤销时回到 before
            m_doc->GetCommandStack().Execute(std::make_unique<ZoomViewCommand>(cam, before, after), m_doc->GetScene());
            m_lastCommand = "ZoomAll";
        }
    }

    // ZOOM 的选项：A / ALL = 全部（缩放到全图）；E / EXTENTS（及 W / WINDOW）= 在屏幕上框选区域缩放（opt 已转大写）
    void Editor::RunZoomOption(const std::string& opt)
    {
        if (opt == "A" || opt == "ALL")
            ZoomAll();
        else if (opt == "E" || opt == "EXTENTS" || opt == "W" || opt == "WINDOW")
            ActivateToolById("ZoomWindow");
        else
            m_commandLine.Echo("*取消*（ZOOM 目前支持 A / E）");
    }

    void Editor::RunCommand(const std::string& text)
    {
        // 去除首尾空白
        size_t b = text.find_first_not_of(" \t");
        if (b == std::string::npos)
        {
            if (m_zoomPending)   // ZOOM 等待选项时空回车 = 取消
            {
                m_zoomPending = false;
                m_commandLine.Echo("*取消*");
                return;
            }
            // 空回车 = 重复上一条命令（AutoCAD 行为）
            if (!m_lastCommand.empty())
                ActivateToolByAlias(m_lastCommand);
            return;
        }
        size_t e = text.find_last_not_of(" \t");
        const std::string cmd = text.substr(b, e - b + 1);

        // ZOOM 正在等待选项：这一行就是选项（E / A；其他或空行取消）
        if (m_zoomPending)
        {
            m_zoomPending = false;
            std::string opt = cmd;
            std::transform(opt.begin(), opt.end(), opt.begin(), [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
            m_commandLine.Echo(cmd);
            RunZoomOption(opt);
            return;
        }

        // ZOOM <选项>：目前支持 E（范围）/ A（全部，同范围）；只输入 ZOOM 时等待下一行输入选项
        const size_t sp = cmd.find_first_of(" \t");
        std::string head = cmd.substr(0, sp);
        std::transform(head.begin(), head.end(), head.begin(), [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
        if (head == "ZOOM" || head == "Z")
        {
            std::string opt;
            if (sp != std::string::npos)
            {
                const size_t o = cmd.find_first_not_of(" \t", sp);
                if (o != std::string::npos)
                    opt = cmd.substr(o);
            }
            std::transform(opt.begin(), opt.end(), opt.begin(), [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
            m_commandLine.Echo("命令: " + cmd);
            if (opt.empty())
                m_zoomPending = true;
            else
                RunZoomOption(opt);
            return;
        }

        ActivateToolByAlias(cmd);
    }

    char Editor::ToCommandChar(KeyCode key)
    {
        if (key >= KeyCode::A && key <= KeyCode::Z)
            return static_cast<char>('A' + (static_cast<int>(key) - static_cast<int>(KeyCode::A)));

        if (key >= KeyCode::Num0 && key <= KeyCode::Num9)
            return static_cast<char>('0' + (static_cast<int>(key) - static_cast<int>(KeyCode::Num0)));

        return '\0';
    }

    void Editor::ActivateToolById(const std::string& toolId)
    {
        if (toolId == "Previous")
        {
            m_picking.RestoreLastSelection();
            m_gripEditor.MarkDirty();
            return;
        }

        if (toolId == "Insert")   // 特例命令:先弹块表选择对话框,选定后才有放置工具
        {
            RequestBlockInsert();
            return;
        }

        if (toolId == "Image")    // 特例命令:先弹文件选择框,选定后才有放置工具
        {
            if (!m_doc)
                return;
            m_imageRequest.Active = true;
            m_commandLine.Echo("命令: Image");
            m_lastCommand = "Image";
            return;
        }

        if (toolId == "ZoomAll")       // 特例命令:ZOOM A（空回车重复上一条命令时用）
        {
            m_commandLine.Echo("命令: ZOOM A");
            ZoomAll();
            return;
        }

        if (toolId == "Zoom")          // 只输入 ZOOM（别名 Z、补全到全名后空格 / 回车）：等待下一行输入选项
        {
            m_commandLine.Echo("命令: ZOOM");
            m_zoomPending = true;
            return;
        }

        if (toolId == "Erase")       // 特例命令:删除选择集（同 Delete 键）
        {
            DeleteSelected();
            return;
        }

        if (toolId == "Explode")     // 特例命令:分解选择集
        {
            ExplodeSelection();
            return;
        }

        if (toolId == "SelectAll")   // 特例命令:全选
        {
            SelectAll();
            return;
        }

        if (toolId == "Region")   // 特例命令:对选择集生成面域
        {
            CreateRegionFromSelection();
            return;
        }

        if (toolId == "Union")     { RegionBoolean(BooleanOp::Union);     return; }
        if (toolId == "Subtract")  { RegionBoolean(BooleanOp::Subtract);  return; }
        if (toolId == "Intersect") { RegionBoolean(BooleanOp::Intersect); return; }

        if (toolId == "Area")     // 特例命令:报告选择集的面积
        {
            ReportArea();
            return;
        }

        if (toolId == "Array")    // 特例命令:预选实体后弹阵列参数对话框
        {
            RequestArray();
            return;
        }

        if (toolId == "Style")    // 特例命令:文字样式对话框
        {
            if (!m_doc)
                return;
            m_textStyleRequest.Active = true;
            m_commandLine.Echo("命令: Style");
            m_lastCommand = "Style";
            return;
        }

        if (toolId == "Hatch" && m_hatchDialogEnabled && m_doc)   // 对话框模式:先选图案
        {
            m_hatchRequest.Active = true;
            m_commandLine.Echo("命令: Hatch");
            m_lastCommand = "Hatch";
            return;
        }

        ActivateRegisteredTool(toolId);
    }

    void Editor::ActivateRegisteredTool(const std::string& toolId)
    {
        auto it = m_toolRegistry.find(toolId);
        if (it == m_toolRegistry.end())
        {
            m_commandLine.Echo("未知命令: " + toolId);
            LOG_WARN("[Editor] Unknown tool: %s", toolId.c_str());
            return;
        }

        auto tool = it->second();

        if (!tool)
        {
            m_commandLine.Echo(toolId + ": 需要先选择对象");
            LOG_WARN("[Editor] Tool '%s' has no targets, skipped.", toolId.c_str());
            return;
        }

        m_commandLine.Echo("命令: " + toolId);
        LOG_INFO("[Editor] Start %s", toolId.c_str());
        m_lastCommand = toolId;
        ActivateTool(std::move(tool));
    }

    // ─────────────────────────────────────────────────────────────
    //  ActivateTool
    // ─────────────────────────────────────────────────────────────
    void Editor::ActivateTool(std::unique_ptr<ITool> tool)
    {
        if (m_tool)
        {
            m_tool->Cancel();
            m_tool.reset();
        }

        m_toolSuspended = false;
        m_overlay.Clear();
        m_picking.ClearSelection();
        // 选中高亮已是独立顶点流（picking 脏标记驱动），清除选择无需场景全局标脏
        m_picking.MarkDirty();
        m_gripEditor.RebuildGrips();

        m_tool = std::move(tool);
        m_pendingToolReset = false;
        m_tool->OnFinished = [this]()
        {
            m_toolSuspended    = false;
            m_overlay.Clear();
            m_pendingToolReset = true;
        };

        // 用最近的光标位置合成一次 MouseMove:跟随光标的工具(粘贴/移动/复制预览)
        // 激活当帧即可出现预览,不必等真实的鼠标移动事件
        InputEvent move{};
        move.Type   = InputEventType::MouseMove;
        move.MouseX = move.LastMouseX = m_mouseX;
        move.MouseY = move.LastMouseY = m_mouseY;
        OnInput(move);
    }

    // ─────────────────────────────────────────────────────────────
    //  绘制工具便捷方法
    // ─────────────────────────────────────────────────────────────
    void Editor::StartLineTool()      { ActivateToolById("Line");      }
    void Editor::StartPointTool()     { ActivateToolById("Point");     }
    void Editor::StartRectangleTool() { ActivateToolById("Rectangle"); }
    void Editor::StartCircleTool()    { ActivateToolById("Circle");    }
    void Editor::StartArcTool()       { ActivateToolById("Arc");       }
    void Editor::StartEllipseTool()   { ActivateToolById("Ellipse");   }
    void Editor::StartPolylineTool()  { ActivateToolById("Polyline");  }
    void Editor::StartSplineTool()    { ActivateToolById("Spline");    }
    void Editor::StartTextTool()      { ActivateToolById("Text");      }
    void Editor::StartMTextTool()     { ActivateToolById("MText");     }

    void Editor::SubmitTextInput(const std::string& utf8Text)
    {
        if (!m_textRequest.Active)
            return;

        m_textRequest.Active = false;

        if (utf8Text.empty())
        {
            m_textRequest.EditTargetId = Object::InvalidID;
            return;
        }

        auto& scene    = m_doc->GetScene();
        auto& cmdStack = m_doc->GetCommandStack();

        if (m_textRequest.EditTargetId != Object::InvalidID)
        {
            auto cmd = std::make_unique<EditTextCommand>(
                m_textRequest.EditTargetId,
                m_textRequest.InitialText,
                utf8Text);
            cmdStack.Execute(std::move(cmd), scene);
            m_textRequest.EditTargetId = Object::InvalidID;
            LOG_DEBUG("[TextEdit] modified: %s", utf8Text.c_str());
            return;
        }

        const auto& layer = scene.GetLayerManager().GetActiveLayer();
        auto id  = scene.NextObjectID();
        auto ent = std::make_unique<TextEntity>(
            id,
            m_textRequest.InsertPos,
            utf8Text,
            m_textRequest.Height,
            m_textRequest.Rotation,
            scene.GetCurrentTextStyle());

        EntityAttr attr;                                   // 颜色默认 ByLayer,随层显示
        attr.LayerId    = layer.GetID();
        attr.LineType   = scene.GetCurrentLineType();
        attr.Lineweight = scene.GetCurrentLineweight();
        ent->SetAttr(attr);

        auto cmd = std::make_unique<AddEntityCommand>(std::move(ent));
        cmdStack.Execute(std::move(cmd), scene);

        LOG_DEBUG("[TextTool] added: %s", utf8Text.c_str());
    }

    // 新建文字的字高：当前文字样式有固定字高时用它，否则用默认字高（TEXTSIZE）
    double Editor::CurrentTextHeight() const
    {
        if (!m_doc)
            return m_textHeight;
        const Scene& scene = m_doc->GetScene();
        const TextStyleRecord& st = scene.GetTextStyleTable().Resolve(scene.GetCurrentTextStyle());
        return st.Height > 0.0 ? st.Height : m_textHeight;
    }

    void Editor::SubmitMTextInput(const std::string& utf8Text)
    {
        if (!m_mtextRequest.Active)
            return;

        m_mtextRequest.Active = false;

        // 表格单元格：内容为空表示清空单元格，与「取消」不同，所以先处理
        if (m_mtextRequest.EditTargetId != Object::InvalidID && m_mtextRequest.EditRow >= 0 && m_mtextRequest.EditCol >= 0)
        {
            auto& scene    = m_doc->GetScene();
            const auto id  = m_mtextRequest.EditTargetId;
            const auto row = static_cast<size_t>(m_mtextRequest.EditRow);
            const auto col = static_cast<size_t>(m_mtextRequest.EditCol);
            m_mtextRequest.EditTargetId = Object::InvalidID;
            m_mtextRequest.EditRow = m_mtextRequest.EditCol = -1;
            if (utf8Text != m_mtextRequest.InitialText)
                m_doc->GetCommandStack().Execute(
                    std::make_unique<EditTableCellCommand>(id, row, col, m_mtextRequest.InitialText, utf8Text), scene);
            return;
        }

        if (utf8Text.empty())
        {
            m_mtextRequest.EditTargetId = Object::InvalidID;
            return;
        }

        auto& scene    = m_doc->GetScene();
        auto& cmdStack = m_doc->GetCommandStack();

        if (m_mtextRequest.EditTargetId != Object::InvalidID)
        {
            auto cmd = std::make_unique<EditMTextCommand>(
                m_mtextRequest.EditTargetId,
                m_mtextRequest.InitialText,
                utf8Text);
            cmdStack.Execute(std::move(cmd), scene);
            m_mtextRequest.EditTargetId = Object::InvalidID;
            LOG_DEBUG("[MTextEdit] modified: %s", utf8Text.c_str());
            return;
        }

        const auto& layer = scene.GetLayerManager().GetActiveLayer();
        auto id  = scene.NextObjectID();
        auto ent = std::make_unique<MTextEntity>(
            id,
            scene.GetCurrentTextStyle(),
            utf8Text,
            m_mtextRequest.InsertPos,
            m_mtextRequest.Height,
            m_mtextRequest.Rotation,
            m_mtextRequest.BoxWidth);

        EntityAttr attr;                                   // 颜色默认 ByLayer,随层显示
        attr.LayerId    = layer.GetID();
        attr.LineType   = scene.GetCurrentLineType();
        attr.Lineweight = scene.GetCurrentLineweight();
        ent->SetAttr(attr);

        auto cmd = std::make_unique<AddEntityCommand>(std::move(ent));
        cmdStack.Execute(std::move(cmd), scene);

        LOG_DEBUG("[MTextTool] added: %s", utf8Text.c_str());
    }

    // ─────────────────────────────────────────────────────────────
    //  编辑工具便捷方法
    // ─────────────────────────────────────────────────────────────
    void Editor::StartMoveTool()   { ActivateToolById("Move");   }
    void Editor::StartCopyTool()   { ActivateToolById("Copy");   }
    void Editor::StartMirrorTool() { ActivateToolById("Mirror"); }
    void Editor::StartRotateTool() { ActivateToolById("Rotate"); }
    void Editor::StartScaleTool()  { ActivateToolById("Scale"); }
    void Editor::StartStretchTool() { ActivateToolById("Stretch"); }
    void Editor::StartFilletTool()  { ActivateToolById("Fillet"); }
    void Editor::StartChamferTool() { ActivateToolById("Chamfer"); }

    void Editor::StartPasteTool(const std::vector<std::unique_ptr<Entity>>* clipboard,
                                std::function<void()> onCommitted,
                                const Math::Point3* basePoint)
    {
        if (!IsBound() || !clipboard || clipboard->empty())
        {
            LOG_WARN("[Editor] Paste: clipboard empty or editor unbound");
            return;
        }

        m_commandLine.Echo("命令: Paste");
        LOG_INFO("[Editor] Start Paste");
        ActivateTool(std::make_unique<PasteTool>(clipboard, std::move(onCommitted), basePoint));
    }

    void Editor::StartPickPointTool(std::string prompt, std::function<void(const Math::Point3&)> onPicked)
    {
        if (!IsBound())
            return;

        ActivateTool(std::make_unique<PickPointTool>(std::move(prompt), std::move(onPicked)));
    }

    std::string Editor::GenerateBlockName() const
    {
        const auto& table = m_doc->GetScene().GetBlockTable();
        for (int i = 1;; ++i)
        {
            std::string name = "Block" + std::to_string(i);
            if (table.FindByName(name) == BlockTable::InvalidID)
                return name;
        }
    }

    void Editor::DefineBlock(const std::vector<Object::ObjectID>& ids, const Math::Point3& basePoint,
                             const std::string& name)
    {
        if (!m_doc || ids.empty())
            return;

        auto& scene = m_doc->GetScene();
        auto& table = scene.GetBlockTable();

        std::string blockName = name.empty() ? GenerateBlockName() : name;
        if (table.FindByName(blockName) != BlockTable::InvalidID)
        {
            m_commandLine.Echo("块名已存在: " + blockName);
            return;
        }

        auto cmd = std::make_unique<DefineBlockCommand>(blockName, basePoint, ids);
        if (m_doc->GetCommandStack().Execute(std::move(cmd), scene))
        {
            m_commandLine.Echo("已定义块: " + blockName + " (" + std::to_string(ids.size()) + " 个对象)");
            LOG_INFO("[Editor] DefineBlock %s: %zu entities", blockName.c_str(), ids.size());
            m_picking.MarkDirty();
            m_gripEditor.RebuildGrips();
            scene.MarkDirty();
        }
        else
        {
            m_commandLine.Echo("定义块失败");
        }
    }

    void Editor::SubmitBlockDefine(const std::string& name)
    {
        BlockNameRequest req = std::move(m_blockNameRequest);
        m_blockNameRequest = {};

        if (!req.Active || !m_doc)
            return;

        DefineBlock(req.Ids, req.BasePoint, name.empty() ? req.DefaultName : name);
    }

    void Editor::RequestBlockInsert()
    {
        if (!m_doc)
            return;

        // 没有可插入的块(跳过 *Model_Space/*Paper_Space 保留块与空块)直接提示
        bool any = false;
        m_doc->GetScene().GetBlockTable().ForEach([&](BlockID id, const BlockEntity& b)
        {
            if (id != BlockTable::ModelSpaceID && id != BlockTable::PaperSpaceID && !b.IsEmpty())
                any = true;
        });
        if (!any)
        {
            m_commandLine.Echo("当前文档没有可插入的块定义");
            return;
        }

        m_commandLine.Echo("命令: Insert");
        m_lastCommand = "Insert";   // 空回车可重复插入
        m_blockInsertRequest.Active = true;
    }

    bool Editor::SubmitImagePath(const std::string& path)
    {
        m_imageRequest = {};
        if (!m_doc || path.empty())
            return false;

        std::string dir;
        if (m_doc->HasPath())
            dir = std::filesystem::path(std::u8string(m_doc->GetPath().begin(), m_doc->GetPath().end()))
                      .parent_path().string();
        m_imageLibrary.SetBaseDir(dir);
        m_imageLibrary.Reload(path);        // 用户重新选的文件：丢掉旧缓存，读最新内容

        auto img = m_imageLibrary.Get(path);
        if (!img)
        {
            m_commandLine.Echo("无法读取图像: " + path);
            return false;
        }

        ActivateTool(std::make_unique<ImageTool>(path, img->Width, img->Height));
        return true;
    }

    void Editor::SubmitBlockInsert(BlockID blockId)
    {
        m_blockInsertRequest = {};

        if (!m_doc)
            return;

        const BlockEntity* blk = m_doc->GetScene().GetBlockTable().Find(blockId);
        if (!blk || blk->IsEmpty())
        {
            m_commandLine.Echo("无效的块定义");
            return;
        }

        ActivateTool(std::make_unique<InsertBlockTool>(blk, blockId));
    }

    // ─────────────────────────────────────────────────────────────
    //  阵列(Array,AR)
    // ─────────────────────────────────────────────────────────────
    // ─────────────────────────────────────────────────────────────
    //  面域(Region,REG)与面积(Area,AA)
    // ─────────────────────────────────────────────────────────────
    void Editor::CreateRegionFromSelection()
    {
        if (!m_doc)
            return;
        m_commandLine.Echo("命令: Region");
        m_lastCommand = "Region";

        auto& scene = m_doc->GetScene();
        std::vector<const Entity*> ents;
        for (auto id : m_picking.GetSelection())
        {
            const Object* o = scene.GetEntity(id);
            if (o && o->IsKindOf<Entity>())
                ents.push_back(static_cast<const Entity*>(o));
        }
        if (ents.empty())
        {
            m_commandLine.Echo("Region: 需要先选择封闭的对象（圆、椭圆、矩形、封闭多段线，或首尾相接的直线 / 圆弧）");
            return;
        }

        auto built = RegionBuilder::Build(ents);
        if (built.Loops.empty())
        {
            m_commandLine.Echo("Region: 选中的对象里没有封闭的边界（已忽略 " + std::to_string(built.Skipped) + " 个）");
            return;
        }

        // 嵌套的环归为一个面域（外环 + 孔），互不包含 / 互相交叠的环各成一个面域
        EntityAttr attr;                                        // 颜色默认 ByLayer,随层显示
        attr.LayerId    = scene.GetLayerManager().GetActiveLayer().GetID();
        attr.LineType   = scene.GetCurrentLineType();
        attr.Lineweight = scene.GetCurrentLineweight();

        std::vector<std::unique_ptr<Object>> regions;
        double totalArea = 0.0, totalPerimeter = 0.0;
        for (auto& group : RegionBuilder::GroupIntoRegions(built.Loops))
        {
            auto region = std::make_unique<RegionEntity>(scene.NextObjectID(), std::move(group));
            region->SetAttr(attr);
            totalArea      += region->Area();
            totalPerimeter += region->Perimeter();
            regions.push_back(std::move(region));
        }

        const size_t regionCount = regions.size();
        if (regionCount == 1)
            m_doc->GetCommandStack().Execute(std::make_unique<AddEntityCommand>(
                std::unique_ptr<Entity>(static_cast<Entity*>(regions[0].release()))), scene);
        else
            m_doc->GetCommandStack().Execute(std::make_unique<BatchAddCommand>(std::move(regions)), scene);

        char buf[200];
        std::snprintf(buf, sizeof(buf), "已创建 %zu 个面域，面积 = %.4f，周长 = %.4f%s", regionCount, totalArea, totalPerimeter,
                      built.Skipped > 0 ? ("（忽略 " + std::to_string(built.Skipped) + " 个对象）").c_str() : "");
        m_commandLine.Echo(buf);
        m_commandLine.Echo("原对象已保留；不再需要时请自行删除");
    }

    void Editor::RegionBoolean(BooleanOp op)
    {
        if (!m_doc)
            return;
        static const char* kNames[] = { "Union", "Subtract", "Intersect" };
        static const char* kLabel[] = { "并集", "差集", "交集" };
        const int opi = static_cast<int>(op);
        m_commandLine.Echo(std::string("命令: ") + kNames[opi]);
        m_lastCommand = kNames[opi];

        auto& scene = m_doc->GetScene();

        // 操作数：每个面域一个，另外每个自身封闭的对象（圆、矩形、封闭多段线…）各一个；按 ID 升序
        struct Operand { Object::ObjectID id; std::vector<HatchLoop> loops; };
        std::map<Object::ObjectID, Operand> operands;
        int ignored = 0;
        int lockedSkipped = 0;
        for (auto id : m_picking.GetSelection())
        {
            const Object* o = scene.GetEntity(id);
            if (!o || !o->IsKindOf<Entity>()) continue;
            if (scene.IsEntityLocked(*o)) { ++lockedSkipped; continue; }       // 运算会替换原对象，锁定的不参与
            const auto& ent = static_cast<const Entity&>(*o);

            std::vector<HatchLoop> loops;
            if (ent.IsKindOf<RegionEntity>())
                loops = static_cast<const RegionEntity&>(ent).GetLoops();
            else
                loops = RegionBuilder::Build({ &ent }).Loops;
            if (loops.empty()) { ++ignored; continue; }
            operands[id] = { id, std::move(loops) };
        }

        if (operands.size() < 2)
        {
            m_commandLine.Echo(std::string(kNames[opi]) + ": 需要至少选择两个面域或封闭对象"
                               + (ignored > 0 ? "（有 " + std::to_string(ignored) + " 个对象不能围成区域，已忽略）" : "")
                               + (lockedSkipped > 0 ? "（有 " + std::to_string(lockedSkipped) + " 个对象在锁定的图层上，已跳过）" : ""));
            return;
        }

        std::vector<std::vector<HatchLoop>> ordered;
        for (auto& kv : operands) ordered.push_back(kv.second.loops);

        const auto result = RegionBoolean::Combine(ordered,
            op == BooleanOp::Union ? PolygonBoolean::Op::Union
          : op == BooleanOp::Subtract ? PolygonBoolean::Op::Subtract : PolygonBoolean::Op::Intersect);

        if (result.empty())
        {
            m_commandLine.Echo(std::string(kNames[opi]) + ": 结果为空" + (op == BooleanOp::Intersect ? "（这些区域没有公共部分）" : "") + "，未做修改");
            return;
        }

        // 原对象替换为一个新面域，作为一步操作（撤销一次全部还原）；属性取自 ID 最小的原对象
        const auto firstId = operands.begin()->first;
        std::vector<GeometryEditCommand::Item> items;
        for (auto& kv : operands)
        {
            GeometryEditCommand::Item it;
            it.id     = kv.first;
            it.before = static_cast<const Entity*>(scene.GetEntity(kv.first))->Clone(kv.first);
            items.push_back(std::move(it));
        }
        auto region = std::make_unique<RegionEntity>(scene.NextObjectID(), result);
        region->SetAttr(static_cast<const Entity*>(scene.GetEntity(firstId))->GetAttr());
        const double area = region->Area(), perimeter = region->Perimeter();
        GeometryEditCommand::Item add;
        add.id    = region->GetID();
        add.after = std::move(region);
        items.push_back(std::move(add));

        m_picking.ClearSelection();
        m_picking.MarkDirty();
        m_gripEditor.MarkDirty();
        m_doc->GetCommandStack().Execute(
            std::make_unique<GeometryEditCommand>(std::string("面域") + kLabel[opi], std::move(items)), scene);

        char buf[200];
        std::snprintf(buf, sizeof(buf), "%s：已生成面域（%zu 个环），面积 = %.4f，周长 = %.4f", kLabel[opi], result.size(), area, perimeter);
        m_commandLine.Echo(buf);
        if (ignored > 0)
            m_commandLine.Echo("有 " + std::to_string(ignored) + " 个对象不能围成区域，已忽略");
    }

    void Editor::ReportArea()
    {
        if (!m_doc)
            return;
        m_commandLine.Echo("命令: Area");
        m_lastCommand = "Area";

        auto& scene = m_doc->GetScene();
        double total = 0.0;
        int    count = 0;
        char   buf[200];
        for (auto id : m_picking.GetSelection())
        {
            const Object* o = scene.GetEntity(id);
            if (!o || !o->IsKindOf<Entity>())
                continue;
            double a = 0.0, p = 0.0;
            if (!RegionMeasure::Measure(static_cast<const Entity&>(*o), a, p))
                continue;
            std::snprintf(buf, sizeof(buf), "对象 %llu：面积 = %.4f，周长 = %.4f", static_cast<unsigned long long>(id), a, p);
            m_commandLine.Echo(buf);
            total += a;
            ++count;
        }

        if (count == 0)
            m_commandLine.Echo("Area: 需要先选择能围成区域的对象（面域、填充、圆、椭圆、矩形、封闭多段线或首尾相接的线段）");
        else if (count > 1)
        {
            std::snprintf(buf, sizeof(buf), "共 %d 个对象，总面积 = %.4f", count, total);
            m_commandLine.Echo(buf);
        }
    }

    void Editor::RequestArray()
    {
        if (!m_doc)
            return;

        const auto& sel = m_picking.GetSelection();
        if (sel.empty())
        {
            m_commandLine.Echo("Array: 需要先选择对象");
            LOG_WARN("[Editor] Array: no selection");
            return;
        }

        m_arrayRequest = {};
        m_arrayRequest.Active = true;
        m_arrayRequest.SourceIds.assign(sel.begin(), sel.end());
        // 环形默认中心 = 选择集合并包围盒中心
        m_arrayRequest.Params.center =
            ComputeArrayGroupBase(m_doc->GetScene(), m_arrayRequest.SourceIds);

        m_commandLine.Echo("命令: Array");
        m_lastCommand = "Array";
    }

    void Editor::BeginArrayCenterPick()
    {
        if (!m_arrayRequest.Active)
            return;

        m_arrayRequest.PickingCenter = true;

        ActivateTool(std::make_unique<PickPointTool>(
            "指定阵列中心点 [右键/ESC 返回]:",
            [this](const Math::Point3& p) { m_arrayRequest.Params.center = p; }));

        // 覆盖 ActivateTool 设置的 OnFinished:无论拾取成功还是取消,
        // 都退出拾取态,让阵列对话框重新显示。
        if (m_tool)
        {
            m_tool->OnFinished = [this]()
            {
                m_toolSuspended    = false;
                m_overlay.Clear();
                m_pendingToolReset = true;
                m_arrayRequest.PickingCenter = false;
            };
        }
    }

    void Editor::SubmitArray()
    {
        ArrayRequest req = std::move(m_arrayRequest);
        m_arrayRequest = {};
        m_overlay.Clear();

        if (!req.Active || !m_doc || req.SourceIds.empty())
            return;

        if (!req.Params.ProducesCopies())
        {
            m_commandLine.Echo("Array: 参数无效(数量过小)");
            return;
        }

        auto& scene = m_doc->GetScene();
        auto  cmd   = std::make_unique<ArrayCommand>(req.SourceIds, req.Params);
        if (m_doc->GetCommandStack().Execute(std::move(cmd), scene))
        {
            m_commandLine.Echo("阵列完成");
            LOG_INFO("[Editor] Array done: %zu sources", req.SourceIds.size());
            m_picking.MarkDirty();
            m_gripEditor.RebuildGrips();
        }
        else
        {
            m_commandLine.Echo("阵列失败");
        }
    }

    // ─────────────────────────────────────────────────────────────
    //  图案填充(Hatch,H):对话框确认后进入拾取内部点
    // ─────────────────────────────────────────────────────────────
    void Editor::SubmitHatch()
    {
        m_hatchRequest = {};
        ActivateRegisteredTool("Hatch");
    }

    void Editor::CancelHatch()
    {
        m_hatchRequest = {};
    }

    void Editor::CancelArray()
    {
        m_arrayRequest = {};
        m_overlay.Clear();
    }

    void Editor::BuildArrayPreview(const ArrayParams& p)
    {
        m_overlay.Clear();
        if (!m_doc)
            return;

        const auto& ids = m_arrayRequest.SourceIds;
        if (ids.empty() || !p.ProducesCopies())
            return;

        auto&        scene     = m_doc->GetScene();
        Math::Point3 groupBase = ComputeArrayGroupBase(scene, ids);
        const auto   placements = ComputeArrayPlacements(p, groupBase);

        const Math::Color4 kGhost{ 0.55, 0.55, 0.55, 0.7 };
        const size_t       kMaxGhost = 600;   // 预览幽灵上限,避免超大阵列卡顿
        size_t             drawn = 0;

        for (const auto& pl : placements)
        {
            for (auto id : ids)
            {
                auto* obj = scene.GetEntity(id);
                if (!obj || !obj->IsKindOf<Entity>())
                    continue;

                auto clone = static_cast<Entity*>(obj)->Clone(0);
                pl.Apply(*clone);
                DrawEntityToOverlay(m_overlay, *clone, kGhost);

                if (++drawn >= kMaxGhost)
                {
                    if (p.type == ArrayType::Polar)
                        m_overlay.AddPoint(p.center, kGhost);
                    return;
                }
            }
        }

        if (p.type == ArrayType::Polar)
            m_overlay.AddPoint(p.center, kGhost);
    }

    // ─────────────────────────────────────────────────────────────
    //  几何编辑工具便捷方法
    // ─────────────────────────────────────────────────────────────
    void Editor::StartTrimTool()   { ActivateToolById("Trim");   }
    void Editor::StartExtendTool() { ActivateToolById("Extend"); }
    void Editor::StartBreakTool()  { ActivateToolById("Break");  }

    // ─────────────────────────────────────────────────────────────
    //  动态输入
    // ─────────────────────────────────────────────────────────────
    Editor::DynamicInputState Editor::GetDynamicInput() const
    {
        DynamicInputState s;
        if (!m_tool || !m_tool->HasAnchor() || !m_hasResolved)
            return s;

        s.Active  = true;
        s.Anchor  = m_tool->GetAnchor();
        s.Current = m_lastResolved;

        const double dx = s.Current.x - s.Anchor.x;
        const double dy = s.Current.y - s.Anchor.y;
        s.Length = std::sqrt(dx * dx + dy * dy);

        double deg = std::atan2(dy, dx) * 180.0 / Math::PI;
        if (deg < 0.0) deg += 360.0;
        s.AngleDeg = deg;

        s.OrthoOn       = m_constraintEngine.IsOrthoEnabled();
        s.PolarOn       = m_constraintEngine.IsPolarEnabled();
        s.PolarAngleDeg = m_constraintEngine.GetPolar().GetAngleDeg();
        s.SnapType      = m_currentSnap.SnapType;
        return s;
    }

    void Editor::ApplyDynamicInput(bool hasLen, double len, bool hasAngle, double angleDeg)
    {
        if (!m_doc || !m_viewport) return;
        if (!m_tool || m_toolSuspended || !m_tool->HasAnchor()) return;
        if (!hasLen && !hasAngle) return;

        const Math::Point3 anchor = m_tool->GetAnchor();

        // 方向:键入角度优先,否则取当前橡皮筋方向
        double dirX = 1.0, dirY = 0.0;
        if (hasAngle)
        {
            const double rad = angleDeg * Math::PI / 180.0;
            dirX = std::cos(rad);
            dirY = std::sin(rad);
        }
        else if (m_hasResolved)
        {
            const double dx = m_lastResolved.x - anchor.x;
            const double dy = m_lastResolved.y - anchor.y;
            const double d  = std::sqrt(dx * dx + dy * dy);
            if (d < Math::LengthEPS) return;   // 无方向可依
            dirX = dx / d;
            dirY = dy / d;
        }

        // 距离:键入长度优先,否则取当前橡皮筋长度
        double dist = 0.0;
        if (hasLen)
        {
            dist = len;
        }
        else
        {
            const double dx = m_lastResolved.x - anchor.x;
            const double dy = m_lastResolved.y - anchor.y;
            dist = std::sqrt(dx * dx + dy * dy);
        }
        if (dist <= Math::LengthEPS) return;

        const Math::Point3 target{ anchor.x + dirX * dist, anchor.y + dirY * dist, anchor.z };
        SubmitPoint(target);
    }

    bool Editor::SubmitPoint(const Math::Point3& target)
    {
        if (!m_doc || !m_viewport || !m_tool || m_toolSuspended)
            return false;

        // 合成左键点击事件直接送给工具:HasSnap+SnapWorld 携带精确点,
        // 不经 InputResolver,避免捕捉/约束改写键入值。
        InputEvent e{};
        e.Type   = InputEventType::MouseButtonDown;
        e.Button = MouseButton::Left;
        const auto sp = m_viewport->GetCamera().WorldToScreen(target);
        e.MouseX = e.PressMouseX = e.LastMouseX = static_cast<int>(std::lround(sp.x));
        e.MouseY = e.PressMouseY = e.LastMouseY = static_cast<int>(std::lround(sp.y));
        e.HasSnap   = true;
        e.SnapWorld = target;

        EditorContext ctx{
                .event      = e,
                .scene      = m_doc->GetScene(),
                .viewport   = *m_viewport,
                .snap       = m_snap,
                .constraint = m_constraintEngine,
                .picking    = m_picking,
                .cmdStack   = m_doc->GetCommandStack(),
                .overlay    = m_overlay,
                .tool       = m_tool.get(),
                .grip       = &m_gripEditor
        };

        m_tool->OnInput(ctx);

        // 提交后锚点变为 target,刷新预览基准
        m_lastResolved = target;
        m_hasResolved  = true;
        return true;
    }

    bool Editor::SubmitCoordinateText(const std::string& raw)
    {
        if (!m_tool || m_toolSuspended)
            return false;

        // 去空白；全角逗号、小于号、@ 也接受（中文输入法下常见）
        std::string text;
        for (size_t i = 0; i < raw.size(); ++i)
        {
            const unsigned char c = static_cast<unsigned char>(raw[i]);
            if (c == ' ' || c == '\t')
                continue;
            if (c == 0xEF && i + 2 < raw.size() && static_cast<unsigned char>(raw[i + 1]) == 0xBC)
            {
                const unsigned char d = static_cast<unsigned char>(raw[i + 2]);
                if (d == 0x8C) { text.push_back(','); i += 2; continue; }   // ，
                if (d == 0x9C) { text.push_back('<'); i += 2; continue; }   // ＜
                if (d == 0xA0) { text.push_back('@'); i += 2; continue; }   // ＠
            }
            text.push_back(static_cast<char>(c));
        }
        if (text.empty())
            return false;

        // 整串都是一个数
        auto number = [](const std::string& s, double& out)
        {
            if (s.empty())
                return false;
            char* end = nullptr;
            out = std::strtod(s.c_str(), &end);
            return end == s.c_str() + s.size() && std::isfinite(out);
        };

        // 整串是一个数，且工具要的就是数值（如圆角半径）：交给工具
        if (double only = 0.0; number(text, only) && m_tool->OnNumberInput(only))
        {
            m_commandLine.Echo(raw);
            return true;
        }

        const bool relative = text[0] == '@';
        const std::string body = relative ? text.substr(1) : text;
        Math::Point3 anchor;
        const bool hasAnchor = m_tool->HasAnchor();
        if (hasAnchor)
            anchor = m_tool->GetAnchor();

        Math::Point3 target;
        const size_t comma = body.find(',');
        const size_t less  = body.find('<');
        double a = 0.0, b = 0.0;
        if (comma != std::string::npos && number(body.substr(0, comma), a) && number(body.substr(comma + 1), b))
        {
            // "a,b" 两个数且工具要的就是两个数值（如倒角距离）：交给工具
            if (!relative && m_tool->OnNumberPairInput(a, b))
            {
                m_commandLine.Echo(raw);
                return true;
            }
            if (relative && !hasAnchor)
                return false;
            target = relative ? Math::Point3{ anchor.x + a, anchor.y + b, anchor.z } : Math::Point3{ a, b, 0.0 };
        }
        else if (relative && less != std::string::npos && number(body.substr(0, less), a) && number(body.substr(less + 1), b))
        {
            if (!hasAnchor)
                return false;
            const double rad = b * Math::PI / 180.0;
            target = { anchor.x + a * std::cos(rad), anchor.y + a * std::sin(rad), anchor.z };
        }
        else if (!relative && number(body, a))
        {
            // 直接距离：沿当前橡皮筋方向
            if (!hasAnchor || !m_hasResolved || a <= Math::LengthEPS)
                return false;
            const double dx = m_lastResolved.x - anchor.x;
            const double dy = m_lastResolved.y - anchor.y;
            const double d  = std::sqrt(dx * dx + dy * dy);
            if (d < Math::LengthEPS)
                return false;
            target = { anchor.x + dx / d * a, anchor.y + dy / d * a, anchor.z };
        }
        else
        {
            return false;
        }

        const std::string prompt = m_tool->GetPrompt();
        m_commandLine.Echo((prompt.empty() ? std::string() : prompt + " ") + raw);
        return SubmitPoint(target);
    }

    // ─────────────────────────────────────────────────────────────
    //  OnInput
    // ─────────────────────────────────────────────────────────────
    bool Editor::OnInput(const InputEvent& inputEvent)
    {
        if (!m_doc || !m_viewport) return false;

        m_mouseX = inputEvent.MouseX;
        m_mouseY = inputEvent.MouseY;

        auto& scene    = m_doc->GetScene();
        auto& cmdStack = m_doc->GetCommandStack();

        if (m_pendingToolReset)
        {
            m_pendingToolReset = false;
            m_tool.reset();
        }

        // 事件副本 e：ctx.event 绑定到它，Resolve 会就地回填捕获点，工具再经 ctx 读取
        InputEvent e = inputEvent;

        EditorContext ctx{
                .event      = e,
                .scene      = scene,
                .viewport   = *m_viewport,
                .snap       = m_snap,
                .constraint = m_constraintEngine,
                .picking    = m_picking,
                .cmdStack   = cmdStack,
                .overlay    = m_overlay,
                .tool       = m_tool.get(),
                .grip       = &m_gripEditor
        };

        m_resolver.Resolve(ctx);
        m_currentSnap  = ctx.resolved.hasSnap ? ctx.resolved.snap : SnapResult{};
        m_lastResolved = ctx.resolved.point;
        m_hasResolved  = ctx.resolved.hasPoint;


        if (e.Type == InputEventType::KeyDown || e.Type == InputEventType::KeyUp)
        {
            if (m_tool && !m_toolSuspended)
            {
                if (m_tool->OnInput(ctx))
                    return true;
            }

            if (HandleGlobal(e))
                return true;

            if (m_picking.OnInput(e))
            {
                m_gripEditor.MarkDirty();
                return true;
            }

            return false;
        }

        if (HandleGlobal(e))
            return true;

        if (m_tool && !m_toolSuspended)
            return m_tool->OnInput(ctx);

        if (m_gripEditor.OnInput(ctx.event))
            return true;

        if (!m_gripEditor.IsDragging())
        {
            // 二次点击已选中的文字实体 → 触发编辑（必须在 Picking 消费事件之前检测）
            if (e.IsLeftClick())
            {
                Math::Point2 screenPt{ static_cast<double>(e.MouseX), static_cast<double>(e.MouseY) };
                Object::ObjectID hit = m_picking.HitTest(screenPt, 5.0);
                if (hit != Object::InvalidID && m_picking.GetSelection().count(hit))
                {
                    auto* obj = scene.GetEntity(hit);
                    if (obj && scene.IsEntityLocked(*obj))
                        obj = nullptr;                  // 锁定图层上的文字不能编辑
                    if (obj && obj->IsKindOf<TextEntity>())
                    {
                        auto* te = static_cast<TextEntity*>(obj);
                        m_textRequest.Active       = true;
                        m_textRequest.EditTargetId = hit;
                        m_textRequest.InitialText  = te->GetText();
                        LOG_DEBUG("[Editor] edit text id=%u", static_cast<int>(hit));
#ifdef MINICAD_WEB
                        EM_ASM({
                            var t = UTF8ToString($0);
                            if (typeof window._minicadShowTextEdit === 'function')
                                window._minicadShowTextEdit(t);
                        }, te->GetText().c_str());
#endif
                        return true;
                    }
                    if (obj && obj->IsKindOf<TableEntity>())
                    {
                        auto* te = static_cast<TableEntity*>(obj);
                        const Math::Point3 wp = m_viewport->GetCamera().ScreenToWorld(e.MouseX, e.MouseY);
                        size_t row = 0, col = 0;
                        if (te->HitCell(wp, row, col))
                        {
                            m_mtextRequest = {};
                            m_mtextRequest.Active       = true;
                            m_mtextRequest.EditTargetId = hit;
                            m_mtextRequest.EditRow      = static_cast<int>(row);
                            m_mtextRequest.EditCol      = static_cast<int>(col);
                            m_mtextRequest.InitialText  = te->Cell(row, col).Text;
                            m_mtextRequest.Height       = te->GetHeight();
                            m_mtextRequest.BoxWidth     = te->CellTextWidth(col);
                            const Math::Point3 tl       = te->CellTopLeft(row, col);
                            m_mtextRequest.InsertPos    = { tl.x + te->Margin(), tl.y - te->Margin(), tl.z };
                            LOG_DEBUG("[Editor] edit table cell id=%u (%zu,%zu)", static_cast<int>(hit), row, col);
                            return true;
                        }
                    }
                    if (obj && obj->IsKindOf<MTextEntity>())
                    {
                        auto* me = static_cast<MTextEntity*>(obj);
                        m_mtextRequest.Active       = true;
                        m_mtextRequest.EditTargetId = hit;
                        m_mtextRequest.InitialText  = me->GetText();
                        LOG_DEBUG("[Editor] edit mtext id=%u", static_cast<int>(hit));
#ifdef MINICAD_WEB
                        EM_ASM({
                            var t = UTF8ToString($0);
                            if (typeof window._minicadShowMTextEdit === 'function')
                                window._minicadShowMTextEdit(t);
                        }, me->GetText().c_str());
#endif
                        return true;
                    }
                }
            }

            if (m_picking.OnInput(e))
            {
                m_gripEditor.MarkDirty();
                return true;
            }
        }

        return HandleDefault(e);
    }

    // ─────────────────────────────────────────────────────────────
    //  HandleGlobal
    // ─────────────────────────────────────────────────────────────
    bool Editor::HandleGlobal(const InputEvent& e)
    {
        auto& scene    = m_doc->GetScene();
        auto& cmdStack = m_doc->GetCommandStack();

        if (e.IsUndo())
        {
            if (m_tool) m_tool->OnSceneChanged();
            cmdStack.Undo(scene);
            m_gripEditor.MarkDirty();
            scene.MarkDirty();
            return true;
        }

        if (e.IsRedo())
        {
            if (m_tool) m_tool->OnSceneChanged();
            cmdStack.Redo(scene);
            m_gripEditor.MarkDirty();
            scene.MarkDirty();
            return true;
        }

        if (e.IsCancel())
        {
            if (m_tool)
            {
                m_tool->Cancel();
                m_tool.reset();
                m_toolSuspended = false;
                m_overlay.Clear();
                return true;
            }
            else if (m_gripEditor.IsDragging())
            {
                m_gripEditor.CancelDrag();
                m_gripEditor.MarkDirty();
                return true;
            }
            return false;
        }

        if (e.Type == InputEventType::KeyDown)
        {
            if (e.Key >= KeyCode::A && e.Key <= KeyCode::Z)
            {
                m_cmdBuffer += ToCommandChar(e.Key);
                LOG_TRACE("cmdBuffer: %s", m_cmdBuffer.c_str());
                return true;
            }

            if (e.Key == KeyCode::Enter || e.Key == KeyCode::Space)
            {
                LOG_DEBUG("[Editor] Enter/Space: cmdBuffer='%s' lastCommand='%s'",
                    m_cmdBuffer.c_str(), m_lastCommand.c_str());

                if (!m_cmdBuffer.empty())
                {
                    ActivateToolByAlias(m_cmdBuffer);
                    m_lastCommand = m_cmdBuffer;
                    m_cmdBuffer.clear();
                    return true;
                }

                if (!m_lastCommand.empty())
                {
                    ActivateToolByAlias(m_lastCommand);
                    return true;
                }
                return true;
            }

            if (e.Key == KeyCode::Escape)
            {
                m_zoomPending = false;
                m_cmdBuffer.clear();
            }

            if (e.Key == KeyCode::Delete)
            {
                if (m_tool) m_tool->OnSceneChanged();
                DeleteSelected();
                m_gripEditor.RebuildGrips();
                return true;
            }

            if (e.Key == KeyCode::F3)
            {
                ToggleSnap();
                return true;
            }

            if (e.Key == KeyCode::F8)
            {
                ToggleOrtho();
                return true;
            }

            if (e.Key == KeyCode::F10)
            {
                TogglePolar();
                return true;
            }
        }

        // ── 中键平移 ────────────────────────────────────────────
        if (e.Type == InputEventType::MouseButtonDown && e.Button == MouseButton::Middle)
        {
            if (m_tool && !m_toolSuspended)
            {
                m_tool->OnFocusLost();
                m_toolSuspended = true;
            }
            return true;
        }

        if (e.Type == InputEventType::MouseButtonUp && e.Button == MouseButton::Middle)
        {
            if (m_tool && m_toolSuspended)
            {
                m_toolSuspended = false;
                m_tool->OnFocusRestored();
            }
            return false;
        }

        if (e.Type == InputEventType::MouseMove && e.IsMouseButtonDown(MouseButton::Middle))
        {
            m_viewport->Pan(e.MouseX - e.LastMouseX, e.MouseY - e.LastMouseY);
            // 不 MarkDirty：场景顶点是世界空间，平移只改变相机 ViewProj（绘制时在 GPU 应用），
            // 不需要重建/重新细分任何实体顶点。重绘由平台主循环（Win32 消息 / wasm 每帧 Tick）保证。
            // scene.MarkDirty();
            return true;
        }

        // ── 滚轮缩放 ────────────────────────────────────────────
        if (e.Type == InputEventType::MouseWheel)
        {
            m_viewport->Zoom(e.WheelDelta, e.MouseX, e.MouseY);
            // 不 MarkDirty：理由同上（细分段数固定，与缩放无关，无需重建顶点）。
            // scene.MarkDirty();
            return true;
        }

        return false;
    }

    bool Editor::HandleDefault(const InputEvent& /*e*/)
    {
        return false;
    }

    // ─────────────────────────────────────────────────────────────
    //  Picking / 选择
    // ─────────────────────────────────────────────────────────────
    const std::unordered_set<Object::ObjectID>& Editor::GetSelection()
    {
        return m_picking.GetSelection();
    }

    const std::unordered_set<Object::ObjectID>& Editor::GetHovered()
    {
        return m_picking.GetHovered();
    }

    Object* Editor::GetPrimarySelectedObject()
    {
        const auto& sel = m_picking.GetSelection();
        if (sel.empty()) return nullptr;
        return m_doc->GetScene().GetEntity(*sel.begin());
    }

    bool Editor::ChangeSelectionAttr(const AttrChange& change)
    {
        if (!m_doc || change.Empty() || m_picking.GetSelection().empty())
            return false;
        std::vector<Object::ObjectID> ids = EditableSelectionIds();
        if (ids.empty())
            return false;
        return m_doc->GetCommandStack().Execute(std::make_unique<ChangeAttrCommand>(std::move(ids), change), m_doc->GetScene());
    }

    std::vector<CommonProperty> Editor::GetSelectionProperties()
    {
        std::vector<const Entity*> entities;
        for (Object* obj : GetSelectedObjects())
            if (const auto* e = dynamic_cast<const Entity*>(obj))
                entities.push_back(e);
        return CommonProperties(entities);
    }

    bool Editor::SetSelectionProperty(const std::string& name, const PropValue& value)
    {
        if (!m_doc)
            return false;

        const std::vector<Object::ObjectID> ids = EditableSelectionIds();

        std::vector<GeometryEditCommand::Item> items;
        for (auto id : ids)
        {
            const auto* entity = dynamic_cast<const Entity*>(m_doc->GetScene().GetEntity(id));
            if (!entity)
                continue;
            auto after = ApplyProperty(*entity, name, value);
            if (!after)
                continue;
            GeometryEditCommand::Item item;
            item.id     = id;
            item.before = entity->Clone(id);
            item.after  = std::move(after);
            items.push_back(std::move(item));
        }
        if (items.empty())
            return false;

        const bool ok = m_doc->GetCommandStack().Execute(
            std::make_unique<GeometryEditCommand>("修改特性：" + name, std::move(items)), m_doc->GetScene());
        if (ok)
            m_gripEditor.MarkDirty();               // 夹点位置跟着几何走
        return ok;
    }

    size_t Editor::ExplodeSelection()
    {
        if (!m_doc || IsToolRunning())
            return 0;
        if (m_picking.GetSelection().empty())
        {
            m_commandLine.Echo("请先选择要分解的对象");
            return 0;
        }

        const std::vector<Object::ObjectID> ids = EditableSelectionIds();
        if (ids.empty())
            return 0;

        auto& scene = m_doc->GetScene();
        std::vector<GeometryEditCommand::Item> items;
        std::unordered_set<Object::ObjectID> created;
        size_t exploded = 0, skipped = 0;
        std::string reason;
        for (auto id : ids)
        {
            const auto* entity = dynamic_cast<const Entity*>(scene.GetEntity(id));
            if (!entity) continue;

            std::vector<std::unique_ptr<Entity>> parts;
            std::string why;
            if (!ExplodeEntity(*entity, parts, &why))
            {
                ++skipped;
                if (reason.empty()) reason = why;
                continue;
            }

            GeometryEditCommand::Item del;              // 原对象删除
            del.id     = id;
            del.before = entity->Clone(id);
            items.push_back(std::move(del));
            for (auto& part : parts)
            {
                GeometryEditCommand::Item add;
                add.id    = scene.NextObjectID();
                add.after = part->Clone(add.id);
                created.insert(add.id);
                items.push_back(std::move(add));
            }
            ++exploded;
        }

        if (exploded == 0)
        {
            m_commandLine.Echo("无法分解：" + (reason.empty() ? std::string("选择集里没有可分解的对象") : reason));
            return 0;
        }

        const size_t count = created.size();
        m_doc->GetCommandStack().Execute(std::make_unique<GeometryEditCommand>("分解", std::move(items)), scene);
        SetSelection(std::move(created));               // 选中分解出来的对象
        m_commandLine.Echo("已分解 " + std::to_string(exploded) + " 个对象，生成 " + std::to_string(count) + " 个对象" +
                           (skipped ? "；" + std::to_string(skipped) + " 个对象不能分解，保持不变" : std::string()));
        return exploded;
    }

    size_t Editor::SelectAll()
    {
        if (!m_doc || IsToolRunning())
            return 0;

        std::unordered_set<Object::ObjectID> ids;
        m_doc->GetScene().ForEachObject([&](const Object& o)
        {
            if (o.IsKindOf<Entity>() && m_picking.IsPickable(o) && !m_doc->GetScene().IsEntityLocked(o))
                ids.insert(o.GetID());
        });
        const size_t n = ids.size();
        SetSelection(std::move(ids));
        m_commandLine.Echo(n == 0 ? "没有可选择的对象" : "已选择 " + std::to_string(n) + " 个对象");
        return n;
    }

    void Editor::SetSelection(std::unordered_set<Object::ObjectID> ids)
    {
        m_picking.SetSelection(std::move(ids));
        m_gripEditor.MarkDirty();
    }

    std::vector<Object*> Editor::GetSelectedObjects()
    {
        std::vector<Object*> result;
        auto& scene = m_doc->GetScene();
        for (auto id : m_picking.GetSelection())
        {
            if (auto* obj = scene.GetEntity(id))
                result.push_back(obj);
        }
        return result;
    }

    std::vector<Object::ObjectID> Editor::EditableSelectionIds(bool announce)
    {
        std::vector<Object::ObjectID> ids;
        if (!m_doc)
            return ids;
        auto& scene = m_doc->GetScene();
        size_t locked = 0;
        for (auto id : m_picking.GetSelection())
        {
            const Object* obj = scene.GetEntity(id);
            if (!obj) continue;
            if (scene.IsEntityLocked(*obj)) { ++locked; continue; }
            ids.push_back(id);
        }
        std::sort(ids.begin(), ids.end());              // 顺序确定，撤销 / 重做结果一致
        if (announce && locked > 0)
            m_commandLine.Echo(ids.empty() ? std::string("所选对象都在锁定的图层上，不能修改")
                                           : "已跳过 " + std::to_string(locked) + " 个锁定图层上的对象");
        return ids;
    }

    std::vector<Object*> Editor::GetEditableSelectedObjects(bool announce)
    {
        std::vector<Object*> result;
        if (!m_doc)
            return result;
        for (auto id : EditableSelectionIds(announce))
            if (auto* obj = m_doc->GetScene().GetEntity(id))
                result.push_back(obj);
        return result;
    }

    // ─────────────────────────────────────────────────────────────
    //  删除
    // ─────────────────────────────────────────────────────────────
    void Editor::DeleteSelected()
    {
        const auto idsVec = EditableSelectionIds();
        if (idsVec.empty()) return;

        auto cmd = std::make_unique<BatchDeleteCommand>(idsVec);
        m_doc->GetCommandStack().Execute(std::move(cmd), m_doc->GetScene());
    }

    // ─────────────────────────────────────────────────────────────
    //  TryGetAnchor
    // ─────────────────────────────────────────────────────────────
    bool Editor::TryGetAnchor(Math::Point3& out) const
    {
        if (m_tool && m_tool->HasAnchor())
        {
            out = m_tool->GetAnchor();
            return true;
        }

        if (m_gripEditor.IsDragging())
        {
            if (const Grip* g = m_gripEditor.GetActiveGrip())
            {
                out = g->WorldPos;
                return true;
            }
            return false;
        }
        return false;
    }

    // ─────────────────────────────────────────────────────────────
    //  约束 / 捕捉开关
    // ─────────────────────────────────────────────────────────────
    bool Editor::IsOrthoEnabled() const { return m_constraintEngine.IsOrthoEnabled(); }

    void Editor::SetOrthoEnabled(bool enabled)
    {
        if (m_constraintEngine.IsOrthoEnabled() == enabled) return;
        m_constraintEngine.EnableOrtho(enabled);
        LOG_INFO("[Editor] Ortho: %s", enabled ? "ON" : "OFF");
    }

    void Editor::ToggleOrtho() { SetOrthoEnabled(!IsOrthoEnabled()); }

    bool Editor::IsPolarEnabled() const { return m_constraintEngine.IsPolarEnabled(); }

    void Editor::SetPolarEnabled(bool enabled)
    {
        if (m_constraintEngine.IsPolarEnabled() == enabled) return;
        m_constraintEngine.EnablePolar(enabled);
        LOG_INFO("[Editor] Polar: %s (%.1f deg)", enabled ? "ON" : "OFF",
               m_constraintEngine.GetPolar().GetAngleDeg());
    }

    void Editor::TogglePolar() { SetPolarEnabled(!IsPolarEnabled()); }

    double Editor::GetPolarAngle() const { return m_constraintEngine.GetPolar().GetAngleDeg(); }

    void Editor::SetPolarAngle(double deg) { m_constraintEngine.GetPolar().SetAngleDeg(deg); }

    bool Editor::IsSnapEnabled() const { return m_snap.IsEnabled(); }

    void Editor::SetSnapEnabled(bool enabled)
    {
        if (m_snap.IsEnabled() == enabled) return;
        m_snap.SetEnableSnap(enabled);
        LOG_INFO("[Editor] Snap: %s", enabled ? "ON" : "OFF");
    }

    void Editor::ToggleSnap() { SetSnapEnabled(!m_snap.IsEnabled()); }

    // ─────────────────────────────────────────────────────────────
    //  Undo / Redo / Command
    // ─────────────────────────────────────────────────────────────
    void Editor::Undo()
    {
        m_doc->GetCommandStack().Undo(m_doc->GetScene());
    }

    void Editor::Redo()
    {
        m_doc->GetCommandStack().Redo(m_doc->GetScene());
    }

    void Editor::ExecuteCommand(std::unique_ptr<ICommand> cmd)
    {
        m_doc->GetCommandStack().Execute(std::move(cmd), m_doc->GetScene());
    }

    // ─────────────────────────────────────────────────────────────
    //  渲染
    // ─────────────────────────────────────────────────────────────
    void Editor::Render()
    {
        if (!m_doc || !m_viewport) return;

        // 命令行提示 = 当前工具的状态提示（无工具时为空）
        m_commandLine.SetPrompt(m_tool ? m_tool->GetPrompt()
                                       : (m_zoomPending ? std::string("ZOOM 选项 [全部(A)/范围(E)]:") : std::string{}));

        m_overlayVertices.clear();

        // 1 屏幕像素对应的世界长度(线宽/虚线显示用):用单位向量投影求得
        auto& camera = m_viewport->GetCamera();
        auto  s0 = camera.WorldToScreen({ 0, 0, 0 });
        auto  s1 = camera.WorldToScreen({ 1, 0, 0 });
        double pxPerWorld    = Math::Distance(s0, s1);
        double worldPerPixel = (pxPerWorld > 1e-12) ? 1.0 / pxPerWorld : 0.0;

        // 线宽几何按屏幕像素等宽烘焙,缩放变化时含线宽的流需重建
        bool zoomChanged = std::abs(worldPerPixel - m_lastWorldPerPixel) > 1e-12;
        m_lastWorldPerPixel = worldPerPixel;

        const bool dragging       = m_gripEditor.IsDragging();
        const bool selectionDirty = m_picking.IsDirty();

        // 拾取空间索引按脏实体集增量同步——必须在 UpdateSceneVertices 的
        // Scene::ClearDirty 之前,否则脏集被清掉后索引只能整表重建。
        m_picking.SyncIndex();

        bool sceneRebuilt = UpdateSceneVertices(worldPerPixel, zoomChanged);

        // 场景或选择集变化时清预览、重建夹点（原先在场景重建路径内）
        if ((sceneRebuilt || selectionDirty) && !IsActiveTool() && !dragging)
        {
            m_overlay.Clear();
            m_gripEditor.RebuildGrips();
        }

        // 选中流：选择集变化 / 场景重建 / 拖动中（几何逐帧变化）/ 缩放+线宽
        UpdateSelectionVertices(worldPerPixel,
                                sceneRebuilt || selectionDirty || dragging ||
                                (zoomChanged && m_selHasLineweight));

        m_overlay.ToVertices(m_overlayVertices);

        // 约束辅助线：直接写入「每帧重建」的 m_overlayVertices，绝不入持久的
        // m_overlay。后者只在工具 MouseMove/结束或场景/选择变化时才清空，把辅助线
        // 放进去会在绘制结束后残留(陈旧的绿色线)。写进每帧缓冲则停止锚定的当帧即消失。
        // 绘制条件：
        //  · 必须 HasAnchor()——否则 GetGuideLine() 仍是上次解析留下的陈旧值
        //    （Resolve 只在有锚点时调用 Apply，无锚点时不会重置 m_guideLine）。
        //  · 工具收尾期 m_pendingToolReset 为真：此时 m_tool 尚未真正 reset，
        //    IsActiveTool() 仍为真，但绘制已结束，须排除。
        const bool toolAnchoring = IsActiveTool() && !m_pendingToolReset && m_tool->HasAnchor();
        if (m_constraintEngine.IsAnyActive() &&
            (toolAnchoring || m_gripEditor.IsDragging()))
        {
            const Line& guideLine = GetAnchorLine();
            if (guideLine.IsValid())
            {
                const Math::Float4 c{ 0.1f, 0.7f, 0.1f, 0.6f };
                m_overlayVertices.push_back({ { static_cast<float>(guideLine.Start.x),
                                                static_cast<float>(guideLine.Start.y),
                                                static_cast<float>(guideLine.Start.z) }, c });
                m_overlayVertices.push_back({ { static_cast<float>(guideLine.End.x),
                                                static_cast<float>(guideLine.End.y),
                                                static_cast<float>(guideLine.End.z) }, c });
            }
        }

        // 悬停高亮：每帧从当前悬停集重建（追加到 overlay 顶点），不触发场景重建。
        BuildHoverHighlight();
    }

    // 悬停高亮绘制：仅对当前悬停且未被选中的实体描边（选中态已是选中色）。
    // 悬停集通常只有 1 个实体，开销可忽略。
    void Editor::BuildHoverHighlight()
    {
        if (!m_doc) return;

        const auto& hovered = m_picking.GetHovered();
        if (hovered.empty()) return;

        const auto& selection = m_picking.GetSelection();
        auto& scene = m_doc->GetScene();

        HoverHighlightSink sink(m_overlayVertices, IDrawSink::kHoverColor);

        for (auto id : hovered)
        {
            if (selection.contains(id)) continue;  // 已选中：保持选中色

            auto* obj = scene.GetEntity(id);
            if (!obj || !obj->IsKindOf<Entity>()) continue;

            const auto& entity = static_cast<const Entity&>(*obj);

            // 隐藏图层上的实体不参与悬停高亮
            if (!entity.GetAttr().Visible) continue;
            if (const Layer* layer = scene.GetLayerManager().GetLayer(entity.GetLayerID());
                layer && !layer->IsVisible())
                continue;

            entity.Draw(sink, /*isSelected*/ false, /*isHovered*/ true);

            // 文字类实体：字形不输出线段，用包围盒勾边作为悬停反馈
            if (obj->IsKindOf<TextEntity>() || obj->IsKindOf<MTextEntity>())
            {
                AABB bb = entity.GetBoundingBox();
                Math::Point3 p0{ bb.Min.x, bb.Min.y, bb.Min.z };
                Math::Point3 p1{ bb.Max.x, bb.Min.y, bb.Min.z };
                Math::Point3 p2{ bb.Max.x, bb.Max.y, bb.Min.z };
                Math::Point3 p3{ bb.Min.x, bb.Max.y, bb.Min.z };
                sink.DrawLine(p0, p1, IDrawSink::kHoverColor, false);
                sink.DrawLine(p1, p2, IDrawSink::kHoverColor, false);
                sink.DrawLine(p2, p3, IDrawSink::kHoverColor, false);
                sink.DrawLine(p3, p0, IDrawSink::kHoverColor, false);
            }
        }
    }

    // 矢量字体解析回调（场景流与选中流共用）
    static FontResolver MakeFontResolver(Document* doc)
    {
        return MakeTextStyleResolver(doc->GetFontSystem(), &doc->GetScene().GetTextStyleTable());
    }

    bool Editor::UpdateSceneVertices(double worldPerPixel, bool zoomChanged)
    {
        auto& scene = m_doc->GetScene();

        // 拖动跟随中的实体走选中流（每帧小量重建），场景流将其排除。
        std::unordered_set<Object::ObjectID> dragIds;
        if (m_gripEditor.IsDragging())
            for (auto id : m_gripEditor.GetDraggingIDs())
                dragIds.insert(id);

        const bool exclusionChanged = (dragIds != m_sceneExcluded);

        bool need;
        if (!dragIds.empty() && !exclusionChanged)
        {
            // 拖动期间 GripEditor 每帧 MarkDirty（实时几何变化），但被拖实体
            // 已不在场景流中，场景流无需重建；仅缩放+线宽场合仍需重建。
            need = zoomChanged && m_sceneHasLineweight;
        }
        else
        {
            need = scene.IsDirty() || exclusionChanged ||
                   (zoomChanged && m_sceneHasLineweight);
        }

        if (!need) return false;

        // 图像按文档所在目录解析相对路径
        {
            std::string dir;
            if (m_doc->HasPath())
                dir = std::filesystem::path(std::u8string(m_doc->GetPath().begin(), m_doc->GetPath().end()))
                          .parent_path().string();
            m_imageLibrary.SetBaseDir(dir);
        }
        const ImageProvider imageProvider = [this](const std::string& p) { return m_imageLibrary.Get(p); };

        // 全局脏（图层颜色/线型表等影响所有实体）或缩放变化（线宽/虚线按
        // 屏幕像素烘焙进世界顶点）时所有实体缓存整体失效。
        const bool invalidateAll = scene.IsAllDirty() || zoomChanged;

        // ── 纯新增快路径 ────────────────────────────────────
        // 几万实体的场景里新画一个图形:旧实体缓存全部有效,既不该重新细分,
        // 也不该把全场景顶点重新拼接一遍。脏集全部是"缓存中不存在的新实体"
        // 时,只细分新实体并追加到现有顶点流尾部(新实体总在实体链表末尾,
        // 与全量重建的绘制序一致)。修改/删除(脏 ID 已在缓存)仍走全量拼接。
        if (!invalidateAll && dragIds.empty() && m_sceneExcluded.empty())
        {
            bool pureAppend = !scene.GetDirtyEntities().empty();
            for (auto id : scene.GetDirtyEntities())
            {
                const Object* obj = scene.GetEntity(id);
                if (!obj || !obj->IsKindOf<Entity>() || m_entityVertexCache.count(id))
                {
                    pureAppend = false;
                    break;
                }
            }

            if (pureAppend)
            {
                // ID 单调分配 = 加入场景的先后序,按升序追加保持绘制序稳定
                std::vector<Object::ObjectID> ids(scene.GetDirtyEntities().begin(),
                                                  scene.GetDirtyEntities().end());
                std::sort(ids.begin(), ids.end());

                const auto& layerMgr = scene.GetLayerManager();
                DrawContext ctx(m_scratchLines, m_scratchFills, m_scratchTexts, m_overlay,
                                layerMgr, scene.GetLineTypeTable(), worldPerPixel,
                                m_glyphProvider, MakeFontResolver(m_doc));
                ctx.SetImageProvider(imageProvider);

                for (auto id : ids)
                {
                    const auto& entity = static_cast<const Entity&>(*scene.GetEntity(id));
                    const auto& attr   = entity.GetAttr();

                    if (!attr.Visible) continue;
                    if (const Layer* layer = layerMgr.GetLayer(attr.LayerId);
                        layer && !layer->IsVisible())
                        continue;

                    m_scratchLines.clear();
                    m_scratchFills.clear();
                    m_scratchTexts.clear();
                    ctx.ResetLineweightFlag();

                    ctx.BeginEntity(&attr);
                    entity.Draw(ctx, /*isSelected*/ false, false);
                    ctx.BeginEntity(nullptr);

                    EntityVertexCache c;
                    c.lines         = m_scratchLines;
                    c.fills         = m_scratchFills;
                    c.texts         = m_scratchTexts;
                    c.hasLineweight = ctx.HasLineweightGeometry();
                    c.wipes         = ctx.TakeWipes();
                    c.images        = ctx.TakeImages();

                    ApplyWipes(c.wipes);
                    m_sceneImages.insert(m_sceneImages.end(), c.images.begin(), c.images.end());
                    m_sceneVertices.insert(m_sceneVertices.end(), c.lines.begin(), c.lines.end());
                    m_sceneFillVertices.insert(m_sceneFillVertices.end(), c.fills.begin(), c.fills.end());
                    m_textVertices.insert(m_textVertices.end(), c.texts.begin(), c.texts.end());
                    m_sceneHasLineweight |= c.hasLineweight;

                    m_entityVertexCache.emplace(id, std::move(c));
                }

                ++m_sceneVersion;   // 顶点流已追加,渲染后端需重新上传
                scene.ClearDirty();
                return true;
            }
        }

        // ── 逐实体顶点缓存失效（全量拼接路径）────────────────
        // 全局失效时清空全部缓存;否则只丢弃脏实体的缓存,文字排版/标注
        // 绘制等重活只发生在真正变化的实体上。
        if (invalidateAll)
            m_entityVertexCache.clear();
        else
            for (auto id : scene.GetDirtyEntities())   // 含已删除实体：顺带清理
                m_entityVertexCache.erase(id);

        m_sceneVertices.clear();
        m_sceneFillVertices.clear();
        m_textVertices.clear();
        m_sceneImages.clear();

        const auto& layerMgr = scene.GetLayerManager();

        DrawContext ctx(m_scratchLines, m_scratchFills, m_scratchTexts, m_overlay,
                        layerMgr, scene.GetLineTypeTable(), worldPerPixel,
                        m_glyphProvider, MakeFontResolver(m_doc));
        ctx.SetImageProvider(imageProvider);

        bool hasLineweight = false;

        scene.ForEachObject([&](const Object& obj)
        {
           if (obj.IsKindOf<Entity>())
           {
               const auto& entity = static_cast<const Entity&>(obj);
               const auto& attr   = entity.GetAttr();

               // 图层关闭或实体自身隐藏:不产出顶点
               if (!attr.Visible) return;
               if (const Layer* layer = layerMgr.GetLayer(attr.LayerId);
                   layer && !layer->IsVisible())
                   return;

               // 拖动跟随中的实体由选中流绘制
               if (dragIds.contains(obj.GetID())) return;

               // 选中/悬停高亮均不烘焙进场景顶点（选中走选中流、悬停走 overlay），
               // 因此选择集/悬停变化都不会触发整场景顶点重建。
               auto it = m_entityVertexCache.find(obj.GetID());
               if (it == m_entityVertexCache.end())
               {
                   // 缓存未命中：重新细分该实体
                   m_scratchLines.clear();
                   m_scratchFills.clear();
                   m_scratchTexts.clear();
                   ctx.ResetLineweightFlag();

                   ctx.BeginEntity(&attr);
                   entity.Draw(ctx, /*isSelected*/ false, false);
                   ctx.BeginEntity(nullptr);

                   EntityVertexCache c;
                   c.lines         = m_scratchLines;
                   c.fills         = m_scratchFills;
                   c.texts         = m_scratchTexts;
                   c.hasLineweight = ctx.HasLineweightGeometry();
                   c.wipes         = ctx.TakeWipes();
                   c.images        = ctx.TakeImages();
                   it = m_entityVertexCache.emplace(obj.GetID(), std::move(c)).first;
               }

               const auto& c = it->second;
               ApplyWipes(c.wipes);
               m_sceneImages.insert(m_sceneImages.end(), c.images.begin(), c.images.end());
               m_sceneVertices.insert(m_sceneVertices.end(), c.lines.begin(), c.lines.end());
               m_sceneFillVertices.insert(m_sceneFillVertices.end(), c.fills.begin(), c.fills.end());
               m_textVertices.insert(m_textVertices.end(), c.texts.begin(), c.texts.end());
               hasLineweight |= c.hasLineweight;
           }
        });

        m_sceneHasLineweight = hasLineweight;
        m_sceneExcluded      = std::move(dragIds);

        ++m_sceneVersion;   // 通知渲染后端：场景顶点已变化，需重新上传 GPU 缓冲

        scene.ClearDirty();
        return true;
    }

    void Editor::ApplyWipes(const std::vector<std::vector<Math::Point3>>& wipes)
    {
        for (const auto& w : wipes)
        {
            const WipeClip::Poly poly(w);
            if (!poly.Valid()) continue;
            WipeClip::ClipLines(m_sceneVertices, poly);
            WipeClip::ClipFills(m_sceneFillVertices, poly);
            WipeClip::ClipTexts(m_textVertices, poly);
        }
    }

    // 选中流：选中实体 + 拖动跟随中的实体，按选中态绘制，叠加在场景之上。
    // 与悬停高亮同思路：覆盖绘制代替烘焙，选中变化不触发全场景重建。
    void Editor::UpdateSelectionVertices(double worldPerPixel, bool force)
    {
        if (!force) return;

        auto& scene = m_doc->GetScene();

        m_selVertices.clear();
        m_selFillVertices.clear();
        m_selTextVertices.clear();

        const auto& selection = m_picking.GetSelection();
        const auto  dragIds   = m_gripEditor.GetDraggingIDs();

        if (!selection.empty() || !dragIds.empty())
        {
            const auto& layerMgr = scene.GetLayerManager();

            DrawContext ctx(m_selVertices, m_selFillVertices, m_selTextVertices, m_overlay,
                            layerMgr, scene.GetLineTypeTable(), worldPerPixel,
                            m_glyphProvider, MakeFontResolver(m_doc));

            auto drawOne = [&](Object::ObjectID id)
            {
                auto* obj = scene.GetEntity(id);
                if (!obj || !obj->IsKindOf<Entity>()) return;

                const auto& entity = static_cast<const Entity&>(*obj);
                const auto& attr   = entity.GetAttr();

                if (!attr.Visible) return;
                if (const Layer* layer = layerMgr.GetLayer(attr.LayerId);
                    layer && !layer->IsVisible())
                    return;

                ctx.BeginEntity(&attr);
                entity.Draw(ctx, /*isSelected*/ true, false);
                ctx.BeginEntity(nullptr);
            };

            for (auto id : selection)
                drawOne(id);
            for (auto id : dragIds)
                if (!selection.contains(id))
                    drawOne(id);

            m_selHasLineweight = ctx.HasLineweightGeometry();
        }
        else
        {
            m_selHasLineweight = false;
        }

        ++m_selVersion;
        m_picking.ClearDirty();
    }

    ViewState Editor::BuildViewState()
    {
        auto& scene = m_doc->GetScene();

        ViewState vs;

        vs.Scene        = m_sceneVertices;
        vs.SceneFill    = m_sceneFillVertices;
        vs.Overlay      = m_overlayVertices;
        vs.TextScene    = m_textVertices;
        vs.Images       = m_sceneImages;
        vs.SceneVersion = m_sceneVersion;
        vs.SelScene     = m_selVertices;
        vs.SelFill      = m_selFillVertices;
        vs.SelText      = m_selTextVertices;
        vs.SelVersion   = m_selVersion;
        vs.FontTexture  = m_fontTexture;

        vs.Selection.Active = m_picking.IsBoxSelecting();
        vs.Selection.Start  = m_picking.GetBoxStart();
        vs.Selection.End    = m_picking.GetBoxEnd();

        vs.MouseX    = static_cast<float>(m_mouseX);
        vs.MouseY    = static_cast<float>(m_mouseY);

        vs.ShowGrid  = true;

        vs.Snap.SnapType = static_cast<SnapDraw::Type>(m_currentSnap.SnapType);
        vs.Snap.Pos      = m_viewport->GetCamera().WorldToScreen(m_currentSnap.WorldPos);
        if (!IsActiveTool())
        {
            m_currentSnap = {};
        }

        vs.ShowCurrorBox = !IsActiveTool();

        vs.ShowGizmo = true;
        m_gripVertices.clear();
        if (vs.ShowGizmo)
        {
            auto& hoveredIdxs = m_gripEditor.HoveredGrips();
            auto& grips       = m_gripEditor.GetGrips();

            for (int i = 0; i < (int)grips.size(); ++i)
            {
                const auto& g    = grips[i];
                auto        s    = m_viewport->GetCamera().WorldToScreen(g.WorldPos);
                auto        type = static_cast<GripDraw::Type>(g.GripType);

                bool hovered = std::find(hoveredIdxs.begin(), hoveredIdxs.end(), i) != hoveredIdxs.end();

                m_gripVertices.push_back({ s, type, hovered });
            }
        }
        vs.Grips = m_gripVertices;

        return vs;
    }

}
