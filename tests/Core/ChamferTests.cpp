// ── 倒角测试：两个距离、保留点选一侧、延伸、距离为 0、非法情况 ─────────────────────
#include "Core/Entity/LineEntity.hpp"
#include "Core/Entity/CircleEntity.hpp"
#include "Document/Command/EntityChamfer.h"
#include <cmath>
#include <cstdio>
#include <memory>

using namespace MiniCAD;

namespace
{
    int g_failures = 0;

    void Check(bool ok, const char* what)
    {
        std::printf("[%s] %s\n", ok ? "通过" : "失败", what);
        if (!ok)
            ++g_failures;
    }

    bool Near(double a, double b) { return std::abs(a - b) < 1e-9; }
}

int RunChamferTests()
{
    g_failures = 0;

    {
        LineEntity a(1, { 0, 0, 0 }, { 10, 0, 0 }), b(2, { 0, 0, 0 }, { 0, 10, 0 });
        ChamferResult r;
        Check(ComputeChamfer(a, { 5, 0.1, 0 }, b, { 0.1, 5, 0 }, 2.0, 3.0, r), "直角：倒角成功");
        Check(r.first && r.second && r.line, "两条线都被修改，并生成倒角线");
        Check(Near(r.first->GetLine().Start.x, 2) && Near(r.first->GetLine().End.x, 10), "第一条线：沿自己量出 D1 = 2，起点缩到 (2,0)");
        Check(Near(r.second->GetLine().Start.y, 3) && Near(r.second->GetLine().End.y, 10), "第二条线：沿自己量出 D2 = 3，起点缩到 (0,3)");
        const Line& c = r.line->GetLine();
        Check(Near(c.Start.x, 2) && Near(c.Start.y, 0) && Near(c.End.x, 0) && Near(c.End.y, 3), "倒角线连接 (2,0) 和 (0,3)");
        Check(r.first->GetID() == 1 && r.second->GetID() == 2, "修改后的线保持原 ID");
    }
    {
        // 点选顺序互换：D1 对应第一条被点选的线
        LineEntity a(3, { 0, 0, 0 }, { 10, 0, 0 }), b(4, { 0, 0, 0 }, { 0, 10, 0 });
        ChamferResult r;
        ComputeChamfer(b, { 0.1, 5, 0 }, a, { 5, 0.1, 0 }, 2.0, 3.0, r);
        Check(Near(r.first->GetLine().Start.y, 2) && Near(r.second->GetLine().Start.x, 3), "先点竖线：D1 用在竖线上，D2 用在横线上");
    }
    {
        // 有缺口：两条线都延伸到倒角点
        LineEntity a(5, { 6, 0, 0 }, { 10, 0, 0 }), b(6, { 0, 6, 0 }, { 0, 10, 0 });
        ChamferResult r;
        Check(ComputeChamfer(a, { 8, 0, 0 }, b, { 0, 8, 0 }, 2.0, 2.0, r), "有缺口的两条线：倒角成功");
        Check(Near(r.first->GetLine().Start.x, 2) && Near(r.second->GetLine().Start.y, 2), "两条线都延伸到倒角点");
    }
    {
        // 十字相交：点选哪一侧就保留哪一侧
        LineEntity a(7, { -5, 0, 0 }, { 10, 0, 0 }), b(8, { 0, -5, 0 }, { 0, 10, 0 });
        ChamferResult r;
        Check(ComputeChamfer(a, { 6, 0, 0 }, b, { 0, 6, 0 }, 2.0, 2.0, r), "十字相交：倒角成功");
        Check(Near(r.first->GetLine().Start.x, 2) && Near(r.first->GetLine().End.x, 10) && Near(r.second->GetLine().Start.y, 2) && Near(r.second->GetLine().End.y, 10),
              "十字相交：保留右 / 上两侧，左 / 下的两段被去掉");
        ChamferResult r2;
        ComputeChamfer(a, { -4, 0, 0 }, b, { 0, -4, 0 }, 2.0, 2.0, r2);
        Check(Near(r2.first->GetLine().Start.x, -5) && Near(r2.first->GetLine().End.x, -2) && Near(r2.second->GetLine().End.y, -2), "点选左 / 下两侧则保留左 / 下");
    }
    {
        // 线的方向不影响结果（起点 / 终点互换）
        LineEntity a(9, { 10, 0, 0 }, { 0, 0, 0 }), b(10, { 0, 10, 0 }, { 0, 0, 0 });
        ChamferResult r;
        Check(ComputeChamfer(a, { 5, 0, 0 }, b, { 0, 5, 0 }, 2.0, 3.0, r), "线的方向反过来：倒角成功");
        Check(Near(r.first->GetLine().Start.x, 10) && Near(r.first->GetLine().End.x, 2) && Near(r.second->GetLine().End.y, 3), "反向的线：缩短的是靠交点的那一端");
        Check(Near(r.line->GetLine().Start.x, 2) && Near(r.line->GetLine().End.y, 3), "倒角线端点不变");
    }

    // ── 距离为 0：延伸 / 修剪到交点 ─────────────────────────────
    {
        LineEntity a(11, { 0, 0, 0 }, { 5, 0, 0 }), b(12, { 8, -3, 0 }, { 8, 10, 0 });
        ChamferResult r;
        Check(ComputeChamfer(a, { 2, 0, 0 }, b, { 8, 5, 0 }, 0.0, 0.0, r) && !r.line, "距离都为 0：成功且不生成倒角线");
        Check(Near(r.first->GetLine().End.x, 8) && Near(r.first->GetLine().Start.x, 0), "第一条线延伸到交点");
        Check(Near(r.second->GetLine().Start.y, 0) && Near(r.second->GetLine().End.y, 10), "第二条线修剪到交点，保留点选的一侧");
    }
    {
        LineEntity a(13, { 0, 0, 0 }, { 10, 0, 0 }), b(14, { 0, 0, 0 }, { 0, 10, 0 });
        ChamferResult r;
        Check(!ComputeChamfer(a, { 5, 0, 0 }, b, { 0, 5, 0 }, 0.0, 2.0, r), "一个距离为 0 一个不为 0：失败");
    }

    // ── 非法情况 ─────────────────────────────────────────────
    {
        LineEntity a(15, { 0, 0, 0 }, { 10, 0, 0 }), b(16, { 0, 5, 0 }, { 10, 5, 0 });
        ChamferResult r;
        Check(!ComputeChamfer(a, { 5, 0, 0 }, b, { 5, 5, 0 }, 1.0, 1.0, r) && !r.line, "两条平行线：失败");
        Check(!ComputeChamfer(a, { 5, 0, 0 }, a, { 6, 0, 0 }, 1.0, 1.0, r), "同一条线：失败");

        LineEntity c(17, { 0, 0, 0 }, { 0, 10, 0 });
        Check(!ComputeChamfer(a, { 5, 0, 0 }, c, { 0, 5, 0 }, -1.0, 1.0, r), "距离为负：失败");
        Check(!ComputeChamfer(a, { 5, 0, 0 }, c, { 0, 5, 0 }, 12.0, 1.0, r), "距离超过保留一侧的长度（12 > 10）：失败");
        Check(!ComputeChamfer(a, { 0, 0, 0 }, c, { 0, 5, 0 }, 1.0, 1.0, r), "点选恰好在交点上，分不出保留哪一侧：失败");

        CircleEntity circle(18, { 0, 0, 0 }, 5.0);
        Check(!ComputeChamfer(a, { 5, 0, 0 }, circle, { 5, 0, 0 }, 1.0, 1.0, r), "不支持的类型（圆）：失败");
    }

    return g_failures;
}
