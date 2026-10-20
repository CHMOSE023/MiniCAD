#pragma once
#include "NodeGraph/Value.h"
#include <optional>
#include <vector>

// 网格运算（docs/NodeGraph/PLAN.md 第 8 节，N9），不依赖内核（网页版可用）。
// 网格是 Solid::Mesh：顶点共用，Indices 每 3 个一个三角形（从正面看逆时针，法线朝外），Normals 为顶点法线（面积加权）
namespace MiniCAD::Nodes::MeshOps
{
    using Solid::Mesh;

    // 重算顶点法线（面积加权的面法线之和）
    void ComputeNormals(Mesh& m);
    // 包围盒对角线长度（0 表示空网格）
    double Size(const Mesh& m);

    // ── 创建 ─────────────────────────────────────────────────────────
    // 长方体：中心、三个方向的边长、每条边的段数（≥ 1）；顶点共用，封闭
    Mesh Box(const Math::Point3& center, const Math::Vec3& size, int divisions);
    // 经纬球：经向段数 ≥ 3、纬向段数 ≥ 2；两极各一个顶点，封闭
    Mesh Sphere(const Math::Point3& center, double radius, int uSegments, int vSegments);
    // 平面上以原点为中心的 width × height 矩形网格，nx × ny 格（每格两个三角形），正面朝平面法向
    Mesh Grid(const Plane& plane, double width, double height, int nx, int ny);
    // 由顶点与面索引构造：faceSize 为 3（三角形）或 4（四边形，拆成 0-1-2、0-2-3）；索引越界返回空并给出 error
    std::optional<Mesh> Construct(const std::vector<Math::Point3>& vertices, const std::vector<int64_t>& indices, int faceSize, std::string& error);
    // 平面点集的 Delaunay 三角化（Bowyer–Watson）：点投影到 plane 上，结果三角形正面朝平面法向，顶点保持原来的三维位置。
    // 重合的点只保留一个
    Mesh Delaunay(const std::vector<Math::Point3>& points, const Plane& plane);
    // 平面多边形（带洞）的三角化（耳切法，洞经桥接边接到外圈）：outer 与 holes 都是平面上的闭合折线（不重复首点），
    // plane 为多边形所在平面（结果正面朝平面法向）。失败返回空网格
    Mesh Polygon(const std::vector<Math::Point3>& outer, const std::vector<std::vector<Math::Point3>>& holes, const Plane& plane);

    // 实体 / 曲面网格化（ISolidKernel::Tessellate），同一位置的顶点焊接；失败返回空网格
    Mesh FromBody(Solid::ISolidKernel& kernel, const Solid::BodyPtr& body, const Solid::TessParams& params = {});

    // ── 修改 ─────────────────────────────────────────────────────────
    // 焊接：距离不超过 tolerance 的顶点合并（tolerance ≤ 0 时取尺寸的 1e-9），去掉退化的三角形与没用到的顶点，重算法线
    Mesh Weld(const Mesh& m, double tolerance);
    // 翻转：三角形反向、法线取反
    Mesh Flip(const Mesh& m);
    // 合并：顶点与三角形依次拼在一起（不焊接）
    Mesh Join(const std::vector<const Mesh*>& meshes);
    // 拆分：按共用顶点连通的三角形分成几块
    std::vector<Mesh> Split(const Mesh& m);
    // Loop 细分一次：每个三角形变成 4 个，顶点按 Loop 规则平滑（边界按三次 B 样条规则）
    Mesh Subdivide(const Mesh& m);
    // Laplacian 平滑：每次每个顶点向相邻顶点的平均位置移动 factor（0～1），lockBoundary 时边界顶点不动
    Mesh Smooth(const Mesh& m, int iterations, double factor, bool lockBoundary);
    // 简化：边折叠（二次误差度量）到不超过 targetFaces 个三角形；会翻转三角形或破坏流形的折叠被跳过，所以可能达不到目标
    Mesh Reduce(const Mesh& m, size_t targetFaces);
    // 刚体 / 仿射变换（行向量约定）；行列式为负（镜像）时三角形反向，正面仍朝外
    Mesh Transform(const Mesh& m, const Math::Mat4& matrix);

    // ── 分析 ─────────────────────────────────────────────────────────
    double Area(const Mesh& m);
    double Volume(const Mesh& m);        // 有向体积（封闭且朝外时为正）
    // 封闭：每条边恰好被两个三角形以相反方向使用
    bool   IsClosed(const Mesh& m);
    Math::Vec3 FaceNormal(const Mesh& m, size_t face);   // 单位法线（退化三角形为零向量）
    // 最近点：返回最近点、距离、三角形序号；空网格返回 false
    bool   ClosestPoint(const Mesh& m, const Math::Point3& p, Math::Point3& closest, double& distance, size_t& face);
}
