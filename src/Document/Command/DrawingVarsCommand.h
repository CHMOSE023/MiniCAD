#pragma once
#include "Document/CommandStack/ICommand.h"
#include "Scene/Scene.h"

namespace MiniCAD
{
    // 图纸级显示变量（HEADER 的 $PDMODE / $PDSIZE / $LTSCALE）。
    // 只影响显示、不改几何 ⇒ 应用时用 Scene::MarkDisplayDirty()，拾取索引不动。
    struct DrawingVars
    {
        int    PointMode     = 0;      // $PDMODE
        double PointSize     = 0.0;    // $PDSIZE
        double LineTypeScale = 1.0;    // $LTSCALE

        static DrawingVars From(const Scene& scene)
        {
            return { scene.GetPointDisplayMode(), scene.GetPointDisplaySize(), scene.GetLineTypeScale() };
        }
        void ApplyTo(Scene& scene) const
        {
            scene.SetPointDisplayMode(PointMode);
            scene.SetPointDisplaySize(PointSize);
            scene.SetLineTypeScale(LineTypeScale);
            scene.MarkDisplayDirty();
        }
        bool operator==(const DrawingVars&) const = default;
    };

    // 修改图纸级显示变量（点样式、线型比例对话框），可撤销
    class SetDrawingVarsCommand : public ICommand
    {
    public:
        SetDrawingVarsCommand(const DrawingVars& before, const DrawingVars& after, std::string name)
            : m_before(before), m_after(after), m_name(std::move(name)) {}

        bool Execute(Scene& scene) override
        {
            m_after.ApplyTo(scene);
            return true;
        }
        void Undo(Scene& scene) override { m_before.ApplyTo(scene); }
        std::string GetName() const override { return m_name; }

    private:
        DrawingVars m_before;
        DrawingVars m_after;
        std::string m_name;
    };
}
