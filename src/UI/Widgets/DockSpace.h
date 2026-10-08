#pragma once
#include "Core/Node.h"
#include "Data/Json.h"
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace MiniGUI
{
    class DockGroup;
    class DockSplitter;

    // 停靠方位：Center 表示合并到目标标签组（成为一个新标签），其余表示在目标的这一侧拆分
    enum class DockSide { Center, Left, Right, Top, Bottom };

    struct DockPanelOptions
    {
        bool closable   = true;     // 标签上显示 ×
        bool showHeader = true;     // false：没有标题栏（例如文档区），不能拖走，也不能有其他面板合并进来
    };

    // 停靠区：可拖拽、可拆分的面板布局（类似 Visual Studio / AutoCAD 的选项板）。
    // - 布局是一棵树：分割节点（横排 / 竖排，子节点固定尺寸或占剩余空间）+ 标签组（若干面板，显示其中一个）
    // - 拖动标签：放到另一个标签组的上 / 下 / 左 / 右拆分，放到中间或标题栏合并为标签；贴近停靠区边缘时停靠到整体的一侧。
    //   拖动时显示落点预览，Esc 取消
    // - 分隔条拖动调整尺寸；面板最小 kMinPanel
    // - 关闭面板时记住位置，ShowPanel 时回到原处
    // - 面板内容节点在布局变化时原样搬移，不会重建（滚动位置、输入框内容等状态保留）
    // - 布局可保存为 JSON / 从 JSON 恢复：
    //     { "split": "row", "children": [
    //         { "tabs": [ "layers", "properties" ], "active": 1, "size": 260 },
    //         { "tabs": [ { "panel": "documents", "header": false, "closable": false } ] } ] }
    //   分割节点 "split" 为 row（横排）或 column（竖排）；"size" 为在父分割方向上的尺寸（逻辑像素），省略表示占剩余空间
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
        bool  IsPanelVisible(const std::string& id) const;        // 在布局里（不一定是当前标签）
        bool  IsPanelActive(const std::string& id) const;         // 在布局里且是所在组的当前标签
        void  SetPanelTitle(const std::string& id, std::string title);
        std::vector<std::string> GetPanelIds() const;
        // 取出所有面板（界面重建时由 UiLayout 回收宿主面板），停靠区随后为空
        std::map<std::string, std::unique_ptr<Node>> TakeAllPanels();

        // ── 停靠操作 ────────────────────────────────────────────
        // 把面板放到 target 面板所在标签组的 side 一侧；target 为空表示整个停靠区的 side 一侧。
        // size：新拆出的一侧的尺寸（0 = 目标的一半，停靠到整体边缘时为 250）
        bool Dock(const std::string& id, const std::string& target, DockSide side, float size = 0.0f);
        void ClosePanel(const std::string& id);
        void ShowPanel(const std::string& id);       // 不在布局里时回到关闭前的位置（没有记录则停靠到右侧），并设为当前标签
        void ActivatePanel(const std::string& id);
        void TogglePanel(const std::string& id) { IsPanelVisible(id) ? ClosePanel(id) : ShowPanel(id); }

        // ── 布局保存与恢复 ──────────────────────────────────────
        JsonValue SaveLayout() const;
        // 未知的面板 ID、重复引用记入 warnings；布局里没有的面板隐藏
        bool      LoadLayout(const JsonValue& layout, std::vector<std::string>* warnings = nullptr);

        // 用户操作（拖动停靠、关闭、调整尺寸）改变布局后调用，宿主可借此保存布局
        void SetOnLayoutChanged(std::function<void()> cb) { m_onLayoutChanged = std::move(cb); }

        // ── 查询（测试与宿主使用）─────────────────────────────
        Rect      GetPanelRect(const std::string& id) const;  // 面板内容区（窗口坐标）；不可见时为空矩形
        bool      IsDragging() const { return m_drag.active; }
        bool      GetDropPreview(Rect& out) const;            // 拖动中且有有效落点时返回预览矩形（窗口坐标）

        static constexpr float kHeaderHeight = 28.0f;
        static constexpr float kSplitterSize = 1.0f;        // 分隔条在布局中占的宽度：只有一条分界线，面板之间没有缝隙
        static constexpr float kSplitterHit  = 6.0f;        // 可拖动的热区宽度：叠在两侧面板的边缘上，不额外占位
        static constexpr float kMinPanel     = 80.0f;
        static constexpr float kEdgeDrop     = 28.0f;       // 贴近停靠区边缘多少像素算停靠到整体一侧
        static constexpr float kDragStart    = 6.0f;        // 按下后移动多少像素开始拖动

    protected:
        void OnLayout() override;
        void OnPaintOverlay(DrawList& dl, const Rect& screenRect) override;

    private:
        friend class DockGroup;
        friend class DockSplitter;

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
            std::unique_ptr<Node> owned;            // 不在布局里时由这里持有
            // 关闭前的位置：旁边的面板 + 方位 + 尺寸
            std::string           lastTarget;
            DockSide              lastSide = DockSide::Right;
            float                 lastSize = 0.0f;
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

        // 布局与重建
        void   LayoutTree(Tree* node, const Rect& rect);
        void   Rebuild();                                       // 按树重建标签组和分隔条节点
        void   BuildNodes(Tree* node);
        void   LayoutChanged();

        // 拖动（由 DockGroup 转发）
        void   BeginDrag(const std::string& panelId, Vec2 windowPos);
        void   UpdateDrag(Vec2 windowPos);
        void   EndDrag(Vec2 windowPos, bool cancel);
        DropTarget ComputeDrop(Vec2 local) const;

        // 保存 / 加载
        JsonValue SaveTree(const Tree* node) const;
        std::unique_ptr<Tree> LoadTree(const JsonValue& v, std::vector<std::string>& warnings, std::vector<std::string>& used);

        std::unique_ptr<Tree>          m_root;
        std::map<std::string, Panel>   m_panels;
        std::function<void()>          m_onLayoutChanged;
        bool                           m_rebuildPending = false;

        struct Drag
        {
            bool        active = false;
            std::string panel;
            Vec2        pos;                // 窗口坐标
            DropTarget  target;
            uint32_t    escShortcut = 0;    // 拖动期间临时注册的 Esc（取消拖动），不需要抢键盘焦点
        } m_drag;
    };
}
