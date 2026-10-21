# Lesson14：多文档管理

## 学习目标

本课讲清楚 MiniCAD 如何管理多个文档，以及切换文档时 Editor 如何安全地重新绑定。完成后学习者应理解 `DocumentManager` 的所有权模型、`Editor::Bind/Unbind` 的延迟绑定模式，以及 FontStyle 注册与跨文档共享的机制。

## 相关源码

- `src/Document/DocumentManager.h`
- `src/Document/DocumentManager.cpp`
- `src/Document/Document.h`
- `src/Editor/Editor.h`（`Bind`、`Unbind`、`IsBound`）
- `src/Text/FontSystem.h`
- `src/UI/UIManager.cpp`（文档选项卡渲染）

## 原理

**所有权模型**：`DocumentManager` 持有：

- `vector<unique_ptr<Document>> m_docs`：所有文档，DocumentManager 拥有生命周期。
- `unique_ptr<Viewport> m_viewport`：唯一视口，所有文档共用同一个视口。
- `Editor m_editor`：唯一编辑器实例，与视口共存亡。
- `Document* m_active`：当前活跃文档，非拥有指针。

**延迟绑定模式**：`Editor` 默认构造时 `m_doc == nullptr`（未绑定状态）。`InitViewport()` 之后才能调用 `Editor::Bind()`，因为绑定需要已初始化的 `Viewport`。`Editor` 内部的 `Overlay`、`Picking`、`GripEditor` 也使用同样的 `Bind()` 模式，构造时不持有指针，调用 `Bind()` 后才连接到具体对象。

**切换文档流程**：

```text
SetActive(newDoc)
  ↓
m_editor.Unbind()         // 断开与旧文档的连接，清理选择集、工具状态
  ↓
m_active = newDoc
  ↓
m_editor.Bind(*newDoc, *m_viewport)   // 重新连接新文档
```

切换后视口和渲染器不变，只有文档侧（Scene、CommandStack）被替换。

**FontStyle 跨文档共享**：`FontSystem` 由 `DocumentManager` 持有，注册字体样式时调用 `RegisterFontStyle()` 返回全局 ID。每个文档通过 `Document::SetFontSystem()` 注入指针，`MTextEntity` 存的是样式 ID，运行时通过 FontSystem 查找实际字体。这样多个文档共享同一套字体配置。

## MiniCAD 中的应用

`UIManager` 的文档选项卡通过以下方式实现切换：

```cpp
for (auto& doc : dm.GetAll())
{
    if (ImGui::TabItem(doc->GetName()))
        dm.SetActive(doc.get());
}
```

新建文档：`DocumentManager::New()` 调用 `Create()`，生成新文档并注入 FontSystem，然后调用 `SetActive()`。

关闭文档：`Close(doc)` 先 `Unbind()`（如果是活跃文档），再从 `m_docs` 中移除，然后激活剩余文档中的第一个。

## 课堂演示

1. 打开程序，新建两个文档，分别画不同图形，切换选项卡观察场景切换。
2. 在切换前后打断点，观察 `Bind/Unbind` 被调用的时机。
3. 注册一个自定义 FontStyle，验证它在两个文档中都可以使用。

## 拓展练习

1. 修改 `GenerateUniqueName()`，让新文档名称包含当前时间。
2. 思考：如果允许多个 Editor 同时绑定（分屏编辑），需要修改哪些设计？
3. 实现文档保存提示：关闭 dirty 文档时弹出"是否保存"对话框。
