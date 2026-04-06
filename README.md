# MiniCAD

一个基于 Direct3D 11 的轻量级 CAD 原型系统，使用 C++ 和 Win32 构建。

项目目标不是成为完整的工程软件，而是用尽可能少的代码，把一个 CAD 系统该有的骨架——绘图、选择、Undo/Redo、图层、文件序列化——都搭清楚，方便学习和二次开发。

 

## 功能

- **直线绘制**：按 `L` 激活工具，左键连续点击绘制折线段，右键结束当前段，空格从上一个端点继续，ESC 退出
- **选择与高亮**：鼠标悬停高亮，左键点选，Ctrl+左键多选，Delete 删除选中实体
- **Undo / Redo**：Ctrl+Z / Ctrl+Y，基于命令模式，支持添加和删除的完整撤销
- **视口导航**：鼠标中键拖拽平移，滚轮以鼠标为中心缩放，自适应动态网格
- **文件序列化**：Ctrl+S 保存为 `.mcad`（二进制格式），Ctrl+O 打开文件对话框加载，支持几何数据、颜色、图层属性的完整读写
- **图层管理**：图层数据结构完整（名称、颜色、可见性、锁定），序列化已接入
  
 

## 构建

用 Visual Studio 2022 及以上版本打开 `CMakeLists.txt`。
 

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

序列化接口使用虚基类 `ISerializer` 定义，具体实现为 `AsciiSerializer`。对象通过 `Serialize()` 和 `Deserialize()` 方法分别处理写入和读取操作（需维护两套逻辑保持同步）。`.mcad` 文件是二进制格式，包含版本号、图层表、实体列表和 ID 计数器。

新实体类型只需在 `ObjectFactory` 中注册工厂函数，即可自动支持序列化读取。
  
## 快捷键

| 快捷键 | 功能 |
|---|---|
| `L` | 激活直线工具 |
| 空格 | 从上一个端点继续绘制 |
| ESC | 退出绘制工具 |
| 左键 | 点选实体 |
| Ctrl + 左键 | 追加选择实体 |
| Delete | 删除选中实体 |
| Ctrl + Z | 撤销 |
| Ctrl + Y | 重做 |
| Ctrl + S | 保存文档 |
| Ctrl + O | 打开文档 |
| 中键拖拽 | 平移视口 |
| 滚轮 | 缩放视口 |
| **Ctrl + Shift + N** | **创建新图层** |
| **Ctrl + Shift + H** | **切换活跃图层可见性** |
| **Ctrl + Shift + L** | **切换活跃图层锁定状态** |
| **Ctrl + Shift + Delete** | **删除活跃图层** |
| **Ctrl + PgUp** | **切换到前一个图层** |
| **Ctrl + PgDn** | **切换到后一个图层** |

---

## 目录

```
src/
├── App/
│   ├── Abstractions/       接口定义（ICommand、ITool、IInputHandler、IViewContext）
│   ├── Command/            具体命令（AddEntityCommand、DeleteEntityCommand、LayerCommands）
│   ├── CommandStack/       Undo/Redo 栈管理
│   ├── Document/           文档根对象，持有 Scene、Editor、CommandStack
│   ├── Editor/             编辑器逻辑（选择、工具调度、快捷键、框选）
│   ├── Input/              Win32 消息 → InputEvent 转换，责任链分发
│   ├── Picking/            点选拾取（点到线段距离计算）
│   ├── Preview/            工具绘制预览图元定义
│   ├── Scene/              场景容器、图层管理（Layer、LayerManager）
│   ├── Tools/              绘图工具（LineTool）
│   ├── Main.cpp            程序入口
│   └── MainWindow.h/cpp    Win32 窗口、D3D11 初始化、消息循环
├── Core/
│   ├── Entity/             实体定义（LineEntity、EntityAttr）
│   ├── GeomKernel/         几何基础（Line、AABB）
│   ├── Object/             对象基类、RTTI 系统、ObjectFactory
│   └── Serialization/      ISerializer、AsciiSerializer、ObjectFactory
└── Render/
    ├── D3D11/              Device、SwapChain、Renderer、RenderTarget、Basic.hlsl
    └── Viewport/           Viewport、Camera（正交投影）、Grid（动态网格）
```
  
## 已实现功能

- [x] 框选（拖拽 AABB 多选）——`Editor` 中通过 `m_isRubberBanding` 实现
- [x] 图层可见性过滤——`Scene::IsEntityVisible()` 检查图层可见性
- [x] 图层锁定状态——实体锁定时无法被编辑

## 待实现功能

- [ ] 更多绘图工具（矩形、圆、多段线等）
- [ ] 网格自适应缩放显示
- [ ] 工具栏 / 属性面板 UI
- [ ] 坐标输入与捕捉（格点捕捉、端点捕捉）
- [ ] 图形变换工具（移动、旋转、缩放）
- [ ] 复制 / 粘贴功能
 
  
