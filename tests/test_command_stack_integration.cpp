// ============================================================
// MiniCAD — tests/Command/test_CommandStack_Integration.cpp
// 集成测试：CommandStack + Add / ChangeAttr / Delete 完整流程
// ============================================================
#include <gtest/gtest.h>
#include "doc/UndoRedo/CommandStack.h"
#include "app/Command/AddEntityCommand.h"
#include "app/Command/DeleteEntityCommand.h"
#include "app/Command/ChangeAttrCommand.h"
#include "app/Scene/Scene.h"
#include "core/Entity/LineEntity.h"
#include "core/Entity/EntityAttr.h"
#include <memory>
#include <core/Object/Object.hpp>
#include <math/Point.hpp>

namespace MiniCAD {

    // ────────────────────────────────────────────────────────────
    // 辅助
    // ────────────────────────────────────────────────────────────
    static std::unique_ptr<LineEntity> MakeLine(Object::ObjectID id) {
        return std::make_unique<LineEntity>(id, Point3{ 0,0,0 }, Point3{ 1,1,0 });
    }

    static EntityAttr AttrWithColor(Color4 c) {
        EntityAttr a;
        a.color = c;
        return a;
    }

    static const EntityAttr& GetAttr(Scene& scene, Object::ObjectID id) {
        return static_cast<LineEntity*>(scene.GetEntity(id))->GetAttr();
    }

    // ────────────────────────────────────────────────────────────
    // Fixture
    // ────────────────────────────────────────────────────────────
    class CommandStackIntegrationTest : public ::testing::Test {
    protected:
        Scene        scene;
        CommandStack stack;
    };

    // ════════════════════════════════════════════════════════════
    // 1. Add → ChangeAttr → Delete，全程 Undo × 3 还原到空场景
    // ════════════════════════════════════════════════════════════
    TEST_F(CommandStackIntegrationTest, AddChangeDelete_UndoAll_SceneEmpty) {
        // --- 操作序列 ---
        stack.Push(std::make_unique<AddEntityCommand>(scene, MakeLine(1)));
        EXPECT_TRUE(scene.Has(1));

        stack.Push(std::make_unique<ChangeAttrCommand>(scene, 1, AttrWithColor(Color4::Red())));
        EXPECT_EQ(GetAttr(scene, 1).color.r, 255);

        stack.Push(std::make_unique<DeleteEntityCommand>(scene, 1));
        EXPECT_FALSE(scene.Has(1));

        // --- Undo × 3 ---
        stack.Undo(); // 撤销 Delete → 实体回来，颜色仍是 Red
        ASSERT_TRUE(scene.Has(1));
        EXPECT_EQ(GetAttr(scene, 1).color.r, 255);

        stack.Undo(); // 撤销 ChangeAttr → 颜色恢复 White
        EXPECT_EQ(GetAttr(scene, 1).color.r, 255); // White.r == 255，用 g/b 区分
        EXPECT_EQ(GetAttr(scene, 1).color.g, 255);
        EXPECT_EQ(GetAttr(scene, 1).color.b, 255);

        stack.Undo(); // 撤销 Add → 场景为空
        EXPECT_FALSE(scene.Has(1));
        EXPECT_EQ(scene.EntityCount(), 0);
    }

    // ════════════════════════════════════════════════════════════
    // 2. Undo 全部后 Redo × 3 重放回最终状态
    // ════════════════════════════════════════════════════════════
    TEST_F(CommandStackIntegrationTest, UndoAll_ThenRedoAll_RestoresFinalState) {
        stack.Push(std::make_unique<AddEntityCommand>(scene, MakeLine(2)));
        stack.Push(std::make_unique<ChangeAttrCommand>(scene, 2, AttrWithColor(Color4::Red())));
        stack.Push(std::make_unique<DeleteEntityCommand>(scene, 2));

        stack.Undo(); stack.Undo(); stack.Undo();
        EXPECT_EQ(scene.EntityCount(), 0);

        stack.Redo(); // Add
        EXPECT_TRUE(scene.Has(2));

        stack.Redo(); // ChangeAttr
        EXPECT_EQ(GetAttr(scene, 2).color.r, 255);
        EXPECT_EQ(GetAttr(scene, 2).color.g, 0);

        stack.Redo(); // Delete
        EXPECT_FALSE(scene.Has(2));
        EXPECT_EQ(scene.EntityCount(), 0);
    }

    // ════════════════════════════════════════════════════════════
    // 3. Undo 途中 Push 新命令，Redo 栈清空
    // ════════════════════════════════════════════════════════════
    TEST_F(CommandStackIntegrationTest, PushAfterUndo_ClearsRedoStack) {
        stack.Push(std::make_unique<AddEntityCommand>(scene, MakeLine(3)));
        stack.Push(std::make_unique<ChangeAttrCommand>(scene, 3, AttrWithColor(Color4::Red())));

        stack.Undo(); // 撤销 ChangeAttr
        EXPECT_EQ(stack.RedoCount(), 1);

        // 插入新命令：Redo 栈应被清空
        stack.Push(std::make_unique<ChangeAttrCommand>(scene, 3, AttrWithColor(Color4::Black())));
        EXPECT_EQ(stack.RedoCount(), 0);
        EXPECT_EQ(GetAttr(scene, 3).color.r, 0); // Black
    }

    // ════════════════════════════════════════════════════════════
    // 4. CanUndo / CanRedo 状态随操作正确变化
    // ════════════════════════════════════════════════════════════
    TEST_F(CommandStackIntegrationTest, CanUndoRedo_StateTransitions) {
        EXPECT_FALSE(stack.CanUndo());
        EXPECT_FALSE(stack.CanRedo());

        stack.Push(std::make_unique<AddEntityCommand>(scene, MakeLine(4)));
        EXPECT_TRUE(stack.CanUndo());
        EXPECT_FALSE(stack.CanRedo());

        stack.Undo();
        EXPECT_FALSE(stack.CanUndo());
        EXPECT_TRUE(stack.CanRedo());

        stack.Redo();
        EXPECT_TRUE(stack.CanUndo());
        EXPECT_FALSE(stack.CanRedo());
    }

    // ════════════════════════════════════════════════════════════
    // 5. UndoCount / RedoCount 计数正确
    // ════════════════════════════════════════════════════════════
    TEST_F(CommandStackIntegrationTest, UndoRedoCount_Accurate) {
        stack.Push(std::make_unique<AddEntityCommand>(scene, MakeLine(5)));
        stack.Push(std::make_unique<ChangeAttrCommand>(scene, 5, AttrWithColor(Color4::Red())));
        EXPECT_EQ(stack.UndoCount(), 2);
        EXPECT_EQ(stack.RedoCount(), 0);

        stack.Undo();
        EXPECT_EQ(stack.UndoCount(), 1);
        EXPECT_EQ(stack.RedoCount(), 1);

        stack.Undo();
        EXPECT_EQ(stack.UndoCount(), 0);
        EXPECT_EQ(stack.RedoCount(), 2);
    }

    // ════════════════════════════════════════════════════════════
    // 6. CurrentDescription 返回最新可 Undo 命令的描述
    // ════════════════════════════════════════════════════════════
    TEST_F(CommandStackIntegrationTest, CurrentDescription_ReflectsTopCommand) {
        EXPECT_EQ(stack.CurrentDescription(), "");

        stack.Push(std::make_unique<AddEntityCommand>(scene, MakeLine(6)));
        EXPECT_EQ(stack.CurrentDescription(), "Add Entity");

        stack.Push(std::make_unique<ChangeAttrCommand>(scene, 6, AttrWithColor(Color4::Red())));
        EXPECT_EQ(stack.CurrentDescription(), "Change Attribute");

        stack.Undo();
        EXPECT_EQ(stack.CurrentDescription(), "Add Entity");
    }

    // ════════════════════════════════════════════════════════════
    // 7. 容量限制：超过 capacity 淘汰最旧命令
    // ════════════════════════════════════════════════════════════
    TEST_F(CommandStackIntegrationTest, Capacity_OldestCommandEvicted) {
        CommandStack smallStack(3);
        Scene s;

        smallStack.Push(std::make_unique<AddEntityCommand>(s, MakeLine(10)));
        smallStack.Push(std::make_unique<AddEntityCommand>(s, MakeLine(11)));
        smallStack.Push(std::make_unique<AddEntityCommand>(s, MakeLine(12)));
        EXPECT_EQ(smallStack.UndoCount(), 3);

        // 第 4 条命令推入，最旧的 Add(10) 被淘汰
        smallStack.Push(std::make_unique<AddEntityCommand>(s, MakeLine(13)));
        EXPECT_EQ(smallStack.UndoCount(), 3); // 容量上限

        // Undo × 3 只能回到 Add(11) 之前，Add(10) 已不可撤销
        smallStack.Undo(); smallStack.Undo(); smallStack.Undo();
        EXPECT_FALSE(smallStack.CanUndo());

        // 10 仍在场景里（无法撤销）
        EXPECT_TRUE(s.Has(10));
    }

    // ════════════════════════════════════════════════════════════
    // 8. Clear 后状态归零
    // ════════════════════════════════════════════════════════════
    TEST_F(CommandStackIntegrationTest, Clear_ResetsStack) {
        stack.Push(std::make_unique<AddEntityCommand>(scene, MakeLine(20)));
        stack.Push(std::make_unique<AddEntityCommand>(scene, MakeLine(21)));
        stack.Clear();

        EXPECT_FALSE(stack.CanUndo());
        EXPECT_FALSE(stack.CanRedo());
        EXPECT_EQ(stack.UndoCount(), 0);
        EXPECT_EQ(stack.RedoCount(), 0);
        EXPECT_EQ(stack.CurrentDescription(), "");
        // Scene 里的实体仍在（Clear 不回滚）
        EXPECT_EQ(scene.EntityCount(), 2);
    }

    // ════════════════════════════════════════════════════════════
    // 9. 多实体并行操作互不干扰
    // ════════════════════════════════════════════════════════════
    TEST_F(CommandStackIntegrationTest, MultipleEntities_IndependentUndoRedo) {
        stack.Push(std::make_unique<AddEntityCommand>(scene, MakeLine(30)));
        stack.Push(std::make_unique<AddEntityCommand>(scene, MakeLine(31)));
        stack.Push(std::make_unique<ChangeAttrCommand>(scene, 30, AttrWithColor(Color4::Red())));
        stack.Push(std::make_unique<ChangeAttrCommand>(scene, 31, AttrWithColor(Color4::Black())));

        // Undo ChangeAttr(31)
        stack.Undo();
        EXPECT_EQ(GetAttr(scene, 31).color.r, 255); // 还原 White
        EXPECT_EQ(GetAttr(scene, 30).color.r, 255); // Red 的 r 也是 255，用 g 区分
        EXPECT_EQ(GetAttr(scene, 30).color.g, 0);   // Red

        // Undo ChangeAttr(30)
        stack.Undo();
        EXPECT_EQ(GetAttr(scene, 30).color.g, 255); // 还原 White

        // 两个实体仍在场景中
        EXPECT_TRUE(scene.Has(30));
        EXPECT_TRUE(scene.Has(31));
    }

} // namespace MiniCAD