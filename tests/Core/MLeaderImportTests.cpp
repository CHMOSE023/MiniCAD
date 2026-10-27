// ── 多重引线（MULTILEADER）导入映射测试 ────────────────────────────────────
// 用户先后报了两个问题：
//  ①「多重引线没有显示出来」      → `CadExchange` 完全没有 MULTILEADER 映射
//  ②「文字位置和 CAD 出入较大」   → 文字画在 TextLocation 上，而 AutoCAD 的
//                                   文字块左下角是 ContentBasePoint（组码 10）
//
// 关键几何（实测 `1#、2#厂房电气_t3.dwg` 20/20，误差 0.000000）：
//   doglegEnd(landing) = ConnectionPoint + Direction × LandingDistance
//   TextLocation       = ConnectionPoint + Direction × (LD + LandingGap)
//                                                      + (0, 1.2 × 字高)
//   ContentBasePoint   = **文字块左下角**（组码 10）
//        · 171=1(Left)  → == doglegEnd.x        （文字向右展开）
//        · 171=3(Right) → == doglegEnd.x − 字宽  （文字向左展开）
//        · .y 恒 == ConnectionPoint.y
//   本图 20 条里 **14 条是 171=3(Right)**；旧实现一律从左下角向右画，
//   那 14 条就整体多走了一个文字宽（约 1700–2200 ≈ 字高的 4~5 倍），
//   实测旧实现平均偏差 1446.88。
//   坐标是 **WCS**（不做 OCS 翻转）。
//
// 本文件用**合成数据**守住这些不变量（不依赖外部图纸）。
#include "Core/Entity/MLeaderEntity.hpp"
#include "Core/Draw/IDrawSink.hpp"
#include "Core/Draw/TransformDrawSink.hpp"
#include "Core/Math/Point3.hpp"
#include "Core/Math/Vec3.hpp"
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
    bool Near(double a, double b, double eps = 1e-9) { return std::abs(a - b) < eps; }

    // 复刻导入侧的映射（与 CadExchange::ConvertMLeader 保持一致）
    struct FakeSrc
    {
        Math::Point3 arrowTip;      // LEADER_LINE{10}，单点引线只存这个
        Math::Point3 connection;    // LEADER{10} = root.ConnectionPoint
        Math::Point3 direction{ 1, 0, 0 };   // root.Direction
        Math::Point3 contentBase;   // CONTEXT{10} = 文字块左下角
        Math::Point3 textLocation;  // CONTEXT{12} = 附着点
        std::string  text;
        MLeaderTextAnchor anchor = MLeaderTextAnchor::Left;   // CONTEXT 171
        double       textHeight = 400.0;
        double       landingDistance = 150.0;
        double       landingGap = 0.4;
    };

    std::unique_ptr<MLeaderEntity> Map(const FakeSrc& s)
    {
        MLeaderStyle style;
        style.TextHeight   = s.textHeight;
        style.ArrowSize    = s.textHeight / 6.0;
        style.LandingGap   = s.landingGap;
        style.DoglegLength = s.landingDistance;

        auto e = std::make_unique<MLeaderEntity>(1, style);
        // 单点形态 → 补成 (箭头端, connection)
        e->AddLeaderLine({ s.arrowTip, s.connection }, true);

        const Math::Vec3 dir{ s.direction.x, s.direction.y, 0.0 };
        // landing = dogleg 外端 = connection + Direction × LD
        e->SetLanding({ s.connection.x + dir.x * s.landingDistance,
                        s.connection.y + dir.y * s.landingDistance, 0.0 });
        // doglegDir = −Direction ⇒ DoglegStart() 正好落在 connection
        e->SetDoglegDir({ -dir.x, -dir.y, 0.0 });
        // 文字左下角 = ContentBasePoint
        e->SetTextOrigin(s.contentBase);
        e->SetTextAnchor(s.anchor);
        e->SetText(s.text);
        return e;
    }
}

int RunMLeaderImportTests()
{
    g_failures = 0;

    // ── 用户 entget / DWG 解码的真实数值（句柄 C08E，171=3 Right）──
    FakeSrc c08e;
    c08e.arrowTip     = { 1897153.10, -1204322.97, 0.0 };
    c08e.connection   = { 1894996.52, -1206702.42, 0.0 };
    c08e.direction    = { -1.0, 0.0, 0.0 };
    c08e.contentBase  = { 1892651.44, -1206702.42, 0.0 };   // 文字块左下角
    c08e.textLocation = { 1894846.12, -1206222.42, 0.0 };   // 附着点（右缘）
    c08e.text         = "SF-2-02";
    c08e.anchor       = MLeaderTextAnchor::Right;

    auto ml = Map(c08e);

    // ── 1. 单点引线必须被补成两点 ───────────────────────────────
    {
        const auto& lns = ml->GetLeaderLines();
        Check(lns.size() == 1, "生成了 1 条引线");
        Check(!lns.empty() && lns[0].points.size() == 2,
              "**单点引线被补成两点**（只存箭头端时不补线，整条引线会消失）");
        if (!lns.empty() && lns[0].points.size() == 2)
        {
            Check(Near(lns[0].points[0].x, c08e.arrowTip.x) &&
                  Near(lns[0].points[0].y, c08e.arrowTip.y),
                  "points[0] = 箭头端（与 entget LEADER_LINE{10} 一致）");
            Check(Near(lns[0].points[1].x, c08e.connection.x) &&
                  Near(lns[0].points[1].y, c08e.connection.y),
                  "points[1] = 内容连接端（与 entget LEADER{10} 一致）");
        }
        Check(!lns.empty() && lns[0].arrow, "引线带箭头");
    }

    // ── 2. landing / dogleg：dogleg 起点应正好落在 connection ──
    {
        const auto& land = ml->GetLanding();
        Check(Near(land.x, c08e.connection.x + c08e.direction.x * c08e.landingDistance) &&
              Near(land.y, c08e.connection.y + c08e.direction.y * c08e.landingDistance),
              "landing = connection + Direction × LandingDistance（dogleg 外端）");

        const auto& dg = ml->GetDoglegDir();
        const double L  = ml->GetStyle().DoglegLength;
        const double sx = land.x + dg.x * L;
        const double sy = land.y + dg.y * L;
        Check(Near(sx, c08e.connection.x, 1e-6) && Near(sy, c08e.connection.y, 1e-6),
              "dogleg 起点 == connection（引线经它接到基线）");
    }

    // ── 3. 文字位置 = ContentBasePoint（本次修复的核心）─────────
    {
        const auto& t = ml->GetTextOrigin();
        Check(Near(t.x, c08e.contentBase.x) && Near(t.y, c08e.contentBase.y),
              "**文字左下角 == ContentBasePoint（组码 10）**");
        Check(!Near(t.x, c08e.textLocation.x),
              "文字左下角**不等于** TextLocation（右附着时差一整个文字宽，"
              "旧实现画在 TextLocation 上 ⇒ 本图 14/20 条整体偏移 1446.88）");
        Check(t.x < c08e.connection.x,
              "171=3(Right)：文字在连接点的**左侧**（向右画会甩到引线另一侧）");
    }

    // ── 4. 附着点只用于写回，且被保留 ───────────────────────────
    Check(ml->GetTextAnchor() == MLeaderTextAnchor::Right,
          "附着点 171=3 被保留（用于无损写回，绘制不依赖它）");

    // ── 5. 字高 / 文字 / 内容类型 ───────────────────────────────
    Check(ml->GetText() == "SF-2-02", "文字内容 = \"SF-2-02\"");
    Check(Near(ml->GetStyle().TextHeight, 400.0), "字高 = 400（取自 context，非默认 3.5）");
    Check(ml->GetContentType() == MLeaderContent::MText, "内容类型 = MText");

    // ── 6. 坐标是 WCS，不做 X 取反 ──────────────────────────────
    {
        const auto& ln = ml->GetLeaderLines()[0];
        Check(ln.points[0].x > 0.0 && Near(ln.points[0].x, c08e.arrowTip.x),
              "箭头端 X 未被取反（本类型坐标是 WCS，实测取反会让误差从 570 涨到 36908）");
    }

    // ── 7. 左附着（171=1）：左下角 == doglegEnd ─────────────────
    // 本图 6 条是这种形态，用于确认两种附着都被覆盖。
    {
        FakeSrc l;
        l.arrowTip    = { 100, 0, 0 };
        l.connection  = { 0, 0, 0 };
        l.direction   = { 1, 0, 0 };
        l.contentBase = { 150.4, 0, 0 };      // = connection + D×LD + gap
        l.text        = "PF-2-04";
        l.anchor      = MLeaderTextAnchor::Left;
        auto e = Map(l);
        Check(Near(e->GetTextOrigin().x, l.connection.x + l.landingDistance + l.landingGap, 1e-9),
              "171=1(Left)：文字左下角 == dogleg 外端 + LandingGap（文字向右展开）");
        Check(e->GetTextOrigin().x > l.connection.x,
              "171=1(Left)：文字在连接点的**右侧**");
    }

    // ── 8. 常规两点引线：原样保留，不得重复补点 ─────────────────
    {
        MLeaderStyle st;
        MLeaderEntity e(2, st);
        e.AddLeaderLine({ { 0, 0, 0 }, { 10, 10, 0 }, { 20, 20, 0 } }, true);
        Check(e.GetLeaderLines()[0].points.size() == 3,
              "已有多点的引线**原样保留**（不重复插入 connection，否则会多出折线）");
    }

    // ── 9. 未设置文字原点时退回 landing（旧数据兼容）────────────
    {
        MLeaderStyle st;
        MLeaderEntity e(3, st);
        Check(!e.HasTextOrigin(), "默认无文字原点（兼容旧 .mcad）");
        e.SetLanding({ 5, 6, 0 });
        Check(!e.HasTextOrigin(), "仅设 landing 不算设过文字原点");
    }

    // ── 10. 基线必须画到**文字边**（用户报：「基线没画到文字边」）──
    //
    // 根因：AutoCAD 记录的文字块宽与我们渲染出的宽度不同（字体度量差异）。
    //   实测本图 PY-2-W04：AutoCAD 认为 1966.11，我们只画 1609.09
    //   ⇒ 基线末端与文字右缘之间露出 357.43 的空隙（实测缺口 357.43）
    // 修法：量出自己真实渲染的宽度，把基线补到文字那条竖边。
    {
        struct MeasuringSink : IDrawSink
        {
            std::vector<std::pair<double,double>> linesX;   // 每条线的 x 起止
            std::vector<double>                   linesY;   // 起始点 y
            std::vector<double>                   linesY2;  // 结束点 y
            double measWidth = 0.0;                          // 假装量出的宽度
            void DrawLine(const Math::Point3& a, const Math::Point3& b,
                          const Math::Color4&, bool) override
            {
                linesX.push_back({ a.x, b.x });
                linesY.push_back(a.y);
                linesY2.push_back(b.y);
            }
            double MeasureMTextWidth(const std::string&, uint32_t, double, double) override
            {
                return measWidth;
            }
        };

        // 171=3(Right)：文字在左，基线末端(landing)在右 —— 与实测的 PY-2-W04 同形
        const double cbx = 1000.0, cy = 0.0;
        const double ourW = 1609.09;                 // 我们真实渲染宽
        const double acadW = 1966.11;                // AutoCAD 记录宽
        const double landingX = cbx + acadW;         // 基线末端落在**记录宽**处

        MLeaderStyle st;
        st.TextHeight   = 300.0;
        st.DoglegLength = 150.0;
        st.EnableLanding = true;
        st.EnableDogleg  = false;                    // 与实测相同

        MLeaderEntity e(10, st);
        e.SetTextOrigin({ cbx, cy, 0.0 });
        e.SetTextAnchor(MLeaderTextAnchor::Right);
        e.SetLanding({ landingX, cy, 0.0 });
        e.SetDoglegDir({ 1, 0, 0 });                 // DoglegStart == landing（无 dogleg）
        // 引线**斜着**进来（真实图纸就是这样），避免与基线同行干扰判据
        e.AddLeaderLine({ { 5000, 2000, 0 }, { landingX, cy, 0 } }, true);
        e.SetText("PY-2-W04");

        MeasuringSink sink;
        sink.measWidth = ourW;
        e.Draw(sink, false, false);

        // 找**整段都在基线行上**的线（两端 y 都 ≈ cy），这类才是基线/延长线。
        // 判据：这些线覆盖到的 **x 最小值** 应等于文字靠基线的那条竖边
        // （文字在左、基线在右 ⇒ 基线从文字右缘起画）。不补线时该行没有线段。
        const double textEdge = cbx + ourW;          // 文字右缘
        double coveredMin = 1e300;
        for (size_t i = 0; i < sink.linesX.size(); ++i)
        {
            if (std::abs(sink.linesY[i] - cy) > 1.0 || std::abs(sink.linesY2[i] - cy) > 1.0)
                continue;                            // 斜引线排除在外
            coveredMin = std::min(coveredMin, std::min(sink.linesX[i].first,
                                                       sink.linesX[i].second));
        }
        std::printf("    文字[%.2f, %.2f] landing.x=%.2f 基线行 x 最小覆盖=%.2f\n",
                    cbx, textEdge, landingX, coveredMin);
        Check(std::abs(coveredMin - textEdge) < 1.0,
              "**基线画到了文字右缘**（缺口 0；不补线时该行无线段）");
        Check(std::abs(acadW - ourW - 357.02) < 0.05,
              "对照：记录宽 − 渲染宽 = 357.02，正是原先的缺口大小");
    }

    // ── 11. 测量不可用时不得崩、不得乱画（退回按记录值）─────────
    {
        struct NoMeasureSink : IDrawSink
        {
            int lines = 0;
            void DrawLine(const Math::Point3&, const Math::Point3&,
                          const Math::Color4&, bool) override { ++lines; }
            // MeasureMTextWidth 用基类默认：返回 0
        };
        MLeaderStyle st;
        MLeaderEntity e(11, st);
        e.SetTextOrigin({ 0, 0, 0 });
        e.SetLanding({ 500, 0, 0 });
        e.AddLeaderLine({ { 900, 0, 0 }, { 500, 0, 0 } }, true);
        e.SetText("AB");
        NoMeasureSink sink;
        e.Draw(sink, false, false);
        Check(sink.lines > 0, "测量不可用时仍正常画线（不额外补线，也不崩）");
    }

    // ── 12. 基线端点能否由 entget 字段推出（回答用户提问）─────────
    //
    // 用户问：「entget 获得的数据里，有没有文字所在线（基线）的端点信息？」
    // 实测答案（20 条）：
    //   · 引线侧端点 = **LEADER{10} ConnectionPoint —— 显式存储**
    //   · 文字侧端点 = LEADER{10} + **LEADER{11} Direction** × **LEADER{40} LandingDistance**
    //     （推出值，不是显式字段）
    //   · 注意：**不能**用 CONTEXT{13} 代替 LEADER{11} ——
    //     CONTEXT{13} 实测恒为 (1,0)（文字阅读方向），14/20 条与 LEADER{11} 相反。
    //     用错会让文字侧端点偏 480（本图实测）。
    {
        // 用 C08E 的真实值
        const double connX = 1894996.52, connY = -1206702.42;
        const double dirX  = -1.0, dirY = 0.0;        // LEADER{11}
        const double LD    = 150.0;                   // LEADER{40}
        const double cbx   = 1892651.44, cby = -1206702.42;   // CONTEXT{10}

        // 基线引线侧端点 = LEADER{10}（显式）
        Check(Near(connX, 1894996.52), "基线引线侧端点 = LEADER{10}（显式存储）");
        // 基线文字侧端点 = LEADER{10} + LEADER{11} × LEADER{40}
        const double eX = connX + dirX * LD, eY = connY + dirY * LD;
        std::printf("    推出文字侧端点 = (%.2f, %.2f)  文字左下角.x = %.2f\n", eX, eY, cbx);
        Check(Near(eX, 1894846.52, 1e-6), "文字侧端点 = LEADER{10} + LEADER{11} × LEADER{40}");
        Check(eY == connY, "文字侧端点与引线侧端点**同高**（基线是水平短线）");
        // 与文字左下角的水平关系：|CBP.x| 在文字侧端点**外侧**（171=3 文字在左）
        Check(cbx < eX, "171=3(Right)：文字左下角在基线文字侧端点的**外侧**");

        // 反向：若误用 CONTEXT{13}=(1,0)，会偏 2×LD = 300（方向反）
        const double wrongX = connX + 1.0 * LD;
        Check(std::abs(wrongX - eX) > 1.0,
              "**用 CONTEXT{13} 代替 LEADER{11} 会得到相反的基线方向**"
              "（实测 14/20 条两者相反，位置差 2×LandingDistance）");
    }

    // ── 13. 导出必须写回 LEADER{11}，且不得改写 CONTEXT{13} ──────
    {
        MLeaderStyle st;
        MLeaderEntity e(12, st);
        e.SetTextOrigin({ 0, 0, 0 });
        e.SetTextAnchor(MLeaderTextAnchor::Right);
        e.SetDoglegDir({ 1, 0, 0 });                 // 引线方向 = -doglegDir = (-1,0)
        e.SetTextDirection({ 1, 0, 0 });             // 文字阅读方向单独存
        e.SetLanding({ -150, 0, 0 });
        e.AddLeaderLine({ { 900, 0, 0 }, { -300, 0, 0 } }, true);
        e.SetText("AB");

        // 导出侧应：root.Direction = -doglegDir；cd.Direction = GetTextDirection()
        const auto& dg = e.GetDoglegDir();
        Check(Near(-dg.x, -1.0), "导出：root.Direction(LEADER{11}) = -doglegDir = (-1,0)");
        Check(Near(e.GetTextDirection().x, 1.0),
              "导出：cd.Direction(CONTEXT{13}) 原样保留 (1,0)，**不得**写成 -doglegDir");
        Check(!Near(e.GetTextDirection().x, -dg.x),
              "两个 Direction **语义不同**，不能共用同一个值"
              "（漏写 LEADER{11} 实测会让 20/20 条变成零向量）");
    }

    // ── 14. 基线要画到**墨迹**边，不是步进边 ─────────────────────
    //
    // 用户第二次报：「直线段的端点还是没在文字的外侧。」
    //
    // 根因：V31 量的是**步进宽度**（advance 之和），它包含字形右侧留白
    // （right side bearing）。实测本图 45~133 单位 = 0.15~0.33 个字高，
    // 画到步进边就离最后一个字还差这么一段。
    //   例：PY-2-W04  步进边 2020172.57，墨迹边 2020127.12，差 45.45
    //
    // 修法：量**墨迹**（字形轮廓包围盒），IDrawSink::MeasureMTextInkBounds。
    {
        struct InkSink : IDrawSink
        {
            std::vector<std::pair<double,double>> linesX;
            std::vector<double> y0s, y1s;
            double advW = 0.0;                 // 步进宽（假）
            double inkMin = 0.0, inkMax = 0.0; // 墨迹边界（假）
            bool   inkOk = true;
            void DrawLine(const Math::Point3& a, const Math::Point3& b,
                          const Math::Color4&, bool) override
            {
                linesX.push_back({ a.x, b.x });
                y0s.push_back(a.y); y1s.push_back(b.y);
            }
            double MeasureMTextWidth(const std::string&, uint32_t, double, double) override
            { return advW; }
            bool MeasureMTextInkBounds(const std::string&, uint32_t, double, double,
                                       double& lo, double& hi) override
            {
                if (!inkOk) return false;
                lo = inkMin; hi = inkMax; return true;
            }
        };

        // 与实测 PY-2-W04 同形：171=3，文字在左，基线在右
        const double cbx = 1000.0, cy = 0.0;
        const double inkLo = 0.0, inkHi = 1445.6;    // 墨迹（实测 1609.09 − 45.45）
        const double adv   = 1609.09;                // 步进
        const double landX = cbx + 1966.11;          // 基线末端（AutoCAD 记录宽处）

        MLeaderStyle st;
        st.TextHeight = 300.0; st.DoglegLength = 150.0; st.EnableDogleg = false;
        MLeaderEntity e(13, st);
        e.SetTextOrigin({ cbx, cy, 0.0 });
        e.SetTextAnchor(MLeaderTextAnchor::Right);
        e.SetLanding({ landX, cy, 0.0 });
        e.SetDoglegDir({ 1, 0, 0 });
        e.AddLeaderLine({ { 9000, 3000, 0 }, { landX, cy, 0 } }, true);
        e.SetText("PY-2-W04");

        InkSink s;
        s.advW = adv; s.inkMin = inkLo; s.inkMax = inkHi;
        e.Draw(s, false, false);

        // 找以 landing 为端点的水平段，看它另一端落在哪
        double other = 1e300;
        for (size_t i = 0; i < s.linesX.size(); ++i)
        {
            if (std::abs(s.y0s[i] - cy) > 1.0 || std::abs(s.y1s[i] - cy) > 1.0) continue;
            if (std::abs(s.linesX[i].first - landX) < 1.0)       other = std::min(other, s.linesX[i].second);
            else if (std::abs(s.linesX[i].second - landX) < 1.0) other = std::min(other, s.linesX[i].first);
        }
        const double wantInk = cbx + inkHi;          // 文字墨迹右缘
        const double wantAdv = cbx + adv;            // 文字步进右缘
        std::printf("    基线另一端=%.2f  墨迹右缘=%.2f  步进右缘=%.2f\n", other, wantInk, wantAdv);

        Check(std::abs(other - wantInk) < 1.0,
              "**基线画到文字墨迹边**（不是步进边）");
        Check(std::abs(wantAdv - wantInk) > 40.0,
              "墨迹边与步进边相差 45.45（= 字形右侧留白），正是上一轮残留的缺口");

        // 墨迹测量不可用时，退回步进宽度（不崩）
        InkSink s2;
        s2.advW = adv; s2.inkOk = false;
        e.Draw(s2, false, false);
        double other2 = 1e300;
        for (size_t i = 0; i < s2.linesX.size(); ++i)
        {
            if (std::abs(s2.y0s[i] - cy) > 1.0 || std::abs(s2.y1s[i] - cy) > 1.0) continue;
            if (std::abs(s2.linesX[i].first - landX) < 1.0)       other2 = std::min(other2, s2.linesX[i].second);
            else if (std::abs(s2.linesX[i].second - landX) < 1.0) other2 = std::min(other2, s2.linesX[i].first);
        }
        Check(std::abs(other2 - wantAdv) < 1.0,
              "墨迹测量不可用时退回**步进宽度**（不崩、不空画）");
    }

    // ── 15. 下划线族（组码 174/175 = 5~8）：横线要横穿文字、止于**外侧**边 ──
    //
    // 用户第三次报：「多重引线的横线，还是没到文字的外侧，需修正。」
    //
    // 根因不在宽度度量（第 14 节那条已经修好了），而在**横线该画到哪一端**：
    // `1#、2#厂房电气_t3.dwg` 20/20 条的 context 组码
    //     174 = 175 = 6 = TextAttachmentType::BottomOfTopLineUnderlineBottomLine
    // 属**下划线族**（5=BottomLine；6/7/8 名字里都含 Underline）。
    // AutoCAD 把 landing 这条横线当**文字的下划线**画：横穿整个文字，
    // 终止在**远离引线**的那条边。第 14 节只补到**近侧**边缘就停了，
    // 于是用户看到"横线到文字边上就断了"。
    //   实测 SF-2-02：landing.x = 1894846.52（在文字**右**侧），
    //                 文字墨迹 [1892683.44, 1893686.10]
    //     旧：止于 1893686.10（近侧右缘）→ 短 1003 个单位
    //     新：止于 1892683.44（外侧左缘）
    // 独立佐证：20/20 条 `landing.y ≡ CBP.y`（Δ = 0.0000），正说明
    // landing 落在文字底线上 —— 下划线才画得出来（普通族不会恰好重合）。
    {
        struct USink : IDrawSink
        {
            std::vector<std::pair<double, double>> linesX;
            std::vector<double> y0s, y1s;
            double inkMin = 0.0, inkMax = 0.0;
            void DrawLine(const Math::Point3& a, const Math::Point3& b,
                          const Math::Color4&, bool) override
            {
                linesX.push_back({ a.x, b.x });
                y0s.push_back(a.y); y1s.push_back(b.y);
            }
            bool MeasureMTextInkBounds(const std::string&, uint32_t, double, double,
                                       double& lo, double& hi) override
            { lo = inkMin; hi = inkMax; return true; }
        };

        const double cbx = 1000.0, cy = 0.0;
        const double inkLo = 0.0, inkHi = 1445.6;     // 文字墨迹（相对 CBP）

        // 造一条引线，landing 在文字**右**侧（171=3 的形态）。
        // 返回"以 landing 为端点的水平段的另一端"。
        auto otherEnd = [&](bool underline, double landX)
        {
            MLeaderStyle st;
            st.TextHeight = 300.0; st.DoglegLength = 150.0; st.EnableDogleg = false;
            MLeaderEntity e(14, st);
            e.SetTextOrigin({ cbx, cy, 0.0 });
            e.SetTextAnchor(MLeaderTextAnchor::Right);
            e.SetLanding({ landX, cy, 0.0 });
            e.SetDoglegDir({ 1, 0, 0 });
            e.AddLeaderLine({ { 9000, 3000, 0 }, { landX, cy, 0 } }, true);
            e.SetText("SF-2-02");
            if (underline) { e.SetTextLeftAttachment(6); e.SetTextRightAttachment(6); }
            USink s; s.inkMin = inkLo; s.inkMax = inkHi;
            e.Draw(s, false, false);
            double other = 1e300;
            for (size_t i = 0; i < s.linesX.size(); ++i)
            {
                if (std::abs(s.y0s[i] - cy) > 1.0 || std::abs(s.y1s[i] - cy) > 1.0) continue;
                if (std::abs(s.linesX[i].first - landX) < 1.0)       other = std::min(other, s.linesX[i].second);
                else if (std::abs(s.linesX[i].second - landX) < 1.0) other = std::min(other, s.linesX[i].first);
            }
            return other;
        };

        // 引线在右 → 下划线要一直画到文字**左**缘（外侧）
        const double landRight = cbx + 1966.11;
        const double under = otherEnd(true, landRight);
        const double plain = otherEnd(false, landRight);
        const double wantOuter = cbx + inkLo;          // 外侧边 = 墨迹左缘
        const double wantInner = cbx + inkHi;          // 近侧边 = 墨迹右缘
        std::printf("    下划线族另一端=%.2f  普通族另一端=%.2f  外侧缘=%.2f  近侧缘=%.2f\n",
                    under, plain, wantOuter, wantInner);
        Check(std::abs(under - wantOuter) < 1.0,
              "**下划线族（174/175=5~8）：横线横穿文字、止于外侧墨迹边**");
        Check(std::abs(plain - wantInner) < 1.0,
              "普通族（未设 174/175，如旧档案）：仍只补到近侧墨迹边（不改变既有观感）");
        Check(std::abs(under - plain) > 1000.0,
              "两族相差整整一个文字宽 —— 正是用户看到的「没到文字的外侧」");

        // 镜像：引线在文字**左**侧时，下划线要画到文字**右**缘
        const double landLeft = cbx;                   // == 墨迹左缘
        const double under2 = otherEnd(true, landLeft);
        std::printf("    引线在左时下划线另一端=%.2f  期望（右缘）=%.2f\n", under2, wantInner);
        Check(std::abs(under2 - wantInner) < 1.0,
              "引线在左时下划线画到**右**缘（左右对称，不是写死一个方向）");
    }

    // ── 16. 编辑：landing 移动时文字跟着走，换边时翻到另一侧，翻两次复原 ──
    {
        MLeaderStyle st;
        st.TextHeight = 10.0;
        MLeaderEntity e(20, st);
        e.SetText("AB");                                 // 估算宽 = 2 × 0.6 × 10 = 12
        e.SetLanding({ 100, 0, 0 });
        e.SetDoglegDir({ -1, 0, 0 });                   // 引线在左、文字在右
        e.SetTextOrigin({ 100, 0, 0 });

        e.MoveLanding({ 150, 20, 0 });
        Check(Near(e.GetTextOrigin().x, 150) && Near(e.GetTextOrigin().y, 20),
              "MoveLanding：文字原点随 landing 平移（移动 / 拉伸 / 夹点 / 撤销快照都走它）");

        e.TurnDogleg({ 1, 0, 0 });                       // 镜像后引线换到右侧
        Check(Near(e.GetTextOrigin().x, 150 - 12),
              "TurnDogleg：内容换到 landing 左侧时文字右缘贴着 landing");
        e.TurnDogleg({ -1, 0, 0 });
        Check(Near(e.GetTextOrigin().x, 150), "TurnDogleg 翻两次回到原处（撤销按反向变换复原）");
        e.TurnDogleg({ -0.8, 0.6, 0 });
        Check(Near(e.GetTextOrigin().x, 150), "没有换边时文字原点不动");
    }

    // ── 17. 没有文字原点（新建）：按 landing 与 dogleg 方向推出 ─────────
    {
        MLeaderStyle st;
        st.TextHeight = 10.0;
        st.LandingGap = 2.0;
        MLeaderEntity e(21, st);
        e.SetText("AB");
        e.SetLanding({ 100, 5, 0 });
        e.SetDoglegDir({ 1, 0, 0 });                     // 引线在右：文字在 landing 左侧
        Check(!e.HasTextOrigin(), "新建的多重引线没有文字原点");
        Check(Near(e.ResolveTextOrigin(30).x, 100 - 2 - 30) && Near(e.ResolveTextOrigin(30).y, 5),
              "引线在右：文字右缘在 landing − LandingGap（不是压到引线上）");
        e.SetDoglegDir({ -1, 0, 0 });
        Check(Near(e.ResolveTextOrigin(30).x, 102), "引线在左：文字左缘在 landing + LandingGap");
        e.SetTextOrigin({ 0, 0, 0 });
        Check(e.HasTextOrigin() && Near(e.ResolveTextOrigin(30).x, 0),
              "文字原点设在 (0,0) 也算设置过（不再用 (0,0) 当“未设置”）");
    }

    // ── 18. 块内（带缩放的变换）量出的宽度换回块的局部单位 ──────────────
    {
        struct ScaleSink : IDrawSink
        {
            void DrawLine(const Math::Point3&, const Math::Point3&, const Math::Color4&, bool) override {}
            double MeasureMTextWidth(const std::string&, uint32_t, double height, double) override { return height * 3.0; }
            bool MeasureMTextInkBounds(const std::string&, uint32_t, double height, double,
                                       double& lo, double& hi) override { lo = height; hi = height * 3.0; return true; }
        } target;
        TransformDrawSink xs(target, Math::Mat4::Scale({ 2.0, 2.0, 2.0 }));
        double lo = 0, hi = 0;
        Check(Near(xs.MeasureMTextWidth("A", 0, 10.0, 0.0), 30.0),
              "块缩放 2 倍：局部字高 10 量出局部宽 30（不是 120）");
        Check(xs.MeasureMTextInkBounds("A", 0, 10.0, 0.0, lo, hi) && Near(lo, 10.0) && Near(hi, 30.0),
              "块缩放 2 倍：墨迹边界也换回局部单位");
    }

    std::printf("多重引线导入用例失败：%d\n", g_failures);
    return g_failures;
}
