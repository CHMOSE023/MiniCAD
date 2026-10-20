#pragma once
#include "Core/Math/Mat4.hpp"
#include "Core/Math/Point3.hpp"
#include "Core/Math/Vec3.hpp"
#include "Core/GeomKernel/AABB.hpp"
#include "Solid/NurbsCurve.h"
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <vector>

// 三维实体建模内核接口（开发计划见 docs/Solid/PLAN.md）。
// MiniCAD 只通过这个接口使用实体建模内核，接口里只出现 MiniCAD 自己的类型；
// 具体实现（如 SMLibKernel）的头文件与类型不进入这里。
namespace MiniCAD::Solid
{
    // 实体（BRep）。不可变：所有运算都返回新的实体，输入不变，因此可以在撤销栈、剪贴板等处共享。
    // 开放的曲面片（放样、直纹面等的结果：一个或多个面，不围成封闭的体积）也用它表示，见 IsSolid
    class IBody
    {
    public:
        virtual ~IBody() = default;
    };
    using BodyPtr = std::shared_ptr<const IBody>;

    // 闭合轮廓：位于 z = 常数的水平面上，用于拉伸、旋转。
    // 多段线顶点按 MiniCAD 的约定带凸度：Bulge 描述从本顶点到下一顶点的段，0 为直线，
    // tan(圆心角 / 4)，正值为逆时针圆弧；最后一个顶点连回第一个顶点（不要重复首点）。
    struct ProfileVertex
    {
        Math::Point3 Point;
        double       Bulge = 0.0;
    };

    struct Profile
    {
        std::vector<ProfileVertex> Vertices;   // 多段线轮廓（至少 2 个顶点，带凸度时 2 个也能闭合）
        bool         IsCircle = false;          // 整圆轮廓：用 Center、Radius，忽略 Vertices
        Math::Point3 Center;
        double       Radius   = 0.0;

        static Profile Circle(const Math::Point3& center, double radius)
        {
            Profile p;
            p.IsCircle = true;
            p.Center   = center;
            p.Radius   = radius;
            return p;
        }
    };

    // 扫掠路径：z = 常数的水平面上首尾相接的直线 / 圆弧，顶点带凸度（约定同 Profile）。
    // 不闭合时 Vertices 依次为各段起点和最后一段的终点；闭合时最后一个顶点连回第一个（不要重复首点）
    struct SweepPath
    {
        std::vector<ProfileVertex> Vertices;
        bool                       Closed = false;
    };

    enum class BooleanOp { Union, Difference, Intersection };

    // 离散化参数，含义同 SMLib：弦高为三角形到真实曲面的最大距离，0 表示不限制
    struct TessParams
    {
        double ChordHeight     = 0.0;
        double CurveAngleDeg   = 15.0;
        double SurfaceAngleDeg = 15.0;
    };

    // 三角网格：顶点按面拆分（同一位置在不同面上各有一个顶点，带各自的法线），
    // 三角形逆时针为外侧（右手定则，法线指向实体外）
    struct Mesh
    {
        std::vector<Math::Point3> Positions;
        std::vector<Math::Vec3>   Normals;     // 与 Positions 一一对应，单位长度
        std::vector<uint32_t>     Indices;     // 每 3 个一个三角形

        size_t TriangleCount() const { return Indices.size() / 3; }
    };

    // 一组折线。Ids 与 Lines 一一对应：Edges() 中是边的编号（同一实体内稳定，可用于 Fillet），其余为 -1
    struct Polylines
    {
        std::vector<std::vector<Math::Point3>> Lines;
        std::vector<int>                       Ids;
    };

    // 隐藏线消除的结果：沿视线方向看实体时可见 / 被遮挡的线（实体的边与曲面轮廓线），
    // 仍是原位置上的三维折线，投影到图纸平面由调用方完成
    struct HiddenLineResult
    {
        Polylines Visible;
        Polylines Hidden;
    };

    // 失败时返回空指针 / 空结果，原因由 LastError() 给出（下一次调用前有效）。
    // 实现不保证线程安全：同一内核对象只能在一个线程里使用。
    class ISolidKernel
    {
    public:
        virtual ~ISolidKernel() = default;

        virtual const char* Name() const = 0;
        virtual const std::string& LastError() const = 0;

        // ── 创建 ─────────────────────────────────────────────────────
        // 长方体：origin 为 x / y / z 最小的角点，三个边长都必须为正
        virtual BodyPtr Box(const Math::Point3& origin, double dx, double dy, double dz) = 0;
        // 圆柱：底面圆心、半径、高度（沿 +Z）
        virtual BodyPtr Cylinder(const Math::Point3& baseCenter, double radius, double height) = 0;
        virtual BodyPtr Sphere(const Math::Point3& center, double radius) = 0;
        // 拉伸：轮廓沿 +Z 拉伸 height（负值沿 -Z）
        virtual BodyPtr Extrude(const Profile& profile, double height) = 0;
        // 旋转：轮廓绕轴旋转 angleDeg 度（360 为整圈）；轴不能穿过轮廓内部
        virtual BodyPtr Revolve(const Profile& profile, const Math::Point3& axisPoint, const Math::Vec3& axisDir, double angleDeg) = 0;
        // 扫掠：轮廓先移到路径起点、转到与路径起点切线垂直（轮廓包围盒中心落在路径上），再沿路径扫掠；
        // 路径的折角处斜接，不闭合的路径两端封口
        virtual BodyPtr Sweep(const Profile& profile, const SweepPath& path) = 0;
        // 圆锥（圆台）：底面圆心、底面半径、顶面半径（0 为尖顶，两者不能都为 0）、高度（沿 +Z）
        virtual BodyPtr Cone(const Math::Point3& baseCenter, double radiusBase, double radiusTop, double height) = 0;
        // 圆环：中心、轴沿 +Z；主半径 > 截面半径 > 0
        virtual BodyPtr Torus(const Math::Point3& center, double majorRadius, double minorRadius) = 0;
        // 封口放样：依次经过各闭合截面曲线（至少 2 条，方向一致）、两端用平面封口的实体；
        // ruled 时相邻截面之间为直纹（一次），否则光滑（阶数最高为 3）
        virtual BodyPtr LoftSolid(std::span<const NurbsCurve> sections, bool ruled) = 0;
        // 沿空间曲线扫掠：闭合轮廓（第一条为外轮廓，其余为洞；调用方已把它转到与路径起点切线垂直的方向）
        // 先平移到包围盒中心落在路径起点，再沿 path 扫掠，轮廓随路径转动，两端封口
        virtual BodyPtr SweepAlong(std::span<const NurbsCurve> profile, const NurbsCurve& path) = 0;

        // ── 修改（都返回新实体）──────────────────────────────────────
        virtual BodyPtr Boolean(BooleanOp op, const BodyPtr& a, const BodyPtr& b) = 0;
        // 对 Edges() 给出编号的边倒圆角
        virtual BodyPtr Fillet(const BodyPtr& body, std::span<const int> edgeIds, double radius) = 0;
        // 对 Edges() 给出编号的边倒角（平面截面）：distance 为滚球半径，两面垂直时即沿两个面各退回 distance
        virtual BodyPtr Chamfer(const BodyPtr& body, std::span<const int> edgeIds, double distance) = 0;
        // 任意仿射变换（行向量约定，与 Math::Mat4 相同）；非均匀缩放会把几何转成 NURBS
        virtual BodyPtr Transform(const BodyPtr& body, const Math::Mat4& m) = 0;
        // 抽壳：封闭实体向内留下厚度 thickness 的壳；openFaces 为去掉的面（序号同 FaceCount），为空时得到带封闭空腔的实体
        virtual BodyPtr Shell(const BodyPtr& body, double thickness, std::span<const int> openFaces) = 0;

        // ── 显示与查询 ───────────────────────────────────────────────
        virtual Mesh      Tessellate(const BodyPtr& body, const TessParams& params = {}) = 0;
        // 所有边离散成折线，angleTolDeg 为相邻采样点切线的最大夹角
        virtual Polylines Edges(const BodyPtr& body, double angleTolDeg = 10.0) = 0;
        // 隐藏线消除：视线沿 viewDir（如俯视为 (0, 0, −1)）；光滑相接处的边与曲面接缝不输出
        virtual HiddenLineResult HiddenLines(const BodyPtr& body, const Math::Vec3& viewDir) = 0;
        // 与平面的截交线
        virtual Polylines Section(const BodyPtr& body, const Math::Point3& planePoint, const Math::Vec3& planeNormal) = 0;
        virtual double    Volume(const BodyPtr& body) = 0;   // 失败返回负数；开放曲面没有体积，结果无意义（先用 IsSolid 判断）
        virtual AABB      Bounds(const BodyPtr& body) = 0;   // 失败返回 AABB::Empty()
        virtual double    Area(const BodyPtr& body) = 0;     // 全部面的面积之和（实体与开放曲面都可以），失败返回负数
        // 封闭的流形实体（有体积）为 true；开放曲面、失败为 false
        virtual bool      IsSolid(const BodyPtr& body) = 0;
        virtual int       FaceCount(const BodyPtr& body) = 0;   // 失败返回 0

        // ── 空间曲线（结果是 NurbsCurve 数据，失败时 IsValid() 为 false）──────
        // 插值：曲线依次经过全部点（至少 3 个，相邻点不能重合；按弦长参数化，三次）。
        // closed：首尾相接、接点处切线连续（不要重复首点）
        virtual NurbsCurve InterpolateCurve(std::span<const Math::Point3> points, bool closed = false) = 0;
        // 拟合：逼近全部点的光滑三次曲线（不一定经过各点，至少 4 个点），各点到曲线的距离不超过 tolerance
        //（≤ 0 时取点集尺寸的 1/1000）；控制点从点数的 1/4 起逐步增加，直到满足容差
        virtual NurbsCurve FitCurve(std::span<const Math::Point3> points, double tolerance = 0.0) = 0;
        // 螺旋线：轴沿 +Z 过 base，高 height，底部 / 顶部半径（不同时为锥形），圈数，右旋 / 左旋；起点在 base + (radius0, 0, 0)
        virtual NurbsCurve Helix(const Math::Point3& base, double height, double radius0, double radius1, double turns, bool rightHanded = true) = 0;
        // 偏移（docs/NodeGraph/PLAN.md 第 4.2 节“曲线/修改”，N6b）：曲线须在 z 为常数的水平面内，
        // distance > 0 偏向前进方向的左侧（+Z × 切线），< 0 偏向右侧
        virtual NurbsCurve OffsetCurve(const NurbsCurve& curve, double distance) = 0;
        // 连接：已排好顺序、方向的首尾相接的曲线连成一条（阶数不同时自动升阶）
        virtual NurbsCurve JoinCurves(std::span<const NurbsCurve> curves) = 0;
        // 投影：曲线沿 direction 投影到实体 / 曲面上，得到 0～n 条曲线（投影不到时返回空，不算失败）
        virtual std::vector<NurbsCurve> ProjectCurve(const NurbsCurve& curve, const BodyPtr& target, const Math::Vec3& direction) = 0;

        // ── 曲面（结果一般是开放曲面片：BodyPtr，IsSolid 为 false）────────
        // 放样（蒙皮）：依次经过各截面曲线的曲面（至少 2 条；截面全部闭合时得到管状曲面，不封口）
        virtual BodyPtr Loft(std::span<const NurbsCurve> sections) = 0;
        // 直纹面：两条曲线对应点之间连直线
        virtual BodyPtr RuledSurface(const NurbsCurve& a, const NurbsCurve& b) = 0;
        // 旋转曲面：曲线绕轴旋转 angleDeg 度（右手定则，360 为整圈）
        virtual BodyPtr RevolveSurface(const NurbsCurve& curve, const Math::Point3& axisPoint, const Math::Vec3& axisDir, double angleDeg) = 0;
        // 点阵曲面：rows × columns 个点（points[r * columns + c]），曲面经过全部点；行、列都至少 2 个（不足 4 个时该方向的阶数降低）
        virtual BodyPtr SurfaceFromPoints(std::span<const Math::Point3> points, int rows, int columns) = 0;
        // 偏移曲面：每个面沿法线偏移 distance（负值反向）；裁剪过的面按整个曲面偏移
        virtual BodyPtr OffsetSurface(const BodyPtr& surface, double distance) = 0;
        // 管道：半径为 radius 的圆截面沿空间曲线扫掠；capEnds 时两端封口成实体（路径闭合时不需要封口）
        virtual BodyPtr Pipe(const NurbsCurve& path, double radius, bool capEnds = true) = 0;
        // 加厚：开放曲面沿法线方向加厚 thickness（负值反向），侧边用直纹面封闭，得到实体
        virtual BodyPtr Thicken(const BodyPtr& surface, double thickness) = 0;
        // 曲面上的点与单位法线：face 为面的序号（0 起，见 FaceCount），u、v 为归一化参数 ∈ [0, 1]（按面所在曲面的参数域）。
        // 法线朝向与 Tessellate 的三角形外侧一致
        virtual bool EvaluateSurface(const BodyPtr& surface, int face, double u, double v, Math::Point3& point, Math::Vec3& normal) = 0;

        // 曲面上一点的位置与对归一化参数 (u, v) 的一阶导数（曲率由调用方用相邻参数处的导数差分得到）
        virtual bool SurfaceDerivatives(const BodyPtr& surface, int face, double u, double v, Math::Point3& point, Math::Vec3& du, Math::Vec3& dv) = 0;

        // ── 曲面：创建、修改、分析（docs/NodeGraph/PLAN.md 第 6 节，N8）──────
        // 平面曲面：共面的闭合曲线围成的面，被包含的曲线为洞
        virtual BodyPtr PlanarSurface(std::span<const NurbsCurve> loops) = 0;
        // 挤出曲面：曲线沿向量平移扫过的开放曲面（不封口）
        virtual BodyPtr ExtrudeCurve(const NurbsCurve& curve, const Math::Vec3& vector) = 0;
        // 单轨扫描：截面沿轨道扫过的开放曲面（截面的起点应在轨道起点，截面随轨道转动）
        virtual BodyPtr SweepSurface(const NurbsCurve& section, const NurbsCurve& rail) = 0;
        // 边界曲面（Coons）：四条边，方向为 bottom P00→P10、right P10→P11、top P01→P11、left P00→P01
        virtual BodyPtr CoonsSurface(const NurbsCurve& bottom, const NurbsCurve& right, const NurbsCurve& top, const NurbsCurve& left) = 0;
        // 曲线网络曲面（Gordon）：两组交叉的曲线（各至少 2 条），曲面经过全部曲线
        virtual BodyPtr NetworkSurface(std::span<const NurbsCurve> u, std::span<const NurbsCurve> v) = 0;
        // 拟合曲面：散乱点（至少 9 个，投影到最佳平面上分配参数）最小二乘逼近，每个方向 controlPoints 个控制点、三次
        virtual BodyPtr FitSurface(std::span<const Math::Point3> points, int controlPoints) = 0;
        // 修剪 / 分割：曲线沿 direction 投影到曲面上切开；split 时保留两侧（结果的面数增加），否则只保留 keepPoint 所在一侧
        virtual BodyPtr TrimSurface(const BodyPtr& surface, const NurbsCurve& cutter, const Math::Vec3& direction, const Math::Point3& keepPoint, bool split) = 0;
        // 拼接：全部面合进一个实体并按容差缝合；围成封闭体积时结果是实体（IsSolid 为真）
        virtual BodyPtr JoinSurfaces(std::span<const BodyPtr> bodies, double tolerance) = 0;
        // 封口：开口（边界边连成的环）是平面的补上平面面并缝合；全部开口都补上时得到实体。没有平面开口时失败
        virtual BodyPtr CapPlanarHoles(const BodyPtr& body) = 0;
        // 炸开：每个面成为一个单独的曲面
        virtual std::vector<BodyPtr> ExplodeFaces(const BodyPtr& body) = 0;
        // 翻转法线、延伸、重建：只用于单个面的曲面，按面所在的整张曲面计算（修剪过的面会失败）
        virtual BodyPtr FlipSurface(const BodyPtr& surface) = 0;
        virtual BodyPtr ExtendSurface(const BodyPtr& surface, double distance) = 0;                       // 四周各延伸 distance（线性）
        virtual BodyPtr RebuildSurface(const BodyPtr& surface, int uCount, int vCount) = 0;               // 各方向的控制点数
        // 精确的边曲线（与 Edges() 的顺序相同）
        virtual std::vector<NurbsCurve> EdgeCurves(const BodyPtr& body) = 0;
        // 等参线：第 face 个面在归一化参数 t 处的 u 向（alongU：v = t、u 变化）或 v 向曲线（按整张曲面，不按修剪）
        virtual NurbsCurve IsoCurve(const BodyPtr& surface, int face, bool alongU, double t) = 0;
        // 最近点（在修剪后的面上）
        virtual bool ClosestPoint(const BodyPtr& body, const Math::Point3& p, Math::Point3& closest, double& distance) = 0;
        // 曲线与实体 / 曲面的交点（没有交点返回空，不算失败）
        virtual std::vector<Math::Point3> IntersectCurve(const BodyPtr& body, const NurbsCurve& curve) = 0;
        // 两个实体 / 曲面的交线（没有交线返回空）
        virtual std::vector<NurbsCurve> IntersectBodies(const BodyPtr& a, const BodyPtr& b) = 0;

        // 网格转实体（docs/NodeGraph/PLAN.md 第 8 节，N9）：每个三角形一个平面面，按容差缝合；
        // 封闭网格得到多面实体（IsSolid 为真，可做布尔），开放网格得到多面曲面
        virtual BodyPtr FromMesh(const Mesh& mesh) = 0;

        // ── 读写（内核自己的格式，用于 .mcad / JSON）────────────────────
        // Load 先校验数据（格式标记、长度、校验和），损坏的数据返回空指针，不会交给内核解析
        virtual std::vector<uint8_t> Save(const BodyPtr& body) = 0;   // 失败返回空
        virtual BodyPtr              Load(std::span<const uint8_t> bytes) = 0;
    };
}
