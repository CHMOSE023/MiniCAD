#pragma once
#include "Editor/Tools/ITool.h"
#include "Editor/Overlay/Overlay.h"
#include "Editor/Picking/Picking.h"
#include "Editor/Snap/SnapEngine.h"
#include "Editor/Snap/SnapResult.h"
#include "Editor/Grip/GripEditor.h"
#include "Editor/Input/InputEvent.h"
#include "Editor/Input/KeyCode.h"
#include "Editor/Constraint/ConstraintEngine.h"
#include "Editor/Resolver/InputResolver.h"
#include "Editor/CommandLine/CommandLine.h"
#include "Viewport/Viewport.h"
#include "Viewport/ViewState.h" 
#include "Document/CommandStack/CommandStack.h"
#include "Document/Command/ArrayCommand.h"
#include "Document/Command/ChangeAttrCommand.h"
#include "Editor/Properties/PropertyTable.h"
#include "Editor/Tools/HatchSettings.h"
#include "Editor/Tools/MLineSettings.h"
#include "Document/GlyphTypes.h"
#include "Core/GeomKernel/Line.hpp"
#include "Core/Object/Object.hpp"
#include "Render/VertexTypes.hpp"
#include "Render/ImageDraw.hpp"
#include "Core/Image/ImageLibrary.h"
#include <unordered_map>
#include <unordered_set>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace MiniCAD
{
    class Document;
    class Entity;

    class Editor
    {
    public:
        Editor();

        // ── 绑定 / 解绑文档与视口 ────────────────────────────
        void Bind(Document& doc, Viewport& viewport);
        void Unbind();
        bool IsBound() const { return m_doc != nullptr; }

        // ── 输入 ─────────────────────────────────────────────
        bool OnInput(const InputEvent& e);

        // ── 渲染：收集顶点 + 提交到 Viewport ─────────────────
        void Render();

        // ── Picking / 选择 ────────────────────────────────────
        const std::unordered_set<Object::ObjectID>& GetSelection();
        void SetSelection(std::unordered_set<Object::ObjectID> ids);   // 整体替换选择集
        // 全选：选中所有可拾取的对象（可见、所在图层未关闭也未锁定），替换当前选择集；返回选中的个数。
        // 工具进行中不处理（返回 0），避免改动工具已经拿到的对象列表
        size_t SelectAll();
        // 分解选择集：多段线 → 直线 / 圆弧，矩形 → 4 条直线，块插入 → 块里的对象（见 ExplodeEntity）。
        // 原对象被替换为分解出的对象并选中它们，一步可撤销；不能分解的对象保持不变并提示。返回被分解的对象个数
        size_t ExplodeSelection();

        // 修改选择集的常规属性（图层 / 颜色 / 线型 / 线宽 / 透明度…），一步可撤销。
        // 特性面板、MatchProp 等都走这里。返回是否真的改动了对象。
        bool ChangeSelectionAttr(const AttrChange& change);

        // 选择集的几何特性（属性描述表）：多种类型取公共特性；没有选择返回空
        std::vector<CommonProperty> GetSelectionProperties();
        // 把选择集里所有具有该特性的对象改成 value，一步可撤销。
        // 值非法 / 只读 / 没有变化的对象跳过；一个都没改返回 false（不入撤销栈）
        bool SetSelectionProperty(const std::string& name, const PropValue& value);
        const std::unordered_set<Object::ObjectID>& GetHovered();

        Object*              GetPrimarySelectedObject();
        std::vector<Object*> GetSelectedObjects();
        // 选择集里可以修改的对象（排除锁定图层上的）；announce 为真时在命令行说明跳过了几个。
        // 所有修改 / 删除已有对象的操作都从这里取对象；复制 / 偏移这类只读原对象的操作用 GetSelectedObjects
        std::vector<Object::ObjectID> EditableSelectionIds(bool announce = true);
        std::vector<Object*>          GetEditableSelectedObjects(bool announce = true);

        // ── 夹点 ─────────────────────────────────────────────
        GripEditor&        GetGripEditor()       { return m_gripEditor; }
        const Line&        GetAnchorLine() const { return m_constraintEngine.GetGuideLine(); }
        bool               IsConstraintActive() const { return m_constraintEngine.IsAnyActive(); }
        ConstraintEngine&  GetConstraintEngine() { return m_constraintEngine; }
        bool               IsActiveTool() const  { return m_tool != nullptr; }
        // 工具是否还在进行：工具刚结束（等下一次输入事件才释放）的收尾期不算
        bool               IsToolRunning() const { return m_tool != nullptr && !m_pendingToolReset; }
        // 最近启动的工具 ID（"Line"、"Move"…）；IsActiveTool 为 true 时即当前工具
        const std::string& GetLastCommand() const { return m_lastCommand; }

        // ── 工具注册表 ───────────────────────────────────────
        void RegisterTool(const std::string& toolId, std::function<std::unique_ptr<ITool>()> factory);
        void RegisterAlias(const std::string& alias, const std::string& toolId);
        void ActivateToolById(const std::string& toolId);
        void ActivateRegisteredTool(const std::string& toolId);     // 跳过特例命令,直接按注册表启动工具

        // ── 命令行 ───────────────────────────────────────────
        CommandLine&       GetCmdLine()       { return m_commandLine; }
        const CommandLine& GetCmdLine() const { return m_commandLine; }
        void RunCommand(const std::string& text);   // 命令行/键盘提交的命令统一入口
        void RunZoomOption(const std::string& opt);          // ZOOM 的选项（大写）
        bool IsZoomPending() const { return m_zoomPending; }   // ZOOM 已输入、正等待选项（E / A）
        // 缩放到全图（ZOOM A）。undoable = false 用于打开文件后的初始视图，不进撤销栈
        void ZoomAll(bool undoable = true);
        std::vector<std::string> GetCommandNames() const;  // 所有别名 + 工具全名，供补全
        std::string ResolveCommandAlias(const std::string& word) const; // 别名/全名 → 工具ID（大小写不敏感），未匹配返回空串

        // ── 绘制工具便捷方法 ────────────────────────────────
        void StartLineTool();
        void StartPointTool();
        void StartRectangleTool();
        void StartCircleTool();
        void StartArcTool();
        void StartEllipseTool();
        void StartPolylineTool();
        void StartSplineTool();
        void StartTextTool();
        void StartMTextTool();

        // ── 编辑工具便捷方法 ─────────────────────────────────
        void StartMoveTool();
        void StartCopyTool();
        void StartMirrorTool();
        void StartRotateTool();
        void StartScaleTool();
        void StartStretchTool();
        void StartFilletTool();
        void StartChamferTool();

        // 粘贴:剪贴板实体跟随光标预览,左键指定插入点(剪贴板由 DocumentManager 持有)
        // basePoint 非空时为带基点复制(COPYBASE)的基点,光标即基点;否则取包围盒左下角
        void StartPasteTool(const std::vector<std::unique_ptr<Entity>>* clipboard,
                            std::function<void()> onCommitted,
                            const Math::Point3* basePoint = nullptr);

        // 单点拾取:左键拾取一个点后回调并结束,右键/ESC 取消(带基点复制等轻交互)
        void StartPickPointTool(std::string prompt, std::function<void(const Math::Point3&)> onPicked);

        // 定义块(AutoCAD BLOCK):把指定实体收进新块并原位替换为块引用;
        // name 为空时自动生成唯一块名(Block1..N)
        void DefineBlock(const std::vector<Object::ObjectID>& ids, const Math::Point3& basePoint,
                         const std::string& name);

        // ── 块名输入请求(定义块,B):拾取基点后由 UI 弹块名对话框 ──
        struct BlockNameRequest
        {
            bool                          Active = false;
            std::string                   DefaultName;   // 预生成的唯一块名
            std::vector<Object::ObjectID> Ids;           // 待入块实体
            Math::Point3                  BasePoint;
        };
        BlockNameRequest&       GetBlockNameRequest()       { return m_blockNameRequest; }
        const BlockNameRequest& GetBlockNameRequest() const { return m_blockNameRequest; }
        void SubmitBlockDefine(const std::string& name);    // 确认块名并执行定义;空名用 DefaultName

        // ── 插入块请求(Insert,I):UI 列块表,选定后启动放置工具 ──
        using BlockID = uint32_t;   // 与 Scene/BlockTable.h 的别名一致
        struct BlockInsertRequest { bool Active = false; };
        BlockInsertRequest&       GetBlockInsertRequest()       { return m_blockInsertRequest; }
        const BlockInsertRequest& GetBlockInsertRequest() const { return m_blockInsertRequest; }
        void SubmitBlockInsert(BlockID blockId);             // 选定块,光标预览放置

        // ── 阵列请求(Array,AR):预选实体后由 UI 弹阵列参数对话框 ──
        // 对话框直接编辑 Params,确认后 SubmitArray 生成 ArrayCommand。
        struct ArrayRequest
        {
            bool                          Active        = false;
            bool                          PickingCenter = false;  // 正在拾取环形中心(对话框暂隐)
            std::vector<Object::ObjectID> SourceIds;              // 源对象快照
            ArrayParams                   Params;                 // 对话框就地编辑
        };
        ArrayRequest&       GetArrayRequest()       { return m_arrayRequest; }
        const ArrayRequest& GetArrayRequest() const { return m_arrayRequest; }
        // ── 面域（REGION）与面积查询（AREA）：对当前选择执行，结果回显到命令行 ──
        void CreateRegionFromSelection();                    // 选中的封闭对象 / 可接成环的线段 → 面域
        void ReportArea();                                   // 报告选中对象的面积与周长
        // 面域布尔运算：对选择集中的面域 / 封闭对象运算，结果是一个新面域，原对象被替换（可一步撤销）。
        // 差集：ID 最小（最先画的）为被减对象，其余为减去的对象。
        enum class BooleanOp { Union, Subtract, Intersect };
        void RegionBoolean(BooleanOp op);                                   // 报告选中对象的面积与周长
        void RequestArray();                                 // 检查选择,弹对话框(无选择则提示)
        void SubmitArray();                                  // 按 Params 执行阵列
        void CancelArray();                                  // 关闭对话框,清预览
        void BeginArrayCenterPick();                         // 进入环形中心拾取
        void BuildArrayPreview(const ArrayParams& p);        // 每帧重建幽灵预览

        // ── 图案填充(Hatch,H) ─────────────────────────────────
        // 宿主提供图案对话框时(Win32)开启对话框模式:Hatch 先置请求由 UI 弹对话框,
        // 确认后 SubmitHatch 进入拾取内部点;未开启时(Web)Hatch 直接进入拾取。
        struct HatchRequest { bool Active = false; };
        HatchRequest&        GetHatchRequest()        { return m_hatchRequest; }
        const HatchRequest&  GetHatchRequest()  const { return m_hatchRequest; }
        MLineSettings&       GetMLineSettings()       { return m_mlineSettings; }
        HatchSettings&       GetHatchSettings()       { return m_hatchSettings; }
        const HatchSettings& GetHatchSettings() const { return m_hatchSettings; }
        void SetHatchDialogEnabled(bool on) { m_hatchDialogEnabled = on; }

        // ── 文字 ─────────────────────────────────────────────
        double GetTextHeight() const      { return m_textHeight; }       // 默认字高(TEXTSIZE),样式未固定字高时用
        void   SetTextHeight(double h)    { if (h > 0.0) m_textHeight = h; }
        double CurrentTextHeight() const;                                 // 新建文字实际使用的字高

        // 文字样式管理(Style,ST):置请求由 UI 弹文字样式对话框;对话框直接编辑场景的文字样式表
        // 插入光栅图像（IMAGE）：Active 时界面弹出文件选择框，选定后调用 SubmitImagePath
        struct ImageRequest { bool Active = false; };
        ImageRequest&       GetImageRequest()       { return m_imageRequest; }
        const ImageRequest& GetImageRequest() const { return m_imageRequest; }
        bool SubmitImagePath(const std::string& path);       // 读取图像并启动放置工具；失败返回 false
        ImageLibrary&       GetImageLibrary()       { return m_imageLibrary; }

        struct TextStyleRequest { bool Active = false; };
        TextStyleRequest&       GetTextStyleRequest()       { return m_textStyleRequest; }
        const TextStyleRequest& GetTextStyleRequest() const { return m_textStyleRequest; }
        void CloseTextStyle() { m_textStyleRequest = {}; }
        void SubmitHatch();                                  // 关闭对话框,按当前设置拾取内部点
        void CancelHatch();

        // ── 几何编辑工具便捷方法 ─────────────────────────────
        void StartTrimTool();
        void StartExtendTool();
        void StartBreakTool();

        // ── 文字输入请求 ─────────────────────────────────────
        struct TextInputRequest
        {
            bool             Active       = false;
            Math::Point3     InsertPos;
            float            Height       = 2.5f;
            float            Rotation     = 0.f;
            Object::ObjectID EditTargetId = Object::InvalidID;
            std::string      InitialText;
        };

        TextInputRequest&       GetTextInputRequest()       { return m_textRequest; }
        const TextInputRequest& GetTextInputRequest() const { return m_textRequest; }
        void SubmitTextInput(const std::string& utf8Text);

        // ── 多行文字输入请求 ─────────────────────────────────
        struct MTextInputRequest
        {
            bool             Active       = false;
            Math::Point3     InsertPos;
            double           Height       = 2.5;
            double           Rotation     = 0.0;
            double           BoxWidth     = 0.0;
            Object::ObjectID EditTargetId = Object::InvalidID;
            std::string      InitialText;
            int              EditRow      = -1;      // EditTargetId 是表格时：正在编辑的单元格
            int              EditCol      = -1;
        };

        MTextInputRequest&       GetMTextInputRequest()       { return m_mtextRequest; }
        const MTextInputRequest& GetMTextInputRequest() const { return m_mtextRequest; }
        void SubmitMTextInput(const std::string& utf8Text);

        // ── 动态输入(AutoCAD 式) ─────────────────────────────
        // 工具有锚点(橡皮筋阶段)时,app 每帧读取该快照在光标旁显示
        // 长度/角度与约束捕捉信息;用户键入数值后经 ApplyDynamicInput 提交。
        struct DynamicInputState
        {
            bool             Active   = false;  // 有活跃工具且已有锚点
            Math::Point3     Anchor;            // 锚点(上一点)
            Math::Point3     Current;           // 最近解析点(含捕捉/约束)
            double           Length   = 0.0;    // 预览长度(世界单位)
            double           AngleDeg = 0.0;    // 预览角度(度,X 正向起逆时针,[0,360))
            bool             OrthoOn  = false;
            bool             PolarOn  = false;
            double           PolarAngleDeg = 0.0;
            SnapResult::Type SnapType = SnapResult::Type::None;  // 当前捕捉类型
        };
        DynamicInputState GetDynamicInput() const;

        // 提交键入的长度/角度,合成精确定点送给当前工具(不经捕捉/约束改写):
        //   只给长度 → 沿当前橡皮筋方向;只给角度 → 取当前长度;都给 → 极坐标。
        void ApplyDynamicInput(bool hasLen, double len, bool hasAngle, double angleDeg);

        // 把精确点交给当前工具（不经捕捉/约束改写），等同于在该点左键单击；没有可用工具时返回 false
        bool SubmitPoint(const Math::Point3& p);

        // 命令行坐标输入（工具进行中，同 AutoCAD）：
        //   "x,y" 绝对坐标；"@dx,dy" 相对上一点；"@距离<角度" 相对极坐标；"距离" 沿当前橡皮筋方向（直接距离输入）
        // 成功时回显"提示 输入"并返回 true；不是坐标格式、或需要上一点但没有时返回 false（不改变任何状态）
        bool SubmitCoordinateText(const std::string& text);

        // ── 删除 ─────────────────────────────────────────────
        void DeleteSelected();

        // ── 约束 ─────────────────────────────────────────────
        bool TryGetAnchor(Math::Point3& out) const;

        bool IsOrthoEnabled() const;
        void SetOrthoEnabled(bool enabled);
        void ToggleOrtho();

        bool   IsPolarEnabled() const;
        void   SetPolarEnabled(bool enabled);
        void   TogglePolar();
        double GetPolarAngle() const;
        void   SetPolarAngle(double deg);

        // ── 捕捉 ─────────────────────────────────────────────
        bool IsSnapEnabled() const;
        void SetSnapEnabled(bool enabled);
        void ToggleSnap();

        // 捕捉模式/孔径等细粒度设置直接操作 SnapEngine（见 SnapMode 位掩码）
        SnapEngine&       GetSnapEngine()       { return m_snap; }
        const SnapEngine& GetSnapEngine() const { return m_snap; }

        // ── 悬停高亮 ─────────────────────────────────────────
        bool IsHoverEnabled() const { return m_picking.IsHoverEnabled(); }
        void SetHoverEnabled(bool enabled) { m_picking.SetHoverEnabled(enabled); }
        void ToggleHover() { SetHoverEnabled(!IsHoverEnabled()); }

        // ── 全局细线：所有线宽按 1px 细线显示（只影响显示，不改图纸）────
        bool IsThinLines() const { return m_thinLines; }
        void SetThinLines(bool thin);
        void ToggleThinLines() { SetThinLines(!m_thinLines); }

        // 关闭文档后调用：丢弃已解码的光栅图像（仍在用的下一帧按需重新加载）。
        // 每次加载得到新的 ImageData::Key，渲染器里按 Key 缓存的纹理要同时释放
        void ClearImageCache() { m_imageLibrary.Clear(); }

        // ── Undo / Redo / Command ─────────────────────────────
        void Undo();
        void Redo();
        void ExecuteCommand(std::unique_ptr<ICommand> cmd);

        // ── 字体纹理（由应用层注入）──────────────────────────
        void SetFontTexture (void* srv)              { m_fontTexture = srv; }
        // 纹理字形回调（由应用层注入：WebFontAtlas 路径）
        void SetGlyphProvider(GlyphProvider provider) { m_glyphProvider = std::move(provider); }

        // ── 渲染辅助 ─────────────────────────────────────────
        ViewState BuildViewState();  // app calls after Render() to get snapshot for Viewport

    private:
        bool HandleGlobal (const InputEvent& e);
        bool HandleDefault(const InputEvent& e);

        void ActivateTool(std::unique_ptr<ITool> tool);
        void ActivateToolByAlias(const std::string& alias);
        char ToCommandChar(KeyCode key);
        std::string GenerateBlockName() const;   // 生成唯一块名 Block1..N
        void RequestBlockInsert();               // Insert 命令入口:置插入块请求,UI 弹块表选择

        void RegisterBuiltinTools();

        // 场景顶点流：所有实体按未选中态绘制；仅几何/图层变化时重建。
        // 拖动跟随中的实体被排除（由选中流绘制）。返回本帧是否重建。
        bool UpdateSceneVertices(double worldPerPixel, bool zoomChanged);
        void ApplyWipes(const std::vector<std::vector<Math::Point3>>& wipes);   // 用区域覆盖裁剪已拼接的场景顶点流

        // 选中流：选中 + 拖动中的实体，叠加绘制在场景之上。
        // 仅在选择集变化/场景重建/拖动中重建，避免选中触发全场景重建。
        void UpdateSelectionVertices(double worldPerPixel, bool force);

        void BuildHoverHighlight();   // 每帧重建悬停高亮（overlay），与场景顶点解耦

    private:
        // 绑定目标（非拥有）
        Document*  m_doc      = nullptr;
        Viewport*  m_viewport = nullptr;

        // Editor 拥有的子系统
        Overlay          m_overlay;
        Picking          m_picking;
        SnapEngine       m_snap;
        SnapResult       m_currentSnap;
        Math::Point3     m_lastResolved{};        // 最近一次解析点(动态输入预览用)
        bool             m_hasResolved = false;
        GripEditor       m_gripEditor;
        ConstraintEngine m_constraintEngine;
        InputResolver    m_resolver;        // 输入解析器
        CommandLine      m_commandLine;     // 命令行提示与回显缓冲

        // 工具
        std::unique_ptr<ITool> m_tool;
        bool                   m_toolSuspended    = false;
        bool                   m_pendingToolReset = false;
        double                 m_filletRadius = 1.0;           // 圆角半径，多次圆角之间保留
        double                 m_chamferD1 = 1.0;              // 倒角距离，多次倒角之间保留
        double                 m_chamferD2 = 1.0;

        std::unordered_map<std::string,
            std::function<std::unique_ptr<ITool>()>>     m_toolRegistry;
        std::unordered_map<std::string, std::string>     m_aliasRegistry;
        bool                                             m_zoomPending = false;   // ZOOM 命令等待选项
        std::string                                      m_cmdBuffer;
        std::string                                      m_lastCommand;

        TextInputRequest   m_textRequest;
        MTextInputRequest  m_mtextRequest;
        BlockNameRequest   m_blockNameRequest;
        BlockInsertRequest m_blockInsertRequest;
        ArrayRequest       m_arrayRequest;
        HatchRequest       m_hatchRequest;
        HatchSettings      m_hatchSettings;
        MLineSettings      m_mlineSettings;
        bool               m_hatchDialogEnabled = false;
        double             m_textHeight = 2.5;
        TextStyleRequest   m_textStyleRequest;
        ImageRequest       m_imageRequest;

        // 鼠标位置（每帧更新）
        double m_mouseX = 0;
        double m_mouseY = 0;

        // 渲染数据（每帧收集）
        std::vector<Vertex_P3_C4>    m_sceneVertices;
        std::vector<Vertex_P3_C4>    m_sceneFillVertices;
        std::vector<ImageDraw>       m_sceneImages;
        ImageLibrary                 m_imageLibrary;
        std::vector<Vertex_P3_C4_UV> m_textVertices;
        uint64_t                     m_sceneVersion = 0;   // 场景顶点重建计数（GPU 缓存键）

        // 选中流（选中 + 拖动中的实体，叠加在场景之上）
        std::vector<Vertex_P3_C4>    m_selVertices;
        std::vector<Vertex_P3_C4>    m_selFillVertices;
        std::vector<Vertex_P3_C4_UV> m_selTextVertices;
        uint64_t                     m_selVersion = 0;
        bool                         m_selViewDependent = false;

        // 最近一次场景流构建时排除的实体（拖动跟随中的实体）
        std::unordered_set<Object::ObjectID> m_sceneExcluded;

        // ── 逐实体顶点缓存 ───────────────────────────────────
        // 场景流重建时只重新细分脏实体（Scene::GetDirtyEntities），
        // 其余实体直接拷贝缓存顶点，避免增量编辑触发全场景重新细分
        // （文字排版/标注绘制在大场景下是主要开销）。
        struct EntityVertexCache
        {
            std::vector<Vertex_P3_C4>    lines;
            std::vector<Vertex_P3_C4>    fills;
            std::vector<Vertex_P3_C4_UV> texts;
            bool                         viewDependent = false;   // 线宽/虚线按像素烘焙：缩放后需重新细分
            std::vector<ImageDraw>       images;
            std::vector<std::vector<Math::Point3>> wipes;   // 区域覆盖多边形：拼接时裁剪此前的顶点
        };
        std::unordered_map<Object::ObjectID, EntityVertexCache> m_entityVertexCache;

        // 细分临时缓冲（复用容量，避免逐实体分配）
        std::vector<Vertex_P3_C4>    m_scratchLines;
        std::vector<Vertex_P3_C4>    m_scratchFills;
        std::vector<Vertex_P3_C4_UV> m_scratchTexts;
        std::vector<Vertex_P3_C4>    m_overlayVertices;
        std::vector<GripDraw>        m_gripVertices;
        void*                        m_fontTexture    = nullptr;
        GlyphProvider                m_glyphProvider;           // 由应用层注入

        // 线宽/虚线几何按屏幕像素烘焙到世界顶点,缩放改变像素比例时需重建
        double                       m_lastWorldPerPixel  = 0.0;
        bool                         m_sceneViewDependent = false;

        bool                         m_thinLines      = false;   // 全局细线：忽略线宽
        bool                         m_displayInvalid = false;   // 显示设置变化：下一帧所有实体重新细分
    };
}
