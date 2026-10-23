#pragma once
#include "Scene/Scene.h"
#include "Editor/Tools/ITool.h"
#include "Editor/EditorContext.h"
#include "Editor/MLineEdit.hpp"
#include "Document/Command/GeometryEditCommand.h"
#include "Core/Entity/MLineEntity.hpp"
#include "Core/Math/Point3.hpp"
#include "Core/Log.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include <memory>
#include <string>
#include <vector>

namespace MiniCAD
{
    // =========================================================================
    // MLineEditTool —— 多线编辑（MLEDIT）
    //
    // 先按字母选编辑方式，再点选多线；几何计算在 MLineEdit（纯函数），这里只负责拾取与提交：
    //   C 十字交叉：第一条多线在交点处断开，缺口宽度 = 第二条多线的宽度
    //   T T形相交：第一条（竖线）剪到第二条（横线）的近侧元素线，保留点选的一侧
    //   R 拐角结合：两条各在交点处剪掉未点选的一侧，接成一条 L 形多线
    //   O 顶点断开：在离点选位置最近的顶点处拆开（封闭多线变为开放）
    //   M 顶点结合：两条多线最近的端点接成一条
    // 每次编辑经 GeometryEditCommand 提交（可撤销），完成后保持当前方式继续点选；右键 / ESC 退出。
    // 编辑后的多线不再带文件里的顶点参数（路径变了，那些参数已不对应），按样式重算。
    // =========================================================================
    class MLineEditTool : public ITool
    {
    public:
        MLineEditTool()  { LOG_DEBUG("[MLineEditTool] C 十字 | T T形 | R 拐角 | O 顶点断开 | M 顶点结合 | 右键/ESC 退出"); }
        ~MLineEditTool() { LOG_DEBUG("[MLineEditTool] 退出"); }

        bool OnInput(const EditorContext& ctx) override
        {
            m_ctx     = &ctx;
            m_overlay = &ctx.overlay;
            const auto& e = ctx.event;

            struct KindKey { KeyCode key; MLineEditKind kind; };
            static const KindKey kKeys[] = {
                { KeyCode::C, MLineEditKind::Cross }, { KeyCode::T, MLineEditKind::Tee },
                { KeyCode::R, MLineEditKind::Corner }, { KeyCode::O, MLineEditKind::Open },
                { KeyCode::M, MLineEditKind::Merge },
            };
            for (const auto& k : kKeys)
                if (e.IsKeyPressed(k.key))
                {
                    m_kind     = k.kind;
                    m_hasKind  = true;
                    m_firstId  = Object::InvalidID;
                    return true;
                }

            if (e.IsLeftClick())
            {
                if (!m_hasKind)
                {
                    LOG_WARN("[MLineEditTool] 请先选择编辑方式（C / T / R / O / M）");
                    return true;
                }
                const Math::Point3 p = m_ctx->viewport.GetCamera().ScreenToWorld(e.MouseX, e.MouseY);
                MLineEntity* ml = HitMLine(e);
                if (!ml)
                {
                    LOG_WARN("[MLineEditTool] 请点选多线");
                    return true;
                }

                if (m_kind == MLineEditKind::Open)
                {
                    CommitOpen(*ml, p);
                    return true;
                }
                if (m_firstId == Object::InvalidID)
                {
                    m_firstId  = ml->GetID();
                    m_firstPick = p;
                    return true;
                }
                if (ml->GetID() == m_firstId)
                {
                    LOG_WARN("[MLineEditTool] 第二条多线不能与第一条相同");
                    return true;
                }
                if (MLineEntity* first = FindMLine(m_firstId))
                    CommitPair(*first, m_firstPick, *ml, p);
                m_firstId = Object::InvalidID;
                return true;
            }

            if (e.IsRightClick() || e.IsCancel())
            {
                Cancel();
                return true;
            }
            return false;
        }

        void Cancel() override
        {
            if (m_overlay) m_overlay->Clear();
            if (OnFinished) OnFinished();
        }
        void OnSceneChanged() override { m_firstId = Object::InvalidID; }

        // 每帧由 Editor::Render 调用：不能访问 m_ctx（只在 OnInput 内有效）
        std::string GetPrompt() const override
        {
            if (!m_hasKind)
                return "选择编辑方式 [C 十字交叉 / T T形相交 / R 拐角结合 / O 顶点断开 / M 顶点结合]:";
            const char* name = KindName(m_kind);
            switch (m_kind)
            {
            case MLineEditKind::Open:
                return std::string(name) + "：点选多线上要断开的顶点 [右键/ESC 退出]:";
            case MLineEditKind::Tee:
                return std::string(name) + (m_firstId == Object::InvalidID
                    ? "：请点选第一条多线（被剪的一条，点在要保留的一侧）:"
                    : "：请点选第二条多线（被接到的一条）:");
            case MLineEditKind::Cross:
                return std::string(name) + (m_firstId == Object::InvalidID
                    ? "：请点选第一条多线（将被断开）:" : "：请点选第二条多线:");
            default:
                return std::string(name) + (m_firstId == Object::InvalidID
                    ? "：请点选第一条多线（点在要保留的一侧）:" : "：请点选第二条多线（点在要保留的一侧）:");
            }
        }

    private:
        using Path = std::vector<Math::Point3>;

        static const char* KindName(MLineEditKind k)
        {
            switch (k)
            {
            case MLineEditKind::Cross:  return "十字交叉";
            case MLineEditKind::Tee:    return "T形相交";
            case MLineEditKind::Corner: return "拐角结合";
            case MLineEditKind::Open:   return "顶点断开";
            case MLineEditKind::Merge:  return "顶点结合";
            }
            return "";
        }

        MLineEntity* FindMLine(Object::ObjectID id) const
        {
            Object* obj = m_ctx->scene.GetEntity(id);
            return obj && obj->IsKindOf<MLineEntity>() ? static_cast<MLineEntity*>(obj) : nullptr;
        }

        MLineEntity* HitMLine(const InputEvent& e) const
        {
            const Object::ObjectID id = m_ctx->picking.HitTest(
                { static_cast<double>(e.MouseX), static_cast<double>(e.MouseY) }, 8.0);
            if (id == Object::InvalidID) return nullptr;
            Object* obj = m_ctx->scene.GetEntity(id);
            if (!obj || m_ctx->scene.IsEntityLocked(*obj)) return nullptr;     // 锁定图层上的对象不能修改
            return obj->IsKindOf<MLineEntity>() ? static_cast<MLineEntity*>(obj) : nullptr;
        }

        // ── 路径按弧长参数化 ───────────────────────────────────────────────
        static double Dist(const Math::Point3& a, const Math::Point3& b)
        {
            return std::hypot(b.x - a.x, b.y - a.y);
        }
        static double Length(const Path& p)
        {
            double s = 0.0;
            for (size_t i = 1; i < p.size(); ++i) s += Dist(p[i - 1], p[i]);
            return s;
        }
        // 点在路径上的投影弧长
        static double Project(const Path& p, const Math::Point3& q)
        {
            double best = std::numeric_limits<double>::max(), at = 0.0, acc = 0.0;
            for (size_t i = 1; i < p.size(); ++i)
            {
                const double dx = p[i].x - p[i - 1].x, dy = p[i].y - p[i - 1].y;
                const double len2 = dx * dx + dy * dy;
                double t = len2 > 0.0 ? ((q.x - p[i - 1].x) * dx + (q.y - p[i - 1].y) * dy) / len2 : 0.0;
                t = std::clamp(t, 0.0, 1.0);
                const double px = p[i - 1].x + dx * t, py = p[i - 1].y + dy * t;
                const double d2 = (q.x - px) * (q.x - px) + (q.y - py) * (q.y - py);
                if (d2 < best) { best = d2; at = acc + std::sqrt(len2) * t; }
                acc += std::sqrt(len2);
            }
            return at;
        }
        // 弧长区间 [a, b] 上的子路径（含两端的插值点）
        static Path SubPath(const Path& p, double a, double b)
        {
            Path out;
            if (p.size() < 2 || b - a < 1e-9) return out;
            auto lerp = [](const Math::Point3& u, const Math::Point3& v, double t)
            {
                return Math::Point3{ u.x + (v.x - u.x) * t, u.y + (v.y - u.y) * t, u.z + (v.z - u.z) * t };
            };
            double acc = 0.0;
            for (size_t i = 1; i < p.size(); ++i)
            {
                const double len = Dist(p[i - 1], p[i]);
                const double s0 = acc, s1 = acc + len;
                acc = s1;
                if (s1 < a || s0 > b || len < 1e-12) continue;
                const Math::Point3 from = a > s0 ? lerp(p[i - 1], p[i], (a - s0) / len) : p[i - 1];
                const Math::Point3 to   = b < s1 ? lerp(p[i - 1], p[i], (b - s0) / len) : p[i];
                if (out.empty() || Dist(out.back(), from) > 1e-9) out.push_back(from);
                if (Dist(out.back(), to) > 1e-9) out.push_back(to);
            }
            return out.size() >= 2 ? out : Path{};
        }
        // 路径 a 与折线 b 各交点在 a 上的弧长
        static std::vector<double> CrossingsOn(const Path& a, const Path& b)
        {
            std::vector<double> out;
            double acc = 0.0;
            for (size_t i = 1; i < a.size(); ++i)
            {
                const Math::Point3& p0 = a[i - 1];
                const double rx = a[i].x - p0.x, ry = a[i].y - p0.y;
                const double len = std::hypot(rx, ry);
                for (size_t j = 1; j < b.size(); ++j)
                {
                    const Math::Point3& q0 = b[j - 1];
                    const double sx = b[j].x - q0.x, sy = b[j].y - q0.y;
                    const double den = rx * sy - ry * sx;
                    if (std::abs(den) < 1e-15) continue;
                    const double t = ((q0.x - p0.x) * sy - (q0.y - p0.y) * sx) / den;
                    const double u = ((q0.x - p0.x) * ry - (q0.y - p0.y) * rx) / den;
                    if (t >= -1e-9 && t <= 1.0 + 1e-9 && u >= -1e-9 && u <= 1.0 + 1e-9)
                        out.push_back(acc + len * t);
                }
                acc += len;
            }
            return out;
        }
        // 多线 other 的全部元素线（闭合时补上首尾段）
        static std::vector<Path> ElementLines(const MLineEntity& other)
        {
            auto g = other.ComputeGeometry();
            if (other.IsClosed())
                for (auto& el : g.Elements)
                    if (!el.empty()) el.push_back(el.front());
            return std::move(g.Elements);
        }
        // 路径 a 在弧长 s（与 other 中心线的交点）处被 other 的元素线覆盖的区间 [lo, hi]；没有交点时退化为 [s, s]。
        // 只算 reach 以内的交点：两条多线在别处再次相交时，那里的交点不能把缺口撑大
        static void CoveredSpan(const Path& a, double s, const MLineEntity& other, double reach, double& lo, double& hi)
        {
            lo = hi = s;
            for (const auto& el : ElementLines(other))
                for (double c : CrossingsOn(a, el))
                {
                    if (std::abs(c - s) > reach) continue;
                    lo = std::min(lo, c);     // 取两侧最远的一条：缺口覆盖全部元素线
                    hi = std::max(hi, c);
                }
        }
        // 多线 other 的元素线在交点处沿 a 方向能伸出的最远距离：宽度 / sin(夹角)
        static double CrossReach(const Path& a, size_t segA, const Path& b, size_t segB, const MLineEntity& other)
        {
            const auto& off = other.GetMLineStyle().Elements;
            double w = 0.0;
            if (!off.empty())
                w = (off.front().Offset - off.back().Offset) * std::abs(other.GetScale());
            const double ax = a[segA + 1].x - a[segA].x, ay = a[segA + 1].y - a[segA].y;
            const double bx = b[segB + 1].x - b[segB].x, by = b[segB + 1].y - b[segB].y;
            const double la = std::hypot(ax, ay), lb = std::hypot(bx, by);
            const double sinA = (la > 0.0 && lb > 0.0) ? std::abs(ax * by - ay * bx) / (la * lb) : 1.0;
            return w / std::max(sinA, 0.05) + 1e-6;
        }

        // 与 src 同样式 / 对正 / 比例 / 属性的新多线
        static std::unique_ptr<Entity> MakeLike(const MLineEntity& src, Object::ObjectID id, Path path, bool closed)
        {
            auto e = std::make_unique<MLineEntity>(id, std::move(path), src.GetMLineStyle(),
                                                   src.GetJustify(), src.GetScale(), closed);
            e->SetAttr(src.GetAttr());
            return e;
        }

        void Submit(const char* name, std::vector<GeometryEditCommand::Item> items)
        {
            m_ctx->cmdStack.Execute(std::make_unique<GeometryEditCommand>(name, std::move(items)), m_ctx->scene);
            LOG_DEBUG("[MLineEditTool] %s 完成", name);
        }
        static GeometryEditCommand::Item Replace(const MLineEntity& src, std::unique_ptr<Entity> after)
        {
            GeometryEditCommand::Item it;
            it.id     = src.GetID();
            it.before = src.Clone(src.GetID());
            it.after  = std::move(after);
            return it;
        }
        GeometryEditCommand::Item Add(const MLineEntity& like, Path path, bool closed)
        {
            GeometryEditCommand::Item it;
            it.id    = m_ctx->scene.NextObjectID();
            it.after = MakeLike(like, it.id, std::move(path), closed);
            return it;
        }

        // ── 顶点断开 ───────────────────────────────────────────────────────
        void CommitOpen(const MLineEntity& ml, const Math::Point3& pick)
        {
            const Path path = ml.ComputeGeometry().Path;
            size_t idx = 0;
            for (size_t i = 1; i < path.size(); ++i)
                if (Dist(path[i], pick) < Dist(path[idx], pick)) idx = i;

            const MLineSplit sp = MLineEdit::SplitAtVertex(path, idx, ml.IsClosed());
            if (!sp.Valid)
            {
                LOG_WARN("[MLineEditTool] 端点处无法断开，请点选中间的顶点");
                return;
            }
            std::vector<GeometryEditCommand::Item> items;
            items.push_back(Replace(ml, MakeLike(ml, ml.GetID(), sp.First, false)));
            if (sp.Second.size() >= 2)
                items.push_back(Add(ml, sp.Second, false));
            Submit("顶点断开", std::move(items));
        }

        // ── 两条多线的编辑 ─────────────────────────────────────────────────
        void CommitPair(const MLineEntity& a, const Math::Point3& pickA, const MLineEntity& b, const Math::Point3& pickB)
        {
            const Path pa = a.ComputeGeometry().Path;
            const Path pb = b.ComputeGeometry().Path;
            if (a.IsClosed() || b.IsClosed())
            {
                LOG_WARN("[MLineEditTool] %s 只支持开放多线（封闭多线可先用 O 顶点断开）", KindName(m_kind));
                return;
            }

            if (m_kind == MLineEditKind::Merge)
            {
                Path joined = MLineEdit::JoinPaths(pa, pb);
                if (joined.size() < 2) return;
                std::vector<GeometryEditCommand::Item> items;
                items.push_back(Replace(a, MakeLike(a, a.GetID(), std::move(joined), false)));
                items.push_back(Replace(b, nullptr));
                Submit("顶点结合", std::move(items));
                return;
            }

            const auto cross = MLineEdit::Intersect(pa, pb);
            if (!cross.Valid)
            {
                LOG_WARN("[MLineEditTool] 两条多线的中心线不相交");
                return;
            }
            const double la = Length(pa);
            const double sa = Project(pa, cross.Point);      // 交点在 a 上的弧长
            const double sb = Project(pb, cross.Point);
            const double reach = CrossReach(pa, cross.SegA, pb, cross.SegB, b);
            constexpr double eps = 1e-6;

            if (m_kind == MLineEditKind::Cross)
            {
                double lo, hi;
                CoveredSpan(pa, sa, b, reach, lo, hi);
                std::vector<GeometryEditCommand::Item> items;
                Path head = SubPath(pa, 0.0, lo);
                Path tail = SubPath(pa, hi, la);
                if (head.empty() && tail.empty()) { LOG_WARN("[MLineEditTool] 断开后没有剩余部分"); return; }
                if (!head.empty())
                {
                    items.push_back(Replace(a, MakeLike(a, a.GetID(), std::move(head), false)));
                    if (!tail.empty()) items.push_back(Add(a, std::move(tail), false));
                }
                else
                    items.push_back(Replace(a, MakeLike(a, a.GetID(), std::move(tail), false)));
                Submit("十字交叉", std::move(items));
                return;
            }

            const bool keepHeadA = Project(pa, pickA) < sa;

            if (m_kind == MLineEditKind::Tee)
            {
                double lo, hi;
                CoveredSpan(pa, sa, b, reach, lo, hi);
                // 剪到横线近侧的元素线：保留起点一侧时剪到 lo，保留终点一侧时从 hi 开始
                Path kept = keepHeadA ? SubPath(pa, 0.0, lo) : SubPath(pa, hi, la);
                if (kept.empty()) { LOG_WARN("[MLineEditTool] 剪切后没有剩余部分"); return; }
                std::vector<GeometryEditCommand::Item> items;
                items.push_back(Replace(a, MakeLike(a, a.GetID(), std::move(kept), false)));
                Submit("T形相交", std::move(items));
                return;
            }

            // 拐角结合：a 的保留段以交点结尾，b 的保留段以交点开头，接成一条
            const bool keepHeadB = Project(pb, pickB) < sb;
            Path partA = keepHeadA ? SubPath(pa, 0.0, sa) : SubPath(pa, sa, la);
            Path partB = keepHeadB ? SubPath(pb, 0.0, sb) : SubPath(pb, sb, Length(pb));
            if (partA.empty() || partB.empty() || la < eps) { LOG_WARN("[MLineEditTool] 交点在端点上，无法结合"); return; }
            if (!keepHeadA) std::reverse(partA.begin(), partA.end());
            if (keepHeadB)  std::reverse(partB.begin(), partB.end());
            Path corner = partA;
            corner.insert(corner.end(), partB.begin() + 1, partB.end());     // 交点只留一个
            std::vector<GeometryEditCommand::Item> items;
            items.push_back(Replace(a, MakeLike(a, a.GetID(), std::move(corner), false)));
            items.push_back(Replace(b, nullptr));
            Submit("拐角结合", std::move(items));
        }

        const EditorContext* m_ctx     = nullptr;       // 仅在 OnInput 内有效
        Overlay*             m_overlay = nullptr;
        MLineEditKind        m_kind    = MLineEditKind::Cross;
        bool                 m_hasKind = false;
        Object::ObjectID     m_firstId = Object::InvalidID;
        Math::Point3         m_firstPick;
    };
}
