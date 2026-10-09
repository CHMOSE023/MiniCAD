#include "DocumentManager.h"
#include "Document.h"
#include "Core/Log.h"
#include "Import/CadExchange.h"
#include <algorithm>
#include <utility>
#include <memory>
#include <string>
#include <filesystem>
#include <exception>
#include "Text/FontSystem.h"
#include "Core/Entity/MTextEntity.hpp"
#include "Core/Entity/HatchEntity.hpp"
#include "Core/Entity/PolylineEntity.hpp"
#include "Core/Entity/DimensionEntity.hpp"
#include "Core/Entity/ArcEntity.hpp"
#include "Core/Entity/LeaderEntity.hpp"
#include "Core/Entity/MLeaderEntity.hpp"
#include "Core/Entity/ToleranceEntity.hpp"
#include "Core/Entity/XLineEntity.hpp"
#include "Core/Entity/RayEntity.hpp"
#include "Core/Math/Point3.hpp"
namespace MiniCAD
{
    namespace
    {
        std::filesystem::path FilePath(const std::string& utf8)
        {
            return std::filesystem::path(std::u8string(utf8.begin(), utf8.end()));
        }

        bool SameFile(const std::string& a, const std::string& b)
        {
            if (a == b) return true;
            std::error_code ec;
            const auto pa = FilePath(a), pb = FilePath(b);
            // equivalent respects the filesystem's case rules and also identifies hard links.
            if (std::filesystem::equivalent(pa, pb, ec) && !ec) return true;
            const auto ca = std::filesystem::weakly_canonical(pa, ec);
            if (ec) return false;
            const auto cb = std::filesystem::weakly_canonical(pb, ec);
            return !ec && ca == cb;
        }

        // 活动文档的相机状态实时存在于 viewport(切换文档时才回写到文档)，
        // 存盘前需主动同步，否则写出的 camera 永远是上次激活时的旧值。
        void SyncActiveCameraState(Document* active, Viewport* viewport)
        {
            if (active && viewport)
                active->SetCameraState(viewport->GetCamera().GetState());
        }
    }

    Document& DocumentManager::Create()
    {
        auto doc = std::make_unique<Document>();
        doc->SetName(GenerateUniqueName());
        doc->SetFontSystem(m_fontSystem);
       
        if (m_active && m_viewport) // 切换前保存当前文档的视口状态，以便切换回来时恢复
        {
            m_active->SetCameraState(m_viewport->GetCamera().GetState());
        } 
        // 测试代码：创建一些示例实体展示字体样式和标注功能
        // DocumentDrawTest(doc.get());
        m_active = doc.get();
        m_docs.push_back(std::move(doc));

        if (m_viewport)
        {
            m_viewport->GetCamera().SetState(m_active->GetCameraState());
            m_editor.Bind(*m_active, *m_viewport);
        }
        m_active->MarkDirty();
        return *m_active;
    }

	void DocumentManager::DocumentDrawTest(Document* doc)
	{
		// 这个函数里创建了一堆示例实体，展示了字体样式、图案填充、尺寸标注等功能。
		// 你可以在 Create() 里调用它，或者根据需要修改/删除。

        auto styleId = doc->GetScene().GetTextStyleTable().FindByName("GB2312");
        auto styleId1 = 0;

        doc->GetScene().AddEntity(std::make_unique<MTextEntity>(doc->GetScene().NextObjectID(), styleId, "TTF >>> 仿宋字体  GB2312.ttf  ", Math::Point3(1, 2, 0), 1, 0, 100));
        doc->GetScene().AddEntity(std::make_unique<MTextEntity>(doc->GetScene().NextObjectID(), styleId1, "SHX >>> 探索者中文字体 %%% %%p %%c20 %%p0.5 %%13225 %%1318@200  4%%132 %%132   %%132%%131%%130 TSSDCHN.SHX L1 梁 %%132 25 @ 200mm  ", Math::Point3(1, 0.5, 0), 1, 0, 100));
        
        // 添加图层
		auto layerId1 = doc->GetLayerManager().AddLayer("Layer 1");
        doc->GetLayerManager().GetLayer(layerId1)->SetColor({ 1.0,0,0,1 });
		auto layerId2 = doc->GetLayerManager().AddLayer("Layer 2");
        doc->GetLayerManager().GetLayer(layerId2)->SetColor({ 1.0,1.0,0,1 });
         
        // ── MTEXT 新特性预览（附着点 / 行距 / 分栏 / 内联格式码）──────────────────
        {
            auto& scene = doc->GetScene();

            // 在同一插入点用 9 种附着点放置文字,直观验证对齐(组码 71)。
            const Math::Point3 anchor(-30, 0, 0);
            const MTextAttachment attaches[9] = {
                MTextAttachment::TopLeft,    MTextAttachment::TopCenter,    MTextAttachment::TopRight,
                MTextAttachment::MiddleLeft, MTextAttachment::MiddleCenter, MTextAttachment::MiddleRight,
                MTextAttachment::BottomLeft, MTextAttachment::BottomCenter, MTextAttachment::BottomRight,
            };
            for (int i = 0; i < 9; ++i)
            {
                auto t = std::make_unique<MTextEntity>(
                    scene.NextObjectID(), styleId1, "Anchor-" + std::to_string(i + 1),
                    Math::Point3(anchor.x, anchor.y - i * 2.0, 0), 1.0, 0.0, 0.0);
                t->SetAttachment(attaches[i]);
                EntityAttr a; a.Color = { 0.8, 0.8, 0.4, 1.0 };
                t->SetAttr(a);
                scene.AddEntity(std::move(t));
            }

            // 内联格式码:\C 颜色、\H 字高、\P 段落换行;度量时被剥离。
            auto fmt = std::make_unique<MTextEntity>(
                scene.NextObjectID(), styleId1,
                "Inline: \\C1;red \\H1.5x;big\\P second\\~line {\\C5;grouped}",
                Math::Point3(-30, -22, 0), 1.0, 0.0, 30.0);
            fmt->SetLineSpacing(MTextLineSpacing::Exact, 1.5);
            EntityAttr fa; fa.Color = { 0.5, 0.85, 0.95, 1.0 };
            fmt->SetAttr(fa);
            scene.AddEntity(std::move(fmt));

            // 静态分栏:2 栏,栏宽 12,栏间距 2(组码 75/76/48/49)。
            auto col = std::make_unique<MTextEntity>(
                scene.NextObjectID(), styleId1,
                "Column A line 1\\Pline 2\\Pline 3 spilling to column B",
                Math::Point3(-30, -28, 0), 1.0, 0.0, 12.0);
            col->SetColumns(MTextColumnType::Static, 2, 12.0, 2.0);
            col->SetColumnHeights({ 6.0, 6.0 });
            EntityAttr ca; ca.Color = { 0.9, 0.6, 0.9, 1.0 };
            col->SetAttr(ca);
            scene.AddEntity(std::move(col));
        }

        // ── 图案填充预览 ─────────────────────────────────────────────────────
        auto addHatch = [&](double ox, double oy, HatchPattern pat, const Math::Color4& color)
            {
                // 带一个三角形孔岛的方形外轮廓（演示奇偶规则挖空）
                std::vector<Math::Point3> outer{
                    {ox,      oy,      0}, {ox + 8,  oy,      0},
                    {ox + 8,  oy + 8,  0}, {ox,      oy + 8,  0} };
                std::vector<Math::Point3> hole{
                    {ox + 2,  oy + 2,  0}, {ox + 6,  oy + 3,  0}, {ox + 3,  oy + 6,  0} };

                std::vector<Polyline> loops{ Polyline(outer), Polyline(hole) };
                auto h = std::make_unique<HatchEntity>(doc->GetScene().NextObjectID(), std::move(loops), std::move(pat));
                h->SetScale(1.0);
                EntityAttr attr; attr.Color = color;
                h->SetAttr(attr);
                doc->GetScene().AddEntity(std::move(h));

                // 图案四周创建边界 PolylineEntity（闭合，线宽 0 → 细线绘制）
                auto addBoundary = [&](std::vector<Math::Point3> pts)
                    {
                        pts.push_back(pts.front());   // 闭合
                        auto pl = std::make_unique<PolylineEntity>(doc->GetScene().NextObjectID(), std::move(pts));
                        EntityAttr a; a.Color = color;
                        pl->SetAttr(a);
                        pl->SetWidth(0.0);   // 线宽 0 → 细线绘制
                        doc->GetScene().AddEntity(std::move(pl));
                    };
                addBoundary(outer);
                addBoundary(hole);
            };

        addHatch(0, 12, HatchPattern::ANSI31(0.6), { 0.9, 0.3, 0.3, 1.0 }); // 45° 斜剖面线
        addHatch(10, 12, HatchPattern::MakeNet(0.0, 0.6), { 0.3, 0.7, 0.9, 1.0 }); // 正交网格
        addHatch(20, 12, HatchPattern::MakeSolid(), { 0.4, 0.8, 0.4, 0.6 }); // 半透明实心
        addHatch(30, 12, HatchPattern::MakeLines(20.0, 0.5), { 0.9, 0.7, 0.2, 1.0 }); // 单向斜线

        // ── 尺寸标注预览（符合中国工程制图国家标准）────────────────────────
        auto addDim = [&](const Math::Point3& p1, const Math::Point3& p2,
            const Math::Point3& dimLinePt, DimTerminator term,
            const Math::Color4& color)
            {
                DimStyle st;
                st.TextStyle = styleId;   // 尺寸数字使用 GB2312 仿宋字体
                st.TextHeight = 2.5;
                st.ArrowSize = 2.5;
                st.Terminator = term;
                auto d = std::make_unique<DimensionEntity>(
                    doc->GetScene().NextObjectID(), p1, p2, dimLinePt, st);
                EntityAttr a; a.Color = color;
                d->SetAttr(a);
                doc->GetScene().AddEntity(std::move(d));
            };

        // 水平线性标注：箭头终端（机械制图 GB/T 4458.4）
        addDim({ 0, -6, 0 }, { 20, -6, 0 }, { 10, -10, 0 },
            DimTerminator::Arrow, { 0.9, 0.9, 0.4, 1.0 });
        // 水平线性标注：45° 斜线终端（房屋建筑制图 GB/T 50001）
        addDim({ 25, -6, 0 }, { 45, -6, 0 }, { 35, -10, 0 },
            DimTerminator::Oblique, { 0.4, 0.9, 0.9, 1.0 });
        // 垂直线性标注：文字自动保持字头朝上
        addDim({ 50, -6, 0 }, { 50, 6, 0 }, { 56, 0, 0 },
            DimTerminator::Arrow, { 0.9, 0.6, 0.9, 1.0 });

        // ── 标注新子类型预览（角度 / 半径 / 直径 / 坐标 / 基线 / 连续）─────────
        {
            auto& scene = doc->GetScene();

            DimStyle st;
            st.TextStyle = styleId;   // 尺寸数字使用 GB2312 仿宋字体
            st.TextHeight = 2.5;
            st.ArrowSize = 2.5;

            auto place = [&](std::unique_ptr<DimensionEntity> d, const Math::Color4& color)
                {
                    EntityAttr a; a.Color = color;
                    d->SetAttr(a);
                    scene.AddEntity(std::move(d));
                };

            // 角度标注：顶点 (0,-25)，两边指向 (15,-25) 与 (10,-15)，弧经过点定半径与标注侧
            place(DimensionEntity::MakeAngular(scene.NextObjectID(),
                { 0, -25, 0 }, { 15, -25, 0 }, { 10, -15, 0 }, { 9, -20, 0 }, st),
                { 0.9, 0.9, 0.4, 1.0 });

            // 半径标注：圆心 (35,-22)，圆周点 (43,-18)，文字前缀 R
            place(DimensionEntity::MakeRadius(scene.NextObjectID(),
                { 35, -22, 0 }, { 43, -18, 0 }, st),
                { 0.4, 0.9, 0.9, 1.0 });

            // 直径标注：贯穿圆心两端 (50,-25)→(62,-19)，文字前缀 ⌀
            place(DimensionEntity::MakeDiameter(scene.NextObjectID(),
                { 50, -25, 0 }, { 62, -19, 0 }, st),
                { 0.9, 0.6, 0.9, 1.0 });

            // 坐标标注：特征点 (72,-16) 的 X 基准坐标，引线引到 (72,-26)
            place(DimensionEntity::MakeOrdinate(scene.NextObjectID(),
                { 72, -16, 0 }, { 72, -26, 0 }, OrdinateAxis::X, st),
                { 0.6, 0.9, 0.5, 1.0 });

            // 基线标注：以一条水平线性标注为基准，向外再叠两道共起点标注
            auto base = DimensionEntity::MakeAligned(scene.NextObjectID(),
                { 0, -32, 0 }, { 10, -32, 0 }, { 5, -36, 0 }, st);
            const Math::Color4 baseCol{ 0.9, 0.7, 0.3, 1.0 };
            auto b1 = DimensionEntity::MakeBaseline(scene.NextObjectID(), *base, { 22, -32, 0 }, 1);
            auto b2 = DimensionEntity::MakeBaseline(scene.NextObjectID(), *base, { 30, -32, 0 }, 2);
            place(std::move(base), baseCol);
            place(std::move(b1), baseCol);
            place(std::move(b2), baseCol);

            // 连续标注：首尾相接的一串水平线性标注
            auto c0 = DimensionEntity::MakeAligned(scene.NextObjectID(),
                { 40, -32, 0 }, { 48, -32, 0 }, { 44, -36, 0 }, st);
            const Math::Color4 contCol{ 0.5, 0.8, 0.9, 1.0 };
            auto c1 = DimensionEntity::MakeContinuous(scene.NextObjectID(), *c0, { 58, -32, 0 });
            auto c2 = DimensionEntity::MakeContinuous(scene.NextObjectID(), *c1, { 66, -32, 0 });
            place(std::move(c0), contCol);
            place(std::move(c1), contCol);
            place(std::move(c2), contCol);

            // 弧长标注：圆心 (85,-28)、半径 6、20°～120° 的圆弧，标注弧在外侧，文字前缀 ⌒
            const double d2r = Math::PI / 180.0;
            const Math::Point3 ac{ 85, -28, 0 };
            scene.AddEntity(std::make_unique<ArcEntity>(scene.NextObjectID(), ac, 6.0, 20.0 * d2r, 120.0 * d2r));
            place(DimensionEntity::MakeArcLength(scene.NextObjectID(), ac,
                ac + Math::Vec3{ std::cos(20.0 * d2r), std::sin(20.0 * d2r), 0 } * 6.0,
                ac + Math::Vec3{ std::cos(120.0 * d2r), std::sin(120.0 * d2r), 0 } * 6.0,
                ac + Math::Vec3{ std::cos(70.0 * d2r), std::sin(70.0 * d2r), 0 } * 10.0, st),
                { 0.9, 0.5, 0.4, 1.0 });

            // 折弯半径：大圆弧（圆心 (110,-60) 在图外，半径 40），尺寸线从替代圆心 (112,-30) 折弯连到弧上
            const Math::Point3 jc{ 110, -60, 0 };
            scene.AddEntity(std::make_unique<ArcEntity>(scene.NextObjectID(), jc, 40.0, 60.0 * d2r, 100.0 * d2r));
            place(DimensionEntity::MakeJoggedRadius(scene.NextObjectID(), jc,
                jc + Math::Vec3{ std::cos(80.0 * d2r), std::sin(80.0 * d2r), 0 } * 40.0,
                { 112, -30, 0 }, { 115, -26, 0 }, st),
                { 0.5, 0.9, 0.6, 1.0 });
        }

        // ── 引线 / 多重引线 / 形位公差预览（LEADER / MULTILEADER / TOLERANCE）──
        {
            auto& scene = doc->GetScene();

            DimStyle dst;
            dst.TextStyle = styleId;
            dst.TextHeight = 2.5;
            dst.ArrowSize = 2.5;

            // 单引线：折线 + 起端箭头 + 末端基线(hookline) + 文字标签
            auto ld = std::make_unique<LeaderEntity>(scene.NextObjectID(),
                std::vector<Math::Point3>{ { 0, -42, 0 }, { 6, -46, 0 }, { 14, -46, 0 } }, dst);
            ld->SetText("R 倒角");
            EntityAttr la; la.Color = { 0.9, 0.8, 0.4, 1.0 };
            ld->SetAttr(la);
            scene.AddEntity(std::move(ld));

            // 多重引线：两条引线汇聚到公共基线 + dogleg + MTEXT 内容
            MLeaderStyle ms;
            ms.TextStyle = styleId;
            ms.TextHeight = 2.5;
            ms.ArrowSize = 2.5;
            auto ml = std::make_unique<MLeaderEntity>(scene.NextObjectID(), ms);
            ml->SetLanding({ 40, -46, 0 });
            ml->SetDoglegDir({ -1, 0, 0 });   // 内容在右，dogleg 指回左侧引线
            ml->AddLeaderLine({ { 24, -42, 0 }, { 30, -45, 0 } });
            ml->AddLeaderLine({ { 26, -50, 0 }, { 31, -47, 0 } });
            ml->SetText("2× 标记");
            EntityAttr ma; ma.Color = { 0.5, 0.9, 0.7, 1.0 };
            ml->SetAttr(ma);
            scene.AddEntity(std::move(ml));

            // 形位公差：位置度框格，%%v 分隔单元，^J 分隔多行
            auto tol = std::make_unique<ToleranceEntity>(scene.NextObjectID(),
                Math::Point3(56, -42, 0), "POS%%v0.05%%vA%%vB%%vC", dst);
            EntityAttr ta; ta.Color = { 0.9, 0.6, 0.9, 1.0 };
            tol->SetAttr(ta);
            scene.AddEntity(std::move(tol));
        }

        // ── 构造线 / 射线预览（XLINE / RAY）──────────────────────────────────
        {
            auto& scene = doc->GetScene();

            // 无限构造线:过 (60,0) 的水平线 + 过 (60,0) 的 45° 斜线
            auto xl1 = std::make_unique<XLineEntity>(scene.NextObjectID(), Math::Point3(60, 0, 0), Math::Vec3(1, 0, 0));
            EntityAttr xa1; xa1.Color = { 0.6, 0.6, 0.6, 1.0 };
            xl1->SetAttr(xa1);
            scene.AddEntity(std::move(xl1));

            auto xl2 = std::make_unique<XLineEntity>(scene.NextObjectID(),
                Math::Point3(60, 0, 0), Math::Vec3(1, 1, 0));
            EntityAttr xa2; xa2.Color = { 0.5, 0.5, 0.7, 1.0 };
            xl2->SetAttr(xa2);
            scene.AddEntity(std::move(xl2));

            // 半无限射线:从 (60,0) 沿 +Y 与 -X 方向
            auto ray1 = std::make_unique<RayEntity>(scene.NextObjectID(),
                Math::Point3(60, 0, 0), Math::Vec3(0, 1, 0));
            EntityAttr ra1; ra1.Color = { 0.9, 0.5, 0.3, 1.0 };
            ray1->SetAttr(ra1);
            scene.AddEntity(std::move(ray1));

            auto ray2 = std::make_unique<RayEntity>(scene.NextObjectID(),
                Math::Point3(60, 0, 0), Math::Vec3(-1, 0, 0));
            EntityAttr ra2; ra2.Color = { 0.3, 0.9, 0.5, 1.0 };
            ray2->SetAttr(ra2);
            scene.AddEntity(std::move(ray2));
        }
	}

    void DocumentManager::InitViewport(IRenderer& renderer, float w, float h)
    {
        m_viewport = std::make_unique<Viewport>(w, h);

        if (m_active)
            m_editor.Bind(*m_active, *m_viewport);
    }

    void DocumentManager::Close(Document* doc)
    {
        auto it = std::find_if(m_docs.begin(), m_docs.end(),
            [&](const auto& d) { return d.get() == doc; });

        if (it == m_docs.end())
            return;

        if (m_active == doc)
        {
            m_editor.Unbind();
            m_active = nullptr;
        }

        m_docs.erase(it);

        if (!m_docs.empty() && m_active == nullptr)
        {
            m_active = m_docs.back().get();
            if (m_viewport)
                m_editor.Bind(*m_active, *m_viewport);
        }
    }

    Document* DocumentManager::GetActive() const
    {
        return m_active;
    }

    void DocumentManager::SetActive(Document* doc)
    {
        if (m_active == doc) return;

		if (m_active && m_viewport) // 切换前保存当前文档的视口状态，以便切换回来时恢复
        {
            m_active->SetCameraState(m_viewport->GetCamera().GetState());
        }

        m_editor.Unbind();
        m_active = doc;

		if (m_active && m_viewport) // 切换后恢复新活动文档的视口状态，并绑定编辑器
        {
            m_viewport->GetCamera().SetState(m_active->GetCameraState());
            m_editor.Bind(*m_active, *m_viewport);
        }
    }

    void DocumentManager::SetFontSystem(FontSystem* fontSystem)
    {
        m_fontSystem = fontSystem;
    }

    FontStyle::FontStyleId DocumentManager::RegisterFontStyle(FontStyle style)
    {
        if (!m_fontSystem) return 0;
        return m_fontSystem->RegisterStyle(std::move(style));
    }

    const FontStyle* DocumentManager::FindFontStyle(const std::string& name) const
    {
        if (!m_fontSystem) return nullptr;
        return m_fontSystem->FindStyle(name);
    }

    const FontStyle* DocumentManager::FindFontStyle(FontStyle::FontStyleId id) const
    {
        if (!m_fontSystem) return nullptr;
        return m_fontSystem->FindStyle(id);
    }

    std::vector<std::unique_ptr<Document>>& DocumentManager::GetAll()
    {
        return m_docs;
    }

    void DocumentManager::SetRenderer(IRenderer* renderer)
    {
        m_renderer = renderer;
    }

    void DocumentManager::New()
    {
        if (!m_renderer)
            return;

        auto& doc = Create();

        SetActive(&doc);
    }

    void DocumentManager::Open()
    {
        if (!m_fileDialog)
        {
            LOG_WARN("Open: no file dialog handler installed");
            return;
        }

        const std::string path = m_fileDialog(/*save=*/false);
        if (path.empty())
            return;   // 用户取消

        Open(path);
    }

    void DocumentManager::ZoomAll(bool undoable)
    {
        if (m_active)
            m_editor.ZoomAll(undoable);
    }

    Document* DocumentManager::Open(const std::string& path, std::string* error)
    {
        if (error) error->clear();
        // 已打开同一文件:直接激活,不重复加载。
        for (auto& d : m_docs)
        {
            if (!d->GetPath().empty() && SameFile(d->GetPath(), path))
            {
                SetActive(d.get());
                return d.get();
            }
        }

        auto doc = std::make_unique<Document>();
        doc->SetFontSystem(m_fontSystem);

        try
        {
            if (!doc->LoadFromFile(path, error))
                return nullptr;
        }
        catch (const std::exception& e)
        {
            if (error) *error = std::string("读取图纸失败：") + e.what();
            LOG_ERROR("Open failed: %s (%s)", path.c_str(), e.what());
            return nullptr;
        }

        if (m_active && m_viewport) // 切换前保存当前文档的视口状态
        {
            m_active->SetCameraState(m_viewport->GetCamera().GetState());
        }

        m_active = doc.get();
        m_docs.push_back(std::move(doc));

        if (m_viewport)
        {
            m_viewport->GetCamera().SetState(m_active->GetCameraState());
            m_editor.Bind(*m_active, *m_viewport);
            if (CadKindFromPath(path) != CadFileKind::None)
                ZoomAll(false);   // 外来图纸的坐标可能离原点很远；初始视图不进撤销栈
        }

        return m_active;
    }

    void DocumentManager::Save()
    {
        if (!m_active)
            return;

        if (!m_active->HasPath())   // 从未保存过:转「另存为」
        {
            SaveAs();
            return;
        }

        SyncActiveCameraState(m_active, m_viewport.get());   // 存盘前回写活动文档的相机状态
        m_active->Save();
    }

    void DocumentManager::SaveAs()
    {
        if (!m_active)
            return;

        if (!m_fileDialog)
        {
            LOG_WARN("SaveAs: no file dialog handler installed");
            return;
        }

        const std::string path = m_fileDialog(/*save=*/true);
        if (path.empty())
            return;   // 用户取消

        SyncActiveCameraState(m_active, m_viewport.get());   // 同 Save():存盘前回写活动文档的相机状态
        m_active->SaveAs(path);
    }

    void DocumentManager::SaveAll()
    {
        SyncActiveCameraState(m_active, m_viewport.get());   // 只有活动文档的相机在 viewport 上是实时的，其余已在切换时回写
        for (auto& doc : m_docs)
        {
            doc->Save();
        }
    }

    void DocumentManager::Undo() const
    {
        if (auto* doc = GetActive())
            doc->Undo();
    }

    void DocumentManager::Redo() const
    {
        if (auto* doc = GetActive())
            doc->Redo();
    }

    void DocumentManager::CopySelected()
    {
        m_clipboard.clear();
        m_clipboardHasBase = false;   // 普通复制无基点,粘贴回退到包围盒左下角

        if (!m_active)
            return;

        Scene& scene = m_active->GetScene();
        for (auto id : m_editor.GetSelection())
        {
            auto* obj = scene.GetEntity(id);
            if (!obj || !obj->IsKindOf<Entity>())
                continue;

            // 克隆快照存入剪贴板;ID 仅占位,粘贴时按目标场景重新分配
            m_clipboard.push_back(static_cast<Entity*>(obj)->Clone(id));
        }

        LOG_DEBUG("CopySelected: %zu entities", m_clipboard.size());
    }

    void DocumentManager::CopySelectedWithBase()
    {
        // 先复制快照:拾取工具激活时会清空当前选中,顺序不能反
        CopySelected();
        if (m_clipboard.empty())
            return;

        m_editor.GetCmdLine().Echo("命令: CopyBase");
        m_editor.StartPickPointTool("指定基点:", [this](const Math::Point3& p)
        {
            m_clipboardBase    = p;
            m_clipboardHasBase = true;
        });
        // 右键/ESC 取消拾取时不置基点,效果等同普通复制
    }

    void DocumentManager::CutSelected()
    {
        // 剪切 = 复制 + 删除：锁定图层上的对象不能删除，所以也不进剪贴板，选择集只留可修改的对象
        {
            const auto editable = m_editor.EditableSelectionIds();
            if (editable.empty())
                return;
            m_editor.SetSelection({ editable.begin(), editable.end() });
        }
        CopySelected();

        // 没复制到任何实体(空选择)就不删,避免误删不可复制的选中对象
        if (m_clipboard.empty())
            return;

        // 删除经 BatchDeleteCommand 入撤销栈;撤销可恢复被剪切的实体
        m_editor.DeleteSelected();
    }

    void DocumentManager::Paste()
    {
        if (!m_active || m_clipboard.empty())
            return;

        // 启动交互式粘贴:剪贴板实体跟随光标预览,左键指定插入点(类 AutoCAD)
        // 标脏由 CommandStack 的 OnMutate 回调统一处理
        m_editor.StartPasteTool(&m_clipboard, nullptr,
                                m_clipboardHasBase ? &m_clipboardBase : nullptr);
    }

    std::string DocumentManager::GenerateUniqueName()
    {
        int index = 0;

        while (true)
        {
            std::string name = "Untitled";
            if (index > 0)
                name += " " + std::to_string(index);

            bool exists = false;
            for (auto& d : m_docs)
            {
                if (d->GetName() == name)
                {
                    exists = true;
                    break;
                }
            }

            if (!exists)
                return name;

            index++;
        }
    }

}
