#pragma once
#include "Core/Node.h"
#include "Core/Event.hpp"
#include "Widgets/InlineEditor.h"
#include "NodeGraph/Graph.h"
#include <functional>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <vector>

namespace MiniGUI
{
    class Popup;
}

namespace MiniCAD
{
    // 节点编辑器（docs/NodeGraph/PLAN.md 第 9 节）：MiniGUI 控件，在画布上编辑当前文档的节点图。桌面版与网页版共用。
    //   - 画布：网格；中键 / 右键拖动平移，滚轮以光标为中心缩放，F 适应全部（有选中时适应选中）
    //   - 节点卡片：标题栏按分类着色，上面是输出端口，中间是参数，下面是输入端口（没有连线时显示内联值）；
    //     错误红框、警告橙框，消息显示在卡片下方，耗时显示在标题栏右侧；标题栏右端的圆点切换预览
    //   - 连线：从端口拖到兼容的端口（不兼容的变暗），Shift 追加；拖动已连接的输入可拔下改接；拖到空白处打开添加节点菜单并自动连上；
    //     Alt + 单击连线删除，Alt + 单击引脚断开引脚上的全部连线（按住 Alt 悬停时将被断开的连线画成红色虚线）
    //   - 改值：在数值框 / 滑条上左右拖动（Shift 精细，Ctrl 取整），双击键入；开关单击切换，选项单击弹出菜单
    //   - 选择：单击、Shift / Ctrl 多选、空白处框选；拖动移动；Delete 删除、Ctrl+D 复制、P 切换预览、Esc 取消
    //   - 剪贴板：Ctrl+C / Ctrl+X / Ctrl+V 复制 / 剪切 / 粘贴选中的节点（子图 JSON，见 Graph::CopyToString），粘贴到光标处
    //   - 添加节点：Tab 或空白处双击打开搜索框（按名称、分类、ID 模糊匹配），空白处右键按分类添加
    // 所有修改都经 Host 回调：编辑前存快照（撤销），编辑后求值、刷新预览、标记文档已修改
    class NodeEditorView : public MiniGUI::Node
    {
    public:
        struct Host
        {
            std::function<Nodes::Graph*()>                    graph;          // 当前文档的节点图；没有文档时为空
            std::function<void()>                             beforeEdit;     // 一次编辑之前（存撤销快照）
            std::function<void(bool evaluate)>                edited;         // 编辑之后；evaluate = false 表示只改了位置
            std::function<void()>                             selectionChanged;
            std::function<void(Nodes::NodeId, const std::string&)> action;   // 节点上的操作按钮（NodeTypeInfo::Action）
        };

        explicit NodeEditorView(Host host);

        // ── 选择与视图 ──
        const std::set<Nodes::NodeId>& GetSelection() const { return m_selection; }
        void SetSelection(std::set<Nodes::NodeId> ids);
        void FrameAll();                        // 适应全部（有选中时适应选中）
        void ResetView();                       // 切换文档后：清空选择、取消拖动，适应全部
        // 在画布局部坐标处添加节点（菜单、测试用），返回新节点 ID。
        // 从端口拖出后添加时自动连线：linkPort 为新节点上要连的端口，-1 表示取匹配程度最高的端口
        Nodes::NodeId AddNodeAt(const std::string& typeId, MiniGUI::Vec2 local, int linkPort = -1);
        void OpenAddMenuAt(MiniGUI::Vec2 local) { m_pendingLink.reset(); OpenAddMenu(local); }   // 添加节点菜单，新节点放在 local
        void OpenSearchAt(MiniGUI::Vec2 local)  { m_pendingLink.reset(); OpenSearch(local); }    // 节点搜索框，新节点放在 local
        bool IsSearchOpen() const { return m_search != nullptr; }
        void DeleteSelection();
        void DuplicateSelection();
        void TogglePreviewOfSelection();
        // 剪贴板（经 UIContext::GetClipboard，桌面版与网页版都可用）
        bool CopySelection();                   // 没有选中的节点返回 false
        void CutSelection();
        bool PasteFromClipboard();              // 粘贴到光标处（光标不在画布上时放在画布中央）；剪贴板里不是节点图返回 false

        // ── 坐标（测试用）──
        MiniGUI::Vec2 GraphToLocal(float gx, float gy) const { return { gx * m_zoom + m_pan.x, gy * m_zoom + m_pan.y }; }
        MiniGUI::Vec2 LocalToGraph(MiniGUI::Vec2 p) const   { return { (p.x - m_pan.x) / m_zoom, (p.y - m_pan.y) / m_zoom }; }
        float         GetZoom() const { return m_zoom; }
        // 端口中心（局部坐标）；output = true 为输出端口
        std::optional<MiniGUI::Vec2> PortPosition(Nodes::NodeId id, int port, bool output) const;
        // 输入内联值 / 参数控件的中心（局部坐标）
        std::optional<MiniGUI::Vec2> InputFieldPosition(Nodes::NodeId id, int port) const;
        std::optional<MiniGUI::Vec2> ParamPosition(Nodes::NodeId id, int index) const;

        static constexpr float kNodeWidth = 190.0f;
        static constexpr float kHeaderH   = 26.0f;
        static constexpr float kRowH      = 22.0f;

    protected:
        void OnPaint(MiniGUI::DrawList& dl, const MiniGUI::Rect& screenRect) override;
        void OnPointerEvent(MiniGUI::PointerEvent& e) override;
        void OnKeyEvent(MiniGUI::KeyEvent& e) override;
        bool OnContextMenu(MiniGUI::Vec2 windowPos) override;
        MiniGUI::CursorShape GetCursor(MiniGUI::Vec2 local) const override;

    private:
        // 一张卡片的布局（画布坐标）
        struct Card
        {
            MiniGUI::Rect      Box;
            std::vector<float> OutY, InY, ParamY;
            std::vector<int>   ParamIndex;          // 显示出来的参数（隐藏的不占行）
            float              ActionY  = -1.0f;
            float              DisplayY = -1.0f;    // 面板节点的数据区
            float              DisplayH = 0.0f;
        };
        Card Layout(const Nodes::Node& n) const;

        struct Hit
        {
            enum class Kind { None, Header, Body, InPort, OutPort, InField, Param, Action, Preview, Link } K = Kind::None;
            Nodes::NodeId Node  = Nodes::kInvalidNode;
            int           Index = -1;
            Nodes::Link   L;                        // Kind::Link
        };
        Hit HitTest(MiniGUI::Vec2 local, bool includeLinks = false) const;

        // 输入内联值 / 参数控件在卡片里的矩形（画布坐标）
        MiniGUI::Rect FieldRect(const Card& c, float y) const;
        bool InputEditable(const Nodes::Node& n, int port) const;

        Nodes::Graph* G() const { return m_host.graph ? m_host.graph() : nullptr; }
        void BeginEdit();                           // 本次手势里第一次修改前存快照
        void Edited(bool evaluate);
        void SelectionChanged();
        void SyncGraph();                           // 图对象变了（切换文档）：清空状态

        void OpenAddMenu(MiniGUI::Vec2 local);
        void OpenSearch(MiniGUI::Vec2 local);
        // 从端口拖出后待连接的引脚的类型与实体 / 曲面子类型；没有待连接的引脚时为空
        std::optional<std::pair<Nodes::DataType, Nodes::BodyKind>> PendingPin() const;
        bool FitsPendingLink(const Nodes::NodeTypeInfo& t) const;   // 从端口拖出时：新节点有能连上的端口
        // Alt 单击引脚 / 连线时会断开的连线（悬停时画成红色虚线）
        std::vector<Nodes::Link> LinksToCut(const Hit& h) const;
        bool CutLinksAt(const Hit& h);              // 断开 LinksToCut(h)，一步撤销；没有可断开的返回 false
        void OpenChoiceMenu(Nodes::NodeId id, int param, MiniGUI::Vec2 local);
        void OpenPlaneMenu(Nodes::NodeId id, int port, MiniGUI::Vec2 local);   // 平面输入的内联值：XY / YZ / XZ
        void BeginTextEdit(const Hit& hit);
        void ApplyValueDrag(MiniGUI::Vec2 local, uint8_t modifiers);
        void FinishLinkDrag(MiniGUI::Vec2 local, uint8_t modifiers);
        std::string HoverText() const;              // 悬停端口的提示

        void PaintGrid(MiniGUI::DrawList& dl, const MiniGUI::Rect& r);
        void PaintLinks(MiniGUI::DrawList& dl, MiniGUI::Vec2 origin);
        void PaintCard(MiniGUI::DrawList& dl, MiniGUI::Vec2 origin, const Nodes::Node& n);

        enum class Drag { None, Pan, Nodes, Box, Link, Value };

        Host                    m_host;
        Nodes::Graph*           m_graph = nullptr;  // 上次看到的图（变了就重置）
        std::set<Nodes::NodeId> m_selection;
        MiniGUI::Vec2           m_pan  { 40.0f, 40.0f };
        float                   m_zoom = 1.0f;

        Drag                    m_drag = Drag::None;
        MiniGUI::Vec2           m_pressLocal, m_lastLocal, m_mouseLocal;
        bool                    m_moved      = false;
        bool                    m_editBegun  = false;
        bool                    m_rightPanned = false;
        // 连线拖动
        bool                    m_linkFromOutput = true;
        Nodes::NodeId           m_linkNode = Nodes::kInvalidNode;
        int                     m_linkPort = 0;
        // 拖到空白处后打开菜单：新节点自动连上
        std::optional<std::pair<bool, std::pair<Nodes::NodeId, int>>> m_pendingLink;
        // 改值
        Hit                     m_valueHit;
        Nodes::Value            m_valueStart;
        // 悬停
        Hit                     m_hover;
        bool                    m_hoverAlt    = false;   // 悬停时按着 Alt：m_hover 含连线，显示将被断开的连线
        bool                    m_mouseInside = false;
        // 搜索框（打开时非空）、上次粘贴的位置（在同一处连续粘贴时错开）
        MiniGUI::Popup*         m_search = nullptr;
        std::optional<MiniGUI::Vec2> m_lastPaste;
        int                     m_pasteRepeat = 0;
        std::shared_ptr<int>    m_alive = std::make_shared<int>(0);   // 弹层回调据此判断编辑器是否还在

        MiniGUI::InlineEditor   m_inline;
    };
}
