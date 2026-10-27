#pragma once
#include "Core/Node.h"
#include "Data/Json.h"
#include <algorithm>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace MiniGUI
{
    class DockGroup;
    class DockSplitter;
    class DockSpace;
    class InWindowFloatHost;

    // 停靠方位：Center 表示合并到目标标签组（成为一个新标签），其余表示在目标的这一侧拆分
    enum class DockSide { Center, Left, Right, Top, Bottom };

    struct DockPanelOptions
    {
        bool closable   = true;     // 标签上显示 ×
        bool showHeader = true;     // false：没有标题栏（例如文档区），不能拖走、不能浮出，也不能有其他面板合并进来
    };

    // 浮动面板的宿主：停靠区把浮出的面板交给它放进"浮动窗口"。坐标一律是停靠区所在窗口的坐标（逻辑像素）。
    // 没有设置宿主时停靠区自己在同一窗口里开 FloatingPanel 弹层（面板只能在窗口内浮动）；
    // 桌面版可以提供独立的系统窗口（apps/Common 的 FloatingPanelHost 经 AppPlatform::CreateFloatingWindow）。
    // 浮动窗口被拖动时宿主调用 DockSpace::BeginExternalDrag / UpdateExternalDrag / EndExternalDrag 在停靠区里找落点
    class IDockFloatHost
    {
    public:
        virtual ~IDockFloatHost() = default;
        // 为面板建浮动窗口并放进内容；成功时取走 content 并返回 true，失败时 content 原样留下（停靠区改用窗口内浮动）
        virtual bool Open(DockSpace& dock, const std::string& id, const std::string& title, std::unique_ptr<Node>& content, const Rect& rect) = 0;
        virtual std::unique_ptr<Node> Close(const std::string& id) = 0;     // 取回内容并销毁浮动窗口
        virtual Rect GetRect(const std::string& id) const = 0;              // 当前位置（保存布局用）
        virtual void SetRect(const std::string& id, const Rect& rect) = 0;  // 移动（标签拖出后窗口跟着光标走）
        virtual void SetTitle(const std::string& id, const std::string& title) = 0;
        virtual void Activate(const std::string& id) = 0;                   // 置前
    };

    // 停靠区：可拖拽、可拆分的面板布局（类似 Visual Studio / AutoCAD 的选项板）。
    // - 布局是一棵树：分割节点（横排 / 竖排，子节点固定尺寸或占剩余空间）+ 标签组（若干面板，显示其中一个）
    // - 拖动标签：放到另一个标签组的上 / 下 / 左 / 右拆分，放到中间或标题栏合并为标签；贴近停靠区边缘时停靠到整体的一侧。
    //   拖动时显示落点预览，Esc 取消
    // - 浮动面板：标签拖出停靠区、或双击标签，面板浮出成浮动窗口（IDockFloatHost；默认是窗口内的 FloatingPanel 弹层）。
    //   拖出时鼠标还没松开就浮出，之后停靠区接过鼠标捕获，浮动窗口跟着光标走（同拖浮动窗口的标题栏：经过停靠区画落点，松开在上面停靠回去）。
    //   拖动浮动窗口的标题栏到停靠区里停靠回去，双击标题栏回到浮出前的位置；关闭浮动面板后 ShowPanel 重新浮出到原处。
    //   浮动中的面板仍算"可见"（IsPanelVisible），一个浮动窗口里只有一个面板
    // - 分隔条拖动调整尺寸；面板最小 kMinPanel
    // - 关闭面板时记住位置，ShowPanel 时回到原处
    // - 面板内容节点在布局变化时原样搬移，不会重建（滚动位置、输入框内容等状态保留）
    // - 布局可保存为 JSON / 从 JSON 恢复：
    //     { "split": "row", "children": [
    //         { "tabs": [ "layers", "properties" ], "active": 1, "size": 260 },
    //         { "tabs": [ { "panel": "documents", "header": false, "closable": false } ] } ],
    //       "floatingPanels": [ { "panel": "nodes", "rect": [ 100, 80, 480, 360 ] } ] }
    //   分割节点 "split" 为 row（横排）或 column（竖排）；"size" 为在父分割方向上的尺寸（逻辑像素），省略表示占剩余空间；
    //   根对象可带 "floatingPanels"：浮动的面板与位置 [x, y, 宽, 高]（窗口坐标）
    class DockSpace : public Node
    {
    public:
        DockSpace();
        ~DockSpace() override;

        // ── 面板 ────────────────────────────────────────────────
        // 注册面板（初始不显示，由 LoadLayout / Dock / ShowPanel 放置）；同 ID 重复注册时替换内容
        void  AddPanel(const std::string& id, std::string title, std::unique_ptr<Node> content, DockPanelOptions options = {});
        bool  HasPanel(const std::string& id) const { return m_panels.count(id) > 0; }
        Node* GetPanelContent(const std::string& id) const;
        bool  IsPanelVisible(const std::string& id) const;        // 在布局里（不一定是当前标签）或浮动中
        bool  IsPanelActive(const std::string& id) const;         // 在布局里且是所在组的当前标签，或浮动中
        void  SetPanelTitle(const std::string& id, std::string title);
        std::vector<std::string> GetPanelIds() const;
        // 取出所有面板（界面重建时由 UiLayout 回收宿主面板；浮动窗口先关闭），停靠区随后为空
        std::map<std::string, std::unique_ptr<Node>> TakeAllPanels();

        // ── 停靠操作 ────────────────────────────────────────────
        // 把面板放到 target 面板所在标签组的 side 一侧；target 为空表示整个停靠区的 side 一侧。
        // size：新拆出的一侧的尺寸（0 = 目标的一半，停靠到整体边缘时为 250）。浮动中的面板先收回
        bool Dock(const std::string& id, const std::string& target, DockSide side, float size = 0.0f);
        void ClosePanel(const std::string& id);
        void ShowPanel(const std::string& id);       // 不在布局里时回到关闭前的位置（没有记录则停靠到右侧；关闭前浮动则重新浮出），并设为当前标签
        void ActivatePanel(const std::string& id);
        void TogglePanel(const std::string& id) { IsPanelVisible(id) ? ClosePanel(id) : ShowPanel(id); }

        // ── 浮动面板 ────────────────────────────────────────────
        void SetFloatHost(IDockFloatHost* host);                 // nullptr：窗口内浮动（默认）
        bool FloatPanel(const std::string& id, const Rect& windowRect);   // 浮出（已浮动时只置前）；没有标题栏的面板不能浮出
        bool IsPanelFloating(const std::string& id) const;
        Rect GetFloatingRect(const std::string& id) const;        // 浮动窗口的位置；不是浮动面板时为空矩形
        void RedockPanel(const std::string& id);                  // 浮动面板回到浮出前的位置
        void NotifyLayoutChanged() { LayoutChanged(); }           // 宿主移动了浮动窗口：请宿主保存布局
        // 浮动窗口被拖动：由宿主驱动落点判断（坐标为本停靠区所在窗口的坐标）。
        // EndExternalDrag 返回 true 表示已停靠（内容已从宿主取回，宿主的浮动窗口已经 Close）
        void BeginExternalDrag(const std::string& id, Vec2 windowPos);
        void UpdateExternalDrag(Vec2 windowPos);
        bool EndExternalDrag(Vec2 windowPos, bool cancel);

        // ── 布局保存与恢复 ──────────────────────────────────────
        JsonValue SaveLayout() const;
        // 未知的面板 ID、重复引用记入 warnings；布局里没有的面板隐藏
        bool      LoadLayout(const JsonValue& layout, std::vector<std::string>* warnings = nullptr);

        // 用户操作（拖动停靠、关闭、调整尺寸、浮出、移动浮动窗口）改变布局后调用，宿主可借此保存布局
        void SetOnLayoutChanged(std::function<void()> cb) { m_onLayoutChanged = std::move(cb); }

        // ── 查询（测试与宿主使用）─────────────────────────────
        Rect      GetPanelRect(const std::string& id) const;  // 面板内容区（窗口坐标）；不可见时为空矩形
        bool      IsDragging() const { return m_drag.active; }
        bool      IsTornDragging() const { return m_drag.torn; }   // 标签拖出后浮出、浮动窗口正跟着光标走
        bool      GetDropPreview(Rect& out) const;            // 拖动中且有有效落点时返回预览矩形（窗口坐标）

        // 卡片样式（照 SolidCraft 的浮动布局）：布局样式的 gap > 0 时，每个标签组画成圆角卡片（RadiusLg，细边框），
        // 卡片之间留 gap 宽的缝（缝就是分隔条，悬停时显示强调色），外圈按 padding 留边，缝里露出 Background。
        // gap 为 0（默认）时是紧贴的停靠面板，相邻面板之间一条分界线
        bool  IsCardStyle() const { return GetLayoutStyle().gap > 0.0f || m_floating; }
        // 浮动：停靠区叠在绘图区上面（照 SolidCraft，绘图区铺满窗口）。停靠区自己和缝都是透明的、点击穿透，
        // 没有标题栏的标签组（文档区）是透明的“洞”，只画其余面板的卡片。隐含卡片样式
        void  SetFloating(bool floating);
        bool  IsFloating() const { return m_floating; }
        float SplitterSize() const { return IsCardStyle() ? std::max(GetLayoutStyle().gap, 1.0f) : kSplitterSize; }

        static constexpr float kHeaderHeight = 28.0f;
        static constexpr float kSplitterSize = 1.0f;        // 分隔条在布局中占的宽度：只有一条分界线，面板之间没有缝隙
        static constexpr float kSplitterHit  = 6.0f;        // 可拖动的热区宽度：叠在两侧面板的边缘上，不额外占位
        static constexpr float kMinPanel     = 80.0f;
        static constexpr float kEdgeDrop     = 28.0f;       // 贴近停靠区边缘多少像素算停靠到整体一侧
        static constexpr float kDragStart    = 6.0f;        // 按下后移动多少像素开始拖动
        static constexpr float kFloatMinW    = 200.0f;      // 浮出时浮动窗口的尺寸范围（取自原标签组的尺寸）
        static constexpr float kFloatMinH    = 150.0f;
        static constexpr float kFloatMaxW    = 520.0f;
        static constexpr float kFloatMaxH    = 640.0f;

    protected:
        void OnLayout() override;
        void OnPointerEvent(PointerEvent& e) override;      // 标签拖出后：停靠区持有捕获，移动浮动窗口、找落点
        void OnPaint(DrawList& dl, const Rect& screenRect) override;
        void OnPaintOverlay(DrawList& dl, const Rect& screenRect) override;

    private:
        friend class DockGroup;
        friend class DockSplitter;
        friend class InWindowFloatHost;

        struct Tree
        {
            bool                               split      = false;
            bool                               horizontal = true;   // 分割：子节点横排（row）
            std::vector<std::unique_ptr<Tree>> children;
            float                              size       = 0.0f;   // 在父分割方向上的尺寸；0 = 占剩余空间
            std::vector<std::string>           panels;              // 标签组
            int                                active     = 0;
            Tree*                              parent     = nullptr;
            Rect                               rect;                // 本地坐标，OnLayout 计算
        };

        struct Panel
        {
            std::string           title;
            DockPanelOptions      options;
            Node*                 node = nullptr;
            std::unique_ptr<Node> owned;            // 不在布局里、也没浮动时由这里持有
            // 关闭前的位置：旁边的面板 + 方位 + 尺寸
            std::string           lastTarget;
            DockSide              lastSide = DockSide::Right;
            float                 lastSize = 0.0f;
            // 浮动：内容在宿主的浮动窗口里；floatRect 为最近一次的位置，wasFloating 表示关闭前是浮动的（ShowPanel 重新浮出）
            bool                  floating     = false;
            bool                  floatPending = false;   // 还没挂到 UIContext 上（LoadLayout 在建树之前调用）：第一次布局时再浮出
            bool                  wasFloating  = false;
            Rect                  floatRect;
        };

        struct DropTarget
        {
            bool     valid = false;
            Tree*    leaf  = nullptr;       // 为空表示整个停靠区
            DockSide side  = DockSide::Center;
            int      index = -1;            // 合并到标签组时插入的位置（-1 = 末尾）
            Rect     preview;               // 本地坐标
        };

        // 树操作
        Tree*  FindLeaf(const std::string& panelId) const;
        Tree*  FindLeaf(Tree* node, const std::string& panelId) const;
        Tree*  LeafAt(Vec2 local) const;
        Tree*  LeafAt(Tree* node, Vec2 local) const;
        void   RemoveFromTree(const std::string& panelId);      // 空组删除，单子节点的分割合并
        void   Collapse(Tree* node);
        bool   InsertPanel(const std::string& id, Tree* target, DockSide side, float size, int tabIndex);
        void   RememberPosition(const std::string& panelId);
        bool   AcceptsTabs(const Tree* leaf) const;
        void   SetParents(Tree* node, Tree* parent);
        void   DockToRemembered(const std::string& id);         // 回到关闭 / 浮出前记住的位置（没有记录则停靠到右侧）

        // 浮动
        IDockFloatHost& FloatHost();
        bool   OpenFloat(const std::string& id);                // 把 Panel::owned 交给宿主
        void   Unfloat(const std::string& id);                  // 从宿主取回内容到 Panel::owned
        void   UnfloatAll();
        void   OpenPendingFloats();
        void   TearOff(Vec2 windowPos);                         // 拖动中的标签离开停靠区：浮出，接着拖浮动窗口
        void   MoveFloat(const std::string& id, const Rect& rect);

        // 布局与重建
        void   LayoutTree(Tree* node, const Rect& rect);
        void   Rebuild();                                       // 按树重建标签组和分隔条节点
        void   BuildNodes(Tree* node);
        void   LayoutChanged();

        // 拖动（由 DockGroup 转发；external：拖的是浮动窗口，由宿主转发）
        void   BeginDrag(const std::string& panelId, Vec2 windowPos, bool external = false);
        void   UpdateDrag(Vec2 windowPos);
        bool   EndDrag(Vec2 windowPos, bool cancel);           // 返回是否停靠到了布局里
        DropTarget ComputeDrop(Vec2 local) const;

        // 保存 / 加载
        JsonValue SaveTree(const Tree* node) const;
        std::unique_ptr<Tree> LoadTree(const JsonValue& v, std::vector<std::string>& warnings, std::vector<std::string>& used);

        std::unique_ptr<Tree>          m_root;
        std::map<std::string, Panel>   m_panels;
        std::function<void()>          m_onLayoutChanged;
        bool                           m_floating = false;
        bool                           m_rebuildPending = false;
        bool                           m_quietLayout = false;      // 暂不通知布局变化（标签拖出浮出时，等松开再通知）
        IDockFloatHost*                m_floatHost = nullptr;
        std::unique_ptr<InWindowFloatHost> m_defaultFloatHost;

        struct Drag
        {
            bool        active   = false;
            bool        external = false;   // 拖的是浮动窗口（不画光标旁的名称，松开在外面不再浮出）
            bool        torn     = false;   // 标签拖出后浮出的：停靠区持有捕获，按 grab / size 移动浮动窗口
            Vec2        grab, size;         // 光标相对浮动窗口左上角的位置、窗口尺寸（窗口坐标）
            std::string panel;
            Vec2        pos;                // 窗口坐标
            DropTarget  target;
            uint32_t    escShortcut = 0;    // 拖动期间临时注册的 Esc（取消拖动），不需要抢键盘焦点
        } m_drag;
    };
}
