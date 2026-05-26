# Lesson07：命令模式与撤销重做

## 学习目标

读懂 `ICommand` 接口设计，理解 `Execute`/`Undo` 之间的所有权转移语义，掌握 `CommandStack` 的双栈实现，并能分析 `AddEntityCommand`、`BatchDeleteCommand`、`MoveCommand` 三种具体命令的实现差异。

---

## 相关源码

- `src/Document/CommandStack/ICommand.h`
- `src/Document/CommandStack/CommandStack.h`
- `src/Document/Command/AddEntityCommand.h`
- `src/Document/Command/BatchDeleteCommand.h`
- `src/Document/Command/MoveCommand.h`

---

## ICommand 接口

```cpp
class ICommand {
    virtual bool        Execute(Scene& scene) = 0;  // false = 执行失败，不入栈
    virtual void        Undo(Scene& scene) = 0;
    virtual std::string GetName() const = 0;        // 用于日志 / 菜单显示
};
```

**`Execute` 返回 false 的含义**：命令校验失败，操作不应该发生，`CommandStack` 不会将其推入撤销栈。例如：零长度线段（两点重合）、空选择集的移动、空文本提交等。

---

## CommandStack：双栈实现

```cpp
class CommandStack {
    bool Execute(unique_ptr<ICommand> cmd, Scene& scene);
    void Undo(Scene& scene);
    void Redo(Scene& scene);
    void Push(unique_ptr<ICommand> cmd);  // 只入栈，不执行
    bool CanUndo() const;
    bool CanRedo() const;
    void Clear();
private:
    stack<unique_ptr<ICommand>> m_undoStack;
    stack<unique_ptr<ICommand>> m_redoStack;
};
```

**`Execute` 的完整逻辑**：

```
Execute(cmd, scene):
  1. cmd->Execute(scene)  → 失败（返回 false）→ 丢弃 cmd，返回 false
  2. 成功 → 清空 m_redoStack（新操作使已有重做历史失效）
  3. undoStack.push(cmd)
  4. 返回 true
```

**`Undo`**：

```
undoStack.top()->Undo(scene)
redoStack.push(undoStack.top())
undoStack.pop()
```

**`Redo`**：

```
redoStack.top()->Execute(scene)
undoStack.push(redoStack.top())
redoStack.pop()
```

**为什么新命令执行后清空 redo 栈**：时间线只能单向前进。若已撤销了 3 步，此时执行新命令，那 3 步 redo 历史对应的是另一条时间线，继续保留会造成逻辑矛盾。

**`Push` vs `Execute`**：`Push` 只入 undo 栈，不调用 `Execute()`。用于"操作已经发生"的场景——例如夹点拖拽，实体几何在拖拽期间已经实时修改了，`EndDrag` 时只需要让命令栈记录这次操作以便 Undo，不需要再执行一遍。

---

## 所有权转移语义

这是命令模式中最微妙的部分：

```
命令构造时：Command 持有 unique_ptr<Entity>（m_entity 非空）

Execute 成功：
  scene.AddEntity(std::move(m_entity))  → 所有权转给 Scene
  m_entity = nullptr（Command 不再持有）

Undo：
  m_entity = scene.RemoveEntity(m_id)  → 所有权从 Scene 取回
  Command 重新持有实体

Redo（再次 Execute）：
  scene.AddEntity(std::move(m_entity))  → 再次转给 Scene
```

**意义**：实体始终只有一个所有者，不会析构，也不会双重释放。Command 对象在整个生命周期内都可能需要"持有"实体（Undo 之后），所以不能在 Execute 后释放 Command。

---

## AddEntityCommand

```cpp
class AddEntityCommand : public ICommand {
    explicit AddEntityCommand(unique_ptr<Object> entity)
        : m_entity(std::move(entity)) {}

    bool Execute(Scene& scene) override {
        if (!m_entity) return false;       // 防止重复 Execute
        m_id = m_entity->GetID();
        scene.AddEntity(std::move(m_entity));
        return true;
    }

    void Undo(Scene& scene) override {
        m_entity = scene.RemoveEntity(m_id);  // 取回所有权
    }
private:
    unique_ptr<Object> m_entity;
    Object::ObjectID   m_id = 0;
};
```

注意 `m_id` 单独存储：`Execute` 后 `m_entity` 为空，`Undo` 时需要用 `m_id` 从 Scene 找回实体。

---

## BatchDeleteCommand

```cpp
class BatchDeleteCommand : public ICommand {
    explicit BatchDeleteCommand(vector<ObjectID> ids) : m_ids(std::move(ids)) {}

    bool Execute(Scene& scene) override {
        if (m_ids.empty()) return false;
        m_saved.clear();
        for (auto id : m_ids)
            if (auto e = scene.RemoveEntity(id))
                m_saved.push_back(std::move(e));
        return true;
    }

    void Undo(Scene& scene) override {
        // 逆序还原，恢复插入顺序（unordered_map 不保证顺序，但尽量保持）
        for (int i = (int)m_saved.size() - 1; i >= 0; --i)
            scene.AddEntity(std::move(m_saved[i]));
        m_saved.clear();
    }
private:
    vector<ObjectID>        m_ids;
    vector<unique_ptr<Object>> m_saved;  // Execute 后持有被删除的实体
};
```

对比 `AddEntityCommand`：
- `AddEntityCommand`：创建时持有实体，Execute 转给 Scene，Undo 取回。
- `BatchDeleteCommand`：创建时只持有 ID 列表，Execute 从 Scene 取走实体保存，Undo 还回。

两者的所有权流向相反，但原理相同。

---

## MoveCommand：不转移所有权，只修改数据

移动操作不需要转移实体所有权，只需要修改坐标：

```cpp
class MoveCommand : public ICommand {
    MoveCommand(const vector<ObjectID>& ids, const Vec3& delta, Scene& scene);
    // 构造时用 delta 和 scene 预构建 MoveEntityEntry 列表

    bool Execute(Scene& scene) override;  // 对每个实体施加 delta
    void Undo(Scene& scene) override;     // 对每个实体施加 -delta
};
```

`MoveEntityEntry` 记录每个实体的 ID 和移动向量，Undo 时取反即可。这类命令不需要"持有实体"，因为实体始终在 Scene 里，命令只记录变换参数。

---

## 命令类型归纳

| 命令 | 实体所有权变化 | Undo 策略 |
|---|---|---|
| `AddEntityCommand` | 命令 → Scene | RemoveEntity 取回 |
| `BatchDeleteCommand` | Scene → 命令 | AddEntity 还回 |
| `MoveCommand` | 不变（始终在 Scene） | 施加反向 delta |
| `DragEntitiesCommand` | 不变 | 施加 before/after 快照还原 |
| `CopyCommand` | 新实体命令 → Scene | RemoveEntity 取回 |
| `EditTextCommand` | 不变 | 还原旧文本内容 |

---

## Ctrl+Z / Ctrl+Y 路径

```
用户按 Ctrl+Z
  → Editor::HandleGlobal()
  → Editor::Undo()
  → Document::Undo()
  → CommandStack::Undo(scene)
  → undoStack.top()->Undo(scene)
  → Scene::MarkDirty()
  → 下一帧 Editor 重建顶点
```

`Scene::MarkDirty()` 由各命令的 `Undo`/`Execute` 间接触发：`Scene::AddEntity()` 和 `RemoveEntity()` 内部都会调用 `MarkDirty()`。

---

## 拓展练习

1. 在 `CommandStack::Execute`、`Undo`、`Redo` 中添加 `printf`，画出执行 3 次操作后再 Undo 两次再 Redo 一次的完整栈状态变化图。
2. `AddEntityCommand` 里 `Execute` 开头有 `if (!m_entity) return false`，什么情况下会发生 `m_entity == nullptr`？（提示：`Redo` 时会再次调用 `Execute`）
3. 为"修改线段颜色"设计一个命令：需要保存哪些数据？`Execute` 和 `Undo` 各做什么？
4. `BatchDeleteCommand::Undo` 为什么要逆序还原？如果 Scene 用 `vector` 而不是 `unordered_map` 存储实体，逆序还原有什么意义？
