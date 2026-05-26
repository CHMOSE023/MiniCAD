# C++ 基础课：读懂 MiniCAD 所需的语言知识

本课不是完整的 C++ 教程，而是针对 MiniCAD 代码库中反复出现的语言特性做集中说明。建议在开始第 1 课之前通读一遍，遇到不理解的地方再回来查阅。

---

## 1. 类与继承

C++ 的类通过 `class` 或 `struct` 定义（区别只有默认访问权限）。MiniCAD 大量使用**接口类**：只有纯虚函数、没有数据成员的基类。

```cpp
// 接口：纯虚函数用 = 0 标记
class ICommand
{
public:
    virtual ~ICommand() = default;          // 虚析构，确保派生类正确销毁
    virtual bool Execute(Scene& scene) = 0; // 纯虚：子类必须实现
    virtual void Undo(Scene& scene)    = 0;
};

// 实现：继承接口，用 override 明确覆盖
class AddEntityCommand : public ICommand
{
public:
    bool Execute(Scene& scene) override { /* ... */ return true; }
    void Undo(Scene& scene)    override { /* ... */ }
};
```

**`override` 关键字**：让编译器检查基类是否有匹配的虚函数，防止签名写错。

**虚析构函数**：基类析构必须是 `virtual`，否则通过基类指针 `delete` 派生对象时会内存泄漏。

---

## 2. 智能指针与所有权

MiniCAD 几乎不使用裸 `new`/`delete`，而是用 `std::unique_ptr` 表达**唯一所有权**。

```cpp
// 创建：make_unique 是安全写法
auto entity = std::make_unique<LineEntity>(id, start, end);

// 转移所有权：move 后原 unique_ptr 变为空
auto cmd = std::make_unique<AddEntityCommand>(std::move(entity));
// entity 此时是 nullptr，不能再用

// 向函数传递所有权
commandStack.Execute(std::move(cmd), scene);
// cmd 此时是 nullptr
```

**何时用裸指针（非拥有）**：当只是"借用"而非"持有"时，用裸指针或引用。例如 `Document* m_doc` 表示 Editor 借用文档，但不负责销毁它。

```cpp
Document*  m_doc      = nullptr;  // 非拥有，只是引用
Viewport*  m_viewport = nullptr;  // 同上
```

---

## 3. 移动语义（std::move）

`std::move` 不是真的"移动"，它只是把左值转换成右值引用，告诉编译器"我不再需要这个值，你可以把它的资源拿走"。

```cpp
std::unique_ptr<Entity> a = make_unique<LineEntity>(...);
std::unique_ptr<Entity> b = std::move(a);  // a 变为 nullptr，b 接管资源
```

在 MiniCAD 命令模式中，`Execute` 后 `Scene` 持有实体所有权，`Undo` 后命令重新拿回所有权——所有权在两者之间来回转移，用 `std::move` 实现，不发生拷贝。

---

## 4. 虚函数与多态

多态允许通过基类指针调用派生类实现：

```cpp
ITool* tool = new LineTool();
tool->OnInput(ctx);   // 实际调用 LineTool::OnInput，而不是 ITool::OnInput
```

MiniCAD 的工具系统、实体系统、命令系统、渲染器全部建立在多态上。`Editor` 持有 `unique_ptr<ITool>`，不关心具体是哪个工具；`Scene` 存 `unique_ptr<Object>`，不关心具体是哪个实体。

---

## 5. std::function 与 Lambda

`std::function<返回类型(参数类型...)>` 可以存储任何可调用对象（函数指针、lambda、成员函数绑定）。

```cpp
// 工具工厂：返回新工具实例的函数
std::function<std::unique_ptr<ITool>()> factory;

// 用 lambda 赋值
factory = []() { return std::make_unique<LineTool>(); };

// 调用
auto tool = factory();
```

**Lambda 捕获**：

```cpp
int x = 10;
auto f = [x]()  { return x; };    // 按值捕获，lambda 内部有 x 的副本
auto g = [&x]() { return x; };    // 按引用捕获，修改 x 会影响外部
auto h = [this]() { return m_doc; }; // 捕获 this 指针（成员函数中常用）
```

MiniCAD 中 `OnFinished`、`GlyphProvider`、`FontResolver` 都是 `std::function`，由外部注入具体实现。

---

## 6. enum class（强类型枚举）

```cpp
// 旧式 enum：名字污染全局作用域，可以隐式转换为 int
enum KeyCode { Enter, Escape, A };

// enum class：名字限定在枚举作用域，不可隐式转换
enum class KeyCode { Enter, Escape, A };
KeyCode k = KeyCode::Enter;   // 必须写前缀
```

MiniCAD 的 `KeyCode`、`InputEventType`、`GripType`、`SnapResult::Type` 全部用 `enum class`，避免不同枚举的值相互比较时产生隐患。

---

## 7. 模板基础

模板让一份代码适配多种类型：

```cpp
// 函数模板
template<typename T>
T clamp(T v, T lo, T hi) { return v < lo ? lo : v > hi ? hi : v; }

// 类模板
template<typename T>
struct Point2 { T x, y; };

Point2<double> worldPos;
Point2<float>  screenPos;
```

MiniCAD 的 `Math` 层（`Point2<T>`、`Vec3<T>`、`Color4<T>`）大量使用类模板，让同一套类型既能用 `double`（高精度世界坐标）也能用 `float`（GPU 顶点数据）。

---

## 8. 宏（Macro）与 DECLARE_RUNTIME_TYPE

C++ 预处理宏在编译前展开，可以生成重复模板代码：

```cpp
#define DECLARE_RUNTIME_TYPE(ThisClass, ParentClass)          \
public:                                                       \
    using Super = ParentClass;                                \
    inline static const RuntimeTypeInfo TypeInfo {            \
        #ThisClass, &ParentClass::TypeInfo                    \
    };                                                        \
    virtual const RuntimeTypeInfo* GetTypeInfo() const override { \
        return &TypeInfo;                                     \
    }
```

`#ThisClass` 是"字符串化"操作，把宏参数转成字符串字面量。每个实体类只需写一行：

```cpp
class LineEntity : public Entity
{
    DECLARE_RUNTIME_TYPE(LineEntity, Entity)
    // ...
};
```

展开后自动生成静态类型信息和 `GetTypeInfo()` 虚函数，不需要手动写。

---

## 9. std::unordered_map / unordered_set

哈希表容器，O(1) 平均查找/插入/删除（vs `std::map` 的 O(log n)）。

```cpp
// 工具注册表：工具名 → 工厂函数
std::unordered_map<std::string, std::function<std::unique_ptr<ITool>()>> m_toolRegistry;
m_toolRegistry["Line"] = []() { return std::make_unique<LineTool>(); };

// 选择集：存对象 ID
std::unordered_set<ObjectID> m_selection;
m_selection.insert(id);
bool selected = m_selection.count(id) > 0;
```

MiniCAD 中选择集、夹点处理器注册表、别名注册表都用哈希表，因为需要频繁按 key 查找。

---

## 10. std::span（C++20）

`std::span<T>` 是对连续数组的**非拥有视图**，只存指针和长度，不拷贝数据。

```cpp
std::vector<Vertex_P3_C4> vertices = { ... };

// span 引用 vector 的内容，没有拷贝
std::span<const Vertex_P3_C4> view = vertices;

void Submit(std::span<const Vertex_P3_C4> data)
{
    // 可以遍历 data，但不拥有它
    for (auto& v : data) { ... }
}
```

`ViewState` 中用 `std::span` 把 Editor 的顶点缓冲传给 Viewport，整个渲染路径零拷贝。

---

## 11. 条件编译（预处理器）

```cpp
#ifdef MINICAD_WEB
    // 只在 WebAssembly 构建中编译的代码
    emscripten_set_main_loop(MainLoop, 0, true);
#else
    // 只在 Windows 构建中编译的代码
    while (msg.message != WM_QUIT) { ... }
#endif
```

MiniCAD 用 `MINICAD_WEB` 宏区分平台逻辑，而不是在同一个函数里写 `if (isWeb)`——因为宏是编译期选择，最终二进制中不包含另一平台的代码。

---

## 12. 引用 vs 指针

```cpp
// 引用：必须初始化，不能为 null，不能重新绑定
void Bind(Document& doc, Viewport& viewport);  // 引用参数

// 指针：可以为 nullptr，可以重新指向
Document* m_doc = nullptr;  // 非拥有指针，可以 Unbind 后置 null
```

MiniCAD 的 `EditorContext` 把所有子系统打包为引用成员（`Scene&`、`Viewport&` 等），因为上下文构造时这些对象一定存在；而 `Editor` 的绑定目标用指针，因为需要支持 `Unbind()` 后变为 `nullptr`。

---

## 13. `#pragma once`

替代传统头文件保护（`#ifndef XXXX_H / #define / #endif`），更简洁，被所有主流编译器（MSVC、Clang、GCC）支持。MiniCAD 所有头文件都用 `#pragma once`。

---

## 14. 运算符重载

运算符重载让自定义类型像内置类型一样使用 `+`、`-`、`*`、`==` 等符号。MiniCAD 的 `Core/Math` 层全部基于运算符重载，让向量和矩阵运算写起来和数学公式一致。

### 成员函数形式 vs 自由函数形式

```cpp
// 成员函数：左操作数是 *this
struct Vec3 {
    Vec3 operator+(const Vec3& r) const { return { x+r.x, y+r.y, z+r.z }; }
    Vec3 operator*(double s)      const { return { x*s,   y*s,   z*s   }; }
    Vec3 operator-()              const { return { -x, -y, -z }; }  // 一元负号
};

// 自由函数：用于交换律（scalar * vec）
// 成员函数只能写 v * 2.0，写不了 2.0 * v
inline Vec3 operator*(double s, const Vec3& v) { return { v.x*s, v.y*s, v.z*s }; }
```

**规则**：当左操作数不是本类类型时（如 `double * Vec3`），必须用自由函数形式。

### 赋值运算符（+=、-= 等）

返回 `*this` 引用，支持链式调用：

```cpp
Vec3& operator+=(const Vec3& r) { x += r.x; y += r.y; z += r.z; return *this; }
```

### Point 与 Vec 的运算符设计

MiniCAD 把"位置"（`Point`）和"方向/偏移"（`Vec`）严格分开，运算符只允许几何上有意义的组合：

```cpp
// Point - Point = Vec（两点之差是向量）
inline Vec3   operator-(const Point3& a, const Point3& b);

// Point + Vec = Point（点沿向量偏移仍是点）
inline Point3 operator+(const Point3& p, const Vec3& v);

// Point + Point = ???  → 故意不定义（几何上无意义）
```

这样设计让编译器帮你检查逻辑错误——如果不小心把两个坐标点相加，代码会编译失败。

### 矩阵运算符

```cpp
// Mat4 * Mat4 = 组合变换（矩阵乘法）
Mat4 operator*(const Mat4& rhs) const;

// Mat4 * Vec3 = 变换向量（只旋转/缩放，不平移）
Vec3 operator*(const Vec3& v) const { return TransformVector(v); }

// Mat4 * Point3 = 变换点（旋转+缩放+平移）
Point3 operator*(const Point3& p) const { return TransformPoint(p); }
```

`Vec3` 和 `Point3` 对矩阵乘法的结果不同——向量忽略平移分量（w=0），点包含平移（w=1）。这正是齐次坐标的用途。

### 比较运算符与浮点数容差

浮点数不能用 `==` 精确比较，`Point3` 用 epsilon 容差：

```cpp
inline bool operator==(const Point3& a, const Point3& b)
{
    return std::abs(a.x - b.x) < LengthEPS   // LengthEPS = 1e-8
        && std::abs(a.y - b.y) < LengthEPS
        && std::abs(a.z - b.z) < LengthEPS;
}
inline bool operator!=(const Point3& a, const Point3& b) { return !(a == b); }
```

### `constexpr` 构造函数

```cpp
constexpr Point3(double xx, double yy, double zz) : x(xx), y(yy), z(zz) {}
```

`constexpr` 允许在编译期求值，例如 `IDrawSink` 中的颜色常量：

```cpp
static constexpr Color4 kHoverColor = { 0, 0.5, 0.8, 0.9 };  // 编译期常量
```

### `static constexpr` 工厂函数

```cpp
static constexpr Color4 White() { return { 1.0, 1.0, 1.0, 1.0 }; }
static Mat4 Identity() { ... }
static Mat4 OrthoLH(double w, double h, double nearZ, double farZ) { ... }
```

静态工厂函数比构造函数更有表达力，让 `Mat4::Identity()` 比 `Mat4(1,0,0,0,0,1,...)` 清晰得多。

---

## 15. 常用数据结构

### std::vector — 动态数组

最常用的容器，元素连续存储，随机访问 O(1)，尾部增删均摊 O(1)。

```cpp
std::vector<Vertex_P3_C4> m_sceneVertices;  // Editor 顶点缓冲
m_sceneVertices.push_back(v);               // 追加
m_sceneVertices.clear();                    // 清空（不释放内存）
m_sceneVertices.reserve(1024);             // 预分配，避免反复扩容
for (auto& v : m_sceneVertices) { ... }    // 范围 for 遍历
```

MiniCAD 中顶点缓冲、命令历史行、文档列表都是 `vector`。`reserve()` 在已知大致大小时很重要，可以避免每次 push_back 触发扩容拷贝。

### std::unordered_map — 哈希表（键值对）

平均 O(1) 查找/插入，键无序。

```cpp
// 工具注册表
std::unordered_map<std::string, std::function<std::unique_ptr<ITool>()>> m_toolRegistry;
m_toolRegistry["Line"]   = []() { return std::make_unique<LineTool>(); };
m_toolRegistry["Circle"] = []() { return std::make_unique<CircleTool>(); };

// 查找
auto it = m_toolRegistry.find("Line");
if (it != m_toolRegistry.end())
    auto tool = it->second();   // 调用工厂函数
```

### std::unordered_set — 哈希集合

平均 O(1) 插入/删除/查找，存储不重复的值。

```cpp
std::unordered_set<ObjectID> m_selection;  // 选择集

m_selection.insert(id);                    // 选中
m_selection.erase(id);                     // 取消选中
bool selected = m_selection.count(id) > 0; // 判断是否选中（count 返回 0 或 1）
m_selection.clear();                       // 全部取消选中
```

### std::stack vs std::vector（命令栈）

`std::stack` 是适配器，底层默认用 `deque`，只暴露 `push`/`pop`/`top`。CommandStack 用两个 `vector` 实现 undo/redo 而非 `std::stack`，因为需要限制深度或随机访问历史。

```cpp
// 典型 undo 栈用法
std::vector<std::unique_ptr<ICommand>> m_undoStack;
std::vector<std::unique_ptr<ICommand>> m_redoStack;

// 执行新命令
m_undoStack.push_back(std::move(cmd));
m_redoStack.clear();   // 新操作清空 redo 历史

// 撤销
auto cmd = std::move(m_undoStack.back());
m_undoStack.pop_back();
cmd->Undo(scene);
m_redoStack.push_back(std::move(cmd));
```

### std::string — 字符串

```cpp
std::string name = "Untitled";
name += " (modified)";              // 追加
name.empty();                       // 是否为空
name.size();                        // 字节长度（注意 UTF-8 中文字符占 3 字节）
name.find("mod");                   // 查找子串，返回 string::npos 表示未找到
```

MiniCAD 内部全部使用 UTF-8 编码的 `std::string`（包括中文内容）。

### std::atomic — 原子操作

用于多线程或需要无锁自增的场景。`Scene` 用 `std::atomic<ObjectID>` 分配唯一 ID：

```cpp
std::atomic<ObjectID> m_nextObjectID{ 1 };  // 0 保留为 InvalidID

ObjectID NextObjectID()
{
    // fetch_add 是原子自增，返回自增前的值
    return m_nextObjectID.fetch_add(1, std::memory_order_relaxed);
}
```

`memory_order_relaxed` 表示只保证原子性，不保证其他内存操作的顺序——对 ID 分配这种场景已足够。

### std::span — 非拥有视图（C++20）

见第 10 节。核心用途：把 `vector` 的数据以零拷贝方式传递给 `Viewport`。

```cpp
// Editor 持有 vector（拥有内存）
std::vector<Vertex_P3_C4> m_sceneVertices;

// ViewState 持有 span（只是视图，不拷贝）
ViewState vs;
vs.Scene = m_sceneVertices;  // 隐式转换为 span
```

### 容器选择速查

| 场景 | 容器 | 理由 |
|---|---|---|
| 顶点缓冲、历史列表 | `vector` | 连续内存，随机访问，cache 友好 |
| 工具/夹点注册表 | `unordered_map` | 按名/类型快速查找 |
| 选择集 | `unordered_set` | 快速判断是否包含，无重复 |
| 命令栈 | `vector`（手动管理） | 需要 pop_back + push_back，可限深度 |
| 命令行历史 | `vector`（滑动窗口） | 超过 500 行时 erase(begin()) |
| 零拷贝传参 | `span` | 跨模块传递大数组不拷贝 |

---

## 常见坑与建议

| 问题 | 原因 | 解决 |
|---|---|---|
| `unique_ptr` 不能拷贝 | 所有权唯一，拷贝会导致双重释放 | 用 `std::move` 转移，或用 `get()` 借用裸指针 |
| `std::move` 后继续使用原变量 | 变量处于"已移走"状态，值未定义 | move 后不再使用原变量 |
| 基类析构不是 virtual | `delete base_ptr` 不会调用派生类析构 | 有多态使用的基类必须加 `virtual ~Base()` |
| Lambda 捕获悬空引用 | lambda 比被捕获对象活得更长 | 确认 lambda 生命周期，或改用按值捕获 |
| `#include` 顺序依赖 | 头文件缺少自包含 | 每个头文件独立包含它需要的头文件 |
