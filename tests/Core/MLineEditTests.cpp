// ── 多线编辑（MLEDIT）与多线样式 ────────────────────────────────────────
//
// 用户要求：多线相交**不自动处理**，由 MLEDIT 命令让用户自己选方式处理，
// 逻辑与 CAD 一致。上一轮实现的「自动挖缺口」已移除（见 MLineToolTests 的说明）。
//
// 本文件守住四组：
//   ① 交叉判断：两条多线中心线的交点求对
//   ② 各种编辑方式产出的路径正确（十字打断 / T形 / 拐角 / 顶点断开 / 顶点结合）
//   ③ 多线样式表：增删改、Standard 保护、改样式不影响已有多线（快照语义）
//   ④ 命令能启动、且启动后能继续输入选项字母（用户报「输入 MLEDIT 后无法输入 C」）
#include "Editor/Editor.h"
#include "Editor/Input/InputEvent.h"
#include "Editor/MLineEdit.hpp"
#include "Document/Document.h"
#include "Viewport/Viewport.h"
#include "Scene/Scene.h"
#include "Scene/MLineStyleTable.h"
#include "Core/Entity/MLineEntity.hpp"
#include "Serialization/EntityIO.h"
#include "Serialization/JsonSerializer.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

using namespace MiniCAD;

namespace
{
    int g_failures = 0;
    void Check(bool ok, const char* what)
    {
        std::printf("[%s] %s\n", ok ? "通过" : "失败", what);
        if (!ok) ++g_failures;
    }

    std::vector<Math::Point3> Path(std::initializer_list<std::pair<double, double>> pts)
    {
        std::vector<Math::Point3> v;
        for (const auto& p : pts)
            v.push_back({ p.first, p.second, 0.0 });
        return v;
    }

    double Length(const std::vector<Math::Point3>& p)
    {
        double t = 0.0;
        for (size_t i = 0; i + 1 < p.size(); ++i)
        {
            const double dx = p[i + 1].x - p[i].x, dy = p[i + 1].y - p[i].y;
            t += std::sqrt(dx * dx + dy * dy);
        }
        return t;
    }
}

int RunMLineEditTests()
{
    g_failures = 0;

    // ══ ① 交叉判断 ══════════════════════════════════════════════════════
    {
        // 横线 y=0 从 -100 到 100，竖线 x=0 从 -50 到 50 ⇒ 交点 (0,0)
        const auto a = Path({ { -100, 0 }, { 100, 0 } });
        const auto b = Path({ { 0, -50 }, { 0, 50 } });
        const auto c = MLineEdit::Intersect(a, b);
        std::printf("    交点 (%.2f, %.2f)\n", c.Point.x, c.Point.y);
        Check(c.Valid, "多线编辑：两条交叉的多线能求出交点");
        Check(std::abs(c.Point.x) < 1e-9 && std::abs(c.Point.y) < 1e-9,
              "多线编辑：交点坐标正确 (0,0)");
        Check(std::abs(c.TA - 0.5) < 1e-9 && std::abs(c.TB - 0.5) < 1e-9,
              "多线编辑：两条线上的参数都是 0.5（各自中点）");

        // 不相交（平行）
        const auto p1 = Path({ { 0, 0 }, { 100, 0 } });
        const auto p2 = Path({ { 0, 10 }, { 100, 10 } });
        Check(!MLineEdit::Intersect(p1, p2).Valid, "多线编辑：平行多线判为不相交");

        // 不相交（延长才相交）
        const auto s1 = Path({ { 0, 0 }, { 10, 0 } });
        const auto s2 = Path({ { 50, -10 }, { 50, 10 } });
        Check(!MLineEdit::Intersect(s1, s2).Valid, "多线编辑：延长线才相交的判为不相交");
    }

    // ══ ② 各编辑方式 ════════════════════════════════════════════════════
    {
        // 在路径的第 0 段、t=0.5 处插点：(-100,0)→(100,0) 变成 3 个点，中间是 (0,0)
        const auto a = Path({ { -100, 0 }, { 100, 0 } });
        const auto inserted = MLineEdit::InsertPointAt(a, 0, 0.5);
        std::printf("    插入后点数 %zu，中间点 (%.1f, %.1f)\n",
                    inserted.size(), inserted[1].x, inserted[1].y);
        Check(inserted.size() == 3, "多线编辑：能在交点处插入顶点");
        Check(std::abs(inserted[1].x) < 1e-9 && std::abs(inserted[1].y) < 1e-9,
              "多线编辑：插入的顶点就在交点位置");

        // 顶点断开（开放多线）：4 点在 index=1 断开 → 两段各 2 点
        const auto line4 = Path({ { 0, 0 }, { 10, 0 }, { 20, 0 }, { 30, 0 } });
        const auto sp = MLineEdit::SplitAtVertex(line4, 1, false);
        Check(sp.Valid, "多线编辑：开放多线能在中间顶点断开");
        Check(sp.First.size() == 2 && sp.Second.size() == 3,
              "多线编辑：断开后两段共享断点（2 点 + 3 点）");

        // 端点不能断
        Check(!MLineEdit::SplitAtVertex(line4, 0, false).Valid,
              "多线编辑：端点处不能断开");

        // 顶点结合：两条共端点的线接成一条
        const auto l1 = Path({ { 0, 0 }, { 10, 0 } });
        const auto l2 = Path({ { 10, 0 }, { 20, 0 } });
        const auto joined = MLineEdit::JoinPaths(l1, l2);
        std::printf("    结合后点数 %zu，总长 %.2f\n", joined.size(), Length(joined));
        Check(joined.size() == 3, "多线编辑：共端点的两条接成一条（去掉了重复点）");
        Check(std::abs(Length(joined) - 20.0) < 1e-9, "多线编辑：结合后总长 = 两条之和");

        // 结合：方向相反也要接对（B 反向）
        const auto l3 = Path({ { 20, 0 }, { 10, 0 } });
        const auto j2 = MLineEdit::JoinPaths(l1, l3);
        Check(std::abs(Length(j2) - 20.0) < 1e-9, "多线编辑：端点接法自动选方向（B 反向也对）");

        // 拐角：各取一段接起来，应形成一个 L 形（总长 = 两段长度之和）
        const auto h = Path({ { -100, 0 }, { 0, 0 }, { 100, 0 } });
        const auto v = Path({ { 0, -100 }, { 0, 0 }, { 0, 100 } });
        const auto headH = MLineEdit::HeadPath(h, 1);      // (-100,0) → (0,0)
        const auto headV = MLineEdit::HeadPath(v, 1);      // (0,-100) → (0,0)
        const auto corner = MLineEdit::JoinPaths(headH, headV);
        std::printf("    拐角点数 %zu，总长 %.2f\n", corner.size(), Length(corner));
        Check(corner.size() >= 3, "多线编辑：拐角结合产出至少 3 个点");
        Check(std::abs(Length(corner) - 200.0) < 1e-6,
              "多线编辑：拐角总长 = 两段直段之和（200）");

        // HeadPath / TailPath 互补：拼起来应还原原路径长度
        const auto t = MLineEdit::TailPath(h, 1);
        Check(std::abs(Length(headH) + Length(t) - Length(h)) < 1e-9,
              "多线编辑：Head + Tail 总长等于原路径");
    }

    // ══ ③ 多线样式表 ════════════════════════════════════════════════════
    {
        MLineStyleTable table;
        Check(table.Records().size() == 1, "多线样式：默认只有 Standard 一条");
        Check(table.Records()[0].Elements.size() == 2, "多线样式：Standard 有 2 条元素线");

        // 新建：5 条元素线的墙体样式
        MLineStyleRecord wall;
        wall.Name = "墙体";
        wall.Elements = { { 5.0, false, {} }, { 4.5, false, {} }, { 0.0, false, {} },
                          { -4.5, false, {} }, { -5.0, false, {} } };
        wall.Normalize();
        wall.StartCap = wall.EndCap = true;
        const MLineStyleID id = table.Add(wall);
        Check(id != MLineStyleTable::InvalidID, "多线样式：能新建样式");
        Check(table.Records().size() == 2, "多线样式：新建后共 2 条");

        // 元素线按偏移从大到小
        const auto& rec = table.Resolve(id);
        Check(rec.Elements.front().Offset > rec.Elements.back().Offset,
              "多线样式：元素线按偏移从大到小排序");
        Check(rec.StartCap && rec.EndCap, "多线样式：封口标志保存正确");

        // 重名 / 空名拒绝
        MLineStyleRecord dup;
        dup.Name = "墙体";
        dup.Elements = { { 0.5, false, {} }, { -0.5, false, {} } };
        Check(table.Add(dup) == MLineStyleTable::InvalidID, "多线样式：重名样式被拒绝");

        MLineStyleRecord empty;
        empty.Name = "空的";
        Check(table.Add(empty) == MLineStyleTable::InvalidID,
              "多线样式：没有元素线的样式被拒绝");

        // 改名
        Check(table.Rename(id, "墙线"), "多线样式：能重命名");
        Check(table.FindByName("墙线") == id, "多线样式：改名后能按新名找到");

        // 修改
        MLineStyleRecord edit = table.Resolve(id);
        edit.Joints = true;
        Check(table.Update(id, edit), "多线样式：能修改");
        Check(table.Resolve(id).Joints, "多线样式：修改已生效");

        // Standard 不能删
        Check(!table.Remove(MLineStyleTable::StandardID), "多线样式：Standard 不能删除");
        Check(table.Remove(id), "多线样式：普通样式能删除");
        Check(table.Records().size() == 1, "多线样式：删除后只剩 Standard");
    }

    // ══ ④ 实体持有样式快照：改样式表不影响已画的多线（CAD 语义）══════════
    {
        Scene scene;
        const auto& style0 = scene.GetMLineStyleTable().Resolve(MLineStyleTable::StandardID);
        MLineEntity ml(1, Path({ { 0, 0 }, { 100, 0 } }), style0,
                       MLineJustify::Zero, 1.0, false);
        const auto geo0 = ml.ComputeGeometry();
        const size_t elems0 = geo0.Elements.size();

        // 改 Standard 样式（加一条元素线）
        MLineStyleRecord edit = scene.GetMLineStyleTable().Resolve(MLineStyleTable::StandardID);
        edit.Elements.push_back({ 0.0, false, {} });
        scene.GetMLineStyleTable().Update(MLineStyleTable::StandardID, edit);

        const auto geo1 = ml.ComputeGeometry();
        std::printf("    改样式前元素 %zu 条，改后已有多线仍是 %zu 条\n",
                    elems0, geo1.Elements.size());
        Check(geo1.Elements.size() == elems0,
              "多线样式：改样式表不影响已画好的多线（实体持快照，与 CAD 一致）");

        // 显式 SetMLineStyle 才变
        ml.SetMLineStyle(scene.GetMLineStyleTable().Resolve(MLineStyleTable::StandardID));
        const auto geo2 = ml.ComputeGeometry();
        Check(geo2.Elements.size() == elems0 + 1,
              "多线样式：显式套用样式后已有多线才更新");
    }

    // ══ ④ 命令能启动、启动后能继续输入选项字母 ══════════════════════════
    //
    // 用户报「输入多线编辑命令后，无法继续输入 C」。
    // 根因：命令是全名 `MLineEdit`，但用户按 AutoCAD 习惯输入 `MLEDIT`，
    // 而当时**没注册 MLEDIT 别名** ⇒ 命令行只回「未知命令」，工具根本没启动；
    // 接着敲 C 自然没反应。
    //
    // 这里守住完整链路：输入 MLEDIT → 工具启动 → 按 C 切换到「十字交叉」。
    {
        Document doc;
        Viewport viewport(1000.f, 800.f);
        Editor   editor;
        editor.Bind(doc, viewport);

        // 用命令行输入（用户的实际操作，不是直接调 ActivateToolById）
        editor.RunCommand("MLEDIT");
        editor.Render();          // 提示由 Render 写进命令行

        Check(editor.IsActiveTool(), "多线编辑命令：输入 MLEDIT 能启动工具");
        Check(editor.GetLastCommand() == "MLineEdit", "多线编辑命令：LastCommand 记为 MLineEdit");

        const std::string prompt0 = editor.GetCmdLine().Prompt();
        std::printf("    启动后提示：%s\n", prompt0.c_str());
        Check(prompt0.find("选择编辑方式") != std::string::npos,
              "多线编辑命令：提示要求选择编辑方式");

        // 按 C（十字交叉）
        InputEvent c = {};
        c.Type = InputEventType::KeyDown;
        c.Key  = KeyCode::C;
        const bool handled = editor.OnInput(c);
        editor.Render();

        const std::string prompt1 = editor.GetCmdLine().Prompt();
        std::printf("    按 C 后提示：%s\n", prompt1.c_str());
        Check(handled, "多线编辑命令：按 C 被工具接收（不再「没反应」）");
        Check(prompt1.find("十字交叉") != std::string::npos,
              "多线编辑命令：按 C 后切到「十字交叉」，提示变成请点选多线");

        // 其余四个方式键同样应生效（各自重启一次工具，因为一条命令只做一次编辑）
        struct KindCase { KeyCode key; const char* name; };
        const KindCase cases[] = {
            { KeyCode::T, "T形相交" }, { KeyCode::R, "拐角结合" },
            { KeyCode::O, "顶点断开" }, { KeyCode::M, "顶点结合" },
        };
        for (const auto& kc : cases)
        {
            editor.RunCommand("MLEDIT");
            editor.Render();
            InputEvent k = {};
            k.Type = InputEventType::KeyDown;
            k.Key  = kc.key;
            editor.OnInput(k);
            editor.Render();
            const std::string p = editor.GetCmdLine().Prompt();
            const bool ok = p.find(kc.name) != std::string::npos;
            if (!ok) std::printf("    %s 失败，提示=：%s\n", kc.name, p.c_str());
            Check(ok, (std::string("多线编辑命令：按 ") +
                       static_cast<char>('A' + (static_cast<int>(kc.key) - static_cast<int>(KeyCode::A))) +
                       " 切到「" + kc.name + "」").c_str());
        }

        // 顺带守住多线样式命令的别名
        editor.GetCmdLine().SetPrompt("");
        editor.RunCommand("MLSTYLE");
        Check(editor.GetMLineStyleRequest().Active, "多线样式命令：输入 MLSTYLE 能打开对话框");
        editor.CloseMLineStyle();
    }

    // ══ ⑤ MLEDIT 实际点选两条多线：五种方式的结果与撤销 ═══════════════════
    //
    // 横线 (-100,0)→(100,0)、竖线 (0,-100)→(0,100)，Standard 样式（偏移 ±0.5）× 比例 10 = 宽 10。
    {
        struct Fixture
        {
            Document doc;
            Viewport viewport{ 1000.f, 1000.f };
            Editor   editor;
            Fixture()
            {
                editor.Bind(doc, viewport);
                viewport.GetCamera().ZoomToBounds(-150, -150, 150, 150);
            }
            Object::ObjectID Add(std::vector<Math::Point3> path)
            {
                auto& scene = doc.GetScene();
                const auto id = scene.NextObjectID();
                scene.AddEntity(std::make_unique<MLineEntity>(id, std::move(path),
                    scene.GetMLineStyleTable().Resolve(MLineStyleTable::StandardID), MLineJustify::Zero, 10.0, false));
                return id;
            }
            void Key(KeyCode k)
            {
                InputEvent e = {};
                e.Type = InputEventType::KeyDown;
                e.Key  = k;
                editor.OnInput(e);
            }
            void Click(double x, double y)
            {
                editor.Render();
                const Math::Point2 sp = viewport.GetCamera().WorldToScreen(Math::Point3{ x, y, 0 });
                InputEvent e = {};
                e.MouseX = static_cast<int>(sp.x);
                e.MouseY = static_cast<int>(sp.y);
                e.Type = InputEventType::MouseMove;
                editor.OnInput(e);
                e.Type = InputEventType::MouseButtonDown;
                e.Button = MouseButton::Left;
                e.MouseButtons = static_cast<uint8_t>(MouseButtonState::Left);
                editor.OnInput(e);
                e.Type = InputEventType::MouseButtonUp;
                e.MouseButtons = 0;
                editor.OnInput(e);
            }
            std::vector<std::vector<Math::Point3>> Paths() const
            {
                std::vector<std::vector<Math::Point3>> out;
                auto ids = doc.GetScene().GetAllIDs();
                std::sort(ids.begin(), ids.end());
                for (auto id : ids)
                    if (const auto* ml = dynamic_cast<const MLineEntity*>(doc.GetScene().GetEntity(id)))
                        out.push_back(ml->GetVertices());
                return out;
            }
            bool HasPath(std::initializer_list<std::pair<double, double>> pts) const
            {
                const auto want = Path(pts);
                for (const auto& p : Paths())
                {
                    if (p.size() != want.size()) continue;
                    bool same = true;
                    for (size_t i = 0; i < p.size() && same; ++i)
                        same = std::abs(p[i].x - want[i].x) < 1e-6 && std::abs(p[i].y - want[i].y) < 1e-6;
                    if (same) return true;
                }
                return false;
            }
        };

        {
            Fixture f;
            f.Add(Path({ { -100, 0 }, { 100, 0 } }));
            f.Add(Path({ { 0, -100 }, { 0, 100 } }));
            f.editor.RunCommand("MLEDIT");
            f.Key(KeyCode::C);
            f.Click(-50, 5);      // 横线的上元素线
            f.Click(5, 50);       // 竖线的右元素线
            Check(f.Paths().size() == 3, "MLEDIT 十字交叉：第一条断成两条（共 3 条）");
            Check(f.HasPath({ { -100, 0 }, { -5, 0 } }) && f.HasPath({ { 5, 0 }, { 100, 0 } }),
                  "MLEDIT 十字交叉：缺口正好是竖线宽度（-5 ~ 5）");
            Check(f.HasPath({ { 0, -100 }, { 0, 100 } }), "MLEDIT 十字交叉：第二条不动");
            f.doc.GetCommandStack().Undo(f.doc.GetScene());
            Check(f.Paths().size() == 2 && f.HasPath({ { -100, 0 }, { 100, 0 } }), "MLEDIT 十字交叉：能撤销");
        }
        {
            Fixture f;
            f.Add(Path({ { -100, 0 }, { 100, 0 } }));
            f.Add(Path({ { 0, -100 }, { 0, 100 } }));
            f.editor.RunCommand("MLEDIT");
            f.Key(KeyCode::T);
            f.Click(5, -50);      // 竖线下半段：保留下半段
            f.Click(-50, 5);      // 横线
            Check(f.Paths().size() == 2 && f.HasPath({ { 0, -100 }, { 0, -5 } }),
                  "MLEDIT T形相交：竖线剪到横线近侧元素线（0,-5），保留点选的下半段");
            Check(f.HasPath({ { -100, 0 }, { 100, 0 } }), "MLEDIT T形相交：横线不动");
        }
        {
            Fixture f;
            f.Add(Path({ { -100, 0 }, { 100, 0 } }));
            f.Add(Path({ { 0, -100 }, { 0, 100 } }));
            f.editor.RunCommand("MLEDIT");
            f.Key(KeyCode::R);
            f.Click(-50, 5);      // 横线左半段
            f.Click(5, -50);      // 竖线下半段
            Check(f.Paths().size() == 1 && f.HasPath({ { -100, 0 }, { 0, 0 }, { 0, -100 } }),
                  "MLEDIT 拐角结合：两条接成一条 L 形，拐点在交点");
        }
        {
            Fixture f;
            f.Add(Path({ { -100, 0 }, { 0, 0 }, { 0, 100 } }));
            f.editor.RunCommand("MLEDIT");
            f.Key(KeyCode::O);
            f.Click(-5, 5);       // 靠近中间顶点
            Check(f.Paths().size() == 2 && f.HasPath({ { -100, 0 }, { 0, 0 } }) && f.HasPath({ { 0, 0 }, { 0, 100 } }),
                  "MLEDIT 顶点断开：在中间顶点处拆成两条");
        }
        {
            Fixture f;
            f.Add(Path({ { -100, 0 }, { -20, 0 } }));
            f.Add(Path({ { 20, 0 }, { 100, 0 } }));
            f.editor.RunCommand("MLEDIT");
            f.Key(KeyCode::M);
            f.Click(-50, 5);
            f.Click(50, 5);
            Check(f.Paths().size() == 1 && f.HasPath({ { -100, 0 }, { -20, 0 }, { 20, 0 }, { 100, 0 } }),
                  "MLEDIT 顶点结合：最近的两个端点接成一条");
        }
    }

    // ══ ⑥ 文件里的顶点参数：段长变了即作废，存 .mcad 时不写作废的参数 ═══════
    {
        const MLineStyleRecord std0 = MLineStyleTable().Resolve(MLineStyleTable::StandardID);   // 按值：临时表随即销毁
        MLineEntity ml(1, Path({ { 0, 0 }, { 100, 0 }, { 100, 50 } }), std0, MLineJustify::Zero, 1.0, false);
        MLineEntity::VertexParams vp(3, std::vector<std::vector<double>>(2, { 0.5, 0.0, 10.0, 12.0 }));
        ml.SetVertexParams(vp);
        Check(ml.HasVertexParams(), "多线顶点参数：设置后有效");

        ml.SetVertices(Path({ { 10, 10 }, { 110, 10 }, { 110, 60 } }));        // 平移：段长不变
        Check(ml.HasVertexParams(), "多线顶点参数：平移后仍有效");

        ml.SetVertices(Path({ { 0, 0 }, { 200, 0 }, { 200, 100 } }));          // 缩放：段长变了
        Check(!ml.HasVertexParams(), "多线顶点参数：缩放后作废，按样式重算");
        auto clone = ml.Clone(2);
        Check(!static_cast<MLineEntity&>(*clone).HasVertexParams(), "多线顶点参数：作废的参数复制后仍作废");

        JsonSerializer w;
        EntityIO::Write(w, ml);
        Check(w.Dump().find("vertexParams") == std::string::npos, "多线顶点参数：作废的参数不写进 .mcad");

        MLineEntity fresh(3, Path({ { 0, 0 }, { 100, 0 }, { 100, 50 } }), std0, MLineJustify::Zero, 1.0, false);
        fresh.SetVertexParams(vp);
        JsonSerializer w2;
        EntityIO::Write(w2, fresh);
        JsonSerializer r2;
        auto back = r2.Parse(w2.Dump()) ? EntityIO::Read(r2) : nullptr;
        const auto* bm = dynamic_cast<const MLineEntity*>(back.get());
        Check(bm && bm->HasVertexParams() && bm->GetVertexParams() == vp, "多线顶点参数：有效参数经 .mcad 往返不丢");
    }

    std::printf("多线编辑用例失败：%d\n", g_failures);
    return g_failures;
}