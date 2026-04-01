# MiniCAD

一个基于 Direct3D 11 的轻量级 CAD 原型系统，使用 C++ 和 Win32 构建。

项目目标不是成为完整的工程软件，而是用尽可能少的代码，把一个 CAD 系统该有的骨架——绘图、选择、Undo/Redo、图层、文件序列化——都搭清楚，方便学习和二次开发。

 

## 功能

- **直线绘制**：按 `L` 激活工具，左键连续点击绘制折线段，右键结束当前段，空格从上一个端点继续，ESC 退出
- **选择与高亮**：鼠标悬停高亮，左键点选，Ctrl+左键多选，Delete 删除选中实体
- **Undo / Redo**：Ctrl+Z / Ctrl+Y，基于命令模式，支持添加和删除的完整撤销
- **视口导航**：鼠标中键拖拽平移，滚轮以鼠标为中心缩放，自适应动态网格
- **文件序列化**：Ctrl+S 保存为 `.mcad`（JSON 格式），Ctrl+O 打开文件对话框加载，支持几何数据、颜色、图层属性的完整读写
- **图层管理**：图层数据结构完整（名称、颜色、可见性、锁定），序列化已接入
  
 

## 构建

用 Visual Studio 2022 打开CMaklist.txt。
 

### 关键设计

**命令模式（Command Pattern）**

所有改变场景的操作（添加、删除实体）都封装为 `ICommand`，由 `CommandStack` 统一管理。Undo/Redo 的实现只需在 `Execute` 里把对象所有权转移给命令对象，`Undo` 时再还回 `Scene`，不需要快照。

```
ICommand
├── AddEntityCommand    // Execute: Scene.Add  / Undo: Scene.Remove
└── DeleteEntityCommand // Execute: Scene.Remove / Undo: Scene.Add
```

**责任链（Chain of Responsibility）**

Win32 消息在 `InputSystem` 里统一转换为平台无关的 `InputEvent`，然后沿责任链依次传递：

```
Viewport（Pan/Zoom）→ Document（Ctrl+S/O）→ Editor（选择/工具/快捷键）
```

每个 Handler 返回 `true` 则消费消息，链条终止；返回 `false` 则继续向下传递。

**工具接口（ITool）**

绘图工具实现 `ITool` 接口，Editor 持有当前激活的工具。工具通过 `IViewContext` 与渲染层通信（设置预览图元、坐标转换），不直接依赖 `Viewport`。添加新工具只需实现接口，在 `Editor::OnKeyDown` 里注册快捷键。

**序列化（ISerializer）**

序列化接口采用读写对称设计——`Serialize` 和 `Deserialize` 调用完全相同的 `Field()` 语句，切换底层实现（`JsonWriter` / `JsonReader`）即可完成读写，不需要维护两套逻辑。`.mcad` 文件是可读的 JSON，包含版本号、图层表、实体列表和 ID 计数器。
 

新实体类型只需在 `SceneSerializer::RegisterAllTypes()` 里注册一次工厂函数，读取逻辑无需修改。
  
## 快捷键

| 快捷键 | 功能 |
|---|---|
| `L` | 直线 |
| 空格 | 继续绘制 |
| ESC | 退出绘制 |
| 左键 | 点选 |
| Ctrl + 左键 | 多选 |
| Delete | 删除 |
| Ctrl + Z | 撤销 |
| Ctrl + Y | 重做 |
| Ctrl + S | 保存 |
| Ctrl + O | 打开 |
| 中键 | 平移 |
| 滚轮 | 缩放 |

---

## 目录

```
src/
├── App/
│   ├── Abstractions/       接口定义（ICommand、ITool、IInputHandler、IViewContext）
│   ├── Command/            具体命令（AddEntity、DeleteEntity）
│   ├── CommandStack/       Undo/Redo 栈
│   ├── Document/           文档根对象，持有 Scene、Editor、CommandStack
│   ├── Editor/             编辑器逻辑（选择、工具调度、快捷键）
│   ├── Input/              Win32 消息 → InputEvent 转换，责任链分发
│   ├── Picking/            点选拾取（点到线段距离）
│   ├── Preview/            工具绘制预览图元定义
│   ├── Scene/              场景容器，图层管理
│   ├── Tools/              绘图工具（LineTool）
│   ├── Main.cpp
│   ├── MainWindow.h/cpp    Win32 窗口、D3D11 初始化、消息循环
├── Core/
│   ├── Entity/             实体类型（LineEntity、EntityAttr）
│   ├── GeomKernel/         几何基础（Line、AABB）
│   ├── Object/             基类、运行时类型系统、ID 生成器
│   └── Serialization/      ISerializer、JsonWriter/Reader、SceneSerializer、nlohmann/json
└── Render/
    ├── D3D11/              Device、SwapChain、Renderer、RenderTarget、Basic.hlsl
    └── Viewport/           Camera（正交投影）、Grid（动态网格）、Viewport
```
  
## 待实现

- [ ] 更多绘图工具（矩形、圆、多段线）
- [ ] 框选（拖拽 AABB 多选）
- [ ] 图层可见性过滤 
- [ ] 网格随相机实时更新
- [ ] 工具栏 / 属性面板 UI
- [ ] 坐标输入（捕捉到格点、端点）
 
  
