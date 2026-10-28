#pragma once
#include "Core/Node.h"
#include "Data/Json.h"
#include <algorithm>
#include <cstdint>
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
    class DockFloatView;
    class InWindowFloatHost;

    // 停靠方位：Center 表示合并到目标标签组（成为一个新标签），其余表示在目标的这一侧拆分
    enum class DockSide { Center, Left, Right, Top, Bottom };

    struct DockPanelOptions
    {
        bool closable   = true;     // 标签上显示 ×
        bool showHeader = true;     // false：没有标题栏（例如文档区），不能拖走、不能浮出，也不能有其他面板合并进来
    };

    // 浮动窗口的宿主：停靠区把浮动窗口的内容（DockFloatView：那个窗口的标签组与分隔条）交给它放进"浮动窗口"。
    // 每个浮动窗口有停靠区起的 ID（"float1"…）；坐标一律是停靠区所在窗口的坐标（逻辑像素）。
    // 没有设置宿主时停靠区自己在同一窗口里开 FloatingPanel 弹层（面板只能在窗口内浮动）；
    // 桌面版可以提供独立的系统窗口（apps/Common 的 FloatingPanelHost 经 AppPlatform::CreateFloatingWindow）。
    // 宿主把浮动窗口上的操作转回停靠区：拖标题栏 BeginFloatDrag / UpdateFloatDrag / EndFloatDrag（在停靠区和其他浮动窗口里找落点）、
    // × 或系统关闭 CloseFloat、双击标题栏 RedockFloat、在窗口里按下 RaiseFloat、移动 / 改大小 / 最大化之后 NotifyLayoutChanged
    class IDockFloatHost
    {
    public:
        virtual ~IDockFloatHost() = default;
        // 建浮动窗口并放进内容；成功时取走 content 并返回 true，失败时 content 原样留下（停靠区改用窗口内浮动）
        virtual bool Open(DockSpace& dock, const std::string& id, const std::string& title, std::unique_ptr<Node>& content, const Rect& rect) = 0;
        virtual std::unique_ptr<Node> Close(const std::string& id) = 0;     // 取回内容并销毁浮动窗口
        virtual Rect GetRect(const std::string& id) const = 0;              // 当前位置（含标题栏）
        virtual void SetRect(const std::string& id, const Rect& rect) = 0;  // 移动（标签拖出后窗口跟着光标走）
        virtual void SetTitle(const std::string& id, const std::string& title) = 0;
        virtual void Activate(const std::string& id) = 0;                   // 置前
        // 最大化（标题栏上的按钮由宿主处理）；GetRestoreRect 为还原后的位置（保存布局用），没有最大化时同 GetRect
        virtual bool IsFloatMaximized(const std::string& id) const { (void)id; return false; }
        virtual void SetFloatMaximized(const std::string& id, bool maximized) { (void)id; (void)maximized; }
        virtual Rect GetRestoreRect(const std::string& id) const { return GetRect(id); }
        // 浮动窗口里的坐标（那个窗口界面的逻辑坐标）与停靠区所在窗口的坐标互换；窗口内浮动时两者相同
        virtual Vec2 ToDock(const std::string& id, Vec2 floatPos) const { (void)id; return floatPos; }
        virtual Vec2 FromDock(const std::string& id, Vec2 dockPos) const { (void)id; return dockPos; }
    };

    // 停靠区：可拖拽、可拆分的面板布局（类似 Visual Studio / AutoCAD 的选项板）。
    // - 布局是一棵树：分割节点（横排 / 竖排，子节点固定尺寸或占剩余空间）+ 标签组（若干面板，显示其中一个）
    // - 拖动标签：放到另一个标签组的上 / 下 / 左 / 右拆分，放到中间或标题栏合并为标签；贴近停靠区边缘时停靠到整体的一侧。
    //   拖动时显示落点预览，Esc 取消
    // - 浮动窗口：标签拖出所在的停靠区（或浮动窗口）、或双击标签，面板浮出成单独的浮动窗口（IDockFloatHost；默认是窗口内的 FloatingPanel 弹层）。
    //   拖出时鼠标还没松开就浮出，之后按下的那个窗口接着持有捕获，浮动窗口跟着光标走（同拖浮动窗口的标题栏）。
    //   每个浮动窗口有自己的一棵布局树，停靠规则与停靠区相同：别的面板或整个浮动窗口拖到它上面（标题栏、标签条：合并为标签，
    //   内容区：按位置拆分或合并），它里面的标签也能拖出去、拖到停靠区或别的浮动窗口。只有一个面板时不画标签条，面板铺满窗口。
    //   找落点时浮动窗口在停靠区之上，按前后顺序（最近按下的在最上面）。
    //   拖浮动窗口的标题栏到落点上，里面的面板一起停靠过去；双击标题栏，里面的面板回到各自浮出前在停靠区里的位置；
    //   双击浮动窗口里的标签只让这个面板回去。关闭（窗口的 × 或标签的 ×）后 ShowPanel 回到原来的浮动窗口（还开着时）或在原处重新浮出。
    //   浮动中的面板仍算"可见"（IsPanelVisible）
    // - 分隔条拖动调整尺寸；面板最小 kMinPanel
    // - 关闭面板时记住位置，ShowPanel 时回到原处
    // - 面板内容节点在布局变化时原样搬移，不会重建（滚动位置、输入框内容等状态保留）；没有变化的浮动窗口不动它的内容
    // - 布局可保存为 JSON / 从 JSON 恢复：
    //     { "split": "row", "children": [
    //         { "tabs": [ "layers", "properties" ], "active": 1, "size": 260 },
    //         { "tabs": [ { "panel": "documents", "header": false, "closable": false } ] } ],
    //       "floatingPanels": [ { "panel": "nodes", "rect": [ 100, 80, 480, 360 ] },
    //                           { "tabs": [ "a", "b" ], "active": 1, "rect": [ 600, 80, 300, 400 ], "maximized": true } ] }
    //   分割节点 "split" 为 row（横排）或 column（竖排）；"size" 为在父分割方向上的尺寸（逻辑像素），省略表示占剩余空间；
    //   根对象可带 "floatingPanels"：浮动窗口与位置 [x, y, 宽, 高]（窗口坐标，最大化时为还原后的位置）；
    //   只有一个面板时写 "panel"，否则写这个窗口的布局树（同上，"tabs" 或 "split"）
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
        bool  IsPanelVisible(const std::string& id) const;        // 在停靠区或某个浮动窗口里（不一定是当前标签）
        bool  IsPanelActive(const std::string& id) const;         // 可见且是所在组的当前标签
        void  SetPanelTitle(const std::string& id, std::string title);
        std::vector<std::string> GetPanelIds() const;
        // 取出所有面板（界面重建时由 UiLayout 回收宿主面板；浮动窗口先关闭），停靠区随后为空
        std::map<std::string, std::unique_ptr<Node>> TakeAllPanels();

        // ── 停靠操作 ────────────────────────────────────────────
        // 把面板放到 target 面板所在标签组的 side 一侧（target 可以在浮动窗口里）；target 为空表示整个停靠区的 side 一侧。
        // size：新拆出的一侧的尺寸（0 = 目标的一半，停靠到整体边缘时为 250）
        bool Dock(const std::string& id, const std::string& target, DockSide side, float size = 0.0f);
        void ClosePanel(const std::string& id);
        void ShowPanel(const std::string& id);       // 不可见时回到关闭前的位置（没有记录则停靠到右侧；关闭前浮动则回到那个浮动窗口或重新浮出），并设为当前标签
        void ActivatePanel(const std::string& id);   // 设为所在组的当前标签；在浮动窗口里时把窗口置前
        void TogglePanel(const std::string& id) { IsPanelVisible(id) ? ClosePanel(id) : ShowPanel(id); }

        // ── 浮动窗口 ────────────────────────────────────────────
        void SetFloatHost(IDockFloatHost* host);                 // nullptr：窗口内浮动（默认）
        // 浮出成单独的浮动窗口（已独占一个浮动窗口时只置前；在有别的面板的浮动窗口里时拆出来）；没有标题栏的面板不能浮出
        bool FloatPanel(const std::string& id, const Rect& windowRect);
        bool IsPanelFloating(const std::string& id) const;
        Rect GetFloatingRect(const std::string& id) const;        // 所在浮动窗口的位置；不是浮动面板时为空矩形
        void RedockPanel(const std::string& id);                  // 浮动面板回到浮出前在停靠区里的位置
        size_t GetFloatCount() const;                             // 浮动窗口个数
        std::vector<std::string> GetFloatPanels(const std::string& id) const;   // 与 id 同在一个浮动窗口里的面板（含自己，按布局顺序）
        void NotifyLayoutChanged() { LayoutChanged(); }           // 宿主移动了浮动窗口：请宿主保存布局

        // 宿主转来的浮动窗口操作（floatId 为 IDockFloatHost::Open 给的 ID）
        void CloseFloat(const std::string& floatId);              // 关闭窗口：里面的面板都关闭（不可关闭的回停靠区）
        void RedockFloat(const std::string& floatId);             // 双击标题栏：里面的面板回到停靠区
        void RaiseFloat(const std::string& floatId);              // 在窗口里按下：它成为最上面的浮动窗口（找落点时先看它）
        // 拖动浮动窗口的标题栏：由宿主驱动落点判断（坐标为本停靠区所在窗口的坐标）。
        // EndFloatDrag 返回 true 表示已停靠（窗口里的面板都搬走了，宿主的浮动窗口已经 Close）
        void BeginFloatDrag(const std::string& floatId, Vec2 windowPos);
        void UpdateFloatDrag(Vec2 windowPos);
        bool EndFloatDrag(Vec2 windowPos, bool cancel);

        // ── 布局保存与恢复 ──────────────────────────────────────
        JsonValue SaveLayout() const;
        // 未知的面板 ID、重复引用记入 warnings；布局里没有的面板隐藏
        bool      LoadLayout(const JsonValue& layout, std::vector<std::string>* warnings = nullptr);

        // 用户操作（拖动停靠、关闭、调整尺寸、浮出、移动浮动窗口）改变布局后调用，宿主可借此保存布局
        void SetOnLayoutChanged(std::function<void()> cb) { m_onLayoutChanged = std::move(cb); }

        // ── 查询（测试与宿主使用）─────────────────────────────
        Rect      GetPanelRect(const std::string& id) const;  // 面板内容区（所在窗口的坐标：浮动面板为浮动窗口的坐标）；不可见时为空矩形
        bool      IsDragging() const { return m_drag.active; }
        bool      IsTornDragging() const { return m_drag.torn; }   // 标签拖出后浮出、浮动窗口正跟着光标走
        bool      GetDropPreview(Rect& out) const;            // 拖动中且有有效落点时返回预览矩形（本停靠区所在窗口的坐标）

        // 卡片样式（照 SolidCraft 的浮动布局）：布局样式的 gap > 0 时，每个标签组画成圆角卡片（RadiusLg，细边框），
        // 卡片之间留 gap 宽的缝（缝就是分隔条，悬停时显示强调色），外圈按 padding 留边，缝里露出 Background。
        // gap 为 0（默认）时是紧贴的停靠面板，相邻面板之间一条分界线。浮动窗口里总是紧贴的
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
        void OnPointerEvent(PointerEvent& e) override;      // 停靠区里的标签拖出后：停靠区持有捕获，移动浮动窗口、找落点
        void OnPaint(DrawList& dl, const Rect& screenRect) override;
        void OnPaintOverlay(DrawList& dl, const Rect& screenRect) override;

    private:
        friend class DockGroup;
        friend class DockSplitter;
        friend class DockFloatView;
        friend class InWindowFloatHost;

        static inline uint64_t s_treeSerial = 0;

        struct Tree
        {
            uint64_t                           serial     = ++s_treeSerial;   // 每个节点不同（新建的节点即使结构相同，Signature 也不同）
            bool                               split      = false;
            bool                               horizontal = true;   // 分割：子节点横排（row）
            std::vector<std::unique_ptr<Tree>> children;
            float                              size       = 0.0f;   // 在父分割方向上的尺寸；0 = 占剩余空间
            std::vector<std::string>           panels;              // 标签组
            int                                active     = 0;
            Tree*                              parent     = nullptr;
            Rect                               rect;                // 所在容器（停靠区或浮动窗口的内容）的本地坐标，布局时计算
        };

        // 浮动窗口：自己的一棵布局树，内容节点 view 放在宿主给的窗口里
        struct Float
        {
            std::string           id;
            std::unique_ptr<Tree> root;
            Rect                  rect;                 // 最近一次的位置（还原后的）；还没交给宿主时就是要开的位置
            bool                  maximized = false;
            bool                  open      = false;    // 已交给宿主（停靠区挂到界面上之前先不开）
            IDockFloatHost*       host      = nullptr;  // 开着它的宿主（平台宿主开不了时是默认宿主）
            DockFloatView*        view      = nullptr;
            std::unique_ptr<Node> owned;                // 没交给宿主时由这里持有 view
            std::string           built;                // view 按哪个结构建的（Signature）：没变就不重建，内容不动
        };

        struct Panel
        {
            std::string           title;
            DockPanelOptions      options;
            Node*                 node = nullptr;
            std::unique_ptr<Node> owned;            // 不在任何布局树里时由这里持有
            // 在停靠区里的位置（离开停靠区——关闭、浮出、拖进浮动窗口——时记下）：旁边的面板 + 方位 + 尺寸
            std::string           lastTarget;
            DockSide              lastSide = DockSide::Right;
            float                 lastSize = 0.0f;
            // 关闭时在浮动窗口里：ShowPanel 回到那里（floatTarget 还浮着就合并为它的标签，否则在 floatRect 重新浮出）
            bool                  wasFloating = false;
            std::string           floatTarget;
            Rect                  floatRect;
        };

        struct DropTarget
        {
            bool     valid     = false;
            Float*   container = nullptr;   // 落点所在的浮动窗口；nullptr 为停靠区
            Tree*    leaf      = nullptr;   // 为空表示整个容器
            DockSide side      = DockSide::Center;
            int      index     = -1;        // 合并到标签组时插入的位置（-1 = 末尾）
            Rect     preview;               // 容器的本地坐标
        };

        // 树操作（停靠区与各浮动窗口的树）
        Tree*  FindLeaf(const std::string& panelId) const;               // 在所有树里找
        static Tree* FindLeaf(Tree* node, const std::string& panelId);
        static Tree* LeafAt(Tree* node, Vec2 local);
        static Tree* TopOf(Tree* node);
        static Tree* FirstLeaf(Tree* node);
        static void  CollectPanels(const Tree* node, std::vector<std::string>& out);
        static void  SetParents(Tree* node, Tree* parent);
        static std::string Signature(const Tree* node);
        Float* FloatOf(const Tree* node) const;                         // 所在的浮动窗口；停靠区的树为 nullptr
        Float* FindFloat(const std::string& panelId) const { return FloatOf(FindLeaf(panelId)); }
        Float* FindFloatById(const std::string& floatId) const;
        bool   HasFloat(const Float* f) const;
        std::unique_ptr<Tree>& RootSlot(Tree* top);
        void   FixParents();
        void   RemoveFromTree(const std::string& panelId);              // 空组删除，单子节点的分割合并
        void   Collapse(Tree* node);
        // 放进 container（nullptr 为停靠区）里 target 的 side 一侧；Center 时 sub 的面板都成为 target 的标签。成功时取走 sub
        bool   InsertTree(std::unique_ptr<Tree>& sub, Tree* target, DockSide side, float size, int tabIndex, Float* container);
        bool   InsertPanel(const std::string& id, Tree* target, DockSide side, float size, int tabIndex, Float* container);
        void   RememberPosition(const std::string& panelId);            // 在停靠区里时记下位置
        bool   AcceptsTabs(const Tree* leaf) const;
        bool   LeafHasHeader(const Tree* leaf) const;                   // 画标签条（浮动窗口里只有一个面板时不画）
        void   DockToRemembered(const std::string& id);                 // 回到关闭 / 浮出前在停靠区里的位置（没有记录则停靠到右侧）
        void   SelectTab(Tree* leaf, int index);

        // 浮动窗口
        IDockFloatHost& FloatHost();
        Float* NewFloat(const Rect& rect, std::unique_ptr<Tree> root);
        bool   OpenFloat(Float* f);                                     // 把 view 交给宿主
        std::unique_ptr<Node> CloseHostWindow(Float& f);                // 从宿主取回 view（记下位置与是否最大化）
        void   OpenPendingFloats();
        void   ClearTrees();                                            // 所有面板摘出布局（浮动窗口关闭）
        Rect   FloatRestoreRect(const Float& f) const;
        std::string FloatTitle(const Float& f) const;
        void   UpdateFloatTitle(Float* f);
        Vec2   FloatToDock(const Float* f, Vec2 floatPos) const;
        Vec2   DockToFloat(const Float* f, Vec2 dockPos) const;
        bool   InContainer(const Float* c, Vec2 windowPos) const;       // 光标在停靠区 / 浮动窗口里
        void   TearOff(Vec2 windowPos);                                 // 拖动中的标签离开所在的容器：浮出，接着拖浮动窗口
        void   MoveFloat(const std::string& panelId, const Rect& rect);
        void   InvalidateViews();

        // 布局与重建
        static void LayoutTree(Tree* node, const Rect& rect, float gap, float scale);
        static void PlaceNodes(Node* container, float gap);             // 按树的矩形摆放容器里的标签组和分隔条
        void   Rebuild();                                               // 按树重建标签组和分隔条节点（停靠区与有变化的浮动窗口）
        void   BuildNodes(Node* container, DockFloatView* view, Tree* node);
        void   DiscardChildren(Node* container);
        void   LayoutChanged();

        // 拖动（由 DockGroup 转发；external：拖的是整个浮动窗口，由宿主转发）
        void   BeginDrag(const std::string& panelId, Vec2 windowPos, bool external = false);
        void   UpdateDrag(Vec2 windowPos);
        bool   EndDrag(Vec2 windowPos, bool cancel);           // 返回是否停靠到了某个布局树里
        DropTarget ComputeDrop(Vec2 windowPos) const;
        DropTarget ComputeDropIn(Float* container, Vec2 local) const;

        // 保存 / 加载
        JsonValue SaveTree(const Tree* node) const;
        std::unique_ptr<Tree> LoadTree(const JsonValue& v, std::vector<std::string>& warnings, std::vector<std::string>& used);

        std::unique_ptr<Tree>               m_root;
        std::vector<std::unique_ptr<Float>> m_floats;          // 前后顺序：最后一个在最上面
        int                                 m_nextFloat = 1;
        std::map<std::string, Panel>        m_panels;
        std::function<void()>               m_onLayoutChanged;
        bool                                m_floating = false;
        bool                                m_quietLayout = false;      // 暂不通知布局变化（标签拖出浮出时，等松开再通知）
        IDockFloatHost*                     m_floatHost = nullptr;
        std::unique_ptr<InWindowFloatHost>  m_defaultFloatHost;

        struct Drag
        {
            bool        active   = false;
            bool        external = false;   // 拖的是整个浮动窗口（不画光标旁的名称，松开在外面不再浮出）
            bool        torn     = false;   // 标签拖出后浮出的：按下的那个窗口持有捕获，按 grab / size 移动浮动窗口
            Float*      source   = nullptr; // 标签从哪个浮动窗口拖起（nullptr：停靠区）；拖出后由它持有捕获
            Vec2        grab, size;         // 光标相对浮动窗口左上角的位置、窗口尺寸（窗口坐标）
            std::string panel;              // 拖的面板；拖整个浮动窗口时是里面的一个面板
            Vec2        pos;                // 窗口坐标
            DropTarget  target;
            uint32_t    escShortcut = 0;    // 拖动期间临时注册的 Esc（取消拖动），不需要抢键盘焦点
        } m_drag;
    };
}
