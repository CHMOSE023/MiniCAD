// ── 多线（MLINE）的 DWG/DXF 导入与导出 ──────────────────────────────────
//
// 用户报「多线不能正常的读取」。
//
// 根因：`CadExchange.cpp` 里**完全没有 MLINE 的映射** ——
// MiniDWG 库本身有 `Dwg::MLine` / `Dwg::MLineStyle` 类型，
// 但导入的 `Convert` 里没有对应分支，于是多线走到 `Skip()` 被静默丢弃，
// 标着「不支持的实体 MLINE × N 已跳过」。导出侧同样缺失，另存为后也会丢。
//
// 本文件守住：
//   ① 样例图纸里的 MLINE 能导入，几何（路径 / 元素线 / 缩放 / 包围盒）合理
//   ② 导入 → 导出 → 再导入，多线数量与几何一致（往返不丢）
//
// DXF 的 MLINE 结构（实测 sample_AC1015_ascii.dxf）：
//   72 = 顶点数、73 = 元素线数；每个顶点后跟 11/21/31 位置、12/22/32 方向、
//   13/23/33 斜接，再是每条元素线一段（74 参数个数、41×n 参数、75 面积填充）。
//   组码 41 的第一个值是**元素线相对中心线的偏移距离**（样例 0.75 / -0.75）。
#include "Document/Document.h"
#include "Scene/Scene.h"
#include "Scene/MLineStyleTable.h"
#include "Core/Entity/MLineEntity.hpp"
#include "Core/Draw/IDrawSink.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <utility>
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

    // 记录多线画出的线段（用来验证交叉断开）
    struct RecSink : IDrawSink
    {
        std::vector<std::pair<Math::Point3, Math::Point3>> segs;
        void DrawLine(const Math::Point3& a, const Math::Point3& b,
                      const Math::Color4&, bool) override { segs.emplace_back(a, b); }
        Math::Color4 GetLayerColor(LayerID) const override { return Math::Color4::White(); }
        bool IsLayerVisible(LayerID) const override { return true; }
    };

    struct MLineInfo
    {
        size_t PathPoints = 0;
        size_t Elements   = 0;
        double Scale      = 0.0;
        AABB   Box;
        int    Count      = 0;
    };

    MLineInfo ScanMLines(Scene& s)
    {
        MLineInfo info;
        info.Box = AABB::Empty();
        s.ForEachObject([&](const Object& o)
        {
            const auto* ml = dynamic_cast<const MLineEntity*>(&o);
            if (ml == nullptr) return;
            ++info.Count;
            const auto geo = ml->ComputeGeometry();
            if (info.Count == 1)                 // 记录第一条的细节
            {
                info.PathPoints = geo.Path.size();
                info.Elements   = geo.Elements.size();
                info.Scale      = ml->GetScale();
                info.Box        = ml->GetBoundingBox();
            }
        });
        return info;
    }

    std::filesystem::path WritableDir()
    {
        std::error_code ec;
        for (const std::filesystem::path& cand :
             { std::filesystem::temp_directory_path(ec), std::filesystem::current_path(ec) })
        {
            if (cand.empty()) continue;
            const std::filesystem::path probe = cand / "minicad_mline_probe.tmp";
            {
                std::ofstream out(probe, std::ios::binary | std::ios::trunc);
                if (!out) continue;
                out << "x";
                out.close();
            }
            ec.clear();
            std::filesystem::remove(probe, ec);
            if (!ec) return cand;
        }
        return {};
    }
}

int RunMLineImportTests()
{
    g_failures = 0;

    // ══ ① 样例图纸里的 MLINE 能导入 ══════════════════════════════════════
    //
    // ⚠ 路径不能用 std::filesystem::path 构造（含中文，会抛异常），
    //   也不能用 std::ifstream 探测 —— MSVC 窄字符流打不开中文路径。
    //   直接交给 Document::LoadFromFile（内部转宽字符）。
    const std::string sample = std::string(MINIDWG_SAMPLE_DIR) + "/sample_AC1015_ascii.dxf";
    {
        Document doc;
        std::string err;
        const bool loaded = doc.LoadFromFile(sample, &err);
        Check(loaded, "多线导入：样例图纸能导入");
        if (!loaded)
        {
            std::printf("    样例路径: %s（%s）\n", sample.c_str(), err.c_str());
            std::printf("多线导入用例失败：%d\n", g_failures);
            return g_failures;
        }

        Scene& scene = doc.GetScene();
        const MLineInfo info = ScanMLines(scene);
        std::printf("    多线 %d 条，首条：路径 %zu 点 / 元素 %zu 条 / 缩放 %.2f\n",
                    info.Count, info.PathPoints, info.Elements, info.Scale);

        Check(info.Count > 0, "多线导入：样例里的 MLINE 不再被跳过（此前是 0 条）");
        Check(info.PathPoints >= 2, "多线导入：路径至少 2 个点");
        Check(info.Elements >= 2, "多线导入：至少 2 条元素线（否则画不出多线）");
        Check(info.Scale > 0.0, "多线导入：缩放系数有效");

        // 几何合理：包围盒非空且尺寸有界（不是退化成一个点、也不是天文数字）
        const double w = info.Box.Max.x - info.Box.Min.x;
        const double h = info.Box.Max.y - info.Box.Min.y;
        std::printf("    首条包围盒 %.2f × %.2f\n", w, h);
        Check(w > 0.0 || h > 0.0, "多线导入：包围盒非退化");
        Check(w < 1e6 && h < 1e6, "多线导入：包围盒尺寸有界（没有坐标错乱）");

        // ══ ② 往返：导入 → 导出 → 再导入，多线不丢 ═══════════════════════
        //
        // 必须强制走「重新生成整张图纸」：格式版本不变时 SaveAs 走「以原图为底」，
        // 那是把旧文件字节写回，根本不经过新写的导出代码。
        const std::filesystem::path dir = WritableDir();
        Check(!dir.empty(), "多线往返：找到可写的临时目录");
        if (!dir.empty())
        {
            doc.SetCadSaveVersion(CadSaveVersion::R2018);
            const std::filesystem::path out = dir / "minicad_mline_roundtrip.dxf";
            const auto u8 = out.u8string();
            const std::string file(u8.begin(), u8.end());

            const bool saved = doc.SaveAs(file);
            Check(saved, "多线往返：能另存为 DXF");
            if (saved)
            {
                Document doc2;
                const bool loaded2 = doc2.LoadFromFile(file);
                Check(loaded2, "多线往返：往返文件能读回");
                if (loaded2)
                {
                    const MLineInfo after = ScanMLines(doc2.GetScene());
                    std::printf("    往返后：多线 %d 条，首条 路径 %zu 点 / 元素 %zu 条 / 缩放 %.2f\n",
                                after.Count, after.PathPoints, after.Elements, after.Scale);

                    Check(after.Count == info.Count, "多线往返：数量一致（不丢失）");
                    Check(after.PathPoints == info.PathPoints, "多线往返：路径点数一致");
                    Check(after.Elements == info.Elements, "多线往返：元素线数一致");
                    Check(std::abs(after.Scale - info.Scale) < 1e-6, "多线往返：缩放一致");

                    const double w2 = after.Box.Max.x - after.Box.Min.x;
                    const double h2 = after.Box.Max.y - after.Box.Min.y;
                    std::printf("    往返前包围盒 %.4f × %.4f，往返后 %.4f × %.4f\n", w, h, w2, h2);
                    Check(std::abs(w2 - w) < 1e-3 && std::abs(h2 - h) < 1e-3,
                          "多线往返：包围盒一致（坐标没有走样）");
                }
                std::error_code ec;
                std::filesystem::remove(out, ec);
            }
        }
    }

    // ══ ③ 其他格式的样例也应有 MLINE（确认不是只对一个文件生效）══════════
    for (const char* name : { "sample_AC1018_ascii.dxf", "sample_AC1032_ascii.dxf" })
    {
        Document doc;
        std::string err;
        if (!doc.LoadFromFile(std::string(MINIDWG_SAMPLE_DIR) + "/" + name, &err))
            continue;      // 样例缺失时跳过（不算失败）
        const MLineInfo info = ScanMLines(doc.GetScene());
        std::printf("    %s：多线 %d 条\n", name, info.Count);
        Check(info.Count > 0, "多线导入：多版本样例都能读到 MLINE");
    }

    // ══ ④ T 形 / 十字接头：DWG 里的接头端头推移必须保留 ═══════════════════
    //
    // 用户报「sample_AC1032.dwg 的多线相交情况没有正常显示」。
    //
    // 根因：MLINE 在文件里不只存中心线路径，还存**每顶点每元素线的位置参数**
    // （组码 41）。这些参数带着 AutoCAD 已经算好的斜接信息 —— 尤其是被 MLEDIT
    // 处理过的接头，端头会沿路径方向被推出若干距离。
    //
    // 实测 sample_AC1032.dwg：
    //   #2 竖线 中心线 x=132.547，元素线在 x=132.547 / 134.678（宽度 2.131）
    //   #3 横线 起点参数 = 0.7991(横向) + 2.1309(沿路径推出)
    //          ⇒ 左端元素线落在 x = 132.547 + 2.1309 = **134.678**
    //          正好停在竖线的内侧元素线上 —— 这就是 T 形接头。
    //
    // 旧实现只用「路径 + 样式偏移」重算，丢掉了 2.1309 这个推移，
    // 横线左端停在 x=132.547（竖线中心线），看起来就是直接穿透。
    {
        Document doc;
        std::string err;
        if (doc.LoadFromFile(std::string(MINIDWG_SAMPLE_DIR) + "/sample_AC1032.dwg", &err))
        {
            // 收集所有多线的几何
            struct ML { size_t PathN; std::vector<std::vector<Math::Point3>> Elems; AABB Box; bool HasParams; };
            std::vector<ML> all;
            doc.GetScene().ForEachObject([&](const Object& o)
            {
                const auto* ml = dynamic_cast<const MLineEntity*>(&o);
                if (!ml) return;
                const auto g = ml->ComputeGeometry();
                all.push_back({ g.Path.size(), g.Elements, ml->GetBoundingBox(), ml->HasVertexParams() });
            });

            std::printf("    AC1032 多线 %zu 条\n", all.size());
            Check(!all.empty(), "多线接头：AC1032 能读到多线");
            Check(all.empty() || all[0].HasParams,
                  "多线接头：导入时保留了 DWG 的每顶点参数（斜接信息）");

            // 找那条「横线」：包围盒扁长、且左端被推出（起点 x 大于路径点 x）
            bool foundTee = false;
            for (const auto& m : all)
            {
                if (m.PathN != 2 || m.Elems.size() < 2) continue;
                // 横线：宽 > 高
                const double w = m.Box.Max.x - m.Box.Min.x;
                const double h = m.Box.Max.y - m.Box.Min.y;
                if (!(w > h * 2.0)) continue;
                // 两条元素线的起点 x 应相同且被推出到 134.678 附近
                const double x0 = m.Elems[0].front().x;
                const double x1 = m.Elems[1].front().x;
                std::printf("    横线左端元素线 x = %.3f / %.3f（包围盒 X 起点 %.3f）\n",
                            x0, x1, m.Box.Min.x);
                // 关键：元素线左端必须明显大于包围盒最小 x 之外的「中心线」起点，
                // 且落在竖线内侧 134.678 附近
                if (std::abs(x0 - 134.678) < 0.05 && std::abs(x1 - 134.678) < 0.05)
                    foundTee = true;
            }
            std::printf("    横向多线找到 T 形接头端头：%s\n", foundTee ? "是" : "否");
            Check(foundTee, "多线接头：横线左端停在竖线内侧边（T 形接头正确，不再穿透）");
        }
    }

    // ══ ⑤ 两条多线交叉：被穿过的那条要在交叉处断开 ═══════════════════════
    //
    // 用户反馈「多线相交情况没有正常显示」—— 对照 CAD 截图，我们的竖线是通的，
    // 而 CAD 里竖线内侧那条元素线在横线穿过处断开了。
    //
    // DWG 里存了这个信息：每顶点每元素线的参数 [2]、[3] 是**断开区间**端点
    // （从该顶点沿路径方向起算的距离）。实测 sample_AC1032.dwg 的竖线：
    //   元素线1 参数 = -2.1309, 0.0, 3.4628, 5.0610
    //   37.5406 + 3.4628 = 41.003 ≈ 横线下元素线 y
    //   37.5406 + 5.0610 = 42.602 ≈ 横线上元素线 y
    // ⇒ 竖线内侧线在 41.003~42.602 之间不画，正好留出横线的宽度。
    {
        Document doc;
        std::string err;
        if (doc.LoadFromFile(std::string(MINIDWG_SAMPLE_DIR) + "/sample_AC1032.dwg", &err))
        {
            // 找那条竖线（路径 2 点、包围盒窄高），记录它实际画出的线段
            bool foundBreak = false;
            std::vector<std::pair<Math::Point3, Math::Point3>> segs;
            doc.GetScene().ForEachObject([&](const Object& o)
            {
                const auto* ml = dynamic_cast<const MLineEntity*>(&o);
                if (!ml) return;
                const auto g = ml->ComputeGeometry();
                if (g.Path.size() != 2) return;
                const AABB box = ml->GetBoundingBox();
                const double w = box.Max.x - box.Min.x;
                const double h = box.Max.y - box.Min.y;
                if (!(h > w * 2.0)) return;      // 竖线：高远大于宽

                RecSink sink;
                ml->Draw(sink, false, false);
                segs = sink.segs;
            });

            std::printf("    竖线绘制出 %zu 段（未断开时是 2 段）\n", segs.size());
            Check(segs.size() > 2, "多线交叉：竖线被断开成多段（此前是整条通到底）");

            // 关键：画出的线段之间应存在缺口，且缺口两端落在横线的两条元素线上
            // （41.003 与 42.602 附近）。
            //
            // 注意筛选方式：断开后竖线会是 3 段 —— 下半段、缺口、上半段。
            // 报两个端点之间比「相邻段」更直接：把所有线段的 y 端点排序，
            // 找相邻两个端点之间**宽度最接近横线宽度（约 1.6）**的间隙。
            std::vector<double> ys;
            for (const auto& s : segs)
            {
                ys.push_back(s.first.y);
                ys.push_back(s.second.y);
            }
            std::sort(ys.begin(), ys.end());
            double gapLo = 0.0, gapHi = 0.0, best = 1e300;
            for (size_t i = 0; i + 1 < ys.size(); ++i)
            {
                const double gap = ys[i + 1] - ys[i];
                // 缺口宽度应接近横线的总宽（元素线 41.003..42.602 ≈ 1.599）
                if (gap > 1e-6 && std::abs(gap - 1.599) < best)
                {
                    best  = std::abs(gap - 1.599);
                    gapLo = ys[i];
                    gapHi = ys[i + 1];
                }
            }
            std::printf("    缺口 y ∈ [%.3f, %.3f]，宽 %.3f\n", gapLo, gapHi, gapHi - gapLo);
            Check(gapHi - gapLo > 0.5, "多线交叉：缺口确实存在（不是整条连续的）");
            Check(std::abs(gapLo - 41.003) < 0.05 && std::abs(gapHi - 42.602) < 0.05,
                  "多线交叉：缺口两端正好落在横线的两条元素线上");

            if (!segs.empty())
                foundBreak = true;
            Check(foundBreak, "多线交叉：找到并验证了 AC1032 的交叉断开");
        }
    }

    std::printf("多线导入用例失败：%d\n", g_failures);
    return g_failures;
}
