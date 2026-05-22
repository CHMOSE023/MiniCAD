#include "ConstraintEngine.h"

namespace MiniCAD
{
    bool ConstraintEngine::Apply(const ConstraintContext& ctx)
    {
        m_hasResult = false;
        m_guideLine = { {}, {} };

        ConstraintResult r;

        // 正交优先；正交未启用时尝试极轴（二者互斥，实际只有一个会生效）
        if (!m_ortho.Apply(ctx, r))
            m_polar.Apply(ctx, r);

        if (r.hasResult)
        {
            m_resultPoint = r.point;
            m_guideLine   = { ctx.anchor, r.lineEnd };
            m_hasResult   = true;
        }

        return m_hasResult;
    }

    void ConstraintEngine::EnableOrtho(bool on)
    {
        m_ortho.SetEnabled(on);
        if (on) m_polar.SetEnabled(false);
    }

    void ConstraintEngine::EnablePolar(bool on)
    {
        m_polar.SetEnabled(on);
        if (on) m_ortho.SetEnabled(false);
    }
}
