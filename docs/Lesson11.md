# Lesson11：文字与字体系统

## 学习目标

读懂 MiniCAD 文字系统的两条渲染路径（纹理路径 vs 矢量路径），理解 `FontSystem` 的状态机和样式注册机制，掌握 `TextInputRequest` 流程，以及 Windows 和 Web 两端的文字输入差异。

---

## 相关源码

- `src/Text/Font/IFont.h`
- `src/Text/Font/SHXFont.h` / `src/Text/Parser/SHXParser.h` / `src/Text/Parser/SHXVM.h`
- `src/Text/Font/TTFFont.h`
- `src/Text/Font/SHXCompositeFont.h`
- `src/Text/Font/FontEngine.h`
- `src/Text/Glyph/Glyph.h` / `GlyphCache.h` / `GlyphKey.h`
- `src/Text/Layout/TextLayoutEngine.h`
- `src/Text/FontSystem.h`
- `src/Core/Entity/TextEntity.hpp` / `MTextEntity.hpp`
- `src/Document/DrawContext.hpp`（`EmitText` 和 `EmitMText` 的实现）
- `src/Editor/Editor.h`（`TextInputRequest` / `MTextInputRequest`）
- `src/App/Web/WebMain.cpp`（`MiniCAD_SubmitText`、`MiniCAD_SubmitMText`）

---

## 文字实体的两种类型

| 类型          | 字体路径               | 布局                       | 用途       |
| ------------- | ---------------------- | -------------------------- | ---------- |
| `TextEntity`  | 纹理（ImGui 字体图集） | 单行，无换行               | 简单标注   |
| `MTextEntity` | 矢量（SHX / TTF）      | 多行，支持文本框宽度和换行 | 工程图注释 |

两者都是 CAD 对象：有插入点、高度、旋转角、图层属性，参与选择、移动、旋转、镜像和撤销重做。

---

## 两条渲染路径

### 路径一：EmitText（纹理，TextEntity）

```cpp
void DrawContext::EmitText(const Point3& pos, const string& utf8,
                           float height, float rotation, const Color4& color) {
    // 遍历 UTF-8 码点
    while (p < end) {
        unsigned int cp;
        p = DecodeUtf8(p, end, cp);

        GlyphInfo g;
        float fallback;
        if (!m_glyphProvider(cp, g, fallback)) { curX += fallback; continue; }
        //   ↑ GlyphProvider 回调：查询字形在 ImGui 字体图集中的 UV 坐标

        // 生成两个三角形（一个矩形），带旋转变换
        // 每个字符 6 个顶点 → 推入 m_textVerts
        curX += g.AdvanceX;
    }
}
```

`GlyphProvider` 是注入到 `DrawContext` 的回调，由 `UIManager` 在每帧提供：

```cpp
// UIManager::SyncFonts 中
auto provider = [](uint32_t cp, GlyphInfo& out, float& fallback) -> bool {
    // 从 ImGui::GetFont() 查询字形 UV
    // 填充 GlyphInfo
};
```

字形 UV 坐标指向 ImGui 字体图集（一张 PNG 纹理），渲染时由 `SubmitTextured` 采样。

**字形坐标系**：`GlyphInfo` 里的坐标归一化到"字高=1"的本地空间，`EmitText` 乘以 `height` 和旋转矩阵变换到世界坐标。

### 路径二：EmitMText（矢量，MTextEntity）

```cpp
void DrawContext::EmitMText(const Point3& pos, const string& utf8,
                            uint32_t styleId, double height, ...) {
    IFont* font = m_fontResolver(styleId);  // FontResolver 回调
    if (!font) return;

    TextLayoutEngine layout;
    auto result = layout.Layout(utf8, font, height, 1.0, rotation, boxWidth);
    // ↑ 排版引擎输出字形实例列表（每个字符的位置、缩放）

    for (const auto& inst : result.m_glyphs) {
        for (const auto& seg : inst.m_glyph.Lines) {
            // 字形线段 → 变换到世界坐标 → 推入 m_verts（普通线段缓冲）
        }
    }
}
```

矢量字形是**线段集合**，直接进入 `m_sceneVertices`，走普通 `Submit(PrimitiveType::Line)` 渲染，不需要纹理。

---

## IFont：统一字体接口

```cpp
class IFont {
    virtual Glyph       GetGlyph  (uint32_t codepoint) = 0;  // 获取字形几何
    virtual double      GetAdvance(uint32_t codepoint) = 0;  // 字符前进宽度（排版用）
    virtual double      GetHeight () const             = 0;  // 字体设计高度（基准单位）
    virtual uint64_t    GetFontId () const             = 0;  // 全局唯一 ID（GlyphCache 键）
    virtual const char* GetName   () const             = 0;
    virtual bool        HasGlyph  (uint32_t codepoint) = 0;
};
```

所有字体类型（SHXFont、TTFFont、SHXCompositeFont）都实现此接口，`EmitMText` 只调用 `IFont`，不关心底层是 SHX 还是 TTF。

### Glyph：统一输出结构

```cpp
struct Glyph {
    vector<Line>     Lines;      // 字形轮廓线段（SHX/TTF 均输出到此）
    vector<Triangle> Triangles;  // 填充三角形（TTFFont 的填充模式，SHX 不使用）
    bool   Filled   = false;
    double Advance  = 0.0;       // 水平前进量（字体单元空间）
    double MinX, MinY, MaxX, MaxY;  // 包围盒（排版/裁剪用）
};
```

SHX 和 TTF 最终都输出 `Glyph`，`EmitMText` 遍历 `Glyph.Lines` 生成世界坐标线段。

---

## SHX 字体

SHX（Shape eXtended）是 AutoCAD 定义的二进制矢量字体格式，用字节码描述字形的笔画路径。MiniCAD 用 `SHXParser` 解析文件，用 `SHXVM` 执行字节码。

### SHX 的三种子类型

`SHXParser` 解析时先识别文件头：

| 类型      | `Kind`           | 典型文件                     | 说明                              |
| --------- | ---------------- | ---------------------------- | --------------------------------- |
| `Shapes`  | 西文形状字体     | `simplex.shx`、`tssdeng.shx` | ASCII 字符，每个字符一个 shape    |
| `BigFont` | 大字体（CJK）    | `TSSDCHN.SHX`、`gbcbig.shx`  | 双字节编码（GBK），覆盖中日韩字符 |
| `Unifont` | Unicode 统一字体 | `unifont.shx`                | 每个字符用 2 字节 Unicode 编号    |

西文字体（Shapes）用 1 字节编码（0x00–0xFF），大字体（BigFont）用 GBK 双字节编码（cp ≥ 0x80 的字符走大字体查询）。

### SHXParser：加载与字形表

```
SHXFont::LoadFont()
  → SHXParser::Load(filePath)
     → 读取二进制文件到 m_fileData
     → ClassifyHeader：读第一行识别 Shapes / BigFont / Unifont
     → ParseShapesContent / ParseBigFontContent / ParseUnifontContent
         → 建立 m_shapes 表：shape code → RawShape{data*, len}
           （RawShape 只存指针和长度，不解析字节码）
     → ExtractFontMeta：从 shape code 0 读字体名和基准高度
```

`m_shapes` 是一个哈希表，key 是 shape code，value 是字节码在文件缓冲区中的位置和长度。**字节码不预先解析**，只在 `BuildGlyph()` 第一次被调用时才执行。

### SHXVM：字节码虚拟机

`SHXParser::BuildGlyph()` 把对应 shape 的字节码交给 `SHXVM::Execute()` 执行：

```
SHXVM 执行上下文（SHXContext）：
  double x, y    — 当前笔的世界坐标（SHX 单元空间）
  bool pen       — 笔落下（true）= 移动时产生线段；抬起（false）= 只移动
  double scale   — 当前缩放因子（0x03/0x04 指令修改）
  vector<Pos> posStack  — 位置栈（push/pop 用于子形状调用）
  vector<Line> lines    — 输出线段列表
```

**关键字节码指令**：

| 字节           | 含义                                    |
| -------------- | --------------------------------------- |
| `0x00`         | 形状结束                                |
| `0x01`         | 抬笔（pen up）                          |
| `0x02`         | 落笔（pen down）                        |
| `0x03`         | 缩小：scale /= 2                        |
| `0x04`         | 放大：scale \*= 2                       |
| `0x05`         | 压栈当前位置                            |
| `0x06`         | 弹栈恢复位置                            |
| `0x07`         | 调用子形状（subshape）                  |
| `0x08`         | X,Y 偏移移动（2 字节有符号增量）        |
| `0x09`         | 多段折线移动（直到 0,0 结束）           |
| `0x00A`–`0x0F` | 8 方向等距移动（每步距离由 scale 决定） |
| `0x00C`        | 弧线（圆心偏移 + 起止角度）             |
| `0x00E`        | 斜线弧（bulge arc，用于字形圆角）       |

**执行过程**：VM 顺序读取字节，遇到移动指令就更新 `(x, y)`，`pen == true` 时生成 `Line{旧位置, 新位置}`；遇到弧线指令调用 `EmitOctantArc` / `EmitBulgeArc` 将弧细分为若干线段；遇到 `0x07` 递归调用子形状（有深度限制，防止无限递归）。

### SHX 字符编码路由

`SHXFont::ResolveShxKey(codepoint)` 把 Unicode 码点映射到 SHX shape code：

- 英文字体（Shapes）：ASCII 范围内直接用码点作 shape code
- 大字体（BigFont）：`codepoint → GBK 编码 → shape code`（通过 `EncodingGBK` 查表）
- Unifont：直接用 Unicode 码点

这就是为什么 `SHXCompositeFont` 按 `cp < 0x80` / `cp >= 0x80` 路由：ASCII 字符只有英文字体有，非 ASCII 字符（中文）只有大字体有。

---

## TTF 字体

TTF（TrueType Font）用贝塞尔曲线描述字形轮廓。MiniCAD 用 `stb_truetype`（单头文件库）解析 TTF，将曲线展平为折线段输出到 `Glyph.Lines`。

```cpp
// TTFFont 注释
// Web 端（MINICAD_WEB）不使用本类，全部走 WebFontAtlas 纹理路径。
class TTFFont : public IFont {
    void*  m_stbFont = nullptr;  // stbtt_fontinfo*，用 void* 隐藏，避免暴露大型头文件
    double m_scale   = 1.0;      // stbtt 坐标 → 归一化（字高=1）的比例因子
    vector<uint8_t> m_fileData;  // TTF 文件字节（必须保持生命周期，stbtt 持有指针）
};
```

### 贝塞尔曲线展平

TTF 字形轮廓由二次贝塞尔曲线（glyf 表）和三次贝塞尔曲线（CFF 表）组成，`TTFFont` 用自适应细分将其展平为线段：

```cpp
// 二次贝塞尔：P0→控制点C→P1
void FlattenQuad(double x0, double y0,
                 double cx, double cy,
                 double x1, double y1,
                 double tolerance,
                 vector<Line>& out) const;
// 三次贝塞尔：P0→C1→C2→P1
void FlattenCubic(double x0, double y0,
                  double c1x, double c1y,
                  double c2x, double c2y,
                  double x1,  double y1,
                  double tolerance,
                  vector<Line>& out) const;
```

**自适应细分原则**：递归二分，当控制点与连线的偏差小于 `tolerance` 时停止，输出一条线段。`tolerance` 越小曲线越平滑，线段越多。

### 坐标归一化

`stb_truetype` 输出的坐标单位是字体设计单位（通常 2048 或 1000），`TTFFont` 用 `m_scale` 换算为"字高=1"的归一化坐标，与 `SHXFont` 输出坐标系一致，`TextLayoutEngine` 不需要区分来源。

### TTFFont 的填充模式

```cpp
void BuildFill(Glyph& g);
void ScanlineFill(const vector<Line>& lines, vector<Triangle>& out);
```

对于需要填充的字形（如粗体、实心汉字），`ScanlineFill` 用扫描线算法把轮廓线段转换为三角形列表，输出到 `Glyph.Triangles`。渲染时 `PrimitiveType::Triangle` 填充。

### Web 端不用 TTFFont

```cpp
// TTFFont.h 注释
// Web 端（MINICAD_WEB）不使用本类，全部走 WebFontAtlas 纹理路径。
```

Web 端所有文字（包括原本走矢量路径的 MTextEntity）统一通过浏览器字体渲染到 Canvas/WebGL 纹理图集，不使用 stb_truetype。

---

## SHXCompositeFont：主字体 + 大字体组合

```cpp
class SHXCompositeFont : public IFont {
    shared_ptr<IFont> m_mainFont;  // 西文（tssdeng.shx），处理 cp < 0x80
    shared_ptr<IFont> m_bigFont;   // 中文（TSSDCHN.SHX），处理 cp >= 0x80
    unordered_map<uint32_t, IFont*> m_routeCache;  // 路由结果缓存
};
```

**`PickFontFor(cp)` 路由逻辑**：

```
cp < 0x80  → 先查 mainFont::HasGlyph → 有：用 mainFont
                                      → 无：fallback 到 bigFont
cp >= 0x80 → 先查 bigFont::HasGlyph  → 有：用 bigFont
                                      → 无：fallback 到 mainFont
两个都没有 → 返回 nullptr（字符跳过，不渲染）
```

**`NormFactor(font)`**：两个字体的设计高度可能不同（SHX Shapes 通常高度=1，BigFont 可能不同），`NormFactor` 计算比例，使两种字体的字形在同一排版行内等高。

路由结果缓存到 `m_routeCache`，每个码点只路由一次，后续直接查缓存。

---

## FontEngine：懒加载与缓存

```cpp
class FontEngine {
    IFont& Resolve(const FontStyle& style);  // 按样式懒加载，缓存实例
    void   SetFontDir(string dir);           // 设置字体文件搜索目录，默认 "fonts/"
private:
    unordered_map<string, shared_ptr<IFont>> m_cache;  // key = 文件名+参数
    uint64_t m_nextRuntimeFontId = 1;
};
```

`Resolve()` 流程：

```
1. BuildKey(style) → 生成缓存键（文件名 + widthFactor + oblique）
2. 查 m_cache：命中 → 直接返回
3. 未命中 → ResolvePath(fontFile) 拼接完整路径
4. style.isShx ? new SHXFont(...) : new TTFFont(...)
5. 分配 m_nextRuntimeFontId++ 作为 fontId
6. 存入 m_cache，返回
```

**延迟加载**：字体文件在第一次被某个 `MTextEntity` 渲染时才读取，不在启动时全部加载。

---

## GlyphCache：字形缓存

```cpp
class GlyphCache {
    const Glyph& Get(const GlyphKey& key, function<Glyph()> loader);
    // key = { FontId, Codepoint }，首次调用 loader() 构建并缓存
};

struct GlyphKey {
    uint64_t FontId;     // IFont::GetFontId()
    uint32_t Codepoint;  // Unicode 码点（或 SHX shape code）
};
```

`GlyphCache` 用于避免每帧重复执行 SHXVM 或 TTF 贝塞尔展平。首次渲染某字符时缓存字形，后续直接返回。**内置 mutex**，线程安全。

SHXFont 和 TTFFont 各自也有 `m_glyphCache`（`unordered_map<uint32_t, Glyph>`）作为第一级缓存，`GlyphCache` 是可选的全局第二级缓存。

---

## TextLayoutEngine：排版

```cpp
class TextLayoutEngine {
    LayoutResult Layout(const string& text, IFont* font,
                        double height, double widthFactor,
                        double rotation, double boxWidth,
                        HAlign align = HAlign::Left);

    // 输出
    struct GlyphInstance {
        Glyph        m_glyph;
        Point3       m_position;  // 世界坐标（由 EmitMText 进一步旋转）
        double       m_scale;     // 字高比例
    };
};
```

**排版流程**：

```
1. DecodeLine(text)：解析控制码（%%d=°、%%c=Ø、%%p=±、%%nnn=shape 编号）
                     → 码点序列
2. BreakLines：按 boxWidth 换行
   对每个码点：
     font->GetAdvance(cp) × widthFactor → 累加行宽
     超过 boxWidth → 换行
3. 对每行按 HAlign 对齐计算起始 X
4. 对每个码点：
     font->GetGlyph(cp) → Glyph（含 Lines）
     记录 GlyphInstance{glyph, 当前笔位置, height/font->GetHeight()}
     curX += glyph.Advance × widthFactor × scale
5. 返回 LayoutResult（GlyphInstance 列表）
```

`EmitMText` 收到 `LayoutResult` 后，再对每个 `GlyphInstance.m_glyph.Lines` 中的线段施加旋转变换（rotation 角度），最终推入 `m_verts`。

---

## FontSystem：字体系统状态机

```cpp
class FontSystem {
    enum class State { Uninitialized, Initializing, Ready, Shutdown };

    void Initialize();         // 创建 FontEngine，注册 Standard 样式
    void Shutdown();
    bool IsReady() const;

    FontStyleId RegisterStyle(FontStyle style);       // 注册新样式，返回 ID
    const FontStyle* FindStyle(const string& name);
    IFont& ResolveFont(FontStyleId id);               // 懒加载，首次调用时加载字体文件
    void PreloadDefaultFonts();                       // 预加载探索者 SHX 字体
};
```

**状态约束**：任何方法调用前必须 `Initialize()`，否则抛 `runtime_error("FontSystem not initialized")`。

### 内置样式

```cpp
// InitDefaultStyles() 中注册
FontStyle standard;
standard.id       = kStandardStyleId;  // = 1
standard.name     = "Standard";
standard.fontFile = "simplex.shx";     // 内置西文 SHX 字体
standard.isShx    = true;
```

`kStandardStyleId = 1` 是保留的内置样式，所有 `MTextEntity` 默认使用此样式。

### 探索者字体（PreloadDefaultFonts）

```cpp
void PreloadDefaultFonts() {
    auto mainFont = make_shared<SHXFont>("tssdeng", "fonts/tssdeng.shx", 100);  // 英文
    auto bigFont  = make_shared<SHXFont>("tssdchn", "fonts/TSSDCHN.SHX",  101); // 中文
    m_shxCompositeFont = make_unique<SHXCompositeFont>(
        "tssdeng+tssdchn", mainFont, bigFont, 200);  // 英中合体
}
```

`SHXCompositeFont` 是组合字体：查字形时先查英文字体，找不到再查中文字体。这是 CAD 工程字体（探索者）的标准做法。

字体文件路径：

- Windows：相对于 `.exe` 所在目录的 `fonts/` 文件夹
- Web：由 `--preload-file=assets/fonts@/fonts` 打包进 WASM 虚拟文件系统的 `/fonts/` 路径

---

## FontStyle 与跨文档共享

`FontStyle` 结构：

```cpp
struct FontStyle {
    using FontStyleId = uint32_t;
    FontStyleId  id       = 0;
    string       name;
    string       fontFile;
    bool         isShx    = false;
};
```

`FontSystem` 由 `DocumentManager` 持有，所有文档共用一套字体样式。每个 `MTextEntity` 存储 `styleId`（整数），运行时通过 `FontResolver` 回调查找实际的 `IFont*`：

```cpp
// DrawContext 中的 FontResolver 回调
auto resolver = [&docManager](uint32_t styleId) -> IFont* {
    const FontStyle* style = docManager.FindFontStyle(styleId);
    if (!style) return nullptr;
    return &docManager.GetFontSystem().ResolveFont(*style);
};
```

---

## TextInputRequest 流程

文字工具分两步：先在视口确定插入点，再弹出输入框输入内容。

### 步骤一：TextTool 设置请求

```cpp
// TextTool::OnInput 中，用户左键点击后：
auto& req     = m_ctx->editor.GetTextInputRequest();
req.Active    = true;
req.InsertPos = clickedWorldPos;
req.Height    = fontSystem.GetDefaultTextHeight();
```

### 步骤二：UI 层弹出输入框

**Windows**（UIManager 每帧检查）：

```cpp
if (editor.GetTextInputRequest().Active) {
    DrawTextInputPopup(editor);  // ImGui 弹窗，用户输入后调用 SubmitTextInput
}
```

**Web**（JS 层检查，HTML input 元素）：

```cpp
// WebMain.cpp 导出函数，由 JS 调用
EMSCRIPTEN_KEEPALIVE void MiniCAD_SubmitText(const char* text, float height) {
    g_editor.GetTextInputRequest().Height = height;
    g_editor.SubmitTextInput(string(text));
}
EMSCRIPTEN_KEEPALIVE void MiniCAD_CancelText() {
    g_editor.SubmitTextInput("");  // 空字符串 = 取消
}
```

### 步骤三：SubmitTextInput 创建实体

```cpp
void Editor::SubmitTextInput(const string& utf8Text) {
    auto& req = m_textRequest;
    if (!req.Active) return;
    req.Active = false;

    if (utf8Text.empty()) return;  // 取消

    auto id   = m_doc->GetScene().NextObjectID();
    auto text = make_unique<TextEntity>(id, req.InsertPos, utf8Text,
                                        req.Height, req.Rotation);
    text->SetAttr(/* 当前图层属性 */);

    auto cmd = make_unique<AddEntityCommand>(std::move(text));
    m_doc->GetCommandStack().Execute(std::move(cmd), m_doc->GetScene());
}
```

`MTextInputRequest` 与此相同，额外有 `BoxWidth`（文本框宽度）字段。

---

## 文字编辑（已有文字）

双击已有 `TextEntity` 时，`TextTool` 设置：

```cpp
req.EditTargetId = existingEntity->GetID();
req.InitialText  = existingEntity->GetText();
```

提交时走 `EditTextCommand`（保存旧文本，Undo 时还原），而不是 `AddEntityCommand`。

---

## 路径对比总结

```
TextEntity（单行）:
  Draw() → EmitText() → DrawContext::m_textVerts → ViewState.TextScene
  → SubmitTextured(fontTexture) → ImGui 字体图集采样 → GPU 渲染纹理四边形

MTextEntity（多行）:
  Draw() → EmitMText() → FontResolver → IFont::GetGlyph() → TextLayoutEngine
  → 字形线段 → DrawContext::m_verts → ViewState.Scene
  → Submit(PrimitiveType::Line) → GPU 渲染线段
```

---

## 拓展练习

1. **SHX 字节码**：打开 `SHXVM.h`，找到 `0x08` 指令（X,Y 偏移移动）。如果当前 `scale = 2.0`，指令字节是 `{0x08, 0x03, 0x04}`，笔会移动多少世界单位？`pen = true` 时会产生几条线段？
2. **TTF 展平**：`FlattenQuad` 的 `tolerance` 参数控制精度。改成很大的值（如 1.0）会有什么视觉效果？改成很小的值（如 0.0001）有什么代价？
3. **SHXCompositeFont 路由**：输入汉字"A"（其 Unicode 码点 0x41 < 0x80），`PickFontFor(0x41)` 走哪条分支？"中"（0x4E2D >= 0x80）呢？如果大字体里有英文字形，会影响"A"的路由吗？
4. **GlyphCache 键设计**：`GlyphKey` 同时包含 `FontId` 和 `Codepoint`，为什么要带 `FontId`？只用 `Codepoint` 作为键会有什么问题？
5. **Web vs Windows**：`TTFFont.h` 注释说 Web 端不使用此类。找到 `MTextEntity` 在 Web 端是否能正常渲染——如果能，走的是哪条路径？如果不能，会有什么表现？
6. **TextLayoutEngine 控制码**：`DecodeLine` 处理 `%%d`（度数符号°）。找到它解析后输出的 Unicode 码点是什么（0x00B0），再追踪这个码点在 SHXCompositeFont 里走哪个字体。
