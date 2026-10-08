// ── Editor 发出的请求 → 界面：文字 / 多行文字原位编辑、定义块、插入块、阵列、图案填充、文字样式；关于 ─────────
// Editor 只置请求（GetTextInputRequest 等），这里在命令状态变化时检查请求，打开或关闭对应的界面，
// 确认后回调 Editor 的 Submit*。对话框都是 MiniGUI 的弹层。
#include "GUI/MainFrame.h"
#include "Core/UIContext.h"
#include "Document/Document.h"
#include "Scene/BlockTable.h"
#include "Scene/HatchPatternLibrary.h"
#include "Core/Draw/IDrawSink.hpp"
#include "Core/Entity/HatchEntity.hpp"
#include "Core/Entity/TextEntity.hpp"
#include "Document/TextStyleResolver.h"
#include <filesystem>
#include "Style/Theme.hpp"
#include "Widgets/Button.h"
#include "Widgets/ComboBox.h"
#include "Widgets/Controls.h"
#include "Widgets/Dialog.h"
#include "Widgets/Label.h"
#include "Widgets/ListView.h"
#include "Widgets/NumberBox.h"
#include "Widgets/Panel.h"
#include "Widgets/TextBox.h"
#include "Widgets/ViewportHost.h"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>


namespace MiniCAD
{
    namespace
    {
        namespace Theme = MiniGUI::Theme;

        // 视口每世界单位对应的屏幕像素数（正交相机，各向同性）
        double PixelsPerUnit(const Camera& cam)
        {
            const auto a = cam.WorldToScreen({ 0.0, 0.0, 0.0 });
            const auto b = cam.WorldToScreen({ 1.0, 0.0, 0.0 });
            return std::abs(b.x - a.x);
        }

        std::string Trimmed(const std::string& s)
        {
            const size_t b = s.find_first_not_of(" \t");
            if (b == std::string::npos)
                return {};
            return s.substr(b, s.find_last_not_of(" \t") - b + 1);
        }

        // 对话框里的一行："名称 [控件]"，名称列对齐
        template<typename T, typename... Args>
        T* AddRow(MiniGUI::Node* parent, const char* name, Args&&... args)
        {
            using namespace MiniGUI;
            Node* row = parent->AddChild<Node>();
            LayoutStyle s;
            s.direction  = FlexDirection::Row;
            s.alignItems = Align::Center;
            s.gap        = 8.0f;
            row->SetLayoutStyle(s);
            Label* l = row->AddChild<Label>(name, 13.0f, Theme::TextDim);
            l->EditLayoutStyle().width = 72.0f;
            T* control = row->AddChild<T>(std::forward<Args>(args)...);
            control->EditLayoutStyle().width = 150.0f;
            return control;
        }

        MiniGUI::LayoutStyle ColumnStyle(float gap)
        {
            MiniGUI::LayoutStyle s;
            s.direction = MiniGUI::FlexDirection::Column;
            s.gap       = gap;
            return s;
        }
    }

    // =========================================================
    // 请求 → 界面
    // =========================================================
    void MainFrame::SyncEditorRequests()
    {
        if (!m_ui || !m_viewport)
            return;
        Document* doc    = m_docManager.GetActive();
        Editor&   editor = m_docManager.GetEditor();

        const bool text  = doc && editor.GetTextInputRequest().Active;
        const bool mtext = doc && editor.GetMTextInputRequest().Active;
        if ((text || mtext) && !m_textPopup)
            OpenTextEditor(mtext);
        else if (!text && !mtext && m_textPopup)
            m_textPopup->Close();       // 请求被撤销（切换文档、撤销…）

        // 插入图像：选择图像文件（Win32 为模态的系统对话框，网页版异步），选定后启动放置工具；取消则撤销请求
        if (doc && editor.GetImageRequest().Active)
        {
            editor.GetImageRequest().Active = false;
            m_platform->PickFile(FileKind::Image, [this, doc](const std::string& path)
            {
                if (m_docManager.GetActive() != doc)
                    return;     // 选择期间切换了文档
                m_docManager.GetEditor().SubmitImagePath(path);
                m_viewport->Focus();
                StateChanged();
            });
        }

        const bool blockName = doc && editor.GetBlockNameRequest().Active;
        if (blockName && !m_blockNameDialog)
            OpenBlockNameDialog();
        else if (!blockName && m_blockNameDialog)
            m_blockNameDialog->Close();

        const bool insert = doc && editor.GetBlockInsertRequest().Active;
        if (insert && !m_insertDialog)
            OpenBlockInsertDialog();
        else if (!insert && m_insertDialog)
            m_insertDialog->Close();

        // 阵列：拾取环形中心时暂时关闭对话框，拾取完成后重新打开（参数保存在请求里）
        const bool textStyle = doc && editor.GetTextStyleRequest().Active;
        if (textStyle && !m_textStyleDialog)
            OpenTextStyleDialog();
        else if (!textStyle && m_textStyleDialog)
            m_textStyleDialog->Close();

        const bool hatch = doc && editor.GetHatchRequest().Active;
        if (hatch && !m_hatchDialog)
            OpenHatchDialog();
        else if (!hatch && m_hatchDialog)
            m_hatchDialog->Close();

        const auto& array = editor.GetArrayRequest();
        const bool  arrayOpen = doc && array.Active && !array.PickingCenter;
        if (arrayOpen && !m_arrayDialog)
            OpenArrayDialog();
        else if (!arrayOpen && m_arrayDialog)
        {
            m_arrayPicking = doc && array.Active && array.PickingCenter;
            m_arrayDialog->Close();
        }
    }

    // =========================================================
    // 文字 / 多行文字：原位编辑（AutoCAD 式，不弹对话框）
    // 编辑框覆盖在插入点的屏幕位置上，字号跟随视口缩放；
    // Enter（单行）/ Ctrl+Enter（多行）或点击编辑框外提交，Esc 取消
    // =========================================================
    void MainFrame::OpenTextEditor(bool multiline)
    {
        using namespace MiniGUI;
        Editor&       editor = m_docManager.GetEditor();
        const Camera& cam    = m_docManager.GetViewport().GetCamera();
        const double  ppu    = PixelsPerUnit(cam);

        Math::Point3 insert;
        double       height = 2.5, boxWidth = 0.0;
        std::string  initial;
        if (multiline)
        {
            const auto& r = editor.GetMTextInputRequest();
            insert = r.InsertPos; height = r.Height; boxWidth = r.BoxWidth;
            if (r.EditTargetId != Object::InvalidID)
                initial = r.InitialText;
        }
        else
        {
            const auto& r = editor.GetTextInputRequest();
            insert = r.InsertPos; height = r.Height;
            if (r.EditTargetId != Object::InvalidID)
                initial = r.InitialText;
        }

        // 视口像素 → 窗口逻辑坐标；字号与画布上的文字高度一致（限幅，避免过小看不清、过大不便编辑）
        const Rect   vb = m_viewport->GetScreenBounds();
        const auto   sp = cam.WorldToScreen(insert);
        const float  hLogical = static_cast<float>(height * ppu) / m_dpiScale;
        const float  fontSize = std::clamp(hLogical, 11.0f, 72.0f);
        // 单行文字的插入点是首行左下角，编辑框上移一个字高；多行文字（左上附着）插入点即左上角
        Vec2 pos{ vb.min.x + static_cast<float>(sp.x) / m_dpiScale - 6.0f,
                  vb.min.y + static_cast<float>(sp.y) / m_dpiScale - (multiline ? 6.0f : fontSize * 1.4f + 6.0f) };

        auto popup = std::make_unique<Popup>();
        PopupStyle ps;
        ps.rounding = 0.0f;
        ps.shadow   = false;
        ps.border   = Theme::Accent;
        popup->SetStyle(ps);
        popup->SetPoint(pos);
        popup->SetCloseOnEscape(false);         // Esc 由编辑框处理：取消
        LayoutStyle s = ColumnStyle(4.0f);
        s.padding = Edges::All(3.0f);
        popup->SetLayoutStyle(s);

        TextBox* box = popup->AddChild<TextBox>(initial);
        TextBoxStyle ts;
        ts.fontSize   = fontSize;
        ts.rounding   = 0.0f;
        ts.border     = Colors::Transparent;
        ts.borderHover = Colors::Transparent;
        ts.borderFocus = Colors::Transparent;
        box->SetStyle(ts);
        const float minWidth = fontSize * (multiline ? 16.0f : 8.0f);
        const float fixedWidth = multiline && boxWidth > 0.0 ? static_cast<float>(boxWidth * ppu) / m_dpiScale : 0.0f;
        box->EditLayoutStyle().width = std::max(fixedWidth > 0.0f ? fixedWidth : minWidth, fontSize * 6.0f);
        if (multiline)
        {
            box->SetMultiline(true);
            box->SetRows(3);
            popup->AddChild<Label>("Ctrl+Enter 确认，Esc 取消，点击外面也会确认", 11.0f, Theme::TextDim);
        }
        else
        {
            // 宽度随内容增长，至少容纳 8 个字
            box->SetOnChanged([box, fontSize, minWidth](const std::string& t)
            {
                UIContext* ctx = box->GetContext();
                if (!ctx)
                    return;
                TextParams p;
                p.size = fontSize;
                const float w = ctx->GetTextSystem().Measure(t, p).x + fontSize * 2.0f;
                box->EditLayoutStyle().width = std::max(minWidth, w);
            });
        }

        // 提交或取消只做一次；点击外面（轻触关闭）视为提交
        auto finished = std::make_shared<bool>(false);
        auto submit = [this, box, multiline, finished]
        {
            if (*finished)
                return;
            *finished = true;
            Editor& ed = m_docManager.GetEditor();
            if (multiline) ed.SubmitMTextInput(box->GetText());
            else           ed.SubmitTextInput(box->GetText());
            m_viewport->RequestRender();
            StateChanged();
        };
        auto cancel = [this, multiline, finished]
        {
            if (*finished)
                return;
            *finished = true;
            Editor& ed = m_docManager.GetEditor();
            if (multiline) { auto& r = ed.GetMTextInputRequest(); r.Active = false; r.EditTargetId = Object::InvalidID; }
            else           { auto& r = ed.GetTextInputRequest();  r.Active = false; r.EditTargetId = Object::InvalidID; }
            m_viewport->RequestRender();
            StateChanged();
        };

        Popup* raw = popup.get();
        box->SetKeyPreview([raw, multiline, submit, cancel](KeyEvent& e)
        {
            if (e.type != KeyEventType::Down)
                return false;
            if (e.key == Key::Escape)
            {
                cancel();
                raw->Close();
                return true;
            }
            if (e.key == Key::Enter && (!multiline || e.Ctrl()))
            {
                submit();
                raw->Close();
                return true;
            }
            return false;
        });
        raw->SetOnClosed([this, raw, submit]
        {
            submit();                       // 已提交 / 取消时什么也不做
            if (m_textPopup == raw)
                m_textPopup = nullptr;
            if (m_viewport->IsVisible())
                m_viewport->Focus();
        });
        m_ui->OpenPopup(std::move(popup));
        m_textPopup = raw;
        box->Focus();
        box->SelectAll();
    }

    // =========================================================
    // 定义块：块名
    // =========================================================
    void MainFrame::OpenBlockNameDialog()
    {
        using namespace MiniGUI;
        Editor&           editor = m_docManager.GetEditor();
        const BlockTable& table  = m_docManager.GetActive()->GetScene().GetBlockTable();

        auto dialog = std::make_unique<Dialog>("定义块");
        Node* body = dialog->GetBody();
        body->SetLayoutStyle(ColumnStyle(8.0f));
        TextBox* name = AddRow<TextBox>(body, "块名", editor.GetBlockNameRequest().DefaultName);
        name->EditLayoutStyle().width = 220.0f;
        Label* error = body->AddChild<Label>("", 12.0f, Theme::Danger);
        body->AddChild<Label>(std::to_string(editor.GetBlockNameRequest().Ids.size()) + " 个对象将收进新块，并原位替换为块引用",
                              12.0f, Theme::TextDim);

        Dialog* raw = dialog.get();
        auto valid = [name, error, &table]
        {
            const std::string n = Trimmed(name->GetText());
            const bool exists = !n.empty() && table.FindByName(n) != BlockTable::InvalidID;
            error->SetText(n.empty() ? "请输入块名" : exists ? "块名已存在" : "");
            error->SetVisible(n.empty() || exists);
            return !n.empty() && !exists;
        };
        Button* ok = raw->AddButton("确定", [this, raw, name, valid]
        {
            if (!valid())
                return;
            m_docManager.GetEditor().SubmitBlockDefine(Trimmed(name->GetText()));
            raw->Close();
            m_viewport->RequestRender();
            StateChanged();
        }, DialogButtonRole::Default);
        raw->AddButton("取消", [this, raw]
        {
            m_docManager.GetEditor().GetBlockNameRequest() = {};
            raw->Close();
            StateChanged();
        }, DialogButtonRole::Cancel);
        name->SetOnChanged([ok, valid](const std::string&) { ok->SetEnabled(valid()); });
        ok->SetEnabled(valid());

        raw->SetOnClosed([this, raw]
        {
            // 被 × 以外的方式关闭（例如请求被撤销）：保证请求清掉
            if (m_blockNameDialog == raw)
                m_blockNameDialog = nullptr;
            m_docManager.GetEditor().GetBlockNameRequest().Active = false;
            if (m_viewport->IsVisible())
                m_viewport->Focus();
        });
        raw->SetInitialFocus(name);
        m_ui->OpenPopup(std::move(dialog));
        m_blockNameDialog = raw;
        name->SelectAll();
    }

    // =========================================================
    // 另存为（没有系统对话框的平台，即网页版）：文件名 + 格式
    // =========================================================
    void MainFrame::OpenSaveAsDialog(Document* doc, std::function<void()> onSaved)
    {
        using namespace MiniGUI;
        struct Format { const char* label; const char* ext; CadSaveVersion version; };
        static const Format kFormats[] =
        {
            { "MiniCAD 文档 (*.mcad)",          ".mcad", CadSaveVersion::R2018 },
            { "AutoCAD 2018 图形 (*.dwg)",      ".dwg",  CadSaveVersion::R2018 },
            { "AutoCAD 2013/2014 图形 (*.dwg)", ".dwg",  CadSaveVersion::R2013 },
            { "AutoCAD 2018 DXF (*.dxf)",       ".dxf",  CadSaveVersion::R2018 },
            { "AutoCAD 2013/2014 DXF (*.dxf)",  ".dxf",  CadSaveVersion::R2013 },
            { "JSON 文件 (*.json)",             ".json", CadSaveVersion::R2018 },
        };

        // 默认文件名去掉已知扩展名；默认格式沿用当前文件的格式和 DWG / DXF 版本
        std::string base   = doc->GetName();
        int         format = 0;
        bool        matched = false;
        auto lower = [](std::string s) { for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c))); return s; };
        const std::string current = lower(doc->HasPath() ? doc->GetPath() : doc->GetName());
        for (int i = 0; i < static_cast<int>(std::size(kFormats)); ++i)
        {
            const std::string ext = kFormats[i].ext;
            if (!matched && current.size() > ext.size() && current.compare(current.size() - ext.size(), ext.size(), ext) == 0
                && (kFormats[i].version == doc->GetCadSaveVersion() || ext == ".mcad" || ext == ".json"))
            {
                format  = i;
                matched = true;
            }
            if (lower(base).size() > ext.size() && lower(base).compare(base.size() - ext.size(), ext.size(), ext) == 0)
                base.resize(base.size() - ext.size());
        }

        auto dialog = std::make_unique<Dialog>("另存为");
        Node* body = dialog->GetBody();
        LayoutStyle bs = ColumnStyle(10.0f);
        bs.padding = Edges::Make(16.0f, 12.0f, 16.0f, 4.0f);
        body->SetLayoutStyle(bs);

        constexpr float kFieldWidth = 280.0f;
        TextBox* name = AddRow<TextBox>(body, "文件名", base);
        name->EditLayoutStyle().width = kFieldWidth;
        std::vector<std::string> labels;
        for (const Format& f : kFormats)
            labels.emplace_back(f.label);
        ComboBox* type = AddRow<ComboBox>(body, "格式", labels, format);
        type->EditLayoutStyle().width = kFieldWidth;

        // 提示与输入框左对齐（名称列 72 + 间距 8）
        Node* hints = body->AddChild<Node>();
        LayoutStyle hs = ColumnStyle(2.0f);
        hs.margin = Edges::Make(80.0f, 0.0f, 0.0f, 0.0f);
        hints->SetLayoutStyle(hs);
        hints->AddChild<Label>("AutoCAD 2013～2017 共用 2013 格式", 12.0f, Theme::TextDim);
        hints->AddChild<Label>("保存后由浏览器下载到本机", 12.0f, Theme::TextDim);

        Dialog* raw = dialog.get();
        Button* ok = raw->AddButton("保存", [this, raw, name, type, doc, onSaved]
        {
            const std::string n = Trimmed(name->GetText());
            const int i = type->GetSelectedIndex();
            auto& docs = m_docManager.GetAll();
            const bool alive = std::any_of(docs.begin(), docs.end(), [doc](const auto& d) { return d.get() == doc; });
            raw->Close();
            if (n.empty() || i < 0 || !alive)
                return;

            ActivateDocument(doc);
            CadSaveVersion version = kFormats[i].version;
            m_pendingSavePath    = m_platform->ChooseSavePath(n + kFormats[i].ext, version);
            m_pendingSaveVersion = kFormats[i].version;
            const std::string path = m_pendingSavePath;
            if (!path.empty())
                m_docManager.SaveAs();          // 文件对话框回调返回 m_pendingSavePath
            m_pendingSavePath.clear();
            if (!path.empty() && !doc->IsDirty())
            {
                m_platform->OnDocumentSaved(doc->GetPath());
                if (onSaved)
                    onSaved();
            }
            StateChanged();
        }, DialogButtonRole::Default);
        raw->AddButton("取消", [raw] { raw->Close(); }, DialogButtonRole::Cancel);
        name->SetOnChanged([ok](const std::string& t) { ok->SetEnabled(!Trimmed(t).empty()); });
        ok->SetEnabled(!Trimmed(name->GetText()).empty());

        raw->SetOnClosed([this]
        {
            if (m_viewport->IsVisible())
                m_viewport->Focus();
        });
        raw->SetInitialFocus(name);
        m_ui->OpenPopup(std::move(dialog));
        name->SelectAll();
    }

    // =========================================================
    // 插入块：块表选择（双击直接插入）
    // =========================================================
    void MainFrame::OpenBlockInsertDialog()
    {
        using namespace MiniGUI;
        const BlockTable& table = m_docManager.GetActive()->GetScene().GetBlockTable();

        // 可插入的块（跳过 *Model_Space / *Paper_Space 保留块与空块）
        std::vector<ListItem> items;
        table.ForEach([&items](BlockID id, const BlockEntity& b)
        {
            if (id == BlockTable::ModelSpaceID || id == BlockTable::PaperSpaceID || b.IsEmpty())
                return;
            ListItem item;
            item.text     = b.GetName();
            item.detail   = std::to_string(b.EntityCount()) + " 个对象";
            item.userData = id;
            items.push_back(std::move(item));
        });

        auto dialog = std::make_unique<Dialog>("插入块");
        Node* body = dialog->GetBody();
        body->SetLayoutStyle(ColumnStyle(6.0f));
        body->AddChild<Label>("选择要插入的块（双击直接插入），然后在绘图区指定插入点：", 12.0f, Theme::TextDim);
        ListView* list = body->AddChild<ListView>();
        list->EditLayoutStyle().width  = 320.0f;
        list->EditLayoutStyle().height = 220.0f;
        list->SetItems(items);
        if (!items.empty())
            list->SetCurrent(0);

        Dialog* raw = dialog.get();
        auto insert = [this, raw, list]
        {
            const int i = list->GetCurrent();
            if (i < 0)
                return;
            const auto id = static_cast<BlockID>(list->GetItems()[static_cast<size_t>(i)].userData);
            raw->Close();
            m_docManager.GetEditor().SubmitBlockInsert(id);      // 关闭请求并启动放置工具
            m_viewport->RequestRender();
            StateChanged();
        };
        list->SetOnItemActivated([insert](int) { insert(); });
        Button* ok = raw->AddButton("插入", insert, DialogButtonRole::Default);
        ok->SetEnabled(!items.empty());
        raw->AddButton("取消", [this, raw]
        {
            m_docManager.GetEditor().GetBlockInsertRequest() = {};
            raw->Close();
            StateChanged();
        }, DialogButtonRole::Cancel);
        raw->SetOnClosed([this, raw]
        {
            if (m_insertDialog == raw)
                m_insertDialog = nullptr;
            m_docManager.GetEditor().GetBlockInsertRequest().Active = false;
            if (m_viewport->IsVisible())
                m_viewport->Focus();
        });
        raw->SetInitialFocus(list);
        m_ui->OpenPopup(std::move(dialog));
        m_insertDialog = raw;
    }

    // =========================================================
    // 阵列（仿 AutoCAD ARRAY）：矩形 / 环形，参数实时预览
    // =========================================================
    namespace
    {
        // 对话框持有单选组（单选组不是节点，要比按钮活得久）
        class ArrayDialog : public MiniGUI::Dialog
        {
        public:
            ArrayDialog() : MiniGUI::Dialog("阵列") {}
            MiniGUI::RadioGroup group;
        };
    }

    void MainFrame::OpenArrayDialog()
    {
        using namespace MiniGUI;
        m_arrayPicking = false;
        ArrayParams& p = m_docManager.GetEditor().GetArrayRequest().Params;

        auto dialog = std::make_unique<ArrayDialog>();
        ArrayDialog* raw = dialog.get();
        Node* body = raw->GetBody();
        body->SetLayoutStyle(ColumnStyle(8.0f));

        // ── 类型 ─────────────────────────────────────────────────
        Node* types = body->AddChild<Node>();
        LayoutStyle ts;
        ts.direction = FlexDirection::Row;
        ts.gap       = 16.0f;
        types->SetLayoutStyle(ts);
        types->AddChild<RadioButton>(&raw->group, static_cast<int>(ArrayType::Rectangular), "矩形阵列");
        types->AddChild<RadioButton>(&raw->group, static_cast<int>(ArrayType::Polar),       "环形阵列");
        raw->group.SetValue(static_cast<int>(p.type));
        body->AddChild<Separator>();

        // 参数变化：刷新幽灵预览、项目数和"确定"是否可用
        Label* summary = nullptr;
        auto changed = std::make_shared<std::function<void()>>();

        // ── 矩形 ─────────────────────────────────────────────────
        Node* rect = body->AddChild<Node>();
        rect->SetLayoutStyle(ColumnStyle(6.0f));
        auto* rows   = AddRow<NumberBox>(rect, "行数",     static_cast<double>(p.rows), 1.0, 1000.0, 1.0, 0);
        auto* cols   = AddRow<NumberBox>(rect, "列数",     static_cast<double>(p.cols), 1.0, 1000.0, 1.0, 0);
        auto* rowGap = AddRow<NumberBox>(rect, "行间距",   p.rowSpacing);
        auto* colGap = AddRow<NumberBox>(rect, "列间距",   p.colSpacing);
        auto* angle  = AddRow<NumberBox>(rect, "阵列角度", p.angleDeg);
        rowGap->SetDecimals(3);
        colGap->SetDecimals(3);
        angle->SetDecimals(2);

        // ── 环形 ─────────────────────────────────────────────────
        Node* polar = body->AddChild<Node>();
        polar->SetLayoutStyle(ColumnStyle(6.0f));
        Node* centerRow = polar->AddChild<Node>();
        LayoutStyle cs;
        cs.direction  = FlexDirection::Row;
        cs.alignItems = Align::Center;
        cs.gap        = 6.0f;
        centerRow->SetLayoutStyle(cs);
        centerRow->AddChild<Label>("中心点", 13.0f, Theme::TextDim)->EditLayoutStyle().width = 72.0f;
        auto* cx = centerRow->AddChild<NumberBox>(p.center.x);
        auto* cy = centerRow->AddChild<NumberBox>(p.center.y);
        cx->SetDecimals(3);
        cy->SetDecimals(3);
        cx->EditLayoutStyle().width = 100.0f;
        cy->EditLayoutStyle().width = 100.0f;
        cx->SetTooltip("X");
        cy->SetTooltip("Y");
        Button* pick = centerRow->AddChild<Button>("拾取 <", [this]
        {
            m_docManager.GetEditor().BeginArrayCenterPick();      // 对话框暂时关闭，在绘图区点选中心
            StateChanged();
            m_viewport->Focus();
        });
        pick->SetTooltip("在绘图区点选阵列中心");
        auto* count  = AddRow<NumberBox>(polar, "项目总数", static_cast<double>(p.count), 2.0, 1000.0, 1.0, 0);
        auto* fill   = AddRow<NumberBox>(polar, "填充角度", p.fillAngleDeg);
        fill->SetDecimals(2);
        auto* rotate = polar->AddChild<CheckBox>("旋转项目", p.rotateItems);

        body->AddChild<Separator>();
        summary = body->AddChild<Label>("", 12.0f, Theme::TextDim);

        *changed = [this, rect, polar, summary, raw]
        {
            ArrayParams& q = m_docManager.GetEditor().GetArrayRequest().Params;
            q.type = static_cast<ArrayType>(raw->group.GetValue());
            rect->SetVisible(q.type == ArrayType::Rectangular);
            polar->SetVisible(q.type == ArrayType::Polar);
            const int n = q.type == ArrayType::Rectangular ? q.rows * q.cols : q.count;
            summary->SetText("共 " + std::to_string(n) + " 个项目（含源对象）");
            m_docManager.GetEditor().BuildArrayPreview(q);
            m_viewport->RequestRender();
        };

        auto bindInt = [this, changed](NumberBox* box, int ArrayParams::*field)
        {
            box->SetOnChanged([this, changed, field](double v)
            {
                m_docManager.GetEditor().GetArrayRequest().Params.*field = static_cast<int>(std::lround(v));
                (*changed)();
            });
        };
        auto bindDouble = [this, changed](NumberBox* box, double ArrayParams::*field)
        {
            box->SetOnChanged([this, changed, field](double v)
            {
                m_docManager.GetEditor().GetArrayRequest().Params.*field = v;
                (*changed)();
            });
        };
        bindInt(rows, &ArrayParams::rows);
        bindInt(cols, &ArrayParams::cols);
        bindInt(count, &ArrayParams::count);
        bindDouble(rowGap, &ArrayParams::rowSpacing);
        bindDouble(colGap, &ArrayParams::colSpacing);
        bindDouble(angle, &ArrayParams::angleDeg);
        bindDouble(fill, &ArrayParams::fillAngleDeg);
        cx->SetOnChanged([this, changed](double v) { m_docManager.GetEditor().GetArrayRequest().Params.center.x = v; (*changed)(); });
        cy->SetOnChanged([this, changed](double v) { m_docManager.GetEditor().GetArrayRequest().Params.center.y = v; (*changed)(); });
        rotate->SetOnChanged([this, changed](bool v) { m_docManager.GetEditor().GetArrayRequest().Params.rotateItems = v; (*changed)(); });
        raw->group.SetOnChanged([changed](int) { (*changed)(); });

        raw->AddButton("确定", [this, raw]
        {
            Editor& ed = m_docManager.GetEditor();
            if (!ed.GetArrayRequest().Params.ProducesCopies())
                return;
            ed.SubmitArray();               // 先提交再关闭：关闭回调会把仍在的请求当作取消
            raw->Close();
            m_viewport->RequestRender();
            StateChanged();
        }, DialogButtonRole::Default);
        raw->AddButton("取消", [this, raw]
        {
            m_docManager.GetEditor().CancelArray();
            raw->Close();
            m_viewport->RequestRender();
            StateChanged();
        }, DialogButtonRole::Cancel);

        raw->SetOnClosed([this, raw]
        {
            if (m_arrayDialog == raw)
                m_arrayDialog = nullptr;
            // 不是为了拾取中心而关闭、请求还在（例如对话框被替换）：按取消处理，清掉预览
            Editor& ed = m_docManager.GetEditor();
            if (!m_arrayPicking && ed.GetArrayRequest().Active && !ed.GetArrayRequest().PickingCenter)
            {
                ed.CancelArray();
                m_viewport->RequestRender();
            }
            if (m_viewport->IsVisible())
                m_viewport->Focus();
        });
        (*changed)();
        m_ui->OpenPopup(std::unique_ptr<Popup>(dialog.release()));
        m_arrayDialog = raw;
    }

    // =========================================================
    // 图案填充（仿 AutoCAD HATCH）：选图案、比例、角度，确定后在绘图区拾取内部点
    // =========================================================
    namespace
    {
        // 把填充实体画进界面：世界坐标（y 向上）映射到 box（y 向下），只取线段和三角形
        class SwatchSink : public IDrawSink
        {
        public:
            SwatchSink(MiniGUI::DrawList& dl, const MiniGUI::Rect& box, double pxPerUnit, MiniGUI::ColorRef color)
                : m_dl(dl), m_box(box), m_k(pxPerUnit), m_color(color) {}

            void DrawLine(const Math::Point3& a, const Math::Point3& b, const Math::Color4&, bool) override
            {
                m_dl.AddLine(Map(a), Map(b), m_color, 1.0f);
            }
            void FillTriangle(const Math::Point3& a, const Math::Point3& b, const Math::Point3& c, const Math::Color4&) override
            {
                m_dl.AddTriangleFilled(Map(a), Map(b), Map(c), m_color);
            }

        private:
            MiniGUI::Vec2 Map(const Math::Point3& p) const
            {
                return { m_box.min.x + static_cast<float>(p.x * m_k), m_box.max.y - static_cast<float>(p.y * m_k) };
            }
            MiniGUI::DrawList& m_dl;
            MiniGUI::Rect      m_box;
            double             m_k;
            MiniGUI::ColorRef  m_color;
        };

        // 在 box 里画图案样例：box 宽度对应 worldWidth 个绘图单位（图案单位 mm）
        void DrawPatternSwatch(MiniGUI::DrawList& dl, const MiniGUI::Rect& box, const HatchPattern& pattern,
                               double scale, double angleDeg, double worldWidth, MiniGUI::ColorRef color)
        {
            const double w = box.max.x - box.min.x, h = box.max.y - box.min.y;
            if (w <= 1.0 || h <= 1.0)
                return;
            if (pattern.Solid)      // 实心直接填矩形：三角形拼接处的抗锯齿会留下一条斜缝
            {
                dl.AddRectFilled(box, color);
                return;
            }
            const double k  = w / worldWidth;
            const double ww = w / k, wh = h / k;
            HatchEntity hatch(1, Polyline({ { 0, 0, 0 }, { ww, 0, 0 }, { ww, wh, 0 }, { 0, wh, 0 }, { 0, 0, 0 } }), pattern);
            hatch.SetScale(scale);
            hatch.SetAngle(angleDeg);
            dl.PushClipRect(box);
            SwatchSink sink(dl, box, k, color);
            hatch.Draw(sink, false, false);
            dl.PopClipRect();
        }

        // 对话框里的大预览：跟随当前图案、比例、角度
        class HatchPreview : public MiniGUI::Node
        {
        public:
            explicit HatchPreview(const HatchSettings& settings) : m_settings(settings) {}
            void OnPaint(MiniGUI::DrawList& dl, const MiniGUI::Rect& r) override
            {
                dl.AddRectFilled(r, Theme::Background, 3.0f);
                const HatchPattern pattern = HatchPatternLibrary::Instance().Get(m_settings.Pattern);
                DrawPatternSwatch(dl, MiniGUI::Rect{ r.min.x + 1.0f, r.min.y + 1.0f, r.max.x - 1.0f, r.max.y - 1.0f },
                                  pattern, m_settings.Scale, m_settings.AngleDeg, 50.0, Theme::Text);
                dl.AddRect(r, Theme::Border, 3.0f);
            }
        private:
            const HatchSettings& m_settings;
        };
    }

    void MainFrame::OpenHatchDialog()
    {
        using namespace MiniGUI;
        HatchSettings& hs = m_docManager.GetEditor().GetHatchSettings();
        const auto& patterns = HatchPatternLibrary::Instance().All();

        auto dialog = std::make_unique<Dialog>("图案填充");
        Dialog* raw = dialog.get();
        Node* body = raw->GetBody();
        LayoutStyle bs = ColumnStyle(8.0f);
        bs.padding = Edges::Symmetric(12.0f, 4.0f);
        body->SetLayoutStyle(bs);

        std::vector<std::string> names;
        int selected = -1;
        for (size_t i = 0; i < patterns.size(); ++i)
        {
            names.push_back(patterns[i].Name);
            if (patterns[i].Name == hs.Pattern)
                selected = static_cast<int>(i);
        }
        auto* combo = AddRow<ComboBox>(body, "图案", names, selected);
        combo->EditLayoutStyle().width = 220.0f;
        combo->SetDropDownWidth(320.0f);
        combo->SetMaxVisibleItems(12);
        // 下拉项：样例 + 名称（列表里另加说明）
        combo->SetItemPainter([combo](DrawList& dl, const Rect& box, int index, bool inList)
        {
            const auto& all = HatchPatternLibrary::Instance().All();
            if (index < 0 || index >= static_cast<int>(all.size()))
                return;
            const HatchPattern& p = all[static_cast<size_t>(index)];
            const float cy = (box.min.y + box.max.y) * 0.5f;
            const Rect swatch{ box.min.x + 6.0f, cy - 8.0f, box.min.x + 42.0f, cy + 8.0f };
            DrawPatternSwatch(dl, swatch, p, 1.0, 0.0, 20.0, Theme::Text);
            dl.AddRect(swatch, Theme::Border);
            TextParams tp;
            tp.size   = 13.0f;
            tp.color  = Theme::Text;
            tp.vAlign = TextAlign::Center;
            std::string text = p.Name;
            if (inList && !p.Description.empty())
                text += "  " + p.Description;
            combo->GetContext()->GetTextSystem().Draw(dl, Rect{ swatch.max.x + 8.0f, box.min.y, box.max.x, box.max.y }, text, tp);
        });

        Label* desc = body->AddChild<Label>("", 12.0f, Theme::TextDim);
        auto* scale = AddRow<NumberBox>(body, "比例", hs.Scale, 0.001, 10000.0, 0.1, 3);
        auto* angle = AddRow<NumberBox>(body, "角度", hs.AngleDeg, -360.0, 360.0, 15.0, 2);

        auto* preview = body->AddChild<HatchPreview>(hs);
        preview->EditLayoutStyle().width  = 300.0f;
        preview->EditLayoutStyle().height = 150.0f;

        auto refresh = [desc, preview, &hs]
        {
            const HatchPattern* p = HatchPatternLibrary::Instance().Find(hs.Pattern);
            desc->SetText(p ? p->Description : std::string());
            preview->Invalidate();
        };
        combo->SetOnChanged([&hs, refresh](int i)
        {
            const auto& all = HatchPatternLibrary::Instance().All();
            if (i >= 0 && i < static_cast<int>(all.size()))
                hs.Pattern = all[static_cast<size_t>(i)].Name;
            refresh();
        });
        scale->SetOnChanged([&hs, refresh](double v) { if (v > 0.0) hs.Scale = v; refresh(); });
        angle->SetOnChanged([&hs, refresh](double v) { hs.AngleDeg = v; refresh(); });
        body->AddChild<Label>("确定后在封闭区域内部点击填充，右键 / Esc 结束", 12.0f, Theme::TextDim);

        raw->AddButton("确定", [this, raw]
        {
            m_docManager.GetEditor().SubmitHatch();     // 先提交再关闭：关闭回调会把仍在的请求当作取消
            raw->Close();
            m_viewport->Focus();
            StateChanged();
        }, DialogButtonRole::Default);
        raw->AddButton("取消", [this, raw]
        {
            m_docManager.GetEditor().CancelHatch();
            raw->Close();
            StateChanged();
        }, DialogButtonRole::Cancel);
        raw->SetOnClosed([this, raw]
        {
            if (m_hatchDialog == raw)
                m_hatchDialog = nullptr;
            Editor& ed = m_docManager.GetEditor();
            if (ed.GetHatchRequest().Active)
                ed.CancelHatch();
            if (m_viewport->IsVisible())
                m_viewport->Focus();
        });
        refresh();
        m_ui->OpenPopup(std::unique_ptr<Popup>(dialog.release()));
        m_hatchDialog = raw;
    }

    // =========================================================
    // 文字样式（仿 AutoCAD STYLE）：新建 / 重命名 / 删除 / 置为当前，编辑字体与字形参数。
    // 直接修改当前文档的文字样式表，图中使用该样式的文字立即更新（同图层面板，不进撤销栈）
    // =========================================================
    namespace
    {
        // 字体目录下的 .shx / .ttf 文件名（不含路径）
        std::vector<std::string> ListFontFiles(const std::string& dir, bool shxOnly)
        {
            std::vector<std::string> out;
            std::error_code ec;
            const std::filesystem::path path(std::u8string(dir.begin(), dir.end()));
            for (const auto& entry : std::filesystem::directory_iterator(path, ec))
            {
                std::string ext = entry.path().extension().string();
                std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
                if (ext == ".shx" || (!shxOnly && ext == ".ttf"))
                {
                    const std::u8string u8 = entry.path().filename().u8string();
                    out.emplace_back(u8.begin(), u8.end());
                }
            }
            std::sort(out.begin(), out.end());
            return out;
        }

        // 忽略 ASCII 大小写比较（字体文件名）
        bool EqualsIgnoreCase(const std::string& a, const std::string& b)
        {
            return a.size() == b.size() && std::equal(a.begin(), a.end(), b.begin(), [](unsigned char x, unsigned char y)
            {
                return std::tolower(x) == std::tolower(y);
            });
        }

        int IndexOf(const std::vector<std::string>& v, const std::string& s)
        {
            for (size_t i = 0; i < v.size(); ++i)
                if (EqualsIgnoreCase(v[i], s))
                    return static_cast<int>(i);
            return -1;
        }

        // 用文档的文字样式表把一行样例文字画进 box（与视口文字同一条渲染路径），按 box 等比缩放居中
        class TextStylePreview : public MiniGUI::Node
        {
        public:
            TextStylePreview(Document* doc, const TextStyleID* styleId) : m_doc(doc), m_styleId(styleId) {}

            void OnPaint(MiniGUI::DrawList& dl, const MiniGUI::Rect& r) override
            {
                dl.AddRectFilled(r, Theme::Background, 3.0f);
                dl.AddRect(r, Theme::Border, 3.0f);
                FontResolver resolver = MakeTextStyleResolver(m_doc->GetFontSystem(), &m_doc->GetScene().GetTextStyleTable());
                if (!resolver)
                    return;

                std::vector<Vertex_P3_C4> lines, fills;
                std::vector<Vertex_P3_C4_UV> textured;
                Overlay overlay;
                const Scene& scene = m_doc->GetScene();
                DrawContext ctx(lines, fills, textured, overlay, scene.GetLayerManager(), scene.GetLineTypeTable(), 0.0, nullptr, resolver);
                TextEntity sample(1, { 0, 0, 0 }, "AaBb 123 中文样例", 10.0f, 0.0f, *m_styleId);
                sample.Draw(ctx, false, false);
                if (lines.empty() && fills.empty())
                    return;

                float minX = 1e30f, minY = 1e30f, maxX = -1e30f, maxY = -1e30f;
                for (const auto* v : { &lines, &fills })
                    for (const auto& p : *v)
                    {
                        minX = std::min(minX, p.pos.x); maxX = std::max(maxX, p.pos.x);
                        minY = std::min(minY, p.pos.y); maxY = std::max(maxY, p.pos.y);
                    }
                const float pad = 10.0f;
                const float w = r.max.x - r.min.x - 2 * pad, h = r.max.y - r.min.y - 2 * pad;
                const float k = std::min(w / std::max(maxX - minX, 1e-3f), h / std::max(maxY - minY, 1e-3f));
                const float ox = r.min.x + pad + (w - (maxX - minX) * k) * 0.5f;
                const float oy = r.max.y - pad - (h - (maxY - minY) * k) * 0.5f;
                auto map = [&](const Vertex_P3_C4& v) { return MiniGUI::Vec2{ ox + (v.pos.x - minX) * k, oy - (v.pos.y - minY) * k }; };

                dl.PushClipRect(r);
                for (size_t i = 0; i + 1 < lines.size(); i += 2)
                    dl.AddLine(map(lines[i]), map(lines[i + 1]), Theme::Text, 1.0f);
                for (size_t i = 0; i + 2 < fills.size(); i += 3)
                    dl.AddTriangleFilled(map(fills[i]), map(fills[i + 1]), map(fills[i + 2]), Theme::Text);
                dl.PopClipRect();
            }

        private:
            Document*          m_doc;
            const TextStyleID* m_styleId;
        };

        class TextStyleDialog : public MiniGUI::Dialog
        {
        public:
            TextStyleDialog() : MiniGUI::Dialog("文字样式") {}
            TextStyleID selected = TextStyleTable::StandardID;     // 正在编辑的样式
        };
    }

    void MainFrame::OpenTextStyleDialog()
    {
        using namespace MiniGUI;
        Document* doc = m_docManager.GetActive();
        if (!doc)
            return;

        auto dialog = std::make_unique<TextStyleDialog>();
        TextStyleDialog* raw = dialog.get();
        raw->selected = doc->GetScene().GetCurrentTextStyle();
        Node* body = raw->GetBody();
        LayoutStyle bs = ColumnStyle(8.0f);
        bs.padding = Edges::Symmetric(12.0f, 4.0f);
        body->SetLayoutStyle(bs);

        const std::string fontDir = ResourceDir() + "/fonts";
        auto* styleCombo = AddRow<ComboBox>(body, "样式");
        styleCombo->EditLayoutStyle().width = 240.0f;
        Label* current = body->AddChild<Label>("", 12.0f, Theme::TextDim);

        auto* name = AddRow<TextBox>(body, "名称");
        name->EditLayoutStyle().width = 240.0f;
        Node* actions = body->AddChild<Node>();
        LayoutStyle as;
        as.direction = FlexDirection::Row;
        as.gap       = 6.0f;
        as.padding   = Edges::Make(80.0f, 0.0f, 0.0f, 0.0f);
        actions->SetLayoutStyle(as);
        body->AddChild<Separator>();

        auto* font    = AddRow<ComboBox>(body, "字体");
        auto* bigFont = AddRow<ComboBox>(body, "大字体");
        font->EditLayoutStyle().width    = 240.0f;
        bigFont->EditLayoutStyle().width = 240.0f;
        bigFont->SetTooltip("SHX 字体中显示中文等双字节字符的字体；TrueType 字体不需要");
        auto* height  = AddRow<NumberBox>(body, "高度", 0.0, 0.0, 10000.0, 0.5, 3);
        height->SetTooltip("固定字高；0 表示不固定，新建文字时使用默认字高");
        auto* width   = AddRow<NumberBox>(body, "宽度因子", 1.0, 0.01, 100.0, 0.1, 3);
        auto* oblique = AddRow<NumberBox>(body, "倾斜角度", 0.0, -85.0, 85.0, 5.0, 1);
        auto* preview = body->AddChild<TextStylePreview>(doc, &raw->selected);
        preview->EditLayoutStyle().width  = 330.0f;
        preview->EditLayoutStyle().height = 80.0f;
        Label* error = body->AddChild<Label>("", 12.0f, Theme::Danger);

        // 表变化后：重画图中文字、刷新特性面板
        auto changed = [this, doc]
        {
            doc->GetScene().MarkDisplayDirty();
            m_viewport->RequestRender();
            StateChanged();
        };

        // 按表的当前内容刷新全部控件（不触发控件回调）
        auto sync = std::make_shared<std::function<void()>>();
        *sync = [=]
        {
            const TextStyleTable& table = doc->GetScene().GetTextStyleTable();
            if (!table.Find(raw->selected))
                raw->selected = TextStyleTable::StandardID;
            const TextStyleRecord& rec = *table.Find(raw->selected);

            std::vector<std::string> names;
            int sel = 0;
            for (const auto& r : table.Records())
            {
                if (r.Id == raw->selected) sel = static_cast<int>(names.size());
                names.push_back(r.Name);
            }
            styleCombo->SetItems(names);
            styleCombo->SetSelectedIndex(sel);
            current->SetText("当前样式：" + table.Resolve(doc->GetScene().GetCurrentTextStyle()).Name);
            name->SetText(rec.Name);

            std::vector<std::string> fonts = ListFontFiles(fontDir, false);
            if (IndexOf(fonts, rec.FontFile) < 0) fonts.push_back(rec.FontFile);     // 字体目录里没有的也列出，便于识别
            font->SetItems(fonts);
            font->SetSelectedIndex(IndexOf(fonts, rec.FontFile));

            std::vector<std::string> bigs = ListFontFiles(fontDir, true);
            bigs.insert(bigs.begin(), "（无）");
            if (!rec.BigFontFile.empty() && IndexOf(bigs, rec.BigFontFile) < 0) bigs.push_back(rec.BigFontFile);
            bigFont->SetItems(bigs);
            bigFont->SetSelectedIndex(rec.BigFontFile.empty() ? 0 : IndexOf(bigs, rec.BigFontFile));
            bigFont->SetEnabled(rec.IsShx());

            height->SetValue(rec.Height);
            width->SetValue(rec.WidthFactor);
            oblique->SetValue(rec.ObliqueDeg);
            preview->Invalidate();
        };

        // 修改当前选中样式的字体参数
        auto edit = [=](const std::function<void(TextStyleRecord&)>& fn)
        {
            TextStyleTable& table = doc->GetScene().GetTextStyleTable();
            const TextStyleRecord* r = table.Find(raw->selected);
            if (!r) return;
            TextStyleRecord copy = *r;
            fn(copy);
            if (!copy.IsShx())
                copy.BigFontFile.clear();       // TrueType 不用大字体
            table.Update(raw->selected, copy);
            error->SetText("");
            (*sync)();
            changed();
        };

        styleCombo->SetOnChanged([=](int i)
        {
            const auto& recs = doc->GetScene().GetTextStyleTable().Records();
            if (i >= 0 && i < static_cast<int>(recs.size()))
                raw->selected = recs[static_cast<size_t>(i)].Id;
            error->SetText("");
            (*sync)();
        });
        font->SetOnChanged([=](int i)
        {
            const std::string file = i >= 0 ? font->GetItems()[static_cast<size_t>(i)] : std::string();
            edit([&](TextStyleRecord& r) { r.FontFile = file; });
        });
        bigFont->SetOnChanged([=](int i)
        {
            const std::string file = i > 0 ? bigFont->GetItems()[static_cast<size_t>(i)] : std::string();
            edit([&](TextStyleRecord& r) { r.BigFontFile = file; });
        });
        height->SetOnChanged([=](double v)  { edit([&](TextStyleRecord& r) { r.Height = v; }); });
        width->SetOnChanged([=](double v)   { edit([&](TextStyleRecord& r) { r.WidthFactor = v; }); });
        oblique->SetOnChanged([=](double v) { edit([&](TextStyleRecord& r) { r.ObliqueDeg = v; }); });

        auto addAction = [&](const char* text, std::function<void()> fn)
        {
            Button* b = actions->AddChild<Button>(text, std::move(fn));
            b->SetFocusable(false);
        };
        addAction("新建", [=]
        {
            TextStyleTable& table = doc->GetScene().GetTextStyleTable();
            TextStyleRecord rec = *table.Find(raw->selected);          // 以选中样式为模板
            rec.Name = Trimmed(name->GetText());
            if (rec.Name.empty() || table.FindByName(rec.Name) != TextStyleTable::InvalidID)
                for (int n = 1; ; ++n)                                  // 名称为空或重名：自动取"样式N"
                {
                    rec.Name = "样式" + std::to_string(n);
                    if (table.FindByName(rec.Name) == TextStyleTable::InvalidID) break;
                }
            raw->selected = table.Add(rec);
            error->SetText("");
            (*sync)();
            changed();
        });
        addAction("重命名", [=]
        {
            TextStyleTable& table = doc->GetScene().GetTextStyleTable();
            if (raw->selected == TextStyleTable::StandardID)
                error->SetText("Standard 样式不能重命名");
            else if (!table.Rename(raw->selected, Trimmed(name->GetText())))
                error->SetText("名称为空或与其他样式重名");
            else
            {
                error->SetText("");
                (*sync)();
                changed();
            }
        });
        addAction("删除", [=]
        {
            Scene& scene = doc->GetScene();
            const auto used = scene.CollectUsedTextStyles();
            if (raw->selected == TextStyleTable::StandardID)
                error->SetText("Standard 样式不能删除");
            else if (raw->selected == scene.GetCurrentTextStyle())
                error->SetText("当前样式不能删除，请先把其他样式置为当前");
            else if (std::find(used.begin(), used.end(), raw->selected) != used.end())
                error->SetText("该样式正被图中的文字或标注使用，不能删除");
            else
            {
                scene.GetTextStyleTable().Remove(raw->selected);
                raw->selected = scene.GetCurrentTextStyle();
                error->SetText("");
                (*sync)();
                changed();
            }
        });
        addAction("置为当前", [=]
        {
            doc->GetScene().SetCurrentTextStyle(raw->selected);
            error->SetText("");
            (*sync)();
            changed();
        });

        raw->AddButton("关闭", [raw] { raw->Close(); }, DialogButtonRole::Default);
        raw->SetOnClosed([this, raw]
        {
            if (m_textStyleDialog == raw)
                m_textStyleDialog = nullptr;
            m_docManager.GetEditor().CloseTextStyle();
            if (m_viewport->IsVisible())
                m_viewport->Focus();
            StateChanged();
        });
        (*sync)();
        m_ui->OpenPopup(std::unique_ptr<Popup>(dialog.release()));
        m_textStyleDialog = raw;
    }

    // =========================================================
    // 关于
    // =========================================================
    void MainFrame::ShowAbout()
    {
        using namespace MiniGUI;
        auto dialog = std::make_unique<Dialog>("关于 MiniCAD");
        Node* body = dialog->GetBody();
        LayoutStyle s = ColumnStyle(6.0f);
        s.padding = Edges::Symmetric(8.0f, 4.0f);
        s.width   = 340.0f;
        body->SetLayoutStyle(s);
        body->AddChild<Label>("MiniCAD", 22.0f);
        body->AddChild<Label>("版本 1.0", 13.0f, Theme::TextDim);
        body->AddChild<Separator>();
        body->AddChild<Label>(std::string("界面：MiniGUI（保留模式，") + m_platform->GetGraphicsName() + " 渲染）", 13.0f);
        body->AddChild<Label>("作者：Hello", 13.0f);
        body->AddChild<Label>("鸣谢：Qizhiwoniu（七只蜗牛）", 13.0f);
        body->AddChild<Separator>();
        Label* path = body->AddChild<Label>("界面描述文件：" + m_uiPath, 12.0f, Theme::TextDim);
        path->SetWrap(true);

        Dialog* raw = dialog.get();
        raw->AddButton("关闭", [raw] { raw->Close(); }, DialogButtonRole::Default);
        m_ui->OpenPopup(std::move(dialog));
    }
}
