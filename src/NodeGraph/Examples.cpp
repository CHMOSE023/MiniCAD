#include "NodeGraph/Examples.h"

namespace MiniCAD::Nodes
{
    namespace
    {
        // 搭图的简写：节点按列 / 行放置（列宽 230、行高 120）
        struct Builder
        {
            Graph& g;

            NodeId Add(const char* type, float col, float row)
            {
                Node* n = g.AddNode(type, col * 230.0f, row * 120.0f);
                return n ? n->Id : kInvalidNode;
            }
            void Link(NodeId from, NodeId to, int port, bool append = false, int fromPort = 0)
            {
                g.Connect({ from, fromPort, to, port }, !append);
            }
            // 长度参数（Revit 族参数）
            NodeId Param(const char* name, double value, float col, float row)
            {
                const NodeId id = Add(kParamNodeType, col, row);
                g.SetParam(id, 0, Value(std::string(name)));
                g.SetParam(id, 2, Value(value));
                return id;
            }
            // 只用参数名的表达式
            NodeId Expr(const char* formula, float col, float row)
            {
                const NodeId id = Add("math.expression", col, row);
                g.SetParam(id, 0, Value(std::string(formula)));
                return id;
            }
            NodeId Point(NodeId x, double xv, NodeId y, double yv, NodeId z, double zv, float col, float row)
            {
                const NodeId id = Add("vector.point", col, row);
                const NodeId src[3] = { x, y, z };
                const double val[3] = { xv, yv, zv };
                for (int i = 0; i < 3; ++i)
                    if (src[i] != kInvalidNode)
                        Link(src[i], id, i);
                    else
                        g.SetInputValue(id, i, Value(val[i]));
                return id;
            }
        };

        // 参数化窗框：外框拉伸为实心，上下两块玻璃洞口为空心拉伸，“形体合并”求出窗框（Revit 族的做法）。
        // 窗立在 XZ 平面上，参数：宽、高、框宽、厚度、中梃高（中梃中心离底边的高度）
        void BuildWindow(Graph& graph)
        {
            Builder b{ graph };
            b.Param("宽", 1200, 0, 0);
            b.Param("高", 1500, 0, 1);
            b.Param("框宽", 60, 0, 2);
            const NodeId depth = b.Param("厚度", 80, 0, 3);
            b.Param("中梃高", 900, 0, 4);

            const Plane xz{ {}, { 1, 0, 0 }, { 0, 0, 1 } };
            auto rect = [&](NodeId center, NodeId w, NodeId h, float row)
            {
                const NodeId r = b.Add("curve.rectangle", 2, row);
                b.Link(center, r, 0);
                b.Link(w, r, 1);
                b.Link(h, r, 2);
                return r;
            };
            auto extrude = [&](const char* type, NodeId profile, float row)
            {
                const NodeId e = b.Add(type, 3, row);
                b.Link(profile, e, 0);
                b.Link(depth, e, 1);
                graph.SetInputValue(e, 3, Value(xz));
                graph.SetPreview(e, false);
                return e;
            };

            // 外框
            const NodeId outerC = b.Point(kInvalidNode, 0, b.Expr("高 / 2", 1, 0), 0, kInvalidNode, 0, 1.5f, 0);
            const NodeId outer = extrude("solid.extrude", rect(outerC, b.Expr("宽", 1, 1), b.Expr("高", 1, 2), 0), 0);
            // 洞口：宽度相同，下洞口从框宽到中梃下沿，上洞口从中梃上沿到顶框下沿
            const NodeId paneW = b.Expr("宽 - 2 * 框宽", 1, 3);
            const NodeId lowC = b.Point(kInvalidNode, 0, b.Expr("(框宽 + 中梃高 - 框宽 / 2) / 2", 1, 4), 0, kInvalidNode, 0, 1.5f, 3);
            const NodeId low = extrude("void.extrude", rect(lowC, paneW, b.Expr("中梃高 - 1.5 * 框宽", 1, 5), 2), 2);
            const NodeId upC = b.Point(kInvalidNode, 0, b.Expr("(中梃高 + 框宽 / 2 + 高 - 框宽) / 2", 1, 6), 0, kInvalidNode, 0, 1.5f, 6);
            const NodeId up = extrude("void.extrude", rect(upC, paneW, b.Expr("高 - 中梃高 - 1.5 * 框宽", 1, 7), 4), 4);

            const NodeId combine = b.Add("solid.combine", 4, 2);
            b.Link(outer, combine, 0);
            b.Link(low, combine, 0, true);
            b.Link(up, combine, 0, true);
            const NodeId info = b.Add("solid.info", 5, 2);
            b.Link(combine, info, 0);
        }

        // 曲面屋顶：马鞍面 z = 4 + 0.03 (x − 10)² − 0.05 (y − 6)² 上取三条 U 向、三条 V 向的插值曲线 → 网络曲面 → 加厚成实体 →
        // 减去圆柱开天窗（布尔）
        void BuildRoof(Graph& graph)
        {
            Builder b{ graph };
            const char* surface = "4 + 0.03 * (a - 10) ^ 2 - 0.05 * (b - 6) ^ 2";
            const NodeId xs = b.Add("math.series", 0, 0);   // x = 0, 5, …, 20
            graph.SetInputValue(xs, 1, Value(5.0));
            graph.SetInputValue(xs, 2, Value(int64_t(5)));
            const NodeId ys = b.Add("math.series", 0, 4);   // y = 0, 3, …, 12
            graph.SetInputValue(ys, 1, Value(3.0));
            graph.SetInputValue(ys, 2, Value(int64_t(5)));

            const NodeId net = b.Add("surface.network", 4, 3);
            for (int i = 0; i < 3; ++i)
            {
                // U 向：y 固定，x 取序列
                const double y = 6.0 * i;
                const NodeId zu = b.Expr(surface, 1, static_cast<float>(i));
                b.Link(xs, zu, 0);
                graph.SetInputValue(zu, 1, Value(y));
                const NodeId pu = b.Point(xs, 0, kInvalidNode, y, zu, 0, 2, static_cast<float>(i));
                const NodeId cu = b.Add("nurbs.interpolate", 3, static_cast<float>(i));
                b.Link(pu, cu, 0);
                b.Link(cu, net, 0, i > 0);
                // V 向：x 固定，y 取序列
                const double x = 10.0 * i;
                const NodeId zv = b.Expr(surface, 1, 4.0f + i);
                graph.SetInputValue(zv, 0, Value(x));
                b.Link(ys, zv, 1);
                const NodeId pv = b.Point(kInvalidNode, x, ys, 0, zv, 0, 2, 4.0f + i);
                const NodeId cv = b.Add("nurbs.interpolate", 3, 4.0f + i);
                b.Link(pv, cv, 0);
                b.Link(cv, net, 1, i > 0);
            }
            graph.SetPreview(net, false);

            const NodeId thick = b.Add("surface.thicken", 5, 3);
            b.Link(net, thick, 0);
            b.Link(b.Param("屋面厚度", 0.3, 4, 4.5f), thick, 1);
            graph.SetPreview(thick, false);
            // 天窗：屋顶中央的竖直圆柱
            const NodeId sky = b.Add("solid.cylinder", 5, 5);
            graph.SetInputValue(sky, 0, Value(Math::Vec3{ 10, 6, -5 }));
            b.Link(b.Param("天窗半径", 1.5, 4, 6), sky, 1);
            graph.SetInputValue(sky, 2, Value(20.0));
            graph.SetPreview(sky, false);
            const NodeId roof = b.Add("solid.difference", 6, 3);
            b.Link(thick, roof, 0);
            b.Link(sky, roof, 1);
            const NodeId info = b.Add("solid.info", 7, 3);
            b.Link(roof, info, 0);
        }

        // 网格化导出：长方体与球求交得到圆角方块 → 网格化；另用网格截面画等高线。烘焙后网格是多面网格实体，可导出 DWG / DXF
        void BuildMeshExport(Graph& graph)
        {
            Builder b{ graph };
            const NodeId box = b.Add("solid.box", 0, 0);
            graph.SetInputValue(box, 1, Value(20.0));
            graph.SetInputValue(box, 2, Value(20.0));
            graph.SetInputValue(box, 3, Value(20.0));
            graph.SetParam(box, 0, Value(true));   // 居中：底面中心在原点
            const NodeId ball = b.Add("solid.sphere", 0, 1.5f);
            graph.SetInputValue(ball, 0, Value(Math::Vec3{ 0, 0, 10 }));
            graph.SetInputValue(ball, 1, Value(13.0));
            const NodeId both = b.Add("solid.intersection", 1, 0.5f);
            b.Link(box, both, 0);
            b.Link(ball, both, 0, true);
            for (NodeId id : { box, ball, both })
                graph.SetPreview(id, false);

            // 网格是叶子输出（预览、烘焙只取没有连到下游的输出）
            const NodeId mesh = b.Add("mesh.from", 2, 0.5f);
            b.Link(both, mesh, 0);
            // 等高线：高度 2、5、…、17；实体直接连到网格端口，按默认精度（与“网格化”的默认角度相同）网格化
            const NodeId levels = b.Add("math.series", 1, 2);
            graph.SetInputValue(levels, 0, Value(2.0));
            graph.SetInputValue(levels, 1, Value(3.0));
            graph.SetInputValue(levels, 2, Value(int64_t(6)));
            const NodeId contours = b.Add("mesh.section", 2, 2);
            b.Link(both, contours, 0);
            b.Link(levels, contours, 1);
        }

        // 网格截面与求交（不需要内核，网页版可用）：网格球与网格长方体求交线，球的水平等高线，长方体沿斜方向的轮廓线；合并后的网格作为预览
        void BuildMeshCurves(Graph& graph)
        {
            Builder b{ graph };
            const NodeId ball = b.Add("mesh.sphere", 0, 0);
            graph.SetInputValue(ball, 1, Value(6.0));
            graph.SetInputValue(ball, 2, Value(int64_t(32)));
            graph.SetInputValue(ball, 3, Value(int64_t(16)));
            const NodeId box = b.Add("mesh.box", 0, 1.5f);
            graph.SetInputValue(box, 0, Value(Math::Vec3{ 6, 0, 0 }));
            graph.SetInputValue(box, 2, Value(int64_t(4)));
            const NodeId both = b.Add("mesh.join", 1, 0.5f);
            b.Link(ball, both, 0);
            b.Link(box, both, 0, true);

            const NodeId cut = b.Add("mesh.intersect", 1, 2);
            b.Link(ball, cut, 0);
            b.Link(box, cut, 1);
            const NodeId levels = b.Add("math.series", 1, 3.5f);   // 高度 −4.5、−3、…、4.5
            graph.SetInputValue(levels, 0, Value(-4.5));
            graph.SetInputValue(levels, 1, Value(1.5));
            graph.SetInputValue(levels, 2, Value(int64_t(7)));
            const NodeId contours = b.Add("mesh.section", 2, 3);
            b.Link(ball, contours, 0);
            b.Link(levels, contours, 1);
            const NodeId outline = b.Add("mesh.outline", 2, 1.5f);
            b.Link(box, outline, 0);
            graph.SetInputValue(outline, 1, Value(Math::Vec3{ -1, -1, -1 }));
        }
    }

    const std::vector<ExampleGraph>& Examples()
    {
        static const std::vector<ExampleGraph> list = {
            { "window", "参数化窗框", "拉伸外框 + 两个空心拉伸洞口，形体合并；改“参数”表里的宽、高、框宽、厚度、中梃高", BuildWindow },
            { "roof", "曲面屋顶", "马鞍面上的插值曲线 → 网络曲面 → 加厚 → 差集开天窗", BuildRoof },
            { "meshExport", "网格化导出", "长方体与球求交 → 网格化 → 网格截面等高线；烘焙后网格可导出为 DWG / DXF 多面网格", BuildMeshExport },
            { "meshCurves", "网格截面与求交", "网格球与网格长方体的交线、球的等高线、长方体的轮廓线（不需要实体内核）", BuildMeshCurves, false },
        };
        return list;
    }

    const ExampleGraph* FindExample(const std::string& id)
    {
        for (const auto& e : Examples())
            if (id == e.Id)
                return &e;
        return nullptr;
    }

    std::string ExampleToString(const ExampleGraph& e)
    {
        Graph g;
        e.Build(g);
        std::vector<NodeId> ids;
        for (const auto& n : g.GetNodes())
            ids.push_back(n->Id);
        return g.CopyToString(ids);
    }
}
