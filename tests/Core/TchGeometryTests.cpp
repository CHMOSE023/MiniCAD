// 天正对象显示几何的回归测试：以天正建筑 2014 + AutoCAD 2014 对样例图逐个分解（EXPLODE）的结果为基准。
// 期望数据由 tests/Core/tch_geomref.py 从分解日志生成（TchGeometryRef.inc）：
//   线段：分解出的 LINE / LWPOLYLINE 直线段按共线合并后的区间；圆、圆弧：圆心、半径与起止角；
//   文字：内容（UTF-8）、字高与 TEXTBOX 得到的文字框（中心、半宽、半高、转角）。
// 检查方法：期望的每条线段须被 MiniCAD 生成的共线线段覆盖（实心箭头按“底边中点 → 尖”的中线计入），
// 圆与圆弧须有对应项；文字须有内容相同（或互为子串，天正按中西文分段）、字高相近、对齐点落在文字框（外扩一个字高）内的项。
// 分解与显示本身不同、或 MiniCAD 已知的近似（见 docs/Tch-compatibility.md）在数据中标为 Known，不参与断言；
// MiniCAD 多画的线段数不得超过 ExtraAllowed。用 --report 运行时输出所有差异（生成脚本据此标注 Known）
#include "Import/CadExchange.h"
#include "Scene/Scene.h"
#include "Core/Entity/ArcEntity.hpp"
#include "Core/Entity/BlockEntity.hpp"
#include "Core/Entity/CircleEntity.hpp"
#include "Core/Entity/InsertEntity.hpp"
#include "Core/Entity/LineEntity.hpp"
#include "Core/Entity/MTextEntity.hpp"
#include "Core/Entity/PolylineEntity.hpp"
#include "Core/Entity/SolidEntity.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iterator>
#include <map>
#include <memory>
#include <numbers>
#include <string>
#include <vector>

using namespace MiniCAD;

namespace
{
    struct RefSeg { double X1, Y1, X2, Y2; bool Known = false; };
    struct RefCircle { double X, Y, R; bool Known = false; };
    struct RefArc { double X, Y, R, A0, A1; bool Known = false; };
    struct RefText { const char* Value; double X, Y, HalfW, HalfH, Rotation, Height; bool Known = false; };
    struct RefObject
    {
        const char* File;           // 相对于 tests/Data
        const char* Type;
        unsigned long long Handle;  // 原图句柄（块内对象为块定义中的句柄，坐标为块内坐标）；0：显示为普通实体（柱），按几何在普通实体中查找
        double Tol;
        std::vector<RefSeg> Segs;
        std::vector<RefCircle> Circles;
        std::vector<RefArc> Arcs;
        std::vector<RefText> Texts;
        int ExtraAllowed = 0;
    };

    // 期望数据含项目信息，不入库（.gitignore）；本地没有时跳过几何比对
#if __has_include("TchGeometryRef.inc")
#include "TchGeometryRef.inc"
#else
    const std::vector<RefObject> kRefObjects;
#endif

    // 天正显示为空、但必须恢复（生成空的显示块，不再报“已跳过”）的对象：天正 2014 分解结果为空
    struct EmptyObject { const char* File; unsigned long long Handle; const char* Why; };
    const EmptyObject kEmptyObjects[] = {
        {"TchArch/8#\xe6\xa5\xbc-B\xe7\xab\x8b\xe5\x89\x96\xe9\x9d\xa2.dwg", 0x5dee65, "TCH_PATH_ARRAY whose segments are all shorter than one pattern unit"},
        {"TchArch/8#\xe6\xa5\xbc-B\xe7\xab\x8b\xe5\x89\x96\xe9\x9d\xa2.dwg", 0x6b5d62, "TCH_ARROW without vertices (kernel TDbSymbArrow::IsValid is false)"},
    };

    int g_failed = 0;
    bool g_report = false;

    struct Seg { double X1, Y1, X2, Y2; };
    struct Txt { std::string Value; double X, Y, Height; };
    struct Geometry
    {
        std::vector<Seg> Segs;
        std::vector<RefCircle> Circles;
        std::vector<RefArc> Arcs;
        std::vector<Txt> Texts;
        bool Found = false;
    };

    // *U_TCH_<十进制句柄>（后缀 _ 为重名）：天正对象生成的显示块
    unsigned long long TchHandle(const std::string& name)
    {
        if (!name.starts_with("*U_TCH_")) return 0;
        std::string digits;
        for (char c : name.substr(7))
        {
            if (c >= '0' && c <= '9') digits += c;
            else if (!digits.empty()) break;
        }
        return digits.empty() ? 0 : std::stoull(digits);
    }

    struct Xf { double Ox, Oy, A, Sx, Sy; };

    void Flatten(const BlockEntity& block, const Xf& x, Geometry& g)
    {
        auto P = [&](double px, double py) {
            px = (px - block.GetBasePoint().x) * x.Sx;
            py = (py - block.GetBasePoint().y) * x.Sy;
            return std::pair{x.Ox + px * std::cos(x.A) - py * std::sin(x.A), x.Oy + px * std::sin(x.A) + py * std::cos(x.A)};
        };
        const bool uniform = std::abs(std::abs(x.Sx) - std::abs(x.Sy)) < 1e-9;
        auto angle = [&](double t) { return std::atan2(std::sin(t) * (x.Sy < 0 ? -1 : 1), std::cos(t) * (x.Sx < 0 ? -1 : 1)) + x.A; };
        auto addArc = [&](double cx, double cy, double r, double a0, double a1) {
            if (!uniform) return;       // 不等比缩放下为椭圆弧，分解结果也是 ELLIPSE，不比
            auto [wx, wy] = P(cx, cy);
            double b0 = angle(a0), b1 = angle(a1);
            if (x.Sx * x.Sy < 0) std::swap(b0, b1);
            g.Arcs.push_back({wx, wy, r * std::abs(x.Sx), b0, b1});
        };
        for (const auto& e : block.GetEntities())
        {
            if (const auto* l = dynamic_cast<const LineEntity*>(e.get()))
            {
                auto [x1, y1] = P(l->GetLine().Start.x, l->GetLine().Start.y);
                auto [x2, y2] = P(l->GetLine().End.x, l->GetLine().End.y);
                g.Segs.push_back({x1, y1, x2, y2});
            }
            else if (const auto* a = dynamic_cast<const ArcEntity*>(e.get()))
                addArc(a->GetArc().Center.x, a->GetArc().Center.y, a->GetArc().Radius, a->GetArc().StartAngle, a->GetArc().EndAngle);
            else if (const auto* c = dynamic_cast<const CircleEntity*>(e.get()))
            {
                if (!uniform) continue;
                auto [cx, cy] = P(c->GetCircle().Center.x, c->GetCircle().Center.y);
                g.Circles.push_back({cx, cy, c->GetCircle().Radius * std::abs(x.Sx)});
            }
            else if (const auto* pl = dynamic_cast<const PolylineEntity*>(e.get()))
            {
                const auto& p = pl->GetPolyline();
                for (std::size_t i = 0; i + 1 < p.Points.size(); ++i)
                {
                    const double b = i < p.Bulges.size() ? p.Bulges[i] : 0.0;
                    const auto& s = p.Points[i];
                    const auto& t = p.Points[i + 1];
                    const double d = std::hypot(t.x - s.x, t.y - s.y);
                    if (d < 1e-9) continue;
                    if (std::abs(b) < 1e-9)
                    {
                        auto [x1, y1] = P(s.x, s.y);
                        auto [x2, y2] = P(t.x, t.y);
                        g.Segs.push_back({x1, y1, x2, y2});
                        continue;
                    }
                    const double th = 4 * std::atan(b), r = d / 2 / std::sin(std::abs(th) / 2);
                    const double ux = (t.x - s.x) / d, uy = (t.y - s.y) / d;
                    const double h = r * std::cos(th / 2) * (b > 0 ? 1 : -1);
                    const double cx = (s.x + t.x) / 2 - uy * h, cy = (s.y + t.y) / 2 + ux * h;
                    double a0 = std::atan2(s.y - cy, s.x - cx), a1 = std::atan2(t.y - cy, t.x - cx);
                    if (b < 0) std::swap(a0, a1);
                    addArc(cx, cy, std::abs(r), a0, a1);
                }
            }
            else if (const auto* so = dynamic_cast<const SolidEntity*>(e.get()))
            {
                // 实心三角形箭头：按“最短边中点 → 对顶点”的中线计入，对应分解出的箭头多段线
                if (!so->IsTriangle()) continue;
                const auto& r = so->GetRectangle();
                const Math::Point3 v[3] = {r.P1, r.P2, r.P3};
                int k = 0;
                double best = 1e300;
                for (int i = 0; i < 3; ++i)
                {
                    const auto& a = v[(i + 1) % 3];
                    const auto& b = v[(i + 2) % 3];
                    const double len = std::hypot(b.x - a.x, b.y - a.y);
                    if (len < best) { best = len; k = i; }
                }
                const auto& a = v[(k + 1) % 3];
                const auto& b = v[(k + 2) % 3];
                auto [x1, y1] = P((a.x + b.x) / 2, (a.y + b.y) / 2);
                auto [x2, y2] = P(v[k].x, v[k].y);
                g.Segs.push_back({x1, y1, x2, y2});
            }
            else if (const auto* t = dynamic_cast<const MTextEntity*>(e.get()))
            {
                auto [px, py] = P(t->GetPosition().x, t->GetPosition().y);
                g.Texts.push_back({t->GetPlainText(), px, py, t->GetHeight() * std::abs(x.Sy)});
            }
            else if (const auto* in = dynamic_cast<const InsertEntity*>(e.get()); in && in->GetBlock())
            {
                auto [ox, oy] = P(in->GetPosition().x, in->GetPosition().y);
                Flatten(*in->GetBlock(), Xf{ox, oy, x.A + in->GetRotation(), x.Sx * in->GetScale().x, x.Sy * in->GetScale().y}, g);
            }
        }
    }

    // 场景中句柄 → 生成的显示几何（模型空间与块定义里的天正显示块都收集）。
    // 句柄 0 收集模型空间与各块定义中的普通多段线（柱显示为闭合多段线，不生成天正显示块），坐标为所在空间的坐标
    std::map<unsigned long long, Geometry> Collect(const Scene& scene)
    {
        std::map<unsigned long long, Geometry> out;
        auto visit = [&](const Entity& e) {
            if (dynamic_cast<const PolylineEntity*>(&e))
            {
                BlockEntity holder(0, "", Math::Point3{0, 0, 0});
                holder.AddEntity(e.Clone(0));
                Geometry& plain = out[0];
                plain.Found = true;
                Flatten(holder, Xf{0, 0, 0, 1, 1}, plain);
                return;
            }
            const auto* in = dynamic_cast<const InsertEntity*>(&e);
            if (!in || !in->GetBlock()) return;
            const auto h = TchHandle(in->GetBlock()->GetName());
            if (h == 0) return;
            Geometry& g = out[h];
            g.Found = true;
            Flatten(*in->GetBlock(), Xf{in->GetPosition().x, in->GetPosition().y, in->GetRotation(), in->GetScale().x, in->GetScale().y}, g);
        };
        scene.ForEachObject([&](const Object& o) { if (const auto* e = dynamic_cast<const Entity*>(&o)) visit(*e); });
        scene.GetBlockTable().ForEach([&](BlockID, const BlockEntity& block) {
            if (TchHandle(block.GetName()) != 0) return;
            for (const auto& e : block.GetEntities()) visit(*e);
        });
        return out;
    }

    // 线段 s 是否被集合 set 中共线的线段覆盖（缺口不超过 tol）
    bool Covered(const Seg& s, const std::vector<Seg>& set, double tol)
    {
        const double len = std::hypot(s.X2 - s.X1, s.Y2 - s.Y1);
        if (len < tol) return true;
        const double ux = (s.X2 - s.X1) / len, uy = (s.Y2 - s.Y1) / len;
        auto off = [&](double px, double py) { return -uy * (px - s.X1) + ux * (py - s.Y1); };
        auto par = [&](double px, double py) { return ux * (px - s.X1) + uy * (py - s.Y1); };
        std::vector<std::pair<double, double>> iv;
        for (const auto& o : set)
        {
            if (std::abs(off(o.X1, o.Y1)) > tol || std::abs(off(o.X2, o.Y2)) > tol) continue;
            double a = par(o.X1, o.Y1), b = par(o.X2, o.Y2);
            if (a > b) std::swap(a, b);
            iv.emplace_back(a, b);
        }
        std::sort(iv.begin(), iv.end());
        double cursor = 0;
        for (const auto& [a, b] : iv)
        {
            if (a > cursor + tol) break;
            cursor = std::max(cursor, b);
        }
        return cursor >= len - tol;
    }

    bool SameAngle(double a, double b) { return std::abs(std::remainder(a - b, 2 * std::numbers::pi)) < 0.01; }

    bool TextMatches(const RefText& r, const Txt& t, double tol)
    {
        const std::string a = r.Value, b = t.Value;
        if (b.empty() || (a.find(b) == std::string::npos && b.find(a) == std::string::npos)) return false;
        if (std::abs(t.Height - r.Height) > 0.08 * r.Height + tol) return false;
        const double c = std::cos(r.Rotation), s = std::sin(r.Rotation);
        const double dx = t.X - r.X, dy = t.Y - r.Y;
        const double lx = dx * c + dy * s, ly = -dx * s + dy * c;
        return std::abs(lx) <= r.HalfW + r.Height + tol && std::abs(ly) <= r.HalfH + r.Height + tol;
    }

    std::vector<std::uint8_t> ReadFile(const std::string& path)
    {
        const std::u8string u8(path.begin(), path.end());      // 路径含中文，按 UTF-8 解释
        std::ifstream f(std::filesystem::path(u8), std::ios::binary);
        return {std::istreambuf_iterator<char>(f), {}};
    }
}

int main(int argc, char** argv)
{
    for (int i = 1; i < argc; ++i) g_report |= std::strcmp(argv[i], "--report") == 0;
    std::map<std::string, std::map<unsigned long long, Geometry>> files;
    std::map<std::string, std::map<std::string, std::pair<int, int>>> summary;   // 文件 → 类型 → (对象数, 失败数)
    for (std::size_t oi = 0; oi < std::size(kRefObjects); ++oi)
    {
        const RefObject& r = kRefObjects[oi];
        auto it = files.find(r.File);
        if (it == files.end())
        {
            const auto data = ReadFile(std::string(MINICAD_TEST_DATA_DIR) + "/" + r.File);
            Scene scene;
            std::string error;
            if (data.empty() || !ImportCad(data, scene, &error))
            {
                std::printf("[FAIL] 无法读入 %s %s\n", r.File, error.c_str());
                ++g_failed;
                it = files.emplace(r.File, std::map<unsigned long long, Geometry>{}).first;
            }
            else it = files.emplace(r.File, Collect(scene)).first;
        }
        const auto gi = it->second.find(r.Handle);
        static const Geometry empty;
        const Geometry& g = gi == it->second.end() ? empty : gi->second;
        int missing = 0, extra = 0;
        auto miss = [&](const char* kind, std::size_t index, bool known) {
            if (g_report) std::printf("REPORT %zu %s %zu\n", oi, kind, index);
            if (!known) ++missing;
        };
        std::vector<Seg> refSegs;
        for (std::size_t i = 0; i < r.Segs.size(); ++i)
        {
            const Seg s{r.Segs[i].X1, r.Segs[i].Y1, r.Segs[i].X2, r.Segs[i].Y2};
            refSegs.push_back(s);
            if (!Covered(s, g.Segs, r.Tol)) miss("SEG", i, r.Segs[i].Known);
        }
        if (r.Handle != 0)      // 普通实体池里有大量无关线段，不统计多出的线段
            for (const auto& s : g.Segs)
                if (!Covered(s, refSegs, r.Tol)) ++extra;
        for (std::size_t i = 0; i < r.Circles.size(); ++i)
        {
            const auto& c = r.Circles[i];
            const bool ok = std::any_of(g.Circles.begin(), g.Circles.end(), [&](const RefCircle& m) {
                return std::abs(m.X - c.X) < r.Tol && std::abs(m.Y - c.Y) < r.Tol && std::abs(m.R - c.R) < r.Tol; });
            if (!ok) miss("CIRCLE", i, c.Known);
        }
        for (std::size_t i = 0; i < r.Arcs.size(); ++i)
        {
            const auto& a = r.Arcs[i];
            const bool ok = std::any_of(g.Arcs.begin(), g.Arcs.end(), [&](const RefArc& m) {
                return std::abs(m.X - a.X) < r.Tol && std::abs(m.Y - a.Y) < r.Tol && std::abs(m.R - a.R) < r.Tol &&
                       SameAngle(m.A0, a.A0) && SameAngle(m.A1, a.A1); });
            if (!ok) miss("ARC", i, a.Known);
        }
        for (std::size_t i = 0; i < r.Texts.size(); ++i)
        {
            const bool ok = std::any_of(g.Texts.begin(), g.Texts.end(), [&](const Txt& t) { return TextMatches(r.Texts[i], t, r.Tol); });
            if (!ok && g_report)
            {
                const auto& t = r.Texts[i];
                std::printf("DETAIL ref \"%s\" (%.1f, %.1f) h=%.2f\n", t.Value, t.X, t.Y, t.Height);
                for (const auto& m : g.Texts) std::printf("DETAIL my  \"%s\" (%.1f, %.1f) h=%.2f\n", m.Value.c_str(), m.X, m.Y, m.Height);
            }
            if (!ok) miss("TEXT", i, r.Texts[i].Known);
        }
        if (g_report) std::printf("REPORT %zu EXTRA %d\n", oi, extra);
        const bool ok = g.Found && missing == 0 && extra <= r.ExtraAllowed;
        if (!ok)
        {
            std::printf("[FAIL] %s %s %llx：%s缺 %d 项，多 %d 条线段（允许 %d）\n", r.File, r.Type, r.Handle,
                        g.Found ? "" : "没有生成显示几何，", missing, extra, r.ExtraAllowed);
            ++g_failed;
        }
        auto& s = summary[r.File][r.Type];
        ++s.first;
        s.second += ok ? 0 : 1;
    }
    for (const auto& e : kEmptyObjects)
    {
        auto it = files.find(e.File);
        if (it == files.end())
        {
            const auto data = ReadFile(std::string(MINICAD_TEST_DATA_DIR) + "/" + e.File);
            Scene scene;
            it = files.emplace(e.File, !data.empty() && ImportCad(data, scene) ? Collect(scene) : std::map<unsigned long long, Geometry>{}).first;
        }
        const auto gi = it->second.find(e.Handle);
        const bool ok = gi != it->second.end() && gi->second.Found && gi->second.Segs.empty() && gi->second.Texts.empty();
        std::printf("[%s] %llx: %s is restored as an empty display block\n", ok ? "PASS" : "FAIL", e.Handle, e.Why);
        g_failed += ok ? 0 : 1;
    }
    for (const auto& [file, types] : summary)
        for (const auto& [type, n] : types)
            std::printf("[%s] %s %s：%d 个对象与天正分解结果一致\n", n.second == 0 ? "PASS" : "FAIL", file.c_str(), type.c_str(), n.first - n.second);
    std::printf("天正显示几何回归：%d 项失败\n", g_failed);
    return g_failed;
}
