# Lesson06：Core 核心层：数学、几何、对象与实体

## 学习目标

读懂 `Core` 层的四个子模块（Math / GeomKernel / Object / Entity）的设计意图和关键接口，理解 `Object` 根类的 ID 体系和手写 RTTI，以及 `Entity` 的三个纯虚函数如何驱动整个系统的拾取、渲染和复制。

---

## 相关源码

- `src/Core/Object/Object.hpp` / `RuntimeType.hpp`
- `src/Core/Entity/Entity.hpp` / `EntityAttr.hpp` / `LineEntity.hpp`
- `src/Core/GeomKernel/Line.hpp`（代表 GeomKernel 全家）
- `src/Core/Draw/IDrawSink.hpp`
- `src/Core/Math/Point3.hpp` / `Vec3.hpp` / `Mat4.hpp`（见 Lesson00_Cpp.md 第 14 节）

---

## 为什么用 `.hpp` 扩展名

`Core` 层所有文件用 `.hpp`，而非 `.h`/`.cpp`。约定含义：**这些文件是模板或内联实现，整个定义在一个文件里，不需要单独的翻译单元**。上层模块包含 `.hpp` 即可，不需要在 CMakeLists.txt 里列举 `.cpp`。

---

## 子模块一览

```
Core/
├── Math/         纯数值类型：Point2/3, Vec2/3, Mat4, Color4（见 Lesson00_Cpp.md）
├── GeomKernel/   几何算法：Line, Circle, Arc, AABB, Polyline, Spline …
├── Object/       根对象：ObjectID 体系 + 手写 RTTI
├── Entity/       CAD 实体：Entity 基类 + 11 个具体实体
└── Draw/         绘制抽象：IDrawSink 接口
```

---

## Object：根对象与 ID 体系

```cpp
class Object {
public:
    using ObjectID = uint64_t;
    static constexpr ObjectID InvalidID = 0;  // 0 保留为无效

    ObjectID GetID() const { return m_id; }

    virtual const RuntimeTypeInfo* GetTypeInfo() const = 0;

    template<typename T>
    bool IsKindOf() const noexcept {
        return GetTypeInfo()->IsKindOf(&T::TypeInfo);
    }

    inline static const RuntimeTypeInfo TypeInfo{ "Object", nullptr };
protected:
    explicit Object(ObjectID id) : m_id(id) {}
private:
    ObjectID m_id;
};
```

**ObjectID 分配**：在 `Scene` 中用 `std::atomic<ObjectID>` 维护，`Scene::NextObjectID()` 每次 `fetch_add(1)` 无锁递增。ID 从 1 开始，0 永远是 `InvalidID`。

**为什么 `Object` 不暴露 `SetID`（给外部调用）**：ID 一旦进入 `Scene` 就不应再变。`Clone()` 创建新对象时需要传入新 ID，由调用者负责分配。

---

## 手写 RTTI：RuntimeTypeInfo + DECLARE_RUNTIME_TYPE

C++ 标准 `dynamic_cast` 和 `typeid` 依赖 RTTI 编译选项，在某些嵌入式或 WASM 环境下可能被关闭。MiniCAD 用宏实现了等价功能：

```cpp
struct RuntimeTypeInfo {
    const char*          Name;
    const RuntimeTypeInfo* Parent;

    bool IsKindOf(const RuntimeTypeInfo* other) const {
        const RuntimeTypeInfo* cur = this;
        while (cur) {
            if (cur == other) return true;
            cur = cur->Parent;         // 沿继承链向上走
        }
        return false;
    }
};
```

```cpp
#define DECLARE_RUNTIME_TYPE(ThisClass, ParentClass)                        \
public:                                                                     \
    using Super = ParentClass;                                              \
    inline static const RuntimeTypeInfo TypeInfo { #ThisClass,             \
                                                   &ParentClass::TypeInfo };\
    virtual const RuntimeTypeInfo* GetTypeInfo() const override {           \
        return &TypeInfo;                                                   \
    }
```

宏展开后在类中生成两样东西：
1. `static const RuntimeTypeInfo TypeInfo`：类型信息静态对象（含类名字符串和父类型指针）
2. 虚函数 `GetTypeInfo()`：运行时返回本类的类型信息

**使用示例**：

```cpp
Object* obj = scene.GetEntity(id);

// 判断类型
if (obj->IsKindOf<LineEntity>())          // 走 TypeInfo 链，相当于 dynamic_cast 检查
    auto* line = static_cast<LineEntity*>(obj);  // 已确认类型，可直接 static_cast

// GripEditor 按类型查找 Handler
m_handlers[&LineEntity::TypeInfo];        // RuntimeTypeInfo* 作为 map 键
```

继承链举例：`LineEntity::TypeInfo.Parent` → `Entity::TypeInfo` → `Object::TypeInfo` → `nullptr`。

---

## EntityAttr：CAD 属性

```cpp
struct EntityAttr {
    Math::Color4 Color    = Color4::White();
    LayerID      LayerId  = 0;
    LineType     LineType = LineType::SOLID;
    double       LineWidth = 1.0;
    bool         Visible  = true;
};
```

`Entity` 基类持有一个 `EntityAttr m_attr`，任何具体实体都继承了这 5 个 CAD 属性。`Draw()` 实现里用 `GetAttr().Color` 取颜色，但选中/悬停时覆盖为特殊颜色：

```cpp
// LineEntity::Draw 中
const Color4& color = isSelected ? IDrawSink::kSelectionColor
                    : isHovered  ? IDrawSink::kHoverColor
                    : attr.Color;
```

---

## Entity：三个纯虚函数

```cpp
class Entity : public Object {
    virtual AABB      GetBoundingBox() const = 0;
    virtual void      Draw(IDrawSink&, bool isSelected, bool isHovered) const = 0;
    virtual unique_ptr<Entity> Clone(ObjectID newId) const = 0;

    DECLARE_RUNTIME_TYPE(Entity, Object)
protected:
    explicit Entity(ObjectID id) : Object(id) {}
private:
    EntityAttr m_attr;
};
```

| 方法 | 服务的系统 | 说明 |
|---|---|---|
| `GetBoundingBox()` | `Picking` 的 HitTest / BoxSelect | 返回轴对齐包围盒（AABB），用于快速相交测试 |
| `Draw(IDrawSink&, ...)` | `Editor::UpdateSceneVertices()` | 向 sink 发射几何（线段、纹理四边形），不接触 GPU |
| `Clone(newId)` | `CopyCommand`、`MirrorCopyCommand` 等 | 深拷贝实体，赋予新 ID，保留属性 |

---

## LineEntity 全解

```cpp
class LineEntity : public Entity {
    LineEntity(ObjectID id, const Point3& start, const Point3& end)
        : Entity(id), m_line(start, end) {}

    AABB GetBoundingBox() const override { return m_line.GetBounds(); }

    void Draw(IDrawSink& sink, bool isSelected, bool isHovered) const override {
        const Color4& color = isSelected ? kSelectionColor
                            : isHovered  ? kHoverColor
                            : GetAttr().Color;
        sink.DrawLine(m_line.Start, m_line.End, color, false);
        // false = 不是 overlay（放入正式场景顶点缓冲）
    }

    unique_ptr<Entity> Clone(ObjectID newId) const override {
        auto e = make_unique<LineEntity>(newId, m_line.Start, m_line.End);
        e->SetAttr(GetAttr());  // 复制属性（颜色、图层等）
        return e;
    }

    DECLARE_RUNTIME_TYPE(LineEntity, Entity)
private:
    Line m_line;
};
```

`LineEntity` 只持有一个 `Line m_line`，几何计算完全委托给 `GeomKernel::Line`。

---

## GeomKernel：几何算法集

以 `Line` 为例，`GeomKernel` 的职责是提供算法，不带任何 CAD 属性：

```cpp
struct Line {
    Point3 Start, End;

    Vec3   Vector()      const;  // End - Start
    Vec3   Direction()   const;  // 单位方向向量
    double Length()      const;
    Point3 Midpoint()    const;
    Point3 PointAt(double t) const;       // P(t) = Start + t*(End-Start)
    double ProjectParam(const Point3& p); // p 在直线上的参数 t
    Point3 ClosestPoint(const Point3& p); // p 到直线的最近点
    double DistanceToPoint(const Point3& p);
    AABB   GetBounds()   const;
    bool   IsValid()     const;  // 长度 > LengthEPS
};
```

**`DistanceToPoint` 是拾取的核心**：`Picking::HitTest` 把鼠标世界坐标传给 `line.DistanceToPoint()`，如果距离小于阈值（换算到屏幕空间）则命中。

**`ProjectParam` 的实际用途**：

```cpp
double t = line.ProjectParam(p);
// t < 0：p 在 Start 延长线上
// 0 ≤ t ≤ 1：p 在线段上
// t > 1：p 在 End 延长线上
```

吸附"最近点"时用此判断是否在线段范围内。

---

## IDrawSink：绘制接口

```cpp
class IDrawSink {
    static const Color4 kSelectionColor;  // 蓝色
    static const Color4 kHoverColor;      // 青色

    virtual void DrawLine(const Point3& a, const Point3& b,
                          const Color4& color, bool isOverlay) = 0;
    virtual void EmitText(const Point3& pos, const string& utf8, float height,
                          float rotation, const Color4& color) = 0;
    virtual void EmitMText(/* 多行文字参数 */) = 0;
};
```

`DrawContext` 是它的唯一实现（见 Lesson02）。实体通过 `IDrawSink` 发射几何，与底层渲染 API 完全解耦。

---

## 继承层次总结

```
Object          ← ObjectID + RTTI 根
  └── Entity    ← EntityAttr + 三纯虚函数
        ├── LineEntity        ← Line
        ├── CircleEntity      ← Circle
        ├── ArcEntity         ← Arc
        ├── EllipseEntity     ← Ellipse
        ├── RectangleEntity   ← Rectangle
        ├── PolylineEntity    ← Polyline
        ├── SplineEntity      ← Spline
        ├── PointEntity       ← Point3
        ├── TextEntity        ← 单行文字
        └── MTextEntity       ← 多行文字（带 StyleId）
```

---

## 拓展练习

1. `DECLARE_RUNTIME_TYPE` 宏展开后 `TypeInfo` 的 `Parent` 指针指向哪里？如果父类也有 `TypeInfo`，两个静态对象各自存在哪里？
2. `Object` 的构造函数是 `protected`，意味着什么？能直接 `new Object()` 吗？
3. 找到 `CopyCommand.h`，确认它在哪里调用了 `Clone()`，新 ID 如何获取。
4. 为一个新实体"十字标记（CrossEntity）"设计 `GetBoundingBox()`、`Draw()` 和 `Clone()`：几何是两条垂直线段，中心点由构造函数给定。
