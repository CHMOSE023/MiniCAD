#include "Editor.h"
#include "Editor/EditorContext.h"
#include <atomic>
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
#include <chrono>
#include <cmath>
#include <cstdlib>
#include "Core/Log.h"

#ifdef MINICAD_WEB
#include <emscripten.h>
#endif

// ── 绘制工具 ──────────────────────────────────────────────────
#include "Editor/Tools/LineTool.h"
#include "Editor/Tools/ZoomWindowTool.h"
#include "Editor/Tools/PanTool.h"
#include "Document/Command/ZoomViewCommand.h"
#include "Editor/Tools/PointTool.h"
#include "Editor/Tools/SolidTool.h"
#include "Editor/Tools/WipeoutTool.h"
#include "Editor/Tools/MLineTool.h"
#include "Editor/Tools/MLineEditTool.h"
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
#include "Editor/Tools/SolidTools.h"
#include "Core/Entity/Solid3DEntity.hpp"
#include "Core/Entity/MeshEntity.hpp"
#include "Core/Entity/ViewportEntity.hpp"
#include "Core/Entity/MLineEntity.hpp"
#include "Core/Draw/TransformDrawSink.hpp"

#include <cstdio>
#include <memory>
#include <algorithm>

namespace MiniCAD
{
    namespace
    {
        // 节点图预览的线：实体 Draw() 输出的线段按各自的颜色写入顶点（填充三角形不画）
        class PreviewLineSink : public IDrawSink
        {
        public:
            explicit PreviewLineSink(std::vector<Vertex_P3_C4>& out) : m_out(out) {}
            void DrawLine(const Math::Point3& a, const Math::Point3& b, const Math::Color4& c, bool) override
            {
                const VertexColor color(Math::Float4{ static_cast<float>(c.r), static_cast<float>(c.g), static_cast<float>(c.b), 1.0f });
                m_out.push_back({ { static_cast<float>(a.x), static_cast<float>(a.y), static_cast<float>(a.z) }, color });
                m_out.push_back({ { static_cast<float>(b.x), static_cast<float>(b.y), static_cast<float>(b.z) }, color });
            }

        private:
            std::vector<Vertex_P3_C4>& m_out;
        };

        // 高亮（选中 / 悬停）里的点图元：高亮流只有线段，点画成按当前缩放 ±2 像素的小十字
        void AppendPointMark(std::vector<Vertex_P3_C4>& out, const Math::Float3& p, const VertexColor& color, double worldPerPixel)
        {
            const float d = static_cast<float>(2.0 * worldPerPixel);
            out.push_back({ { p.x - d, p.y, p.z }, color });
            out.push_back({ { p.x + d, p.y, p.z }, color });
            out.push_back({ { p.x, p.y - d, p.z }, color });
            out.push_back({ { p.x, p.y + d, p.z }, color });
        }

        // 悬停高亮收集器：把实体 Draw() 输出的几何以固定高亮色直接写入 overlay 顶点缓冲。
        // 用于每帧重建“当前悬停实体”的高亮，无需触碰整场景顶点。
        class HoverHighlightSink : public IDrawSink
        {
        public:
            HoverHighlightSink(std::vector<Vertex_P3_C4>& out, const Math::Color4& color, const LayerManager& layers)
                : m_out(out), m_color(color), m_layers(layers) {}

            // 点样式与场景流一致（POINT 的 $PDMODE/$PDSIZE），否则悬停时 ⊕ 等图形不出高亮
            void SetPointStyle(int mode, double size, double worldPerPixel, double viewportHeightPx)
            {
                m_pointMode = mode;  m_pointSize = size;
                m_worldPerPixel = worldPerPixel;  m_viewportHeightPx = viewportHeightPx;
                m_hasPointStyle = true;
            }
            bool GetPointStyle(int& mode, double& size) const override
            {
                mode = m_pointMode;  size = m_pointSize;
                return m_hasPointStyle;
            }
            double GetWorldPerPixel() const override    { return m_worldPerPixel; }
            double GetViewportHeightPx() const override { return m_viewportHeightPx; }
            std::string GetLayerName(LayerID layerId) const override
            {
                const Layer* layer = m_layers.GetLayer(layerId);
                return layer ? layer->GetName() : std::string();
            }
            void DrawPoint(const Math::Point3& p, const Math::Color4&, bool) override
            {
                if (m_worldPerPixel > 0.0)
                    AppendPointMark(m_out, V(p).pos, V(p).color, m_worldPerPixel);
            }

            // 块内关闭 / 冻结图层上的子实体不高亮
            bool IsLayerVisible(LayerID layerId) const override
            {
                const Layer* layer = m_layers.GetLayer(layerId);
                return !layer || layer->IsVisible();
            }

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
            int    m_pointMode = 0;
            double m_pointSize = 0.0;
            double m_worldPerPixel = 0.0;
            double m_viewportHeightPx = 0.0;
            bool   m_hasPointStyle = false;

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
            const LayerManager&        m_layers;
        };
    }

    // ─────────────────────────────────────────────────────────────
    //  构造
    // ─────────────────────────────────────────────────────────────
    Editor::Editor()
    {
        RegisterBuiltinTools();
    }

    uint64_t Editor::NextRenderVersion()
    {
        static std::atomic<uint64_t> counter{ 0 };
        return ++counter;
    }

    static std::atomic<bool> s_sceneAppendOnGpu{ false };

    void Editor::SetSceneAppendOnGpu(bool enabled)
    {
        s_sceneAppendOnGpu = enabled;
    }

    void Editor::CopySettingsFrom(const Editor& o)
    {
        m_thinLines          = o.m_thinLines;
        m_visualStyle        = o.m_visualStyle;
        m_hatchDialogEnabled = o.m_hatchDialogEnabled;
        m_solidKernel        = o.m_solidKernel;
        m_viewAnimSeconds    = o.m_viewAnimSeconds;
        m_fontTexture        = o.m_fontTexture;
        m_glyphProvider      = o.m_glyphProvider;
        m_oleRasterizer      = o.m_oleRasterizer;
        m_textHeight         = o.m_textHeight;
        m_hatchSettings      = o.m_hatchSettings;
        m_mlineSettings      = o.m_mlineSettings;
        m_filletRadius       = o.m_filletRadius;
        m_chamferD1          = o.m_chamferD1;
        m_chamferD2          = o.m_chamferD2;
        m_solidFilletRadius    = o.m_solidFilletRadius;
        m_solidChamferDistance = o.m_solidChamferDistance;
        m_snap.SetEnableSnap(o.m_snap.IsEnabled());
        m_snap.SetSnapModes(o.m_snap.GetSnapModes());
        m_snap.SetSnapRadiusPx(o.m_snap.GetSnapRadiusPx());
        m_constraintEngine.EnableOrtho(o.m_constraintEngine.IsOrthoEnabled());
        m_constraintEngine.EnablePolar(o.m_constraintEngine.IsPolarEnabled());
        m_picking.SetHoverEnabled(o.m_picking.IsHoverEnabled());
    }

    // ─────────────────────────────────────────────────────────────
    //  Bind / Unbind
    // ─────────────────────────────────────────────────────────────
    void Editor::Bind(Document& doc, Viewport& viewport)
    {
        Unbind();
        m_viewAnim.Active = false;

        m_doc      = &doc;
        m_viewport = &viewport;

        auto& scene    = doc.GetScene();
        auto& cmdStack = doc.GetCommandStack();
         
        m_overlay.Bind(viewport);
        m_previewOverlay.Bind(viewport);
        ClearPreviewGeometry();
        m_picking.Bind(scene, viewport);
        // 拾取侧与渲染侧用**同一个**布局过滤（Document::IsVisibleInActiveLayout）：
        // 少了这一条，布局页上属于别的布局的图纸空间实体画不出来却点得中 ——
        // 用户报的「布局页视口外还能选中隐藏的图元」
        m_picking.SetLayoutFilter([this](const EntityAttr& attr)
        {
            return m_doc->IsVisibleInActiveLayout(attr);
        });
        // 捕捉同理：布局页上看不见的模型实体不能被捕捉到
        m_snap.SetLayoutFilter([this](const EntityAttr& attr)
        {
            return !m_doc || m_doc->IsVisibleInActiveLayout(attr);
        });
        m_gripEditor.Bind(viewport, scene, cmdStack, m_picking, m_overlay);

        scene.MarkDirty();
		m_picking.MarkDirty();
        m_picking.ClearSelection();
        m_gripEditor.MarkDirty();
        m_overlay.Clear();

    }

    void Editor::SetViewport(Viewport& viewport)
    {
        if (m_viewport == &viewport)
            return;
        if (m_tool)
        {
            m_tool->Cancel();
            m_tool.reset();
        }
        m_toolSuspended    = false;
        m_pendingToolReset = false;
        m_viewAnim.Active  = false;
        m_currentSnap      = {};
        m_overlay.Clear();
        m_viewport = &viewport;
        m_overlay.Bind(viewport);
        m_previewOverlay.Bind(viewport);
        ClearPreviewGeometry();
        m_picking.SetViewport(viewport);
        m_gripEditor.SetViewport(viewport);
        // 新视口的缩放与原来的不同：Render 比较 worldPerPixel 时会重建动态流与随视图变化的选中流
    }

    void Editor::EnsureSceneCached(const IRenderer& renderer)
    {
        if (!m_sceneStreamsReleased || m_sceneRestitch)
            return;
        if (!Viewport::SceneUploaded(renderer, m_sceneVersion, m_sceneGlyphVersion, m_fontTexture != nullptr,
                                     !m_glyphSorted.empty(), m_glyphLibrary.View().Version))
            m_sceneRestitch = true;
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

        // 释放上一个文档的显示数据（大图纸可达数百 MB）。
        // vector = {} 调用 initializer_list 赋值，仍保留容量；与空容器交换才归还存储。
        // 重新绑定时 Bind 会整体标脏，这些缓存本来也要全部重建
        auto releaseStorage = []<typename T>(T& container) { T{}.swap(container); };
        releaseStorage(m_entityVertexCache);
        m_sceneStreamsReleased = false;
        m_sceneRestitch        = false;
        m_sceneStats           = {};
        m_sceneAppendActive    = false;
        releaseStorage(m_sceneAppendLines);
        releaseStorage(m_sceneAppendFills);
        releaseStorage(m_sceneAppendPoints);
        releaseStorage(m_scenePatches);
        releaseStorage(m_scenePatchLines);
        releaseStorage(m_scenePatchFills);
        releaseStorage(m_scenePatchPoints);
        releaseStorage(m_sceneWipeBoxes);
        releaseStorage(m_glyphSorted);
        releaseStorage(m_sceneVertices);
        releaseStorage(m_sceneFillVertices);
        releaseStorage(m_sceneDynVertices);
        releaseStorage(m_sceneDynFillVertices);
        releaseStorage(m_dynOrder);
        releaseStorage(m_textVertices);
        releaseStorage(m_glyphInstances);
        releaseStorage(m_glyphBatches);
        releaseStorage(m_scratchGlyphs);
        m_glyphLibrary.Clear();   // 字形网格按字体的字形对象区分，字体属于文档
        releaseStorage(m_sceneImages);
        releaseStorage(m_selVertices);
        releaseStorage(m_selFillVertices);
        releaseStorage(m_selTextVertices);
        releaseStorage(m_selDynVertices);
        releaseStorage(m_selDynFillVertices);
        releaseStorage(m_selDynTextVertices);
        releaseStorage(m_scratchLines);
        releaseStorage(m_scratchFills);
        releaseStorage(m_scratchTexts);
        releaseStorage(m_overlayVertices);
        releaseStorage(m_hoverCache);
        releaseStorage(m_gripVertices);
        releaseStorage(m_sceneExcluded);
        m_sceneViewDependent = false;
        m_selViewDependent   = false;
        m_build = {};
        m_sceneVersion    = NextRenderVersion();   // 渲染后端的缓存顶点缓冲随之失效
        m_sceneDynVersion = NextRenderVersion();
        m_sceneGlyphVersion = NextRenderVersion();
        m_selVersion      = NextRenderVersion();
        m_picking.Unbind();

        m_doc      = nullptr;
        m_viewport = nullptr;
    }

    void Editor::SetThinLines(bool thin)
    {
        if (m_thinLines == thin) return;
        m_thinLines      = thin;
        m_displayInvalid = true;   // 线宽几何全部重新细分
    }

    // ─────────────────────────────────────────────────────────────
    //  RegisterBuiltinTools
    // ─────────────────────────────────────────────────────────────
    void Editor::RegisterBuiltinTools()
    {
        // ── 绘制工具 ──────────────────────────────────────────
        RegisterTool("Line",      [] { return std::make_unique<LineTool>();      });
        RegisterTool("ZoomWindow", [] { return std::make_unique<ZoomWindowTool>(); });   // ZOOM E：窗口缩放
        RegisterTool("Pan",       [] { return std::make_unique<PanTool>();       });   // PAN：左键拖动平移
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
        RegisterTool("MLineEdit", [] { return std::make_unique<MLineEditTool>(); });   // MLEDIT
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

        // ── 三维实体（需要内核，见 ActivateToolById）──────────────
        RegisterTool("Box", [this]() -> std::unique_ptr<ITool> {
            auto env = MakeSolidToolEnv();
            return env ? std::make_unique<BoxTool>(std::move(*env)) : nullptr;
        });
        RegisterTool("Cylinder", [this]() -> std::unique_ptr<ITool> {
            auto env = MakeSolidToolEnv();
            return env ? std::make_unique<RoundSolidTool>(RoundSolidTool::Kind::Cylinder, std::move(*env)) : nullptr;
        });
        RegisterTool("Sphere", [this]() -> std::unique_ptr<ITool> {
            auto env = MakeSolidToolEnv();
            return env ? std::make_unique<RoundSolidTool>(RoundSolidTool::Kind::Sphere, std::move(*env)) : nullptr;
        });
        RegisterTool("Extrude", [this]() -> std::unique_ptr<ITool> {   // 预选闭合对象
            auto env = MakeSolidToolEnv();
            std::vector<Object::ObjectID> ids = EditableSelectionIds();
            if (!env || ids.empty())
                return nullptr;
            return std::make_unique<ExtrudeTool>(std::move(ids), std::move(*env));
        });
        RegisterTool("SolidSection", [this]() -> std::unique_ptr<ITool> {   // 预选三维实体
            auto env = MakeSolidToolEnv();
            std::vector<Object::ObjectID> ids = EditableSelectionIds();
            if (!env || ids.empty())
                return nullptr;
            return std::make_unique<SectionTool>(std::move(ids), std::move(*env));
        });
        RegisterTool("Flatshot", [this]() -> std::unique_ptr<ITool> {       // 预选三维实体
            auto env = MakeSolidToolEnv();
            std::vector<Object::ObjectID> ids = EditableSelectionIds();
            if (!env || ids.empty())
                return nullptr;
            return std::make_unique<FlatshotTool>(std::move(ids), std::move(*env));
        });
        RegisterTool("Revolve", [this]() -> std::unique_ptr<ITool> {        // 预选闭合对象
            auto env = MakeSolidToolEnv();
            std::vector<Object::ObjectID> ids = EditableSelectionIds();
            if (!env || ids.empty())
                return nullptr;
            return std::make_unique<RevolveTool>(std::move(ids), std::move(*env));
        });
        RegisterTool("Sweep", [this]() -> std::unique_ptr<ITool> {          // 预选闭合对象，再点选路径
            auto env = MakeSolidToolEnv();
            std::vector<Object::ObjectID> ids = EditableSelectionIds();
            if (!env || ids.empty())
                return nullptr;
            return std::make_unique<SweepTool>(std::move(ids), std::move(*env));
        });
        RegisterTool("FilletEdge", [this]() -> std::unique_ptr<ITool> {     // 自带点选边，无需预选
            auto env = MakeSolidToolEnv();
            return env ? std::make_unique<SolidEdgeTool>(SolidEdgeTool::Kind::Fillet, &m_solidFilletRadius, std::move(*env)) : nullptr;
        });
        RegisterTool("ChamferEdge", [this]() -> std::unique_ptr<ITool> {
            auto env = MakeSolidToolEnv();
            return env ? std::make_unique<SolidEdgeTool>(SolidEdgeTool::Kind::Chamfer, &m_solidChamferDistance, std::move(*env)) : nullptr;
        });

        // ── 快捷键绑定 ────────────────────────────────────────
        RegisterAlias("P",      "Previous");
        RegisterAlias("L",      "Line");
        RegisterAlias("LI",     "Line");
        RegisterAlias("REC",    "Rectangle");
        RegisterAlias("PL",     "Polyline");
        RegisterAlias("PLINE",  "Polyline");    // AutoCAD 命令全名与工具名不同的，全名也走别名表
        RegisterAlias("RECTANG", "Rectangle");
        RegisterAlias("DTEXT",  "Text");
        RegisterAlias("DIMLINEAR",  "Dimension");
        RegisterAlias("DIMALIGNED", "Dimension");
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
        RegisterAlias("BOX",      "Box");
        RegisterAlias("CYLINDER", "Cylinder");
        RegisterAlias("CYL",      "Cylinder");
        RegisterAlias("SPHERE",   "Sphere");
        RegisterAlias("EXTRUDE",  "Extrude");
        RegisterAlias("EXT",      "Extrude");
        RegisterAlias("SOLSECTION", "SolidSection");
        RegisterAlias("SECTION",    "SolidSection");
        RegisterAlias("SEC",        "SolidSection");
        RegisterAlias("FLATSHOT",   "Flatshot");
        RegisterAlias("FSHOT",      "Flatshot");
        RegisterAlias("REVOLVE",     "Revolve");
        RegisterAlias("REV",         "Revolve");
        RegisterAlias("SWEEP",       "Sweep");
        RegisterAlias("FILLETEDGE",  "FilletEdge");
        RegisterAlias("CHAMFEREDGE", "ChamferEdge");
        RegisterAlias("Z",      "Zoom");
        RegisterAlias("PAN",    "Pan");
        RegisterAlias("ZOOMALL", "ZoomAll");
        RegisterAlias("PLAN",      "ViewTop");     // 视图与视觉样式是特例命令(非工具),全名也走别名表
        for (const char* view : { "ViewTop", "ViewBottom", "ViewFront", "ViewBack", "ViewLeft", "ViewRight",
                                  "ViewSWIso", "ViewSEIso", "ViewNEIso", "ViewNWIso" })
        {
            std::string upper = view;
            std::transform(upper.begin(), upper.end(), upper.begin(), [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
            RegisterAlias(upper, view);
        }
        RegisterAlias("SHADE",     "Shade");
        RegisterAlias("WIREFRAME", "Wireframe");
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
        RegisterAlias("MLS",    "MLStyle");  // 多线样式（同 AutoCAD 的 MLSTYLE）
        RegisterAlias("MLSTYLE","MLStyle");
        RegisterAlias("MLEDIT", "MLineEdit");   // 多线编辑（同 AutoCAD 的 MLEDIT）
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
        names.push_back("MLStyle");  // 特例命令(非工具):多线样式
        names.push_back("Erase");    // 特例命令(非工具):删除选择集
        names.push_back("SelectAll");   // 特例命令(非工具):全选
        names.push_back("Zoom");        // 特例命令(非工具):视图缩放（A 全部 / E 窗口）
        names.push_back("Explode");     // 特例命令(非工具):分解
        for (const char* view : { "ViewTop", "ViewBottom", "ViewFront", "ViewBack", "ViewLeft", "ViewRight",
                                  "ViewSWIso", "ViewSEIso", "ViewNEIso", "ViewNWIso", "Shade", "Wireframe" })
            names.push_back(view);      // 特例命令(非工具):标准视图、视觉样式（M6）
        std::sort(names.begin(), names.end());
        return names;
    }

    void Editor::ZoomAll(bool undoable)
    {
        if (!m_doc || !m_viewport)
            return;
        AABB box = AABB::Empty();
        bool any = m_doc->GetScene().GetExtents(box);
        if (AABB preview; GetPreviewBounds(preview))   // 节点图预览也算在全图范围内
        {
            box.Expand(preview);
            any = true;
        }
        if (!any)
        {
            m_commandLine.Echo("图形中没有可显示的对象");
            return;
        }
        Camera& cam = m_viewport->GetCamera();
        const CameraState before = cam.GetState();
        cam.ZoomToBounds(box);   // 俯视时只按 XY；三维视图按视图方向投影
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

        // 标准视图与视觉样式（M6，特例命令）
        static const std::pair<const char*, StandardView> kViews[] = {
            { "ViewTop", StandardView::Top }, { "ViewBottom", StandardView::Bottom }, { "ViewFront", StandardView::Front },
            { "ViewBack", StandardView::Back }, { "ViewLeft", StandardView::Left }, { "ViewRight", StandardView::Right },
            { "ViewSWIso", StandardView::SWIso }, { "ViewSEIso", StandardView::SEIso }, { "ViewNEIso", StandardView::NEIso },
            { "ViewNWIso", StandardView::NWIso } };
        for (const auto& [name, view] : kViews)
            if (toolId == name)
            {
                m_commandLine.Echo(std::string("命令: ") + name);
                SetStandardView(view);
                m_lastCommand = name;
                return;
            }
        if (toolId == "Shade" || toolId == "Wireframe")
        {
            const bool shaded = toolId == "Shade";
            SetVisualStyle(shaded ? VisualStyle::Shaded : VisualStyle::Wireframe);
            m_commandLine.Echo(shaded ? "视觉样式：着色" : "视觉样式：线框");
            m_lastCommand = toolId;
            return;
        }

        if (toolId == "Zoom")          // 只输入 ZOOM（别名 Z、补全到全名后空格 / 回车）：等待下一行输入选项
        {
            m_commandLine.Echo("命令: ZOOM");
            m_zoomPending = true;
            return;
        }

        if ((toolId == "Box" || toolId == "Cylinder" || toolId == "Sphere" || toolId == "Extrude"
             || toolId == "SolidSection" || toolId == "Flatshot" || toolId == "Revolve" || toolId == "Sweep"
             || toolId == "FilletEdge" || toolId == "ChamferEdge") && !m_solidKernel)
        {
            m_commandLine.Echo(toolId + ": 三维建模不可用（此版本没有实体内核）");
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

        if (toolId == "MLStyle")  // 特例命令:多线样式对话框（同 AutoCAD 的 MLSTYLE）
        {
            if (!m_doc)
                return;
            m_mlineStyleRequest.Active = true;
            m_commandLine.Echo("命令: MLSTYLE");
            m_lastCommand = "MLStyle";
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
            // 命令已按实体标脏（移除原实体、加入块参照），不整体标脏（那会让整张图重新细分）
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

    std::unique_ptr<Entity> SolidToolDetail::MakeLineEntity(Object::ObjectID id, const Math::Point3& a, const Math::Point3& b)
    {
        return std::make_unique<LineEntity>(id, a, b);
    }

    std::optional<std::pair<Math::Point3, Math::Point3>> SolidToolDetail::LineEndpoints(const Entity& e)
    {
        if (!e.IsKindOf<LineEntity>())
            return std::nullopt;
        const auto& l = static_cast<const LineEntity&>(e).GetLine();
        return std::make_pair(l.Start, l.End);
    }

    std::optional<SolidToolEnv> Editor::MakeSolidToolEnv()
    {
        if (!m_doc || !m_solidKernel)
            return std::nullopt;
        return SolidToolEnv{ m_doc->GetScene(), m_doc->GetCommandStack(), m_picking, m_overlay, *m_solidKernel,
                             [this](const std::string& text) { m_commandLine.Echo(text); } };
    }

    // ── 三维视图（M6）──────────────────────────────────────────────
    static double NowSeconds()
    {
        return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
    }

    void Editor::SetVisualStyle(VisualStyle style)
    {
        if (style == m_visualStyle)
            return;
        m_visualStyle = style;
        m_shadedValid = false;   // 下一帧重建着色面
    }

    void Editor::SetStandardView(StandardView view)
    {
        if (!m_viewport)
            return;
        Camera probe = m_viewport->GetCamera();
        probe.SetStandardView(view);
        SetViewDirection(probe.GetAzimuth(), probe.GetElevation());
    }

    void Editor::SetViewDirection(double azimuthDeg, double elevationDeg)
    {
        if (!m_doc || !m_viewport)
            return;
        if (m_doc->ActiveLayoutIsPaper())       // 布局是二维图纸，只有俯视
        {
            m_commandLine.Echo("布局中不能切换三维视图");
            return;
        }
        Camera& cam = m_viewport->GetCamera();
        const CameraState before = cam.GetState();
        cam.SetView(azimuthDeg, elevationDeg);
        AABB box = AABB::Empty(), preview;
        const bool hasScene   = m_doc->GetScene().GetExtents(box);
        const bool hasPreview = GetPreviewBounds(preview);   // 节点图预览也算在全图范围内
        if (hasPreview)
            box.Expand(preview);
        if (hasScene || hasPreview)
            cam.ZoomToBounds(box);
        const CameraState after = cam.GetState();
        cam.SetState(before);   // 由命令来应用，撤销时回到 before（同 ZoomAll）
        m_doc->GetCommandStack().Execute(std::make_unique<ZoomViewCommand>(cam, before, after, "视图"), m_doc->GetScene());

        // 过渡动画：撤销栈里记的仍是 before → after，这里只把相机放回 before，之后每帧（Render）向 after 插值
        m_viewAnim.Active = false;
        if (m_viewAnimSeconds > 0.0)
        {
            cam.SetState(before);
            m_viewAnim = { true, before, after, cam.GetState(), -1.0 };   // 从第一帧开始计时：命令到首帧之间较慢时也不跳过动画
        }
    }

    // CSS 的 cubic-bezier(x1, y1, x2, y2)：曲线过 (0, 0)、(1, 1)，按时间比例 t（即 x）求进度 y。
    // x(u) 单调，先用牛顿法求 u，导数太小时改用二分
    static double CubicBezierEase(double t, double x1, double y1, double x2, double y2)
    {
        auto bezier = [](double u, double p1, double p2) { const double v = 1.0 - u; return 3.0 * v * v * u * p1 + 3.0 * v * u * u * p2 + u * u * u; };
        auto slope  = [](double u, double p1, double p2) { const double v = 1.0 - u; return 3.0 * v * v * p1 + 6.0 * v * u * (p2 - p1) + 3.0 * u * u * (1.0 - p2); };
        double u = t;
        for (int i = 0; i < 8; ++i)
        {
            const double dx = bezier(u, x1, x2) - t, d = slope(u, x1, x2);
            if (std::abs(dx) < 1e-7)
                return bezier(u, y1, y2);
            if (std::abs(d) < 1e-6)
                break;
            u -= dx / d;
        }
        double lo = 0.0, hi = 1.0;
        u = t;
        for (int i = 0; i < 30 && hi - lo > 1e-7; ++i)
        {
            (bezier(u, x1, x2) < t ? lo : hi) = u;
            u = 0.5 * (lo + hi);
        }
        return bezier(u, y1, y2);
    }

    void Editor::AdvanceViewTransition()
    {
        if (!m_viewAnim.Active)
            return;
        Camera& cam = m_viewport->GetCamera();
        const CameraState cur = cam.GetState();
        const CameraState& last = m_viewAnim.Last;
        if (cur.Target.x != last.Target.x || cur.Target.y != last.Target.y || cur.Target.z != last.Target.z || cur.Zoom != last.Zoom || cur.Azimuth != last.Azimuth || cur.Elevation != last.Elevation)
        {
            m_viewAnim.Active = false;   // 被平移、缩放、撤销等打断：保留当前相机
            return;
        }
        if (m_viewAnim.StartTime < 0.0)
            m_viewAnim.StartTime = NowSeconds();
        const double t = (NowSeconds() - m_viewAnim.StartTime) / m_viewAnimSeconds;
        if (t >= 1.0)
        {
            cam.SetState(m_viewAnim.To);
            m_viewAnim.Active = false;
            return;
        }
        const double s = CubicBezierEase(t, 0.45, 0.0, 0.55, 1.0);   // 同 CSS cubic-bezier(0.45, 0, 0.55, 1)：起止柔和、中间加速
        const CameraState& a = m_viewAnim.From;
        const CameraState& b = m_viewAnim.To;
        double dAz = std::fmod(b.Azimuth - a.Azimuth, 360.0);   // 方位角走近的一侧
        if (dAz > 180.0)
            dAz -= 360.0;
        else if (dAz < -180.0)
            dAz += 360.0;
        CameraState m;
        m.Target    = a.Target + (b.Target - a.Target) * s;
        m.Zoom      = (a.Zoom > 0.0 && b.Zoom > 0.0) ? a.Zoom * std::pow(b.Zoom / a.Zoom, s) : a.Zoom + (b.Zoom - a.Zoom) * s;   // 缩放按比例插值
        m.Azimuth   = a.Azimuth + dAz * s;
        m.Elevation = a.Elevation + (b.Elevation - a.Elevation) * s;
        cam.SetState(m);
        m_viewAnim.Last = cam.GetState();
    }

    void Editor::SetPreviewGeometry(std::vector<std::shared_ptr<const Entity>> entities, std::vector<Math::Point3> points,
                                    std::vector<Math::Point3> highlightPoints)
    {
        if (entities.empty() && points.empty() && highlightPoints.empty() && m_previewEntities.empty() &&
            m_previewPoints.empty() && m_previewHighlightPoints.empty())
            return;
        m_previewEntities        = std::move(entities);
        m_previewPoints          = std::move(points);
        m_previewHighlightPoints = std::move(highlightPoints);
        m_previewBounds          = AABB::Empty();
        for (const auto& e : m_previewEntities)
            m_previewBounds.Expand(e->GetBoundingBox());
        for (const auto* list : { &m_previewPoints, &m_previewHighlightPoints })
            for (const auto& p : *list)
                m_previewBounds.Expand(p);
        ++m_previewVersion;
    }

    bool Editor::GetPreviewBounds(AABB& out) const
    {
        if (m_previewBounds.Min.x > m_previewBounds.Max.x)
            return false;
        out = m_previewBounds;
        return true;
    }

    // 三维实体随视图变化的部分：着色面与轮廓线。场景重建、视线方向或视觉样式变化时重建，平移、缩放不重建。
    // 着色：光源跟着相机（从视线的左上方照来），颜色 = 实体颜色 × (环境光 0.25 + 漫反射 0.55)；朝外的法线背对视线时翻过来
    //（开放网格的背面）。亮度最高 0.8：边（实体颜色）画在面上看得清；纯白顶点在浅色背景下会被渲染器画成黑色（7 号色语义），
    // 着色面也不应参与。
    // 轮廓线：面内部的网格边两侧三角形一个朝向眼睛、一个背向眼睛（圆柱、球的外轮廓），两种视觉样式都画
    void Editor::UpdateShadedVertices(bool sceneRebuilt)
    {
        const Camera& cam = m_viewport->GetCamera();
        const Math::Vec3 f = cam.GetViewDir();
        const bool viewChanged = (f - m_shadedViewDir).LengthSq() > 1e-18;
        if (m_shadedValid && !sceneRebuilt && !viewChanged && m_shadedSceneVersion == m_sceneVersion && m_shadedPreviewVersion == m_previewVersion)
            return;
        m_shadedPreviewVersion = m_previewVersion;

        m_shadedVertices.clear();
        m_silhouetteVertices.clear();
        m_shadedViewDir      = f;
        m_shadedSceneVersion = m_sceneVersion;
        m_shadedValid        = true;
        m_shadedVersion      = NextRenderVersion();

        const bool shaded = m_visualStyle == VisualStyle::Shaded;
        const Math::Vec3 r = cam.GetRight(), u = cam.GetUp();
        const Math::Vec3 light = (-f * 0.75 + u * 0.55 - r * 0.35).Normalized();
        auto dot = [](const Math::Vec3& a, const Math::Vec3& b) { return a.x * b.x + a.y * b.y + a.z * b.z; };
        auto toFloat3 = [](const Math::Point3& p) { return Math::Float3{ static_cast<float>(p.x), static_cast<float>(p.y), static_cast<float>(p.z) }; };

        const Scene& scene = m_doc->GetScene();
        const LayerManager& layers = scene.GetLayerManager();
        std::vector<double> facing;
        auto addSolid = [&](const Solid3DEntity& solid, const Math::Color4& base)
        {
            const auto& mesh = solid.GetLocalMesh();
            if (mesh.Empty())
                return;
            const Math::Mat4& m = solid.GetTransform();

            if (shaded)
            {
                const auto& normals = solid.GetLocalCornerNormals();
                m_shadedVertices.reserve(m_shadedVertices.size() + mesh.Indices.size());
                for (size_t c = 0; c < mesh.Indices.size(); ++c)
                {
                    const Math::Point3 p = m.TransformPoint(mesh.Positions[mesh.Indices[c]]);
                    Math::Vec3 n = m.TransformVector(normals[c]).Normalized();
                    if (dot(n, f) > 0.0)
                        n = -n;
                    const double k = 0.25 + 0.55 * std::max(0.0, dot(n, light));
                    const Math::Float4 color{ static_cast<float>(base.r * k), static_cast<float>(base.g * k), static_cast<float>(base.b * k), 1.0f };
                    m_shadedVertices.push_back({ toFloat3(p), color });
                }
            }

            // 轮廓线：三角形朝向（世界坐标下的面法线与视线的点积）在面内部的边两侧变号
            const auto& edges = solid.GetLocalSmoothEdges();
            if (edges.empty())
                return;
            facing.assign(mesh.TriangleCount(), 0.0);
            for (size_t t = 0; t < facing.size(); ++t)
            {
                const auto& a = mesh.Positions[mesh.Indices[t * 3]];
                const Math::Vec3 e1 = mesh.Positions[mesh.Indices[t * 3 + 1]] - a;
                const Math::Vec3 e2 = mesh.Positions[mesh.Indices[t * 3 + 2]] - a;
                const Math::Vec3 n{ e1.y * e2.z - e1.z * e2.y, e1.z * e2.x - e1.x * e2.z, e1.x * e2.y - e1.y * e2.x };
                facing[t] = dot(m.TransformVector(n), f);
            }
            const VertexColor lineColor(Math::Float4{ static_cast<float>(base.r), static_cast<float>(base.g), static_cast<float>(base.b), 1.0f });
            for (const auto& e : edges)
                if ((facing[e.TriA] < 0.0) != (facing[e.TriB] < 0.0))
                {
                    m_silhouetteVertices.push_back({ toFloat3(m.TransformPoint(mesh.Positions[e.A])), lineColor });
                    m_silhouetteVertices.push_back({ toFloat3(m.TransformPoint(mesh.Positions[e.B])), lineColor });
                }
        };
        scene.ForEachObject([&](const Object& o)
        {
            if (!o.IsKindOf<Solid3DEntity>())
                return;
            const auto& solid = static_cast<const Solid3DEntity&>(o);
            const auto& attr  = solid.GetAttr();
            const Layer* layer = layers.GetLayer(attr.LayerId);
            if (!attr.Visible || (layer && !layer->IsVisible()))
                return;
            // 与场景流同一个布局过滤：布局页上模型空间的实体只经视口显示（视口里画的是它的边）
            if (!m_doc->IsVisibleInActiveLayout(attr))
                return;
            addSolid(solid, attr.Color.Method == ColorMethod::ByLayer && layer ? layer->GetColor() : static_cast<Math::Color4>(attr.Color));
        });

        // 节点图预览：实体的面与轮廓线同上；所有预览实体的线（曲线、实体的边）进轮廓线流，三维视图里按深度测试。
        // 预览在模型空间，布局页上不画
        if (!m_doc->ActiveLayoutIsPaper())
        {
            for (const auto& e : m_previewEntities)
            {
                if (e->IsKindOf<Solid3DEntity>())
                    addSolid(static_cast<const Solid3DEntity&>(*e), static_cast<Math::Color4>(e->GetAttr().Color));
                PreviewLineSink sink(m_silhouetteVertices);
                e->Draw(sink, false, false);
            }
        }

        // 三角形从远到近排序：俯视时着色面不做深度测试（与二维内容一样按绘制顺序叠放），近的面要画在后面
        if (m_shadedVertices.size() >= 6)
        {
            const size_t triCount = m_shadedVertices.size() / 3;
            std::vector<std::pair<double, uint32_t>> order(triCount);
            for (size_t t = 0; t < triCount; ++t)
            {
                double d = 0.0;
                for (int i = 0; i < 3; ++i)
                {
                    const auto& p = m_shadedVertices[t * 3 + i].pos;
                    d += p.x * f.x + p.y * f.y + p.z * f.z;
                }
                order[t] = { -d, static_cast<uint32_t>(t) };
            }
            std::stable_sort(order.begin(), order.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
            std::vector<Vertex_P3_C4> sorted;
            sorted.reserve(m_shadedVertices.size());
            for (const auto& [key, t] : order)
                for (int i = 0; i < 3; ++i)
                    sorted.push_back(m_shadedVertices[t * 3 + i]);
            m_shadedVertices.swap(sorted);
        }
    }

    void Editor::SolidBoolean(BooleanOp op)
    {
        static const char* kNames[] = { "Union", "Subtract", "Intersect" };
        static const char* kLabel[] = { "并集", "差集", "交集" };
        const int opi = static_cast<int>(op);
        m_commandLine.Echo(std::string("命令: ") + kNames[opi]);
        m_lastCommand = kNames[opi];
        if (!m_solidKernel)
        {
            m_commandLine.Echo(std::string(kNames[opi]) + ": 三维实体运算不可用（此版本没有实体内核）");
            return;
        }

        // 操作数：选择集里的三维实体，按 ID 升序（差集以最先创建的为被减对象）
        auto& scene = m_doc->GetScene();
        std::map<Object::ObjectID, const Solid3DEntity*> solids;
        int others = 0, locked = 0;
        for (auto id : m_picking.GetSelection())
        {
            const Object* o = scene.GetEntity(id);
            if (!o || !o->IsKindOf<Entity>()) continue;
            if (!o->IsKindOf<Solid3DEntity>() || o->IsKindOf<MeshEntity>()) { ++others; continue; }   // 网格没有内核实体
            if (scene.IsEntityLocked(*o)) { ++locked; continue; }
            solids[id] = static_cast<const Solid3DEntity*>(o);
        }
        std::string notes;
        if (others > 0) notes += "（" + std::to_string(others) + " 个不是三维实体的对象已忽略）";
        if (locked > 0) notes += "（" + std::to_string(locked) + " 个实体在锁定的图层上，已跳过）";
        if (solids.size() < 2)
        {
            m_commandLine.Echo(std::string(kNames[opi]) + ": 需要至少选择两个三维实体" + notes);
            return;
        }

        const auto kop = op == BooleanOp::Union ? Solid::BooleanOp::Union
                       : op == BooleanOp::Subtract ? Solid::BooleanOp::Difference : Solid::BooleanOp::Intersection;
        Solid::BodyPtr result;
        for (const auto& [id, solid] : solids)
        {
            Solid::BodyPtr body = solid->GetWorldBody(*m_solidKernel);
            if (body && !m_solidKernel->IsSolid(body))
            {
                m_commandLine.Echo(std::string(kLabel[opi]) + "只适用于封闭实体：选择集里有开放曲面（可在节点编辑器里先“加厚”），未做修改");
                return;
            }
            if (body && result)
                body = m_solidKernel->Boolean(kop, result, body);
            if (!body)
            {
                m_commandLine.Echo(std::string(kLabel[opi]) + "失败，未做修改（" + m_solidKernel->LastError() + "）");
                return;
            }
            result = body;
        }

        auto entity = Solid3DEntity::Create(scene.NextObjectID(), *m_solidKernel, result);
        if (!entity || entity->GetLocalWire().empty())
        {
            m_commandLine.Echo(std::string(kLabel[opi]) + "：结果为空" + (op == BooleanOp::Intersect ? "（这些实体没有公共部分）" : "") + "，未做修改");
            return;
        }
        entity->SetAttr(solids.begin()->second->GetAttr());   // 属性取自 ID 最小的原实体
        const double volume = m_solidKernel->Volume(result);

        // 原实体替换为结果，作为一步操作（撤销一次全部还原）
        std::vector<GeometryEditCommand::Item> items;
        for (const auto& [id, solid] : solids)
        {
            GeometryEditCommand::Item it;
            it.id     = id;
            it.before = solid->Clone(id);
            items.push_back(std::move(it));
        }
        GeometryEditCommand::Item add;
        add.id    = entity->GetID();
        add.after = std::move(entity);
        items.push_back(std::move(add));

        m_picking.ClearSelection();
        m_picking.MarkDirty();
        m_gripEditor.MarkDirty();
        m_doc->GetCommandStack().Execute(std::make_unique<GeometryEditCommand>(std::string("实体") + kLabel[opi], std::move(items)), scene);

        char buf[160];
        std::snprintf(buf, sizeof(buf), "%s：%zu 个实体合成一个，体积 = %.4f", kLabel[opi], solids.size(), volume);
        m_commandLine.Echo(buf + notes);
    }

    void Editor::RegionBoolean(BooleanOp op)
    {
        if (!m_doc)
            return;
        // 选择集里有三维实体：做实体布尔（同 AutoCAD 的 UNION / SUBTRACT / INTERSECT 对面域与实体都适用）
        for (auto id : m_picking.GetSelection())
            if (const Object* o = m_doc->GetScene().GetEntity(id); o && o->IsKindOf<Solid3DEntity>() && !o->IsKindOf<MeshEntity>())
            {
                SolidBoolean(op);
                return;
            }
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

        // 撤销 / 重做：每条命令自己按实体标脏（AddEntity / RemoveEntity / MarkEntityDirty），这里不再整体标脏——
        // 整体标脏会让所有实体重新细分，几十万实体的图纸要几十秒
        if (e.IsUndo())
        {
            if (m_tool) m_tool->OnSceneChanged();
            cmdStack.Undo(scene);
            m_gripEditor.MarkDirty();
            return true;
        }

        if (e.IsRedo())
        {
            if (m_tool) m_tool->OnSceneChanged();
            cmdStack.Redo(scene);
            m_gripEditor.MarkDirty();
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
            // 双击中键 = 缩放全图（与 AutoCAD 的「双击中键 → ZOOM E」一致）。
            //
            // 判定放在平移之前：双击的第一下已经开始了平移，但用户没有移动鼠标的话
            // 平移量是 0；这里在第二下按下时直接缩放，并跳过本次平移的准备工作。
            // 连击计数由 UI 层算好（同按键、双击时间窗内、位置接近），见 UIContext::PointerDown。
            //
            // undoable = false：与中键平移、滚轮缩放同类，**视图操作不进撤销栈**。
            // 否则用户会为了"回到上一视图"按 Ctrl+Z，结果把真正的几何编辑撤销掉。
            if (e.ClickCount >= 2)
            {
                ZoomAll(/*undoable=*/false);
                return true;
            }

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

        // 布局是二维图纸：不环绕，Shift + 中键按普通中键平移处理
        if (e.Type == InputEventType::MouseMove && e.IsMouseButtonDown(MouseButton::Middle) && e.HasModifier(ModifierKey::Shift) &&
            !(m_doc && m_doc->ActiveLayoutIsPaper()))
        {
            // Shift + 中键拖动：绕视图中心环绕（同 AutoCAD 的 3DORBIT）
            m_viewport->GetCamera().Orbit(e.MouseX - e.LastMouseX, e.MouseY - e.LastMouseY);
            return true;
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

        AdvanceViewTransition();

        // 命令行提示 = 当前工具的状态提示（无工具时为空）
        m_commandLine.SetPrompt(m_tool ? m_tool->GetPrompt()
                                       : (m_zoomPending ? std::string("ZOOM 选项 [全部(A)/范围(E)]:") : std::string{}));

        m_overlayVertices.clear();

        // 1 屏幕像素对应的世界长度(线宽 / 点样式显示用)
        const double worldPerPixel = m_viewport->GetCamera().GetWorldPerPixel();

        // 线宽 / 点样式按屏幕像素烘焙,缩放变化时依赖像素比例的实体需重建（虚线在世界坐标里，不重建）。
        // 按相对误差比较:平移不改变 worldPerPixel,不能因舍入误差触发整图重建
        bool zoomChanged = std::abs(worldPerPixel - m_lastWorldPerPixel) > 1e-9 * std::max(worldPerPixel, m_lastWorldPerPixel);
        m_lastWorldPerPixel = worldPerPixel;

        const bool dragging       = m_gripEditor.IsDragging();
        const bool selectionDirty = m_picking.IsDirty();

        // 拾取空间索引按脏实体集增量同步——必须在 UpdateSceneVertices 的
        // Scene::ClearDirty 之前,否则脏集被清掉后索引只能整表重建。
        m_picking.SyncIndex();

        bool sceneRebuilt = UpdateSceneVertices(worldPerPixel, zoomChanged);
        UpdateShadedVertices(sceneRebuilt);

        // 场景或选择集变化时清预览、重建夹点（原先在场景重建路径内）
        if ((sceneRebuilt || selectionDirty) && !IsActiveTool() && !dragging)
        {
            m_overlay.Clear();
            m_gripEditor.RebuildGrips();
        }

        // 选中流：选择集变化 / 场景重建 / 拖动中（几何逐帧变化）/ 缩放+线宽虚线 / 细线开关
        {
            const bool selFull = sceneRebuilt || selectionDirty || dragging;
            UpdateSelectionVertices(worldPerPixel, selFull || (zoomChanged && m_selViewDependent), !selFull);
        }

        m_overlay.ToVertices(m_overlayVertices);
        // 节点图预览点在模型空间，布局页上不画（同 UpdateShadedVertices 里的预览实体）
        if ((!m_previewPoints.empty() || !m_previewHighlightPoints.empty()) && !m_doc->ActiveLayoutIsPaper())
        {
            m_previewOverlay.Clear();
            for (const auto& p : m_previewPoints)
                m_previewOverlay.AddPoint(p, Math::Color4{ 0.86, 0.36, 0.30, 1.0 });
            for (const auto& p : m_previewHighlightPoints)
                m_previewOverlay.AddPoint(p, Math::Color4{ 0.30, 0.85, 0.40, 1.0 });
            m_previewOverlay.ToVertices(m_overlayVertices);
        }

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

        // 悬停高亮：追加到 overlay 顶点，不触发场景重建。场景重建（含图层显示变化）、
        // 选择集变化时缓存的高亮顶点失效
        BuildHoverHighlight(sceneRebuilt || selectionDirty);
    }

    // 悬停高亮绘制：仅对当前悬停且未被选中的实体描边（选中态已是选中色）。
    // 悬停集通常只有 1 个实体，但可能是很大的块参照（每次绘制几毫秒），而在密集处移动 1 像素悬停对象
    // 就会在几个实体间来回切换，所以按实体缓存高亮顶点，再次悬停时只复制
    void Editor::BuildHoverHighlight(bool invalidate)
    {
        if (!m_doc) return;

        const auto& hovered = m_picking.GetHovered();
        if (hovered.empty()) return;

        auto& scene = m_doc->GetScene();
        // 缩放变化也要失效：点的高亮十字、相对尺寸的点图形按当前缩放烘焙
        const double wpp = m_viewport->GetCamera().GetWorldPerPixel();
        if (invalidate || m_hoverCacheGeomVersion != scene.GeometryVersion() || m_hoverCacheWorldPerPixel != wpp)
        {
            m_hoverCache.clear();
            m_hoverCacheGeomVersion   = scene.GeometryVersion();
            m_hoverCacheWorldPerPixel = wpp;
        }

        const auto& selection = m_picking.GetSelection();
        ++m_hoverTick;

        for (auto id : hovered)
        {
            if (selection.contains(id)) continue;  // 已选中：保持选中色

            auto cached = std::find_if(m_hoverCache.begin(), m_hoverCache.end(),
                                       [id](const HoverCacheEntry& e) { return e.id == id; });
            if (cached == m_hoverCache.end())
            {
                HoverCacheEntry entry;
                entry.id = id;
                BuildHoverVertices(id, entry.vertices);
                m_hoverCache.push_back(std::move(entry));
                cached = m_hoverCache.end() - 1;
            }
            cached->lastUse = m_hoverTick;
            m_overlayVertices.insert(m_overlayVertices.end(), cached->vertices.begin(), cached->vertices.end());
        }

        // 超出容量时淘汰最久未用的（本帧用到的不淘汰）
        constexpr std::size_t kMaxEntries  = 32;
        constexpr std::size_t kMaxVertices = 2'000'000;
        std::size_t total = 0;
        for (const auto& e : m_hoverCache)
            total += e.vertices.size();
        while (m_hoverCache.size() > 1 && (m_hoverCache.size() > kMaxEntries || total > kMaxVertices))
        {
            auto oldest = std::min_element(m_hoverCache.begin(), m_hoverCache.end(),
                                           [](const HoverCacheEntry& a, const HoverCacheEntry& b) { return a.lastUse < b.lastUse; });
            if (oldest->lastUse == m_hoverTick) break;
            total -= oldest->vertices.size();
            m_hoverCache.erase(oldest);
        }
    }

    // 生成一个实体的悬停高亮顶点（隐藏的实体为空）
    void Editor::BuildHoverVertices(Object::ObjectID id, std::vector<Vertex_P3_C4>& out) const
    {
        auto& scene = m_doc->GetScene();
        auto* obj = scene.GetEntity(id);
        if (!obj || !obj->IsKindOf<Entity>()) return;

        const auto& entity = static_cast<const Entity&>(*obj);

        // 隐藏图层上的实体不参与悬停高亮
        if (!entity.GetAttr().Visible) return;
        if (const Layer* layer = scene.GetLayerManager().GetLayer(entity.GetLayerID());
            layer && !layer->IsVisible())
            return;

        HoverHighlightSink sink(out, IDrawSink::kHoverColor, scene.GetLayerManager());
        sink.SetPointStyle(scene.GetPointDisplayMode(), scene.GetPointDisplaySize(),
                           m_viewport->GetCamera().GetWorldPerPixel(), static_cast<double>(m_viewport->GetHeight()));

        // 块裁剪（XCLIP）：悬停高亮是**叠加绘制**的，绕过了场景流的裁剪 ⇒
        // 不裁的话悬停一个被裁剪的块会把它整块高亮出来（用户报的
        //「鼠标悬停在图块上时，不应显示整个块，而是显示到裁剪线」）。
        // 这里只能裁**本次追加**的那一段，不能对整个向量裁（里面还有别的实体）。
        const size_t from = out.size();
        entity.Draw(sink, /*isSelected*/ false, /*isHovered*/ true);

        if (const auto* ins = dynamic_cast<const InsertEntity*>(&entity);
            ins && ins->HasClipBoundary())
        {
            // 边界在块坐标系里，经插入变换落到世界坐标后才能与已画出的顶点比较
            const Math::Mat4 m = ins->CellMatrix(0, 0);
            std::vector<Math::Point3> world;
            world.reserve(ins->GetClipBoundary().size());
            for (const auto& p : ins->GetClipBoundary())
                world.push_back(m * p);

            WipeClip::Poly poly(world);
            if (poly.Valid() && out.size() > from)
            {
                std::vector<Vertex_P3_C4> tail(out.begin() + static_cast<std::ptrdiff_t>(from), out.end());
                WipeClip::ClipLines(tail, poly, /*keepInside*/ true);
                out.resize(from);
                out.insert(out.end(), tail.begin(), tail.end());
            }

            // 悬停时把裁剪框也画出来，让用户看清"这个块的显示被裁到哪里"
            if (ins->IsClipFrameVisible())
                for (size_t i = 0; i < world.size(); ++i)
                    sink.DrawLine(world[i], world[(i + 1) % world.size()],
                                  IDrawSink::kHoverColor, false);
        }

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

    // 矢量字体解析回调（场景流与选中流共用）
    static FontResolver MakeFontResolver(Document* doc)
    {
        return MakeTextStyleResolver(doc->GetFontSystem(), &doc->GetScene().GetTextStyleTable());
    }

    // 形字体解析回调（复杂线型的形段，组码 75 → ltypeshp.shx 等形文件）
    static ShapeFontResolver MakeShapeFontResolver(Document* doc)
    {
        return MakeShapeFontResolver(doc->GetFontSystem());
    }

    // 细分一个实体到逐实体缓存（顶点、是否随视图变化、区域覆盖、图像）
    void Editor::TessellateEntity(const Entity& entity, DrawContext& ctx, EntityVertexCache& c)
    {
        m_scratchLines.clear();
        m_scratchFills.clear();
        m_scratchTexts.clear();
        m_scratchGlyphs.clear();
        ctx.ClearPoints();
        ctx.ResetViewDependentFlag();

        ctx.BeginEntity(&entity.GetAttr());
        entity.Draw(ctx, /*isSelected*/ false, false);
        ctx.BeginEntity(nullptr);

        // 块裁剪边框（$XCLIPFRAME）：平时也要画，不能只在选中时才出现 ——
        // AutoCAD 里裁剪框是一个常驻的视觉提示，用户据此知道这个块被裁过、裁到哪儿。
        // 必须画在**裁剪之后**（这里是 entity.Draw 返回后），否则边框会被自己的裁剪吃掉。
        // 颜色随块（ByLayer 时取图层色），与 WIPEOUTFRAME / IMAGEFRAME 的做法一致。
        // 分段细分的块参照只在画完的那一段画边框
        if (const auto* ins = dynamic_cast<const InsertEntity*>(&entity);
            ins && ins->HasClipBoundary() && ins->IsClipFrameVisible() && !ctx.ChunkStopped())
        {
            const auto& poly = ins->GetClipBoundary();
            if (poly.size() >= 2)
            {
                // 边界是**块坐标系**里的，要经本块的插入变换才落到世界坐标
                const Math::Mat4 m = ins->CellMatrix(0, 0);
                const Math::Color4 color = ins->ResolveDrawColor(ctx);
                for (size_t i = 0; i < poly.size(); ++i)
                {
                    const Math::Point3 a = m * poly[i];
                    const Math::Point3 b = m * poly[(i + 1) % poly.size()];
                    m_scratchLines.push_back({ { static_cast<float>(a.x), static_cast<float>(a.y), static_cast<float>(a.z) },
                                               { static_cast<float>(color.r), static_cast<float>(color.g),
                                                 static_cast<float>(color.b), static_cast<float>(color.a) } });
                    m_scratchLines.push_back({ { static_cast<float>(b.x), static_cast<float>(b.y), static_cast<float>(b.z) },
                                               { static_cast<float>(color.r), static_cast<float>(color.g),
                                                 static_cast<float>(color.b), static_cast<float>(color.a) } });
                }
            }
        }

        // ── 布局页：视口把模型空间内容合成进来 ──────────────────────────
        //
        // AutoCAD 的布局页里，图形内容**全部靠视口从模型空间取** ——
        // 图纸空间本身通常只有视口框（实测 test0.dwg 的 *Paper_Space 完全为空）。
        // 不合成的话，切到布局页只有一张白纸、框里什么都没有。
        //
        // 合成走的是**同一个 ctx**，所以顶点自然落进 m_scratch* 里、与视口边框
        // 一起组成这个实体的缓存块；不需要另外维护一份缓冲。
        //
        // ⚠ 顺序**必须**在下面的 `c.xxx = m_scratchXxx` 之前！
        //   这两个缓冲是成员变量，合成是把顶点**追加**进去。若先拷贝再合成，
        //   追加进去的模型内容就永远不会进入这个实体的缓存块 ——
        //   表现是布局页里只剩视口边框、模型内容一个都不显示，
        //   但拾取走的是场景数据，仍然能选中（「能选中但看不到」）。
        //
        // 优化：先用视口的「模型可见矩形」按实体包围盒**预剔除**。
        // 没有这一步，每个视口都要遍历全部模型实体 —— 实测有 20 万实体、13 个视口
        // 时就是 260 万次无用构建。包围盒判断是 O(1)，成本可忽略。
        if (const auto* vp = dynamic_cast<const ViewportEntity*>(&entity))
            if (vp->ShowsModel())
                ComposeModelIntoViewport(*vp, ctx);

        // 合成完成后才取缓冲（顺序见上）
        c.lines         = m_scratchLines;
        c.fills         = m_scratchFills;
        c.points        = ctx.TakePoints();
        c.texts         = m_scratchTexts;
        c.glyphs        = m_scratchGlyphs;
        c.viewDependent = ctx.HasViewDependentGeometry();
        c.longDashes    = ctx.TakeDeferredDashes();
        c.wipes         = ctx.TakeWipes();
        c.images        = ctx.TakeImages();
    }

    namespace
    {
        // 视口合成用的变换汇：模型内容经视口变换落到纸面坐标，并让合成结果**不随视图变化**。
        //
        // 以前合成期间把长虚线按像素取舍（延迟出去的段裁不到视口矩形），只要视口里有一段长虚线或一个相对尺寸的点，
        // 整个视口实体就被标成随视图变化 —— 布局页每格滚轮都要把视口看见的模型重新合成一遍
        //（实测 0525 总平面图：模型页每格 16 ms，布局页 340 ms）。现在：
        //   · 长虚线照常延迟（DeferredDash），合成后由调用方按视口矩形裁剪，缩放时只重铺这些段，不重新合成；
        //   · 相对尺寸的点（$PDSIZE ≤ 0，按视口高度百分比）按**本视口**的纸面高度换算，大小随纸面固定；
        //   · 像素比例按视口比例换算，实体里"N 像素"的量经变换后仍是 N 像素；
        //   · MarkViewDependent 不转发。线宽（细线显示关闭时）走 DrawContext::EmitSolid 自己的标记，仍随视图变化。
        //   · 在本视口里冻结的图层（VPLAYER）对块里的子实体也要生效，所以 IsLayerVisible 一并过滤。
        class ViewportComposeSink final : public TransformDrawSink
        {
        public:
            ViewportComposeSink(IDrawSink& target, const Math::Mat4& m, const ViewportEntity& vp)
                : TransformDrawSink(target, m), m_target(target), m_vp(vp)
                , m_scale(vp.ModelToPaperScale()), m_paperHeight(vp.GetHeight()) {}

            bool IsLayerVisible(LayerID layerId) const override
            {
                return !m_vp.IsLayerFrozen(layerId) && m_target.IsLayerVisible(layerId);
            }

            double GetWorldPerPixel() const override { return m_scale > 0.0 ? m_target.GetWorldPerPixel() / m_scale : 0.0; }
            double GetViewportHeightPx() const override
            {
                const double wpp = m_target.GetWorldPerPixel();
                return wpp > 0.0 ? m_paperHeight / wpp : 0.0;     // 本视口框在屏幕上的高度（像素）
            }
            void MarkViewDependent() override {}

        private:
            IDrawSink&            m_target;
            const ViewportEntity& m_vp;
            double                m_scale;
            double                m_paperHeight;
        };
    }

    //
    // 把一个视口「看见」的模型空间内容按视口变换画进 ctx。
    //
    // 视口变换 = 平移(-viewCenter) → 缩放(modelToPaper) → 旋转(-twist) → 平移(+center)，
    // 可以合成一个仿射矩阵交给 TransformDrawSink —— 与块参照用的是同一套机制，
    // 文字、线型、线宽、区域覆盖都会自动按变换处理，不必另写一份绘制逻辑。
    void Editor::ComposeModelIntoViewport(const ViewportEntity& vp, DrawContext& ctx)
    {
        const AABB visible = vp.ModelVisibleBox();
        const double s = vp.ModelToPaperScale();
        if (!(s > 0.0))
            return;

        // $PSLTSCALE = 0：视口里的虚线跟着视口比例缩（模型里的 dash 长度 × s 才是纸面上的长度）；
        // = 1（默认）：按图纸单位铺，与纸面上的虚线一样疏密。几何已经变换到纸面坐标，所以后者什么都不用做
        const double lineTypeScale = ctx.GetLineTypeScale();
        if (!m_doc->GetScene().GetPaperSpaceLineTypeScaling())
            ctx.SetLineTypeScale(lineTypeScale * s);
        const size_t clipDashFrom = ctx.DeferredDashes().size();

        // ── 视口裁剪：模型内容只画在视口矩形内 ──────────────────────────
        //
        // AutoCAD 里视口就是一个「开向模型空间的窗口」：模型实体的线被视口边界
        // 硬裁掉，框外一根线都不露。不做这步时，图元会溢出到视口外的纸面上，
        // 看起来就是「没经过视口裁剪」。
        //
        // 实现：记下本视口合成前的缓冲长度，合成结束后只裁**本次追加的区间**
        // （WipeClip::ClipLines/ClipFills 的 from 参数）。不能用 DrawContext::Wipe —
        // 那个语义是「裁剪此前已写入的顶点」，而视口要裁的正是随后写入的内容。
        // 顶点坐标与视口矩形同在纸面坐标系（变换最后一步就是平移到视口中心），
        // 不需要额外换算。
        const size_t clipLineFrom  = ctx.LineCount();
        const size_t clipFillFrom  = ctx.FillCount();
        const size_t clipTextFrom  = ctx.TextCount();
        const size_t clipPointFrom = ctx.PointCount();
        const size_t clipGlyphFrom = ctx.GlyphCount();
        const size_t clipImageFrom = ctx.ImageCount();

        // 行向量约定（P' = P·M），最左矩阵最先作用
        using namespace Math;
        Mat4 m = Mat4::Translation(Vec3{ -vp.GetViewCenter().x, -vp.GetViewCenter().y, 0.0 })
               * Mat4::Scale(Vec3{ s, s, 1.0 });
        // ⚠ 内容旋转用 **R(+Twist)**，不是 R(-Twist)。
        //
        // 依据（实测 `01-曲陵路.dwg`，5 个 Twist=95.69° 的浮动视口）：
        //   这 5 个视口在纸面上并排一行（Center.x 步长 +420），各看模型里
        //   195 宽的一条带（scale=2，195×2=390=框宽）。把 ViewCenter 按
        //   DCS→WCS 的 R(-Twist) 转过之后，相邻 WCS 位移单位向量恒等于
        //   该视口的 UcsXAxis=(-0.0991,-0.9951) —— 即「纸面 +X 对应的模型方向」。
        //   内容变换必须把 UcsXAxis 映射到纸面 +X：
        //       R(+Twist)·UcsXAxis = (+1.0000, -0.0000)  ✔
        //       R(-Twist)·UcsXAxis = (-0.9804, +0.1972)  ✘（偏 95.7°）
        //   ⇒ 只有 R(+Twist) 能让视口横向对上 UCS 横向。
        //   用 R(-Twist) 时内容整体转错约 90°，视口里的图元相对视口边框是斜的。
        if (vp.GetTwist() != 0.0)
            m = m * Mat4::RotationZ(vp.GetTwist());
        m = m * Mat4::Translation(Vec3{ vp.GetCenter().x, vp.GetCenter().y, 0.0 });

        ViewportComposeSink vsink(ctx, m, vp);

        auto& scene = m_doc->GetScene();
        scene.ForEachObject([&](const Object& obj)
        {
            if (!obj.IsKindOf<Entity>())
                return;
            const auto& e = static_cast<const Entity&>(obj);
            // 只合成**模型空间**实体；图纸空间实体（含视口自身）跳过，
            // 否则会把纸面元素也塞进视口，视口还会自我递归
            if (e.GetAttr().InPaperSpace)
                return;
            if (!e.GetAttr().Visible)
                return;
            if (e.IsKindOf<ViewportEntity>())
                return;
            if (const Layer* layer = scene.GetLayerManager().GetLayer(e.GetAttr().LayerId);
                layer && !layer->IsVisible())
                return;
            // 在本视口里冻结的图层（VPLAYER）：只在这个视口不显示
            if (vp.IsLayerFrozen(e.GetAttr().LayerId))
                return;

            // 包围盒预剔除：完全落在视口可见模型矩形外的直接跳过（不做任何构建）
            const AABB box = e.GetBoundingBox();
            if (box.Max.x < visible.Min.x || box.Min.x > visible.Max.x ||
                box.Max.y < visible.Min.y || box.Min.y > visible.Max.y)
                return;

            // 逐实体切上下文：各实体的线型 / 线宽 / 线型比例不同
            vsink.BeginEntity(&e.GetAttr());
            e.Draw(vsink, /*isSelected*/ false, /*isHovered*/ false);
            vsink.BeginEntity(nullptr);
        });
        ctx.SetLineTypeScale(lineTypeScale);

        // 循环结束后裁掉本次合成落在视口矩形外的顶点（只裁 [from, end) 区间）
        const double hw = vp.GetWidth() * 0.5;
        const double hh = vp.GetHeight() * 0.5;
        const Math::Point3 vc = vp.GetCenter();
        const WipeClip::Poly clipRect({ Math::Point3{ vc.x - hw, vc.y - hh, vc.z },
                                        Math::Point3{ vc.x + hw, vc.y - hh, vc.z },
                                        Math::Point3{ vc.x + hw, vc.y + hh, vc.z },
                                        Math::Point3{ vc.x - hw, vc.y + hh, vc.z } });
        if (clipRect.Valid())
        {
            // keepInside=true：只保留视口矩形**内**的内容。
            // （区域覆盖用默认的 keepInside=false，语义相反：挖掉内部）
            WipeClip::ClipLines(ctx.Lines(), clipRect, clipLineFrom, /*keepInside*/ true);
            WipeClip::ClipFills(ctx.Fills(), clipRect, 5, clipFillFrom, /*keepInside*/ true);
            WipeClip::ClipTexts(ctx.Texts(), clipRect, clipTextFrom);
            WipeClip::ClipPoints(ctx.Points(), clipRect, clipPointFrom, /*keepInside*/ true);
            // ⚠ 字形实例是**单独提交**的一路（桌面 / 网页两版的文字都走它），
            //    漏了它就是漏裁整份文字 —— 表现为「布局页里文字铺到视口外面」。
            //    少了这一行时上面四路裁得再对也看不出问题：线与填充确实规矩了，
            //    只有文字一类整行溢出，用户报的就是「图元在视口外还显示」。
            if (std::vector<GlyphInstance>* glyphs = ctx.Glyphs())
                WipeClip::ClipGlyphInstances(*glyphs, clipRect, clipGlyphFrom, /*keepInside*/ true);
            // 光栅图像同样是单独提交的一路（Viewport 里逐个 SubmitImage），
            // 不裁就整张贴图溢出到视口外的纸面上
            WipeClip::ClipImages(ctx.Images(), clipRect, clipImageFrom, /*keepInside*/ true);

            // 合成期间延迟出来的长虚线段（铺进动态流时才展开）同样只留视口矩形内的部分：
            // 视口框是纸面上的轴对齐矩形，直接 Liang–Barsky；裁掉前半段时把相位往后推，裁剪处的 dash / gap 不错位
            auto& dashes = ctx.DeferredDashes();
            const double xmin = vc.x - hw, xmax = vc.x + hw, ymin = vc.y - hh, ymax = vc.y + hh;
            size_t kept = clipDashFrom;
            for (size_t i = clipDashFrom; i < dashes.size(); ++i)
            {
                DeferredDash d = dashes[i];
                const double dx = d.b.x - d.a.x, dy = d.b.y - d.a.y, dz = d.b.z - d.a.z;
                double t0 = 0.0, t1 = 1.0;
                auto clip = [&](double p, double q)
                {
                    if (p == 0.0) return q >= 0.0;
                    const double r = q / p;
                    if (p < 0.0) { if (r > t1) return false; if (r > t0) t0 = r; }
                    else         { if (r < t0) return false; if (r < t1) t1 = r; }
                    return true;
                };
                if (!clip(-dx, d.a.x - xmin) || !clip(dx, xmax - d.a.x) || !clip(-dy, d.a.y - ymin) || !clip(dy, ymax - d.a.y))
                    continue;
                if (t0 > 0.0 || t1 < 1.0)
                {
                    const Math::Point3 a = d.a;
                    d.a = { a.x + dx * t0, a.y + dy * t0, a.z + dz * t0 };
                    d.b = { a.x + dx * t1, a.y + dy * t1, a.z + dz * t1 };
                    d.phase += t0 * std::sqrt(dx * dx + dy * dy + dz * dz);
                }
                dashes[kept++] = d;
            }
            dashes.resize(kept);
        }
    }

    // 把一个实体的缓存按绘制序接到场景流尾部：记下它的区域覆盖（裁剪此前的顶点，由 FlushSceneWipes 一次完成），
    // 再追加它自己的顶点。随视图变化的线 / 填充进动态流，其余进静态流；文字与图像总在静态流（不随缩放变化）
    void Editor::AppendToSceneStreams(Object::ObjectID id, EntityVertexCache& c, WipeClip::Batch& wipes,
                                      std::vector<Vertex_P3_C4>& lines, std::vector<Vertex_P3_C4>& fills, std::vector<Vertex_P3_C4>& points)
    {
        for (const auto& w : c.wipes)
        {
            wipes.Add(w, { lines.size(), fills.size(), m_textVertices.size(),
                           m_sceneDynVertices.size(), m_sceneDynFillVertices.size(), m_glyphInstances.size(),
                           points.size() });
            AABB box = AABB::Empty();
            for (const auto& p : w) box.Expand(p);
            m_sceneWipeBoxes.push_back(box);
        }
        m_sceneImages.insert(m_sceneImages.end(), c.images.begin(), c.images.end());
        m_textVertices.insert(m_textVertices.end(), c.texts.begin(), c.texts.end());
        c.glyphOff = m_glyphInstances.size();
        m_glyphInstances.insert(m_glyphInstances.end(), c.glyphs.begin(), c.glyphs.end());
        if (c.viewDependent)
        {
            m_sceneDynVertices.insert(m_sceneDynVertices.end(), c.lines.begin(), c.lines.end());
            m_sceneDynFillVertices.insert(m_sceneDynFillVertices.end(), c.fills.begin(), c.fills.end());
        }
        else
        {
            c.lineOff = lines.size();
            c.fillOff = fills.size();
            lines.insert(lines.end(), c.lines.begin(), c.lines.end());
            fills.insert(fills.end(), c.fills.begin(), c.fills.end());
        }
        // 点图元不随视图变化（点是恒定屏幕像素），总进静态流
        c.pointOff = points.size();
        points.insert(points.end(), c.points.begin(), c.points.end());
        c.placed = true;
        // 延迟的长虚线段按当前缩放铺进动态流（缩放时只重铺它们，实体不重新细分）
        EmitDeferredDashes(c, m_sceneDynVertices);
        // 分帧细分接上的一段（id 为空）不登记：整个实体细分完再登记（AppendBuildPiece）
        if (id != Object::InvalidID && (c.viewDependent || !c.wipes.empty() || !c.longDashes.empty()))
            m_dynOrder.push_back(id);
        m_sceneViewDependent |= c.viewDependent || !c.longDashes.empty();
    }

    void Editor::EmitDeferredDashes(const EntityVertexCache& c, std::vector<Vertex_P3_C4>& out) const
    {
        if (c.longDashes.empty()) return;
        const LineTypeTable& lineTypes = m_doc->GetScene().GetLineTypeTable();
        for (const DeferredDash& d : c.longDashes)
            DrawContext::EmitDeferredDash(d, lineTypes, m_lastWorldPerPixel, out);
    }

    // OLE 对象的快照（OleFrameEntity 经 EmitImage 请求 "ole:<哈希>"）：数据从登记处取，展示流是位图就直接解码，
    // 是图元文件就交给平台光栅化；长边 4096 像素（Word 表格里的小字也能看清），结果按数据哈希缓存
    std::shared_ptr<const ImageData> Editor::OleImage(const std::string& path)
    {
        uint64_t key = 0;
        if (!Ole::ParseImagePath(path, key)) return nullptr;
        if (const auto it = m_oleImages.find(key); it != m_oleImages.end())
            return it->second;

        std::shared_ptr<const ImageData> image;
        if (const auto data = Ole::DataStore::Instance().Get(key))
        {
            const auto t0 = std::chrono::steady_clock::now();
            Ole::Presentation pres;
            if (Ole::ExtractPresentation(*data, pres))
            {
                if (pres.Format == Ole::PictureFormat::Dib)
                    image = Ole::DecodeDib(pres.Data, ImageLibrary::NextKey());
                else if (m_oleRasterizer)
                    image = m_oleRasterizer(pres, kOleImageMaxSide);
            }
            else
                LOG_WARN("OLE 对象没有可显示的展示数据，只画边框");
            const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
            if (image)
                LOG_INFO("OLE 对象快照：%s %zu 字节 → %u×%u 像素，%.0f ms", pres.Format == Ole::PictureFormat::Emf ? "EMF" : pres.Format == Ole::PictureFormat::Wmf ? "WMF" : "DIB",
                         pres.Data.size(), image->Width, image->Height, ms);
        }
        m_oleImages.emplace(key, image);
        return image;
    }

    // 只重建动态流：随视图变化的实体重新细分，按绘制序拼接，并用其后的区域覆盖裁剪（与静态流的规则相同）
    void Editor::RebuildDynamicStream(DrawContext& ctx)
    {
        auto& scene = m_doc->GetScene();
        m_sceneDynVertices.clear();
        m_sceneDynFillVertices.clear();
        // 字形实例（文字、复杂线型的文字 / 形符号）在世界坐标里，不随缩放变化，这里不动它们：
        // m_glyphInstances 是全场景的，按 glyphOff 局部改写，清空或重排都会让偏移失效
        WipeClip::Batch wipes(2);
        for (Object::ObjectID id : m_dynOrder)
        {
            auto it = m_entityVertexCache.find(id);
            if (it == m_entityVertexCache.end())
            {
                const Object* obj = scene.GetEntity(id);
                if (!obj || !obj->IsKindOf<Entity>()) continue;
                EntityVertexCache c;
                TessellateEntity(static_cast<const Entity&>(*obj), ctx, c);
                it = m_entityVertexCache.emplace(id, std::move(c)).first;
            }
            const EntityVertexCache& c = it->second;
            if (!m_sceneDynVertices.empty() || !m_sceneDynFillVertices.empty())
                for (const auto& w : c.wipes)
                    wipes.Add(w, { m_sceneDynVertices.size(), m_sceneDynFillVertices.size() });
            if (c.viewDependent)
            {
                m_sceneDynVertices.insert(m_sceneDynVertices.end(), c.lines.begin(), c.lines.end());
                m_sceneDynFillVertices.insert(m_sceneDynFillVertices.end(), c.fills.begin(), c.fills.end());
            }
            EmitDeferredDashes(c, m_sceneDynVertices);
        }
        wipes.ClipLines(m_sceneDynVertices, 0);
        wipes.ClipFills(m_sceneDynFillVertices, 1);
        m_sceneDynVersion = NextRenderVersion();
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

        // 分帧细分（BeginProgressiveBuild）：每帧都接着细分，到本帧的时限为止
        const bool building = m_build.active;
        if (building)
        {
            // 两帧细分之间的上传、整图重画、界面也要时间（大图纸每帧几十毫秒）：本帧细分时限至少是它的 3 倍，
            // 免得大半时间花在帧间开销上；最多放宽到预算的 4 倍，窗口仍能响应
            const auto now = std::chrono::steady_clock::now();
            double budget = m_build.budgetMs;
            if (m_build.lastEnd != std::chrono::steady_clock::time_point{})
                budget = std::clamp(3.0 * std::chrono::duration<double, std::milli>(now - m_build.lastEnd).count(),
                                    m_build.budgetMs, 4.0 * m_build.budgetMs);
            m_build.deadline = now + std::chrono::microseconds(static_cast<int64_t>(budget * 1000.0));
            // 细分到一半的大块参照被改动 / 删除 / 拖动：已接进流里的那几段作废，全量拼接（不含它），之后重新细分
            if (m_build.partId != Object::InvalidID &&
                (scene.GetDirtyEntities().count(m_build.partId) || dragIds.count(m_build.partId)))
            {
                DropBuildPart();
                m_sceneRestitch = true;
            }
            // 后端接不了追加段、拼接流又已交给它：每帧从逐实体缓存全量拼接，在 CPU 端的流上接着细分
            if (m_sceneStreamsReleased && !s_sceneAppendOnGpu)
                m_sceneRestitch = true;
            // 细分期间图纸有改动：改动的实体由增量更新接到流尾，绘制序与画全后不同，画全后全量拼接一次
            if (m_build.queued && scene.IsDirty())
                m_build.finalStitch = true;
        }

        bool need;
        if (!dragIds.empty() && !exclusionChanged)
        {
            // 拖动期间 GripEditor 每帧 MarkDirty（实时几何变化），但被拖实体
            // 已不在场景流中，场景流无需重建；仅缩放+线宽/虚线场合仍需重建。
            need = (zoomChanged && m_sceneViewDependent) || m_displayInvalid || m_sceneRestitch || building;
        }
        else
        {
            need = scene.IsDirty() || exclusionChanged || m_displayInvalid || m_sceneRestitch ||
                   (zoomChanged && m_sceneViewDependent) || building;
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
        const ImageProvider imageProvider = [this](const std::string& p)
        {
            return p.compare(0, 4, "ole:") == 0 ? OleImage(p) : m_imageLibrary.Get(p);
        };

        // 全局脏（图层颜色/线型表等影响所有实体）或细线开关切换时所有实体缓存整体失效；
        // 缩放变化只让依赖像素比例的实体（线宽/虚线按屏幕像素烘焙进世界顶点）失效。
        const bool invalidateAll = scene.IsAllDirty() || m_displayInvalid;
        m_displayInvalid = false;

        const auto& layerMgr = scene.GetLayerManager();
        DrawContext ctx(m_scratchLines, m_scratchFills, m_scratchTexts, m_overlay,
                        layerMgr, scene.GetLineTypeTable(), worldPerPixel,
                        m_glyphProvider, MakeFontResolver(m_doc));
        ctx.SetImageProvider(imageProvider);
        ctx.SetThinLines(m_thinLines);
        ctx.SetGlyphInstanceSink(&m_scratchGlyphs, &m_glyphLibrary);
        ctx.SetDeferLongDashes(true);   // 长虚线段延迟铺设：进动态流，缩放时只重铺这些段

        // 图纸级点样式（HEADER 的 $PDMODE/$PDSIZE）+ 视口像素高度。
        // POINT 实体自身只有坐标，画成什么样完全由这两个变量决定；实体拿不到 Scene，
        // 所以只能在这里经 sink 传下去（与图层颜色同一条路）。
        // $PDSIZE<=0 是视口高度百分比，没有像素高度换算不出世界尺寸（点退化成 1 像素小点）。
        ctx.SetPointStyle(scene.GetPointDisplayMode(), scene.GetPointDisplaySize());
        ctx.SetViewportHeightPx(m_viewport ? static_cast<double>(m_viewport->GetHeight()) : 0.0);

        // 全局线型比例（HEADER 的 $LTSCALE）。虚线实际周期 = LTYPE图案周期 ×
        // 实体级组码 48 × 本值；实体拿不到 Scene，只能在这里经 sink 传下去。
        ctx.SetLineTypeScale(scene.GetLineTypeScale());
        // 复杂线型（带字母/符号）文字段的字高要查文字样式表（组码 340 → 样式）
        ctx.SetTextStyleTable(&scene.GetTextStyleTable());
        // 复杂线型形段（组码 75）的字形来自 SHX 形文件（ltypeshp.shx），
        // 与文字样式表无关，单独一条解析路径
        ctx.SetShapeFontResolver(MakeShapeFontResolver(m_doc));

        auto isShown = [&](const Entity& entity)
        {
            const auto& attr = entity.GetAttr();
            if (!attr.Visible) return false;
            // 布局过滤：模型页只画模型空间实体，布局页只画本布局的图纸空间实体。
            // 没有这一项，切换布局只能改相机、改不了画什么（每张布局都显示同一批内容）。
            if (!m_doc->IsVisibleInActiveLayout(attr)) return false;
            const Layer* layer = layerMgr.GetLayer(attr.LayerId);
            return !layer || layer->IsVisible();
        };

        // ── 只缩放：静态流不动，只重建动态流 ─────────────────
        // 随视图变化的实体（按像素等宽的线宽、点样式；细线显示时几乎没有）通常只占很少一部分；静态流不重新拼接、
        // 不重新裁剪、GPU 不重新上传（几百万顶点的图纸，每次滚轮原本要几百毫秒）
        // 分帧细分时细分到一半的块参照里有随视图变化的段：走下面的全量拼接（丢掉它重新细分）
        if (zoomChanged && !invalidateAll && !exclusionChanged && !m_sceneRestitch && scene.GetDirtyEntities().empty()
            && (!building || (m_build.queued && !m_build.part.viewDependent)))
        {
            for (Object::ObjectID id : m_dynOrder)
                if (auto it = m_entityVertexCache.find(id); it != m_entityVertexCache.end() && it->second.viewDependent)
                    m_entityVertexCache.erase(it);
            RebuildDynamicStream(ctx);
            scene.ClearDirty();
            if (building)
                ContinueBuild(ctx, dragIds, isShown);
            // 只重建了动态流，静态流与实体缓存都没变：不算"场景重建"——夹点、着色面不用跟着重建，
            // 选中流也只需重建随视图变化的那一路（否则选中全图后每格滚轮都要把选中流整体复制上传一遍）
            return false;
        }

        // 分帧细分、图纸没有别的改动：只接着细分，接到流尾（不算场景重建，同上）
        if (building && m_build.queued && !invalidateAll && !zoomChanged && !exclusionChanged && !m_sceneRestitch && !scene.IsDirty())
        {
            ContinueBuild(ctx, dragIds, isShown);
            return false;
        }

        // 布局页上的视口把模型空间内容合成进了自己的缓存（ComposeModelIntoViewport）：
        // 模型实体变了（含增删、撤销）时视口缓存也要丢掉，否则视口里还是旧内容
        bool modelTouchedInPaper = false;
        if (!invalidateAll && m_doc->ActiveLayoutIsPaper())
            for (auto id : scene.GetDirtyEntities())
            {
                const Object* obj = scene.GetEntity(id);
                if (!obj || !obj->IsKindOf<Entity>() || !static_cast<const Entity&>(*obj).GetAttr().InPaperSpace)
                {
                    modelTouchedInPaper = true;
                    break;
                }
            }

        // ── 增量路径 ────────────────────────────────────────
        // 几万实体的场景里新画 / 改 / 删一个图形:旧实体缓存全部有效,既不该重新细分,也不该把全场景顶点重新拼接一遍
        //（见 UpdateSceneIncremental）。拼接流已交给渲染后端释放时 CPU 端没有流可改：后端能接追加段、原地改写
        //（SetSceneAppendOnGpu）就把改动交给它，否则全量拼接（缓存命中，不重新细分；几千万顶点的图纸要几百毫秒）
        const bool appendOnGpu = m_sceneStreamsReleased && s_sceneAppendOnGpu;
        if (!invalidateAll && !zoomChanged && !m_sceneRestitch && !modelTouchedInPaper &&
            (!m_sceneStreamsReleased || appendOnGpu) &&
            UpdateSceneIncremental(ctx, dragIds, appendOnGpu, isShown))
        {
            if (building && m_build.queued)
                ContinueBuild(ctx, dragIds, isShown);
            return true;
        }

        // 全量拼接：之前没接上的追加段、改写段作废（这些实体的顶点在逐实体缓存里，下面会拼进去）
        m_sceneAppendActive = false;
        std::vector<Vertex_P3_C4>{}.swap(m_sceneAppendLines);
        std::vector<Vertex_P3_C4>{}.swap(m_sceneAppendFills);
        std::vector<Vertex_P3_C4>{}.swap(m_sceneAppendPoints);
        std::vector<ViewState::ScenePatch>{}.swap(m_scenePatches);
        std::vector<Vertex_P3_C4>{}.swap(m_scenePatchLines);
        std::vector<Vertex_P3_C4>{}.swap(m_scenePatchFills);
        std::vector<Vertex_P3_C4>{}.swap(m_scenePatchPoints);
        m_sceneWipeBoxes.clear();

        // ── 逐实体顶点缓存失效（全量拼接路径）────────────────
        // 全局失效时清空全部缓存;否则只丢弃脏实体的缓存,文字排版/标注
        // 绘制等重活只发生在真正变化的实体上。
        // 分帧细分到一半的块参照同样作废（它累积的几段按旧的显示设置 / 缩放细分）
        if (invalidateAll)
        {
            m_entityVertexCache.clear();
            DropBuildPart();
        }
        else
        {
            for (auto id : scene.GetDirtyEntities())   // 含已删除实体：顺带清理
                m_entityVertexCache.erase(id);
            if (modelTouchedInPaper)
                std::erase_if(m_entityVertexCache, [&scene](const auto& kv)
                {
                    const Object* obj = scene.GetEntity(kv.first);
                    return obj && obj->IsKindOf<ViewportEntity>();
                });
            if (zoomChanged && m_sceneViewDependent)
                std::erase_if(m_entityVertexCache, [](const auto& kv) { return kv.second.viewDependent; });
            if (zoomChanged && m_build.part.viewDependent)
                DropBuildPart();
        }
        // 这一轮没拼进去的实体（拖动中被排除的）位置作废
        for (auto& kv : m_entityVertexCache)
            kv.second.placed = false;

        m_sceneVertices.clear();
        m_sceneFillVertices.clear();
        m_scenePointVertices.clear();
        m_sceneDynVertices.clear();
        m_sceneDynFillVertices.clear();
        m_textVertices.clear();
        m_glyphInstances.clear();
        m_sceneImages.clear();
        m_dynOrder.clear();
        m_sceneViewDependent = false;

        // 第一遍：按绘制序收集要拼接的实体缓存（缓存未命中的就地细分），并统计总顶点数；
        // 第二遍一次预留后再拼接，避免几百万顶点的顶点流反复扩容拷贝
        std::vector<std::pair<Object::ObjectID, EntityVertexCache*>> visible;
        visible.reserve(m_entityVertexCache.size());
        std::size_t lineCount = 0, fillCount = 0, textCount = 0, glyphCount = 0, imageCount = 0;
        const auto stitchStart = std::chrono::steady_clock::now();
        if (building)
        {
            m_build.queue.clear();
            m_build.next  = 0;
            m_build.total = m_build.done = 0.0;
        }

        scene.ForEachObject([&](const Object& obj)
        {
           if (!obj.IsKindOf<Entity>()) return;
           const auto& entity = static_cast<const Entity&>(obj);

           // 图层关闭或实体自身隐藏:不产出顶点
           if (!isShown(entity)) return;

           // 拖动跟随中的实体由选中流绘制
           if (dragIds.contains(obj.GetID())) return;

           // 选中/悬停高亮均不烘焙进场景顶点（选中走选中流、悬停走 overlay），
           // 因此选择集/悬停变化都不会触发整场景顶点重建。
           Object::ObjectID id = obj.GetID();
           auto it = m_entityVertexCache.find(id);
           EntityVertexCache* cached = it != m_entityVertexCache.end() ? &it->second : nullptr;
           if (building)
           {
               // 分帧细分：缓存里没有的不当帧细分，按绘制序排进队列，由下面的 ContinueBuild 细分到本帧时限
               const double weight = BuildWeight(entity);
               m_build.total += weight;
               if (cached)
                   m_build.done += weight;
               else
               {
                   m_build.queue.push_back(id);
                   if (id != m_build.partId)
                       return;
                   // 细分到一半的大块参照：已细分的几段照常按绘制序拼进去（不登记进动态流的重建序，整个细分完再登记）。
                   // 后面几段里记下的区域覆盖先裁剪它们，否则重新拼进去的是没裁过的
                   ClipBuildPart();
                   cached = &m_build.part;
                   id     = Object::InvalidID;
               }
           }
           else if (!cached)
           {
               EntityVertexCache c;
               TessellateEntity(entity, ctx, c);   // 缓存未命中：重新细分该实体
               cached = &m_entityVertexCache.emplace(id, std::move(c)).first->second;
           }

           auto& c = *cached;
           visible.emplace_back(id, &c);
           if (!c.viewDependent)
           {
               lineCount += c.lines.size();
               fillCount += c.fills.size();
           }
           textCount  += c.texts.size();
           glyphCount += c.glyphs.size();
           imageCount += c.images.size();
        });

        // 遮罩（wipeout）会裁剪已拼接的顶点，裁剪可能多出少量顶点，预留只是下限
        m_sceneVertices.reserve(lineCount);
        m_sceneFillVertices.reserve(fillCount);
        m_textVertices.reserve(textCount);
        m_glyphInstances.reserve(glyphCount);
        m_sceneImages.reserve(imageCount);
        {
            WipeClip::Batch wipes(7);
            for (const auto& [id, c] : visible)
                AppendToSceneStreams(id, *c, wipes, m_sceneVertices, m_sceneFillVertices, m_scenePointVertices);
            FlushSceneWipes(wipes);
        }
        SortSceneGlyphs();

        m_sceneExcluded = std::move(dragIds);

        m_sceneVersion    = NextRenderVersion();   // 通知渲染后端：场景顶点已变化，需重新上传 GPU 缓冲
        m_sceneDynVersion = NextRenderVersion();
        m_sceneStreamsReleased = false;
        m_sceneRestitch        = false;
        UpdateSceneStats();

        if (building)
        {
            // 这次就是按绘制序的全量拼接：此前追加的、改动的都已重新拼过。接着在 CPU 端的流上细分。
            // 后端接不了追加段时每帧都走这里，拼接越来越慢：本帧细分至少是拼接用时的两倍，免得大半时间花在拼接上
            m_build.queued      = true;
            m_build.finalStitch = false;
            const auto now = std::chrono::steady_clock::now();
            m_build.deadline = std::max(m_build.deadline, now + 2 * (now - stitchStart));
            ContinueBuild(ctx, m_sceneExcluded, isShown);
        }

        scene.ClearDirty();
        return true;
    }

    void Editor::BeginProgressiveBuild(double budgetMs)
    {
        m_build          = {};
        m_build.active   = budgetMs > 0.0;
        m_build.budgetMs = budgetMs;
    }

    float Editor::DisplayBuildProgress() const
    {
        if (!m_build.active) return 1.0f;
        if (!(m_build.total > 0.0)) return 0.0f;
        return static_cast<float>(std::clamp((m_build.done + m_build.partDone) / m_build.total, 0.0, 1.0));
    }

    void Editor::DropBuildPart()
    {
        m_build.partId = Object::InvalidID;
        m_build.partResume.clear();
        m_build.partDone = 0.0;
        m_build.part     = {};
        m_build.partWipes.clear();
    }

    // 细分到一半的大块参照：后面几段里的区域覆盖裁剪它之前的顶点（记在 partWipes 里，同 DrawContext 在一个实体里的做法），裁完清空
    void Editor::ClipBuildPart()
    {
        if (m_build.partWipes.empty()) return;
        EntityVertexCache& p = m_build.part;
        WipeClip::Batch own(5);
        for (const auto& [w, ends] : m_build.partWipes)
            own.Add(w, { ends[0], ends[1], ends[2], ends[3], ends[4] });
        own.ClipLines(p.lines, 0);
        own.ClipFills(p.fills, 1);
        own.ClipTexts(p.texts, 2);
        own.ClipPoints(p.points, 4);
        own.ClipGlyphs(p.glyphs, 3, 0, m_glyphLibrary, p.lines, p.fills);
        m_build.partWipes.clear();
    }

    // 进度的权重：普通实体 1，块参照按块内要画的子实体个数（嵌套的块参照展开计，同 DrawContext 的分段计数），按块定义缓存
    double Editor::BuildWeight(const Entity& entity)
    {
        const auto* ins = dynamic_cast<const InsertEntity*>(&entity);
        if (!ins || !ins->GetBlock())
            return 1.0;
        std::function<double(const BlockEntity&)> leaves = [&](const BlockEntity& block) -> double
        {
            if (const auto it = m_build.blockWeights.find(&block); it != m_build.blockWeights.end())
                return it->second;
            m_build.blockWeights[&block] = 0.0;   // 块自己套自己时到这里停
            double n = 0.0;
            for (const auto& e : block.GetEntities())
                if (const auto* sub = dynamic_cast<const InsertEntity*>(e.get()); sub && sub->GetBlock())
                    n += leaves(*sub->GetBlock()) * sub->GetColumnCount() * sub->GetRowCount() + static_cast<double>(sub->GetAttribs().size());
                else
                    n += 1.0;
            m_build.blockWeights[&block] = n;
            return n;
        };
        return std::max(1.0, leaves(*ins->GetBlock()) * ins->GetColumnCount() * ins->GetRowCount() + static_cast<double>(ins->GetAttribs().size()));
    }

    // 分帧细分：把新细分的一段接到场景流尾部。拼接流还在 CPU 端（全量拼接的那一帧）就直接接上，区域覆盖照常裁剪此前的顶点；
    // 已交给后端就放进追加段，位置接在后端已有的顶点之后，区域覆盖裁不到已上传的顶点，留到画全后的全量拼接。
    // whole：piece 是整个实体，记下它的位置（之后修改时原地改写）、登记进动态流的重建序；大块参照的一段只接顶点
    void Editor::AppendBuildPiece(Object::ObjectID id, EntityVertexCache& piece, bool whole, WipeClip::Batch& wipes, bool& glyphs, bool& dyn)
    {
        if (!m_sceneStreamsReleased)
            AppendToSceneStreams(whole ? id : Object::InvalidID, piece, wipes, m_sceneVertices, m_sceneFillVertices, m_scenePointVertices);
        else
        {
            const size_t l0 = m_sceneAppendLines.size(), f0 = m_sceneAppendFills.size(), p0 = m_sceneAppendPoints.size();
            WipeClip::Batch unused(7);
            AppendToSceneStreams(whole ? id : Object::InvalidID, piece, unused, m_sceneAppendLines, m_sceneAppendFills, m_sceneAppendPoints);
            if (!piece.viewDependent)
            {
                piece.lineOff = m_sceneStats.lines + (piece.lineOff - l0);
                piece.fillOff = m_sceneStats.fills + (piece.fillOff - f0);
                m_sceneStats.lines += piece.lines.size();
                m_sceneStats.fills += piece.fills.size();
            }
            piece.pointOff = m_sceneStats.points + (piece.pointOff - p0);
            m_sceneStats.points += piece.points.size();
            m_build.finalStitch |= !piece.wipes.empty();
        }
        glyphs |= !piece.glyphs.empty() || !piece.texts.empty() || !piece.wipes.empty();
        dyn    |= piece.viewDependent || !piece.longDashes.empty() || !piece.wipes.empty();
    }

    void Editor::ContinueBuild(DrawContext& ctx, const std::unordered_set<Object::ObjectID>& dragIds,
                               const std::function<bool(const Entity&)>& isShown)
    {
        auto& scene = m_doc->GetScene();
        WipeClip::Batch wipes(7);
        bool appended = false, glyphs = false, dyn = false, rebuildDyn = false;
        // 大块参照的一段并进已细分的几段。这一段里的区域覆盖要裁剪本实体前几段的顶点：先记下来，整个实体细分完一次裁剪
        //（每段都裁一遍已累积的几百万个顶点太慢），缓存里的整个实体与一次细分完的一致。
        // 流里已接上的前几段：拼接流在 CPU 端时由 AppendBuildPiece 照常裁剪，已交给后端时留到画全后的全量拼接
        auto merge = [this](EntityVertexCache& to, EntityVertexCache& from)
        {
            if (!to.lines.empty() || !to.fills.empty() || !to.texts.empty() || !to.glyphs.empty() || !to.points.empty())
                for (const auto& w : from.wipes)
                    m_build.partWipes.push_back({ w, { to.lines.size(), to.fills.size(), to.texts.size(), to.glyphs.size(), to.points.size() } });
            auto add = [](auto& a, auto& b) { a.insert(a.end(), std::make_move_iterator(b.begin()), std::make_move_iterator(b.end())); };
            add(to.lines, from.lines);
            add(to.fills, from.fills);
            add(to.points, from.points);
            add(to.texts, from.texts);
            add(to.glyphs, from.glyphs);
            add(to.longDashes, from.longDashes);
            add(to.images, from.images);
            add(to.wipes, from.wipes);
            to.viewDependent |= from.viewDependent;
        };

        // 每帧至少细分一个实体（大块参照至少一个子实体），之后到时限就停
        for (bool first = true; m_build.next < m_build.queue.size(); first = false)
        {
            if (!first && std::chrono::steady_clock::now() >= m_build.deadline)
                break;
            const Object::ObjectID id = m_build.queue[m_build.next];
            const Object* obj = scene.GetEntity(id);
            const Entity* entity = obj && obj->IsKindOf<Entity>() ? static_cast<const Entity*>(obj) : nullptr;
            // 排队之后被删掉、隐藏、拖动，或已由增量更新细分过的：跳过
            if (!entity || dragIds.contains(id) || m_entityVertexCache.contains(id) || !isShown(*entity))
            {
                if (id == m_build.partId)
                    DropBuildPart();
                m_build.done += entity ? BuildWeight(*entity) : 1.0;
                ++m_build.next;
                continue;
            }

            EntityVertexCache piece;
            // 块参照按块内子实体分段：到时限就停，下一帧从停下的子实体接着画
            bool fresh = true, complete = true;
            if (dynamic_cast<const InsertEntity*>(entity))
            {
                fresh = id != m_build.partId;
                if (fresh)
                {
                    DropBuildPart();
                    m_build.partId = id;
                }
                ctx.BeginChunk(std::move(m_build.partResume), m_build.deadline);
                TessellateEntity(*entity, ctx, piece);
                m_build.partDone += static_cast<double>(ctx.EndChunk(m_build.partResume));
                complete = m_build.partResume.empty();
            }
            else
                TessellateEntity(*entity, ctx, piece);
            appended = true;

            if (fresh && complete)
            {
                // 一次画完（普通实体，或一段就画完的块参照）：整个接上，位置连续，之后修改能原地改写
                AppendBuildPiece(id, piece, true, wipes, glyphs, dyn);
                m_entityVertexCache.emplace(id, std::move(piece));
                if (id == m_build.partId)
                    DropBuildPart();
            }
            else
            {
                AppendBuildPiece(id, piece, false, wipes, glyphs, dyn);
                merge(m_build.part, piece);
                if (!complete)
                    break;
                // 最后一段：后面几段里的区域覆盖裁剪它之前的顶点，整个实体进缓存。它分几段接进流里、位置不连续，
                // 修改时只能全量拼接（placed 为假）；随视图变化的部分登记进动态流的重建序，重建一次动态流把它作为整个实体排进去
                ClipBuildPart();
                EntityVertexCache& c = m_entityVertexCache.emplace(id, std::move(m_build.part)).first->second;
                c.placed = false;
                if (c.viewDependent || !c.wipes.empty() || !c.longDashes.empty())
                {
                    m_dynOrder.push_back(id);
                    m_sceneViewDependent |= c.viewDependent || !c.longDashes.empty();
                    rebuildDyn = true;
                }
                // 有的段随视图变化、有的不：不随的那几段已进静态流，整个实体却按随视图变化进动态流，画全后全量拼接一次免得画两遍
                m_build.finalStitch |= c.viewDependent;
                DropBuildPart();
            }
            m_build.done += BuildWeight(*entity);
            ++m_build.next;
        }

        if (appended)
        {
            if (!m_sceneStreamsReleased)
                FlushSceneWipes(wipes);
            if (rebuildDyn)
                RebuildDynamicStream(ctx);
            else if (dyn)
                m_sceneDynVersion = NextRenderVersion();
            if (glyphs)
                SortSceneGlyphs();
            if (m_sceneStreamsReleased)
            {
                // 追加段接在后端当前持有的版本之后；上一段还没上传成功时继续累积在同一段里（同 UpdateSceneIncremental）
                if (!m_sceneAppendActive)
                {
                    m_sceneAppendActive = true;
                    m_sceneAppendBase   = m_sceneVersion;
                }
                m_sceneStats.texts  = m_textVertices.size();
                m_sceneStats.glyphs = m_glyphSorted.size();
            }
            else
                UpdateSceneStats();
            m_sceneVersion = NextRenderVersion();
        }

        m_build.lastEnd = std::chrono::steady_clock::now();
        // 全部细分完：追加段里有没裁剪的区域覆盖、或细分期间图纸有改动时，下一帧全量拼接一次（全部命中缓存）再结束
        if (m_build.next >= m_build.queue.size() && m_build.partId == Object::InvalidID)
        {
            if (m_build.finalStitch)
                m_sceneRestitch = true;
            else
                m_build.active = false;
        }
    }

    void Editor::ReleaseUploadedSceneStreams()
    {
        // 追加段、改写段已写进后端的缓冲
        if (m_sceneAppendActive)
        {
            m_sceneAppendActive = false;
            std::vector<Vertex_P3_C4>{}.swap(m_sceneAppendLines);
            std::vector<Vertex_P3_C4>{}.swap(m_sceneAppendFills);
            std::vector<Vertex_P3_C4>{}.swap(m_sceneAppendPoints);
            std::vector<ViewState::ScenePatch>{}.swap(m_scenePatches);
            std::vector<Vertex_P3_C4>{}.swap(m_scenePatchLines);
            std::vector<Vertex_P3_C4>{}.swap(m_scenePatchFills);
            std::vector<Vertex_P3_C4>{}.swap(m_scenePatchPoints);
        }
        if (m_sceneStreamsReleased) return;
        // 与空容器交换才归还存储（clear 保留容量）
        std::vector<Vertex_P3_C4>{}.swap(m_sceneVertices);
        std::vector<Vertex_P3_C4>{}.swap(m_sceneFillVertices);
        std::vector<Vertex_P3_C4>{}.swap(m_scenePointVertices);
        // 后端能接追加段时文字与字形实例留在 CPU 端（每个字 36 字节，远小于线与填充）：改了带文字的实体只重排、
        // 重传字形实例，不必重新拼接线与填充
        if (!s_sceneAppendOnGpu)
        {
            std::vector<Vertex_P3_C4_UV>{}.swap(m_textVertices);
            std::vector<GlyphInstance>{}.swap(m_glyphInstances);
            std::vector<GlyphInstance>{}.swap(m_glyphSorted);
            std::vector<GlyphBatch>{}.swap(m_glyphBatches);
        }
        m_sceneStreamsReleased = true;
    }

    void Editor::UpdateSceneStats()
    {
        m_sceneStats = { .lines = m_sceneVertices.size(), .fills = m_sceneFillVertices.size(), .texts = m_textVertices.size(),
                         .glyphs = m_glyphSorted.size(), .glyphMeshes = m_glyphLibrary.View().Meshes.size(),
                         .points = m_scenePointVertices.size() };
    }

    void Editor::SortSceneGlyphs()
    {
        SortGlyphInstances(m_glyphInstances, m_glyphSorted, m_glyphBatches, m_glyphLibrary.View().Meshes.size());
        m_sceneGlyphVersion = NextRenderVersion();
    }

    void Editor::WriteScenePatch(uint32_t stream, size_t offset, std::span<const Vertex_P3_C4> verts, size_t total)
    {
        if (total == 0) return;
        auto& data = stream == 0 ? m_scenePatchLines : stream == 1 ? m_scenePatchFills : m_scenePatchPoints;
        m_scenePatches.push_back({ stream, offset, data.size(), total });
        data.insert(data.end(), verts.begin(), verts.end());
        // 删掉的部分写成透明的退化图元（零长线、零面积三角形、透明的点），混合后不留痕迹
        data.resize(data.size() + (total - verts.size()), Vertex_P3_C4{ { 0.f, 0.f, 0.f }, VertexColor{} });
    }

    // ── 增量更新 ───────────────────────────────────────────────────
    // 脏实体只有几个、其余实体缓存全部有效时不重新拼接整张图（几千万顶点的图纸每次要几百毫秒）：
    //  · 新实体（缓存里没有）：细分后接到流尾——CPU 端还有拼接流就直接追加；拼接流已释放时放进追加段，后端接到 GPU 缓冲末尾
    //  · 修改 / 删除 / 开始或结束拖动的实体（缓存里有、位置已知）：只在后端能原地改写时处理——旧的那一段改写成透明的退化图元，
    //    新顶点放得下就写回原位，放不下就接到流尾；字形实例在拼接序的流里作废、追加，再按网格重新分批；随视图变化的部分重建动态流
    // 处理不了的（区域覆盖相关、纹理文字、图像、被裁剪挪动过的段、CPU 端拼接流上的修改）返回 false，一个顶点都不动，由调用方全量拼接
    bool Editor::UpdateSceneIncremental(DrawContext& ctx, const std::unordered_set<Object::ObjectID>& dragIds, bool appendOnGpu,
                                        const std::function<bool(const Entity&)>& isShown)
    {
        auto& scene = m_doc->GetScene();
        const auto& dirty = scene.GetDirtyEntities();

        // 要处理的实体：脏的、开始拖动的（离开场景流）、拖完的（回到场景流）。ID 单调分配 = 加入场景的先后序，
        // 按升序接到流尾保持绘制序稳定
        std::vector<Object::ObjectID> ids(dirty.begin(), dirty.end());
        for (auto id : dragIds)
            if (!dirty.count(id)) ids.push_back(id);
        for (auto id : m_sceneExcluded)
            if (!dirty.count(id) && !dragIds.count(id)) ids.push_back(id);
        if (ids.empty()) return false;
        std::sort(ids.begin(), ids.end());
        ids.erase(std::unique(ids.begin(), ids.end()), ids.end());

        struct Work
        {
            Object::ObjectID  id;
            const Entity*     entity = nullptr;   // 要（重新）拼进去的实体；空表示只移除
            EntityVertexCache fresh;              // 它的新细分结果
        };
        std::vector<Work> work;
        work.reserve(ids.size());
        for (auto id : ids)
        {
            const Object* obj    = scene.GetEntity(id);
            const Entity* entity = obj && obj->IsKindOf<Entity>() ? static_cast<const Entity*>(obj) : nullptr;
            const bool    show   = entity && !dragIds.count(id) && isShown(*entity);
            if (const auto it = m_entityVertexCache.find(id); it != m_entityVertexCache.end())
            {
                // 已在流里的实体：只有后端能原地改写、它那段位置可信、既没裁剪过别人（区域覆盖）也没被裁剪挪动过时才增量
                const EntityVertexCache& old = it->second;
                if (!appendOnGpu || !old.placed || !old.wipes.empty() || !old.texts.empty() || !old.images.empty())
                    return false;
            }
            else if (!show)
                continue;   // 从没拼进去、现在也不显示（如删了一个隐藏的实体）：没什么可改

            Work w{ id, show ? entity : nullptr };
            if (show)
            {
                TessellateEntity(*entity, ctx, w.fresh);
                // 追加段接不了区域覆盖：它要裁剪此前的顶点，而拼接流已不在 CPU 端。新几何碰到已拼进去的区域覆盖也得重新裁剪
                if (appendOnGpu && !w.fresh.wipes.empty())
                    return false;
                if (!m_sceneWipeBoxes.empty())
                {
                    AABB box = AABB::Empty();
                    auto expand = [&](const std::vector<Vertex_P3_C4>& vs)
                    {
                        for (const auto& v : vs) box.Expand({ v.pos.x, v.pos.y, v.pos.z });
                    };
                    expand(w.fresh.lines);
                    expand(w.fresh.fills);
                    expand(w.fresh.points);
                    for (const auto& v : w.fresh.texts) box.Expand({ v.pos.x, v.pos.y, v.pos.z });
                    for (const GlyphInstance& g : w.fresh.glyphs)
                    {
                        double x0, y0, x1, y1;
                        m_glyphLibrary.Bounds(g, x0, y0, x1, y1);
                        box.Expand({ x0, y0, g.z });
                        box.Expand({ x1, y1, g.z });
                    }
                    for (const AABB& wb : m_sceneWipeBoxes)
                        if (box.Intersects(wb)) return false;
                }
            }
            work.push_back(std::move(w));
        }

        // 从这里起改流
        bool glyphsChanged = false, dynChanged = false;
        WipeClip::Batch wipes(7);
        for (Work& w : work)
        {
            // 旧段改写成透明的退化图元；新顶点放得下时下面再覆盖回原位
            size_t oldLineOff = 0, oldLines = 0, oldFillOff = 0, oldFills = 0, oldPointOff = 0, oldPoints = 0;
            if (auto it = m_entityVertexCache.find(w.id); it != m_entityVertexCache.end())
            {
                EntityVertexCache& old = it->second;
                if (!old.viewDependent)
                {
                    oldLineOff = old.lineOff; oldLines = old.lines.size();
                    oldFillOff = old.fillOff; oldFills = old.fills.size();
                }
                oldPointOff = old.pointOff;
                oldPoints   = old.points.size();
                WriteScenePatch(0, oldLineOff, {}, oldLines);
                WriteScenePatch(1, oldFillOff, {}, oldFills);
                WriteScenePatch(2, oldPointOff, {}, oldPoints);
                for (size_t k = 0; k < old.glyphs.size(); ++k)
                    m_glyphInstances[old.glyphOff + k].mesh = kGlyphInstanceDead;
                glyphsChanged |= !old.glyphs.empty();
                if (old.viewDependent || !old.longDashes.empty())
                {
                    std::erase(m_dynOrder, w.id);
                    dynChanged = true;
                }
                m_entityVertexCache.erase(it);
            }
            if (!w.entity) continue;

            EntityVertexCache& c = m_entityVertexCache.emplace(w.id, std::move(w.fresh)).first->second;
            if (!appendOnGpu)
            {
                // CPU 端还有拼接流：直接接到流尾（新实体的区域覆盖在下面裁剪此前的顶点）
                glyphsChanged |= !c.glyphs.empty() || !c.texts.empty() || !c.wipes.empty();
                AppendToSceneStreams(w.id, c, wipes, m_sceneVertices, m_sceneFillVertices, m_scenePointVertices);
                continue;
            }
            // 静态线 / 填充 / 点：放得下就写回原位，否则接到流尾（追加段）
            auto place = [&](uint32_t stream, const std::vector<Vertex_P3_C4>& verts, size_t oldOff, size_t oldCount,
                             std::vector<Vertex_P3_C4>& out, size_t& total)
            {
                if (oldCount && verts.size() <= oldCount)
                {
                    WriteScenePatch(stream, oldOff, verts, oldCount);
                    return oldOff;
                }
                const size_t off = total;
                out.insert(out.end(), verts.begin(), verts.end());
                total += verts.size();
                return off;
            };
            if (!c.viewDependent)
            {
                c.lineOff = place(0, c.lines, oldLineOff, oldLines, m_sceneAppendLines, m_sceneStats.lines);
                c.fillOff = place(1, c.fills, oldFillOff, oldFills, m_sceneAppendFills, m_sceneStats.fills);
            }
            c.pointOff = place(2, c.points, oldPointOff, oldPoints, m_sceneAppendPoints, m_sceneStats.points);
            c.glyphOff = m_glyphInstances.size();
            m_glyphInstances.insert(m_glyphInstances.end(), c.glyphs.begin(), c.glyphs.end());
            m_textVertices.insert(m_textVertices.end(), c.texts.begin(), c.texts.end());
            m_sceneImages.insert(m_sceneImages.end(), c.images.begin(), c.images.end());
            glyphsChanged |= !c.glyphs.empty() || !c.texts.empty();
            if (c.viewDependent || !c.longDashes.empty())
            {
                m_dynOrder.push_back(w.id);
                dynChanged = true;
                m_sceneViewDependent = true;
            }
            c.placed = true;
        }

        if (!appendOnGpu)
            FlushSceneWipes(wipes);
        if (dynChanged)
            RebuildDynamicStream(ctx);
        if (glyphsChanged)
            SortSceneGlyphs();
        if (appendOnGpu)
        {
            // 追加段 / 改写段接在后端当前持有的版本之后；上一段还没上传成功时继续累积在同一段里
            if (!m_sceneAppendActive)
            {
                m_sceneAppendActive = true;
                m_sceneAppendBase   = m_sceneVersion;
            }
            m_sceneStats.texts  = m_textVertices.size();
            m_sceneStats.glyphs = m_glyphSorted.size();
        }
        else
            UpdateSceneStats();
        m_sceneVersion    = NextRenderVersion();   // 顶点流已变，渲染后端需重新上传（或接上追加段、写入改写段）
        m_sceneDynVersion = NextRenderVersion();
        m_sceneExcluded   = dragIds;
        scene.ClearDirty();
        return true;
    }

    void Editor::ForEachCachedVertex(const std::function<void(const Vertex_P3_C4&)>& fn) const
    {
        for (const auto& [id, c] : m_entityVertexCache)
        {
            for (const auto& v : c.lines) fn(v);
            for (const auto& v : c.fills) fn(v);
        }
    }

    // 用记录下的区域覆盖裁剪此前已拼接的静态流与动态流
    void Editor::FlushSceneWipes(WipeClip::Batch& wipes)
    {
        if (wipes.Empty()) return;
        wipes.ClipLines(m_sceneVertices, 0);
        wipes.ClipFills(m_sceneFillVertices, 1);
        wipes.ClipTexts(m_textVertices, 2);
        wipes.ClipLines(m_sceneDynVertices, 3);
        wipes.ClipFills(m_sceneDynFillVertices, 4);
        wipes.ClipPoints(m_scenePointVertices, 6);
        // 被遮罩盖到的字形实例展开成顶点裁剪，追加到静态流（在静态流自身的裁剪之后）
        wipes.ClipGlyphs(m_glyphInstances, 5, 0, m_glyphLibrary, m_sceneVertices, m_sceneFillVertices);
        // 裁剪挪动过的段起，之后各实体记下的位置不再可信：作废，UpdateSceneIncremental 对这些实体只能全量拼接
        //（之后再拼进去的实体位置按裁剪后的流记，不受影响）
        const size_t tLines = wipes.FirstTouched(0), tFills = wipes.FirstTouched(1), tPoints = wipes.FirstTouched(6), tGlyphs = wipes.FirstTouched(5);
        if (tLines == SIZE_MAX && tFills == SIZE_MAX && tPoints == SIZE_MAX && tGlyphs == SIZE_MAX) return;
        for (auto& [id, c] : m_entityVertexCache)
        {
            if (!c.placed) continue;
            if ((!c.viewDependent && (c.lineOff + c.lines.size() > tLines || c.fillOff + c.fills.size() > tFills)) ||
                c.pointOff + c.points.size() > tPoints || (!c.glyphs.empty() && c.glyphOff + c.glyphs.size() > tGlyphs))
                c.placed = false;
        }
    }

    // 选中流：选中实体 + 拖动跟随中的实体，按选中态绘制，叠加在场景之上。
    // 与悬停高亮同思路：覆盖绘制代替烘焙，选中变化不触发全场景重建。
    void Editor::UpdateSelectionVertices(double worldPerPixel, bool force, bool zoomOnly)
    {
        if (!force) return;

        auto& scene = m_doc->GetScene();

        // 静态部分：从逐实体缓存复制来的、不随视图变化的实体；只缩放时不动
        if (!zoomOnly)
        {
            m_selVertices.clear();
            m_selFillVertices.clear();
            m_selTextVertices.clear();
        }
        // 随视图变化的部分每次都重建：线宽 / 点样式实体、点图元的小十字、长虚线段、实时绘制的实体
        m_selDynVertices.clear();
        m_selDynFillVertices.clear();
        m_selDynTextVertices.clear();

        const auto& selection = m_picking.GetSelection();
        const auto  dragIds   = m_gripEditor.GetDraggingIDs();

        if (!selection.empty() || !dragIds.empty())
        {
            const auto& layerMgr = scene.GetLayerManager();

            DrawContext ctx(m_selDynVertices, m_selDynFillVertices, m_selDynTextVertices, m_overlay,
                            layerMgr, scene.GetLineTypeTable(), worldPerPixel,
                            m_glyphProvider, MakeFontResolver(m_doc));
            ctx.SetThinLines(m_thinLines);
            // 选中流的虚线周期必须与场景流一致，否则同一实体选中前后线型疏密不同
            ctx.SetLineTypeScale(scene.GetLineTypeScale());
            ctx.SetTextStyleTable(&scene.GetTextStyleTable());
            // 形段同上：选中前后线型的符号必须完全一致
            ctx.SetShapeFontResolver(MakeShapeFontResolver(m_doc));
            // 选中流没有点流一路：线型的点笔划画成 1.5px 小段（随缩放重建）
            ctx.SetLineTypeDotsAsSegments(true);
            // 点样式同理：不设置时按默认 $PDMODE=0 画成点图元，⊕ 等图形选中后不显示
            ctx.SetPointStyle(scene.GetPointDisplayMode(), scene.GetPointDisplaySize());
            ctx.SetViewportHeightPx(static_cast<double>(m_viewport->GetHeight()));

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

                // 块裁剪（XCLIP）：选中流同样是叠加绘制，块参照必须按裁剪后的样子画，
                // 否则选中一个被裁剪的块会把它整块画出来。
                //
                // 例外：正在拖**基点**时按**完整**图块绘制（临时忽略裁剪）——
                // 拖动基点 = 在固定的取景窗口下搬动图元，只画框内那一小块会让人
                // 以为图块缺了图元，也看不清要放到哪里。
                auto* ins = dynamic_cast<InsertEntity*>(obj);
                const bool showWhole = ins && m_gripEditor.IsDraggingBasePoint(id);
                if (ins)
                    ins->SetShowWholeWhileDraggingBase(showWhole);

                ctx.BeginEntity(&attr);
                entity.Draw(ctx, /*isSelected*/ true, false);
                ctx.BeginEntity(nullptr);

                if (ins)
                    ins->SetShowWholeWhileDraggingBase(false);

                // 裁剪边框线：$XCLIPFRAME 允许显示时才画，用选中高亮色。
                // 边框画在**裁剪之后**，否则它自己会被裁掉。
                if (ins && ins->HasClipBoundary() && ins->IsClipFrameVisible())
                {
                    const auto& poly = ins->GetClipBoundary();
                    for (size_t i = 0; i < poly.size(); ++i)
                        ctx.DrawLine(poly[i], poly[(i + 1) % poly.size()],
                                     IDrawSink::kSelectionColor, false);
                }
            };

            // 选中流优先从逐实体缓存复制：场景已经把每个实体细分成线、填充、字形实例，改成选中色即可，
            // 不再调用 Draw 重新细分（选中整张图从 400 ms 降到一次拷贝）。
            // 拖动中的实体几何逐帧变化、缓存里没有的实体（隐藏、带光栅图像）仍实时绘制
            const std::unordered_set<Object::ObjectID> dragSet(dragIds.begin(), dragIds.end());
            const Math::Color4& sc = IDrawSink::kSelectionColor;
            const VertexColor   selColor(static_cast<float>(sc.r), static_cast<float>(sc.g), static_cast<float>(sc.b), static_cast<float>(sc.a));
            const Math::Float4  selColor4{ static_cast<float>(sc.r), static_cast<float>(sc.g), static_cast<float>(sc.b), static_cast<float>(sc.a) };
            const GlyphMeshView  glyphView = m_glyphLibrary.View();
            const LineTypeTable& lineTypes = scene.GetLineTypeTable();
            bool copiedViewDependent = false;

            auto cachedFor = [&](Object::ObjectID id) -> const EntityVertexCache*
            {
                if (dragSet.contains(id)) return nullptr;
                auto it = m_entityVertexCache.find(id);
                if (it == m_entityVertexCache.end() || !it->second.images.empty()) return nullptr;
                return &it->second;
            };
            if (!zoomOnly)
            {
                size_t lines = 0, fills = 0;
                for (auto id : selection)
                    if (const EntityVertexCache* c = cachedFor(id); c && !c->viewDependent)
                    {
                        lines += c->lines.size();
                        fills += c->fills.size();
                    }
                m_selVertices.reserve(lines);
                m_selFillVertices.reserve(fills);
            }
            auto appendRecolored = [&](std::vector<Vertex_P3_C4>& out, const std::vector<Vertex_P3_C4>& in)
            {
                const size_t n = out.size();
                out.resize(n + in.size());
                for (size_t i = 0; i < in.size(); ++i)
                {
                    out[n + i].pos   = in[i].pos;
                    out[n + i].color = selColor;
                }
            };
            auto copyOne = [&](Object::ObjectID id) -> bool
            {
                const EntityVertexCache* c = cachedFor(id);
                if (!c) return false;
                // 随视图变化的实体（线宽、点样式）缩放后缓存已重新细分，复制进动态部分；其余进静态部分（只缩放时跳过）
                if (c->viewDependent || !zoomOnly)
                {
                    auto& lines = c->viewDependent ? m_selDynVertices     : m_selVertices;
                    auto& fills = c->viewDependent ? m_selDynFillVertices : m_selFillVertices;
                    auto& texts = c->viewDependent ? m_selDynTextVertices : m_selTextVertices;
                    appendRecolored(lines, c->lines);
                    appendRecolored(fills, c->fills);
                    for (const auto& t : c->texts)
                    {
                        texts.push_back(t);
                        texts.back().color = selColor4;
                    }
                    for (const GlyphInstance& g : c->glyphs)
                    {
                        ExpandGlyph(g, glyphView, true,  [&](const Vertex_P3_C4& v) { fills.push_back({ v.pos, selColor }); });
                        ExpandGlyph(g, glyphView, false, [&](const Vertex_P3_C4& v) { lines.push_back({ v.pos, selColor }); });
                    }
                }
                for (const auto& p : c->points)   // 选中流没有点流：点图元画成小十字（随缩放重建）
                {
                    AppendPointMark(m_selDynVertices, p.pos, selColor, worldPerPixel);
                    copiedViewDependent = true;
                }
                if (!c->longDashes.empty())
                {
                    std::vector<Vertex_P3_C4> dashes;
                    for (const DeferredDash& d : c->longDashes)
                        DrawContext::EmitDeferredDash(d, lineTypes, worldPerPixel, dashes);
                    appendRecolored(m_selDynVertices, dashes);
                    copiedViewDependent = true;
                }
                copiedViewDependent |= c->viewDependent;
                return true;
            };

            for (auto id : selection)
                if (!copyOne(id))
                    drawOne(id);
            for (auto id : dragIds)
                if (!selection.contains(id))
                    drawOne(id);

            m_selViewDependent = ctx.HasViewDependentGeometry() || copiedViewDependent;

            // 选中流只有线段：点图元（$PDMODE=0 的 POINT、点阵填充）画成小十字，随缩放重建
            const auto points = ctx.TakePoints();
            for (const auto& p : points)
                AppendPointMark(m_selDynVertices, p.pos, p.color, worldPerPixel);
            if (!points.empty())
                m_selViewDependent = true;
        }
        else
        {
            m_selViewDependent = false;
        }

        if (!zoomOnly)
            m_selVersion = NextRenderVersion();
        m_selDynVersion = NextRenderVersion();
        m_picking.ClearDirty();
    }

    ViewState Editor::BuildViewState()
    {
        auto& scene = m_doc->GetScene();

        ViewState vs;

        vs.Scene        = m_sceneVertices;
        vs.SceneFill    = m_sceneFillVertices;
        vs.ScenePoints  = m_scenePointVertices;
        vs.Overlay      = m_overlayVertices;
        vs.TextScene    = m_textVertices;
        vs.Images       = m_sceneImages;
        vs.SceneVersion = m_sceneVersion;
        vs.SceneAppendActive = m_sceneAppendActive;
        vs.SceneAppendBase   = m_sceneAppendBase;
        vs.SceneAppend       = m_sceneAppendLines;
        vs.SceneAppendFill   = m_sceneAppendFills;
        vs.SceneAppendPoints = m_sceneAppendPoints;
        vs.ScenePatches      = m_scenePatches;
        vs.ScenePatchLines   = m_scenePatchLines;
        vs.ScenePatchFills   = m_scenePatchFills;
        vs.ScenePatchPoints  = m_scenePatchPoints;
        vs.SceneDyn        = m_sceneDynVertices;
        vs.SceneDynFill    = m_sceneDynFillVertices;
        vs.SceneDynVersion = m_sceneDynVersion;
        vs.Shaded          = m_shadedVertices;
        vs.Silhouettes     = m_silhouetteVertices;
        vs.ShadedVersion   = m_shadedVersion;
        if (!m_viewport->GetCamera().IsPlanView())
        {
            // 三维视图的深度范围：按场景版本缓存（全图遍历一次）
            if (!m_sceneBoundsValid || m_sceneBoundsVersion != m_sceneVersion)
            {
                m_sceneBounds = AABB::Empty();
                scene.GetExtents(m_sceneBounds);
                m_sceneBoundsVersion = m_sceneVersion;
                m_sceneBoundsValid   = true;
            }
            vs.SceneBounds = m_sceneBounds;
            if (m_previewBounds.Min.x <= m_previewBounds.Max.x)
                vs.SceneBounds.Expand(m_previewBounds);   // 节点图预览也不能被深度范围裁掉
        }
        vs.Glyphs          = m_glyphSorted;
        vs.GlyphBatches    = m_glyphBatches;
        vs.GlyphMeshes     = m_glyphLibrary.View();
        vs.SceneGlyphVersion = m_sceneGlyphVersion;
        vs.SelScene     = m_selVertices;
        vs.SelFill      = m_selFillVertices;
        vs.SelText      = m_selTextVertices;
        vs.SelVersion   = m_selVersion;
        vs.SelDyn        = m_selDynVertices;
        vs.SelDynFill    = m_selDynFillVertices;
        vs.SelDynText    = m_selDynTextVertices;
        vs.SelDynVersion = m_selDynVersion;
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
        vs.ShowCrosshair = !HidesCrosshair();

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
