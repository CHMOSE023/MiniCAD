// ── 图层命令测试：新建 / 删除 / 修改特性的撤销重做、ID 稳定、非法修改被拒绝 ──────────────────
#include "Core/Entity/LineEntity.hpp"
#include "Document/Command/LayerCommands.h"
#include "Document/CommandStack/CommandStack.h"
#include "Scene/Scene.h"
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
}

int RunLayerCommandTests()
{
    g_failures = 0;

    Scene scene;
    CommandStack stack;
    LayerManager& lm = scene.GetLayerManager();

    // ── 新建 ───────────────────────────────────────────────────────────
    auto add = std::make_unique<AddLayerCommand>("墙", true);
    AddLayerCommand* addRaw = add.get();
    Check(stack.Execute(std::move(add), scene), "新建图层");
    const LayerID wall = addRaw->GetLayerID();
    Check(lm.GetLayer(wall) && lm.GetLayer(wall)->GetName() == "墙" && lm.GetActiveLayerID() == wall, "新建的图层存在并成为当前图层");

    Check(!stack.Execute(std::make_unique<AddLayerCommand>("墙"), scene), "重名的图层不能新建");
    Check(!stack.Execute(std::make_unique<AddLayerCommand>(""), scene), "空名称不能新建");

    // ── 修改特性（可撤销、可重做）──────────────────────────────────────
    {
        LayerChange c; c.Color = Math::Color4{ 1, 0, 0, 1 }; c.Visible = false; c.Locked = true; c.Lineweight = Lineweight::W050;
        c.LineType = LineTypeTable::ContinuousID;
        Check(stack.Execute(std::make_unique<ChangeLayerCommand>(wall, c), scene), "一次修改多个特性");
        const Layer* l = lm.GetLayer(wall);
        Check(l->GetColor().r == 1.0 && l->GetColor().g == 0.0 && !l->IsVisible() && l->IsLocked() && l->GetLineweight() == Lineweight::W050, "各特性都写入");

        scene.ClearDirty();
        stack.Undo(scene);
        l = lm.GetLayer(wall);
        Check(l->IsVisible() && !l->IsLocked() && l->GetColor().g == 1.0 && l->GetLineweight() == Lineweight::Default, "撤销还原全部特性");
        Check(scene.IsAllDirty(), "图层特性变化触发全部实体的显示重建（几何版本不变）");
        stack.Redo(scene);
        Check(!lm.GetLayer(wall)->IsVisible() && lm.GetLayer(wall)->IsLocked(), "重做再次生效");
        stack.Undo(scene);
    }

    {
        LayerChange same; same.Visible = true;                          // 本来就是可见
        Check(!stack.Execute(std::make_unique<ChangeLayerCommand>(wall, same), scene), "没有变化：不入撤销栈");
        LayerChange bad; bad.LineType = 9999;
        Check(!stack.Execute(std::make_unique<ChangeLayerCommand>(wall, bad), scene), "不存在的线型被忽略");
        LayerChange byLayer; byLayer.LineType = LineTypeTable::ByLayerID;
        Check(!stack.Execute(std::make_unique<ChangeLayerCommand>(wall, byLayer), scene), "图层线型不能是 ByLayer");
        LayerChange none;
        Check(!stack.Execute(std::make_unique<ChangeLayerCommand>(wall, none), scene), "空修改不入栈");
        Check(!stack.Execute(std::make_unique<ChangeLayerCommand>(424242, LayerChange{ .Visible = false }), scene), "图层不存在：失败");
    }

    // ── 改名 ───────────────────────────────────────────────────────────
    {
        LayerChange rename; rename.Name = "外墙";
        Check(stack.Execute(std::make_unique<ChangeLayerCommand>(wall, rename), scene) && lm.GetLayer(wall)->GetName() == "外墙", "改名");
        stack.Undo(scene);
        Check(lm.GetLayer(wall)->GetName() == "墙", "撤销改名");

        LayerChange dup; dup.Name = "Default";
        Check(!stack.Execute(std::make_unique<ChangeLayerCommand>(wall, dup), scene), "不能改成已有的名字");
        LayerChange empty; empty.Name = "";
        Check(!stack.Execute(std::make_unique<ChangeLayerCommand>(wall, empty), scene), "不能改成空名");
        LayerChange def; def.Name = "别名";
        Check(!stack.Execute(std::make_unique<ChangeLayerCommand>(Layer::DefaultLayerID, def), scene), "0 层不能改名");

        // 改名被拒绝时，同一次修改里其他合法的字段仍然生效
        LayerChange mixed; mixed.Name = "Default"; mixed.Locked = true;
        Check(stack.Execute(std::make_unique<ChangeLayerCommand>(wall, mixed), scene) && lm.GetLayer(wall)->IsLocked() && lm.GetLayer(wall)->GetName() == "墙",
              "非法的名称被忽略，锁定照常生效");
        stack.Undo(scene);
    }

    // ── 删除：实体改到 0 层，撤销全部还原 ───────────────────────────────
    {
        auto addLine = [&](double y, LayerID layer)
        {
            const auto id = scene.NextObjectID();
            auto e = std::make_unique<LineEntity>(id, Math::Point3{ 0, y, 0 }, Math::Point3{ 10, y, 0 });
            e->SetLayerId(layer);
            scene.AddEntity(std::move(e));
            return id;
        };
        const auto onWall1 = addLine(0, wall), onWall2 = addLine(1, wall), onDefault = addLine(2, Layer::DefaultLayerID);
        lm.SetActiveLayerID(wall);

        auto del = std::make_unique<DeleteLayerCommand>(wall);
        DeleteLayerCommand* delRaw = del.get();
        Check(stack.Execute(std::move(del), scene) && delRaw->MovedCount() == 2, "删除图层：2 个对象被移到 0 层");
        auto layerOf = [&](Object::ObjectID id) { return static_cast<Entity*>(scene.GetEntity(id))->GetLayerID(); };
        Check(lm.GetLayer(wall) == nullptr && layerOf(onWall1) == Layer::DefaultLayerID && layerOf(onWall2) == Layer::DefaultLayerID, "图层没了，对象没有悬空引用");
        Check(lm.GetActiveLayerID() == Layer::DefaultLayerID, "删除当前图层后当前图层退回 0 层");

        stack.Undo(scene);
        Check(lm.GetLayer(wall) && lm.GetLayer(wall)->GetName() == "墙" && layerOf(onWall1) == wall && layerOf(onWall2) == wall
              && layerOf(onDefault) == Layer::DefaultLayerID, "撤销：图层、对象的归属都还原，原来就在 0 层的对象不受影响");
        Check(lm.GetActiveLayerID() == wall, "撤销：当前图层也还原");
        stack.Redo(scene);
        Check(lm.GetLayer(wall) == nullptr && layerOf(onWall1) == Layer::DefaultLayerID, "重做删除");
        stack.Undo(scene);

        Check(!stack.Execute(std::make_unique<DeleteLayerCommand>(Layer::DefaultLayerID), scene), "0 层不能删除");
        Check(!stack.Execute(std::make_unique<DeleteLayerCommand>(424242), scene), "不存在的图层：失败");
    }

    // ── 撤销新建后重做：同一个 ID ──────────────────────────────────────
    {
        stack.Clear();
        auto a = std::make_unique<AddLayerCommand>("窗", true);
        AddLayerCommand* raw = a.get();
        stack.Execute(std::move(a), scene);
        const LayerID id = raw->GetLayerID();
        const LayerID prev = lm.GetActiveLayerID();
        Check(prev == id, "新建后是当前图层");

        // 图层被后续命令改过，撤销新建时快照取撤销时的状态
        LayerChange c; c.Color = Math::Color4{ 0, 0, 1, 1 };
        stack.Execute(std::make_unique<ChangeLayerCommand>(id, c), scene);
        stack.Undo(scene);                                                 // 撤销改颜色
        stack.Undo(scene);                                                 // 撤销新建
        Check(lm.GetLayer(id) == nullptr && lm.GetActiveLayerID() != id, "撤销新建：图层消失，当前图层不再指向它");
        stack.Redo(scene);
        Check(lm.GetLayer(id) && lm.GetLayer(id)->GetName() == "窗" && lm.GetActiveLayerID() == id, "重做新建：同一个 ID，重新成为当前");
        const LayerID next = lm.AddLayer("新的");
        Check(next != id, "之后新建的图层不会和已用过的 ID 冲突");
    }

    // ── 工具：不重名的默认名 ───────────────────────────────────────────
    {
        const std::string n = LayerOps::UniqueName(lm);
        Check(!LayerOps::NameTaken(lm, n), "UniqueName 给出不重名的名字");
    }

    return g_failures;
}
