// ── 属性修改命令测试：批量修改、只改指定字段、无变化不入栈、非法值被忽略、一次撤销 / 重做 ──────
#include "Core/Entity/LineEntity.hpp"
#include "Core/Entity/CircleEntity.hpp"
#include "Document/Command/ChangeAttrCommand.h"
#include "Document/CommandStack/CommandStack.h"
#include "Scene/Scene.h"
#include <cmath>
#include <cstdio>
#include <memory>
#include <vector>

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

    const EntityAttr& Attr(Scene& s, Object::ObjectID id) { return static_cast<Entity*>(s.GetEntity(id))->GetAttr(); }
}

int RunChangeAttrTests()
{
    g_failures = 0;

    Scene scene;
    CommandStack stack;

    const LayerID wall   = scene.GetLayerManager().AddLayer("墙");
    const LayerID window = scene.GetLayerManager().AddLayer("窗");

    auto addLine = [&](double y)
    {
        const auto id = scene.NextObjectID();
        scene.AddEntity(std::make_unique<LineEntity>(id, Math::Point3{ 0, y, 0 }, Math::Point3{ 10, y, 0 }));
        return id;
    };
    const auto a = addLine(0), b = addLine(1), c = addLine(2);
    const auto circle = scene.NextObjectID();
    scene.AddEntity(std::make_unique<CircleEntity>(circle, Math::Point3{ 0, 0, 0 }, 5.0));

    // ── 改图层：只改选中的，其余字段不动 ───────────────────────────────
    {
        AttrChange ch; ch.Layer = wall;
        Check(stack.Execute(std::make_unique<ChangeAttrCommand>(std::vector<Object::ObjectID>{ a, b }, ch), scene), "改图层：执行成功");
        Check(Attr(scene, a).LayerId == wall && Attr(scene, b).LayerId == wall && Attr(scene, c).LayerId != wall, "只有选中的对象换了图层");
        Check(Attr(scene, a).Lineweight == Lineweight::ByLayer && Attr(scene, a).Color.Method == ColorMethod::ByLayer, "其他字段保持不变");
        stack.Undo(scene);
        Check(Attr(scene, a).LayerId != wall && Attr(scene, b).LayerId != wall, "一次撤销还原整批");
        stack.Redo(scene);
        Check(Attr(scene, a).LayerId == wall && Attr(scene, b).LayerId == wall, "重做再次生效");
        stack.Undo(scene);
    }

    // ── 多个字段一起改 ─────────────────────────────────────────────────
    {
        AttrChange ch;
        ch.Layer         = window;
        ch.Color         = EntityColor::FromAci(1);
        ch.Lineweight    = Lineweight::W050;
        ch.Transparency  = Transparency::FromAlpha(128);
        ch.LinetypeScale = 2.5;
        ch.Visible       = false;
        ChangeAttrCommand cmd({ a, circle }, ch);
        Check(cmd.Execute(scene) && cmd.ChangedCount() == 2, "多字段、多种实体一次修改");
        const auto& x = Attr(scene, circle);
        Check(x.LayerId == window && x.Color.Method == ColorMethod::ByAci && x.Color.Aci == 1 && x.Lineweight == Lineweight::W050
              && !x.Transparency.ByLayer && x.Transparency.Alpha == 128 && x.LinetypeScale == 2.5 && !x.Visible, "各字段都写入");
        cmd.Undo(scene);
        const auto& y = Attr(scene, circle);
        Check(y.LayerId != window && y.Color.Method == ColorMethod::ByLayer && y.Lineweight == Lineweight::ByLayer
              && y.Transparency.ByLayer && y.LinetypeScale == 1.0 && y.Visible, "撤销还原全部字段");
        Check(cmd.GetName() == "修改特性", "多字段时命令名为“修改特性”");
    }

    // ── 无变化不入栈 ───────────────────────────────────────────────────
    {
        AttrChange ch; ch.Layer = scene.GetLayerManager().GetActiveLayerID();     // 对象本来就在默认图层
        // 先清掉栈里残留的撤销记录，便于判断
        stack.Clear();
        Check(!stack.Execute(std::make_unique<ChangeAttrCommand>(std::vector<Object::ObjectID>{ c }, ch), scene) && !stack.CanUndo(),
              "改成相同的值：不入撤销栈");

        AttrChange same; same.Color = EntityColor::ByLayer();
        Check(!stack.Execute(std::make_unique<ChangeAttrCommand>(std::vector<Object::ObjectID>{ a }, same), scene), "颜色本来就是随层：不入栈");
        Check(!stack.Execute(std::make_unique<ChangeAttrCommand>(std::vector<Object::ObjectID>{}, ch), scene), "没有对象：不入栈");
        Check(!stack.Execute(std::make_unique<ChangeAttrCommand>(std::vector<Object::ObjectID>{ 9999 }, AttrChange{ .Layer = wall }), scene), "对象不存在：不入栈");
        AttrChange none;
        Check(none.Empty() && !stack.Execute(std::make_unique<ChangeAttrCommand>(std::vector<Object::ObjectID>{ a }, none), scene), "空修改：不入栈");
    }

    // ── 非法值被忽略 ───────────────────────────────────────────────────
    {
        AttrChange bad; bad.Layer = 424242;                        // 不存在的图层
        Check(!stack.Execute(std::make_unique<ChangeAttrCommand>(std::vector<Object::ObjectID>{ a }, bad), scene), "不存在的图层 ID：忽略，没有改动");

        AttrChange mixed; mixed.Layer = 424242; mixed.Lineweight = Lineweight::W100; mixed.LinetypeScale = -1.0; mixed.LineType = 777;
        Check(stack.Execute(std::make_unique<ChangeAttrCommand>(std::vector<Object::ObjectID>{ a }, mixed), scene), "混合修改里的有效字段仍然生效");
        const auto& x = Attr(scene, a);
        Check(x.Lineweight == Lineweight::W100 && x.LayerId != 424242 && x.LinetypeScale == 1.0 && x.LineType == LineTypeTable::ByLayerID,
              "非法的图层 / 线型 / 线型比例被忽略，线宽照常修改");
        stack.Undo(scene);
    }

    // ── 线型 ───────────────────────────────────────────────────────────
    {
        AttrChange ch; ch.LineType = LineTypeTable::ContinuousID;
        Check(stack.Execute(std::make_unique<ChangeAttrCommand>(std::vector<Object::ObjectID>{ a }, ch), scene)
              && Attr(scene, a).LineType == LineTypeTable::ContinuousID, "改线型（Continuous）");
        stack.Undo(scene);
        Check(Attr(scene, a).LineType == LineTypeTable::ByLayerID, "撤销还原线型");
    }

    // ── 脏标记：只标被改的实体 ─────────────────────────────────────────
    {
        scene.ClearDirty();
        AttrChange ch; ch.Lineweight = Lineweight::W025;
        ChangeAttrCommand cmd({ b }, ch);
        cmd.Execute(scene);
        Check(scene.GetDirtyEntities().count(b) == 1 && scene.GetDirtyEntities().size() == 1 && !scene.IsAllDirty(),
              "只标记被修改的实体为脏，不触发全场景重建");
        cmd.Undo(scene);
    }

    // ── MatchProp 用法：把一个对象的属性整体刷给其他对象 ─────────────────
    {
        AttrChange src; src.Layer = wall; src.Color = EntityColor::FromAci(3); src.Lineweight = Lineweight::W070;
        ChangeAttrCommand setup({ a }, src);
        setup.Execute(scene);
        ChangeAttrCommand match({ b, c }, AttrChange::FromAttr(Attr(scene, a)));
        Check(match.Execute(scene) && match.ChangedCount() == 2, "FromAttr：取源对象的全部常规属性");
        Check(Attr(scene, c).LayerId == wall && Attr(scene, c).Color.Aci == 3 && Attr(scene, c).Lineweight == Lineweight::W070, "目标对象的属性与源一致");
    }

    return g_failures;
}
