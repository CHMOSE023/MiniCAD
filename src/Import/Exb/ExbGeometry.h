#pragma once
// EXB 第 3 层：块流（/*BlockStg/*Blk_xxx）里的对象记录 → 中立几何。
//
// 这一层刻意不 include Scene / Entity / MiniCAD 任何头文件：它只吐出一批 POD，
// 由 ExbReader.cpp 负责翻译成 MiniCAD 实体。这样阶段 4（块引用展开、外部参照）
// 要复用几何时必须经过 Scene，而不是被绕开。
//
// ── 记录格式 ────────────────────────────────────────────────────────────
// 记录的切分见 ExbGeometry.cpp 里 WalkRecords 上方的说明：每条记录以 16 字节类标识开头，
// 后面是对象 ID、所有者 ID、可选的扩展属性、正文长度和正文。
// 按类标识读的：点、直线、圆、圆弧、椭圆、射线、构造线、多段线、样条（求值成折线）、文字、块参照（含属性）、
// 填充（边界 + 已展开的图案线）、实心 / 渐变填充（闭合环）；标注、工程符号、表格取它们自带的显示用嵌套记录。
// 各类正文的布局写在 ExbGeometry.cpp 对应的 Read* 函数上方。还没对上类型的类仍按结构识别：
//
//   折线        连续 n 组 32 字节 (f64 x, f64 y, 0.0, 1.0)
//   直线        连续 2 组 24 字节 (f64 x, f64 y, 0.0)
//   圆/弧       单点之后紧跟 (半径, cos起始角, sin起始角)，记录末尾是扫过角
//   文字        [u32 字节长][UTF-16LE 文本]，串尾 T 之后：T+0 字高，T+24 旋转角，T+53 / T+61 插入点
//
// 找不到块头（WalkRecords 失败）时退回 ParseBlockStreamByMarks：按 `FB FF FF FF` 标记切分。
#include <cstddef>
#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace MiniCAD::Exb
{
    struct ExbPoint2
    {
        double X = 0.0;
        double Y = 0.0;
    };

    enum class ExbGeometryKind : std::uint8_t
    {
        Unknown = 0,
        Polyline,     // 含多段线、折线、由多点构成的曲线
        Line,         // 两点直线
        Circle,       // 圆心 + 半径（整圆）
        Arc,          // 圆心 + 半径 + 起止角
        Point,        // 孤立点
        Text,
        Insert,       // 块参照
        Ellipse,      // Center、Radius（长半轴）、RadiusMinor、Rotation、StartAngle / EndAngle（参数角）
        Ray,          // Points[0] 起点 + Direction
        XLine,        // Points[0] 过点 + Direction
        Group,        // 只有 Children：标注 / 填充 / 表格等展开后的图形
        Image,        // Points = 对角两点（不带转角），ImageRef = 图片定义的对象 ID
    };

    // ── 图层 / 线型 / 颜色 ─────────────────────────────────────────────────
    //
    // 记录头里紧跟着一对 `[u16 0x0080][i32 值]`，其后 4 字节是颜色字：
    //     [u16 0x0080][i32 图层 ID][u16 0x0080][i32 线型 ID][u32 颜色]
    // 位置随格式代平移（GenA 在 rel 16/24/30，GenC 在 rel 40/48/54），所以不写死偏移，
    // 而是「在前 72 字节内找两对 0x0080 标签」。
    //
    // 实测（v2013/v2015/v2018/v2022/v2024 五版）：12902 条图形记录 100% 命中两对，
    // 3800 条块定义类记录 0 命中 —— 判据干净，不会误伤。
    //
    // 图层 ID 有两种编码，都由 ResolveLayerIndex 统一处理：
    //   ① 内置 8 层：负句柄，按定义顺序严格 +1（`句柄 = 首句柄 + 定义序号`）；
    //   ② 大图自建层：正数持久 ID 45..53，与定义顺序无关，直接等于句柄。
    // 具体名称一律从本文件样式流的图层表读，不在这里硬编码。
    struct ExbGeometryStyle
    {
        std::int32_t  LayerId    = 0;   // 显式图层 ID；负值 = 随层/特殊
        // 线型 ID 的取值全部来自样式流里的线型表（ParseLineTypeTable），
        // 而线型表和图层句柄一样是**文件内局部编号**：
        //   大图（12.5/15/16/17/18）−13 ByLayer、−14 ByBlock、−15 实线、−16 虚线、
        //   −17 点画线、−18 双点画线；旧代 pl3.exb 同样六个是 −19..−24。
        // 所以要拿本文件的 ExbLineTypeDef::Id 去对，不能写死「−15 = 实线」。
        // 早先这里注成「−15 随层、−13 随块、41 实线」是错的：−15 是**显式实线**。
        std::int32_t  LineTypeId = 0;
        std::uint32_t ColorWord  = 0;   // 0x80BBGGRR；最高字节 0x80 = 随层
        bool          Valid      = false;

        // 实体头里的三个方式字节：0 随层、1 随块、2 用自己的值；kUnknownMode = 按标记切分的后备路径没读到
        static constexpr std::uint8_t kUnknownMode = 0xFF;
        std::uint8_t  ColorMode    = kUnknownMode;
        std::uint8_t  LineTypeMode = kUnknownMode;
        std::uint8_t  WidthMode    = kUnknownMode;
        double        LineWidth    = 0.0;   // mm

        bool ColorByLayer() const { return ColorMode == kUnknownMode ? (ColorWord >> 24) == 0x80 : ColorMode == 0; }
        bool ColorByBlock() const { return ColorMode == 1; }
        std::uint8_t R() const { return static_cast<std::uint8_t>((ColorWord >> 16) & 0xFF); }
        std::uint8_t G() const { return static_cast<std::uint8_t>((ColorWord >> 8) & 0xFF); }
        std::uint8_t B() const { return static_cast<std::uint8_t>(ColorWord & 0xFF); }
    };

    // ── 图层定义（/*StyleStream）──────────────────────────────────────────
    //
    // 图层记录的载荷长这样（三个长度前缀串 + 颜色/状态 + 一段固定尾巴）：
    //
    //     [u32 主名长][UTF-16 主名]        ← 0 Layer / 缝纫线 / 6文字层
    //     [u32 别名长][UTF-16 别名]        ← 多数层为空
    //     [u32 key 长 ][UTF-16 key]        ← 中心线层这类是主名的副本
    //     [8B 颜色/状态][u32 28]["UnInit Layer!"][f32 线宽][i32 线型]...
    //
    // **锚点用 ASCII 串 `UnInit Layer!`（含尾部 NUL 共 28 字节），不用 0x5C42。**
    // 早先用 `0x5C42`（它在 UTF-16 里是汉字「层」）当判据，只能匹配到中文名以「层」
    // 结尾的图层，其余全部丢失 —— 8 层的图只解出 3 个，17 层的图只解出 8 个，
    // 而且把 45..53 这九个自建层全部错认成别的内置层。
    //
    // 句柄是**运行时对象 ID**，同名的图层在不同文件里句柄不同：
    //     `0 Layer` 在 lay6.exb 是 −20，在 pl3.exb 是 −26。
    // 所以不能跨文件硬编码，只能从每个文件自己的样式流重建。
    //
    // 句柄的分配有两种编码：
    //   ① 内置 8 层：**按定义顺序从首句柄起严格 +1**，即 `句柄 = 首句柄 + 定义序号`；
    //   ② 自建层（45..53）：句柄就是持久 ID 本身，与定义顺序无关。
    // ResolveLayerIndex 先按 ① 的精确值匹配，不中再按 ② 的连续性推算。
    struct ExbLayerDef
    {
        std::string  Name;
        std::int32_t Handle = 0;      // 0 = 未找到锚点
        // 图层自身属性（实体 ByLayer 时套用）。取值见 reports/12-*.md：
        //   ColorWord 与实体记录里的颜色字同格式：低 3 字节是 0xBBGGRR
        //   （0 Layer 是 0xFFFFFF = 白，缝纫线是 0x3F00FF → #FF003F 玫红）
        std::uint32_t ColorWord  = 0x00FFFFFFu;
        std::int32_t  LineTypeId = 0;
        double        LineWidth  = 0.0;   // mm；2024 版是 0/0.25/0.5，2013 版是 0.18/0.35
        bool          Visible    = true;
        bool          Locked     = false;
    };

    // ── 线型定义（/*StyleStream 前段，紧接在图层表之前）──────────────────
    //
    // 线型表和图层表在同一个流里，以 6 字节标记 `87 92 1b 4f 5d 0b` 分条：
    //
    //     [6B 标记][i32 线型 ID][i32 −12][u16 1][u32 ?]
    //     [u32 名长][UTF-16 中文名]          ← 实线 / 虚线 / DIVIDE / DASHED
    //     [u32 说明长][UTF-16 示意图串]      ← "虚线: __ __ __"，只是图例，不是数据
    //     [f32 图案比例]
    //     [u32 n] [u16 0xFFFF] [n × f32 节拍]
    //     [u32 英文名长][UTF-16 英文名]      ← Continuous / Dash / Dash Dot，可空
    //     [10~11B 常量尾]
    //
    // 节拍是**交替**的「实线段长 / 空白段长」：偶数下标是墨、奇数下标是空，
    // 墨长为 0 表示画一个点。这一点用两条独立证据钉死：
    //   ① DIVIDE = (12.7, 6.35, 0, 6.35, 0, 6.35)，示意图是「____ . . ____ . .」；
    //   ② DASHED2 = (6.35, 3.175) 恰好是 DASHED = (12.7, 6.35) 的一半，
    //      而它自己的示意图写着 "Dashed (.5x)"。
    //
    // `Scale` 是**图案比例**（不是墨/空比例）：2013 版里 虚线/点画线 都是 1.0，
    // 2024 版把它们提到 6.0 / 3.0 —— CAXA 换默认线宽表时一起调了线型比例，
    // 所以渲染时要用 `节拍 × Scale`，否则新文件里的虚线细到看不见。
    //
    // **ID 是文件内局部句柄，不是跨文件常量。** 同一份 CAXA 12.5 大图是
    // −13 ByLayer / −14 ByBlock / −15 实线 / −16 虚线 / −17 点画线 / −18 双点画线，
    // 而旧代小文件（pl3.exb）里同样六个内置线型是 −19..−24 —— 与图层句柄一样
    // 按文件重新分配。≥41 的正数才是文件自定义线型（本图 41 DIVIDE / 42 DASHED /
    // 43 DASHED2 / 44 BORDER）。所以图层的 LineTypeId 只能拿本文件的线型表来对，
    // 不能硬编码 −15 = 实线。
    struct ExbLineTypeDef
    {
        std::int32_t        Id = 0;
        std::string         Name;         // 中文/文件内主名：实线 / DIVIDE / DASHED
        std::string         Alt;          // 英文别名：Continuous / Dash Dot，可空
        std::string         Description;  // 示意图串，仅用于 UI 提示
        double              Scale = 1.0;  // 图案比例
        std::vector<double> Dashes;       // 交替「墨 / 空」，0 = 点
    };

    // 实心填充的一个闭合环：顶点 + 从该顶点出发那一段的凸度（末点与首点重合）
    struct ExbFillLoop
    {
        std::vector<ExbPoint2> Points;
        std::vector<double>    Bulges;   // Points.size() - 1 个
    };

    struct ExbGeometryItem
    {
        ExbGeometryKind Kind = ExbGeometryKind::Unknown;
        ExbGeometryStyle Style;

        std::vector<ExbPoint2> Points;   // Polyline：全部顶点；Line：2 个
        // 多段线类记录才有：与 Points 等长，属于从该顶点出发的那一段（同 DXF 的 LWPOLYLINE）
        std::vector<double> Bulges;
        std::vector<double> StartWidths;
        std::vector<double> EndWidths;
        bool                Closed = false;

        ExbPoint2 Center;                // Circle / Arc / Ellipse
        double    Radius = 0.0;
        double    RadiusMinor = 0.0;     // Ellipse
        ExbPoint2 Direction;             // Ray / XLine
        // 弧度制，逆时针，与 MiniCAD Arc 同向。整圆时 Start=0、End=2π。
        double    StartAngle = 0.0;
        double    EndAngle   = 6.283185307179586;

        // 文字：Raw 是原始串（含 MText 格式码），Plain 是剥掉格式码的可见文字
        std::string Raw;
        std::string Plain;
        ExbPoint2   Insert;
        double      Height   = 0.0;
        double      BoxWidth = 0.0;   // 多行文字的换行宽度；0 = 单行文字
        std::int32_t StyleId = 0;     // 文字样式的对象 ID，对 ExbTextStyleDef::Id
        std::uint8_t Align   = 0;     // 0～8：列 = Align % 3（左 / 中 / 右），行 = Align / 3（上 / 中 / 下）
        double      Rotation = 0.0;

        std::size_t Offset = 0;   // 记录在块流里的起点与长度，排查识别错误用
        std::size_t Size   = 0;
        std::string  ClassId;     // 记录的类标识（32 位十六进制）；按标记切分的后备路径里为空
        std::int32_t Id = 0;      // 对象 ID

        // 块参照：插入点在 Insert，转角在 Rotation；ScaleY 为负表示镜像
        std::int32_t BlockRef = 0;   // 块的对象 ID，对 ExbBlockDef::Id
        std::int32_t ImageRef = 0;   // 图片定义的对象 ID，对 ExbImageDef::Id
        double       ScaleX   = 1.0;
        double       ScaleY   = 1.0;

        // Group 的内容；块参照的属性文字也放在这里（坐标已是所在空间的坐标）
        std::vector<ExbGeometryItem> Children;
        bool Filled = false;   // Group：实心填充，Children 是它的边界
        std::vector<ExbFillLoop> Loops;   // Filled 时：首尾接好的边界环
        // 渐变填充：GradientName 非空时有效，名字同 AutoCAD（LINEAR、SPHERICAL …），颜色字低三字节是 0xBBGGRR
        std::string   GradientName;
        double        GradientAngle = 0.0;   // 弧度
        double        GradientShift = 0.0;
        bool          GradientSingleColor = false;
        double        GradientTint  = 0.0;
        std::uint32_t GradientColors[2] = { 0, 0 };
    };

    // 图片定义（/*ImageStg/*Img_<十六进制 ID>）
    struct ExbImageDef
    {
        std::int32_t Id = 0;
        std::string  Path;           // 保存时的绝对路径
        std::string  RelativePath;   // 相对图纸的路径，可空
    };

    // 文字样式（/*StyleStream）。字体是字体名或 .shx 文件名，西文与中文各一个
    struct ExbTextStyleDef
    {
        std::int32_t Id = 0;
        std::string  Name;
        std::string  WesternFont;
        std::string  CjkFont;
        double       WidthFactor = 1.0;
        double       Height      = 0.0;
        double       Oblique     = 0.0;   // 弧度
    };

    struct ExbBlockDef
    {
        std::int32_t Id = 0;
        std::string  Name;
        std::vector<ExbGeometryItem> Items;
        std::size_t  RecordCount       = 0;
        std::size_t  UnclassifiedCount = 0;
    };

    struct ExbGeometryResult
    {
        std::vector<ExbGeometryItem> Items;
        std::vector<std::string>     LayerNames;
        std::vector<ExbLayerDef>     Layers;      // 与 LayerNames 同序，但带句柄
        std::vector<ExbLineTypeDef>  LineTypes;   // 样式流里的线型表，按定义顺序

        std::size_t RecordCount        = 0;
        std::size_t UnclassifiedCount  = 0;   // 记录存在但没识别出几何（块定义 / 标注等）
        std::size_t TextCount          = 0;
        std::size_t PolylineCount      = 0;
        std::size_t LineCount          = 0;
        std::size_t CircleCount        = 0;
        std::size_t ArcCount           = 0;
        std::size_t PointCount         = 0;
        std::size_t InsertCount        = 0;
        std::size_t OtherCount         = 0;   // 椭圆、射线、构造线、Group
        std::size_t UnparsedBytes      = 0;   // 记录之间没有长度前缀、只能跳过的字节（表格的附加数据等）
        std::map<std::string, std::size_t> UnclassifiedByClass;   // 类标识 → 没识别出几何的记录数

        std::vector<std::string> Warnings;
    };

    // 块流逻辑字节 → 几何。失败（找不到记录）返回 false。
    // productMajor：/*FileInfoStream 里版本号的主版本（12.5.0.5302 → 12）；0 = 从数据里看实体头宽度。
    bool ParseBlockStream(const std::uint8_t* data, std::size_t size, ExbGeometryResult& out, unsigned productMajor = 0);

    // 块定义流（`/*BlockStg/*GBlock`，一个流里放着全部块）→ 各块的名字与几何。
    // unclassified：累加没出几何的记录数（类标识 → 条数）。
    bool ParseBlockDefinitions(const std::uint8_t* data, std::size_t size, std::vector<ExbBlockDef>& blocks,
                               unsigned productMajor = 0, std::map<std::string, std::size_t>* unclassified = nullptr);

    // 样式流逻辑字节 → 图层表（按定义顺序）。
    void ParseStyleStream(const std::uint8_t* data, std::size_t size, std::vector<ExbLayerDef>& layers);

    // 图片定义流逻辑字节 → 图片路径。
    bool ParseImageDef(const std::uint8_t* data, std::size_t size, ExbImageDef& def, unsigned productMajor = 0);

    // 类标识（32 位十六进制）→ 不显示的对象的英文名（PDF underlay 等）；不认识的返回空串。
    std::string UnsupportedClassName(const std::string& classId);

    // 样式流逻辑字节 → 文字样式表。
    void ParseTextStyles(const std::uint8_t* data, std::size_t size, std::vector<ExbTextStyleDef>& styles,
                         unsigned productMajor = 0);

    // 样式流逻辑字节 → 线型表（按定义顺序）。与图层表共用同一个流，互不影响。
    void ParseLineTypeTable(const std::uint8_t* data, std::size_t size, std::vector<ExbLineTypeDef>& lineTypes);

    // 样式流逻辑字节 → 图层名（兼容旧调用方）。
    void ParseStyleStream(const std::uint8_t* data, std::size_t size, std::vector<std::string>& layers);

    // 实体记录的图层 ID → 图层表下标。返回 −1 表示无法确定（应落到默认层）。
    // 两条分支依次尝试：① 句柄精确匹配 → ② 旧代连续句柄（首句柄 + 序号）。
    int ResolveLayerIndex(std::int32_t layerId, const std::vector<ExbLayerDef>& layers);

    // UTF-16LE → UTF-8（含代理对）
    std::string Utf16LeToUtf8(const std::uint8_t* data, std::size_t size);

    // 去掉 MText / CAXA 文本格式码，只留可见文字。
    // `\A3;LB1 *18` 与 `\A3;{\fsimsun|b0|i0;\f^simsun|b0|i0;LB1 *18}` 都得到 `LB1 *18`。
    std::string StripTextCodes(const std::string& utf8);
}
