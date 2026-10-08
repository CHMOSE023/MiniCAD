#pragma once
#include "Core/Node.h"
#include "Core/ShortcutTable.h"
#include "Render/DrawData.hpp"
#include "Widgets/Controls.h"
#include <string>
#include <vector>

namespace MiniGUI
{
    class ComboBox;
    class Label;
    class ListView;
    class MenuBar;
    class PropertyGrid;
    class TabView;
    class TreeView;
    class UIContext;

    // 控件展示：菜单栏 + 六个标签页 + 状态栏。
    // 与平台无关：Gallery 示例程序和截图测试都用它
    class GalleryRoot : public Node
    {
    public:
        explicit GalleryRoot(std::string assetsDir);

        // 必须在挂到 UIContext 之后调用（需要加载图标纹理）
        void Populate();
        void RegisterShortcuts(ShortcutTable& shortcuts);

        TabView*  GetTabs()        const { return m_tabs; }
        MenuBar*  GetMenuBar()     const { return m_menuBar; }
        ListView* GetLayerTable()  const { return m_layerTable; }
        TabView*  GetDocuments()   const { return m_docs; }
        void      SetStatus(std::string text);

        // 主题：切换 UIContext 的深色/浅色配色（Ctrl+T、视图 > 主题）
        void SetTheme(bool light);
        bool IsLightTheme() const;

        void OpenLayerDialog();
        void OpenFloatingPanel();
        void OpenSaveMessageBox();
        void NewDocument();

        enum Page { PageBasic, PageList, PageLayers, PageDocs, PagePopup, PageCommand, PageCount };

    protected:
        // 根节点自己铺主题底色：宿主清屏的颜色不随主题变化
        void OnPaint(DrawList& dl, const Rect& screenRect) override;

    private:
        struct Layer
        {
            std::string name;
            Color32     color      = Colors::White;
            int         linetype   = 0;        // StandardLinetypes() 下标
            float       lineweight = 0.25f;
            bool        on         = true;
            bool        frozen     = false;
            bool        locked     = false;
        };

        Node* BuildBasicPage();
        Node* BuildListPage();
        Node* BuildLayerPage();
        Node* BuildDocumentPage();
        Node* BuildPopupPage();
        Node* BuildCommandPage();
        void  BuildMenuBar();

        std::string IconPath(const char* name) const;
        void  RunCommand(const std::string& text);

        // 图层数据：表格、树、下拉框共用
        void  RefreshLayerViews();
        void  SortLayers(bool ascending);
        bool  RenameLayer(int index, const std::string& name);
        void  AddLayer();
        void  DeleteLayer(int index);

        // CAD 风格的下拉框
        ComboBox* MakeLinetypeCombo(Node* parent, int selected);
        ComboBox* MakeLineweightCombo(Node* parent, float selected);
        ComboBox* MakeLayerCombo(Node* parent);

        std::string m_assetsDir;
        MenuBar*    m_menuBar    = nullptr;
        TabView*    m_tabs       = nullptr;
        Label*      m_status     = nullptr;
        ListView*   m_history    = nullptr;
        ListView*   m_layerTable = nullptr;
        TreeView*   m_layerTree  = nullptr;
        TabView*    m_docs       = nullptr;
        std::vector<ComboBox*>   m_layerCombos;
        std::vector<std::string> m_historyItems;

        std::vector<Layer> m_layers;
        int  m_currentLayer = 0;
        bool m_sortAscending = true;
        int  m_untitled      = 1;

        RadioGroup  m_units;
        bool        m_showGrid       = true;
        bool        m_showLineweight = false;
        CheckBox*   m_gridCheck      = nullptr;
        CheckBox*   m_lineweightCheck = nullptr;
    };

    // 创建并挂到 ui 的主界面层
    GalleryRoot* BuildGallery(UIContext& ui, const std::string& assetsDir);
}
