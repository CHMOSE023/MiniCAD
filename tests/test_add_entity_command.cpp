// ============================================================
// MiniCAD — tests/Command/test_AddEntityCommand.cpp
// 覆盖：Execute / Undo / Redo / 边界 / 所有权转移
// ============================================================
#include <gtest/gtest.h>
#include "app/Command/AddEntityCommand.h"
#include "app/Scene/Scene.h"
#include "core/Entity/LineEntity.h"
#include <core/Object/Object.hpp>
#include <math/Point.hpp>

namespace MiniCAD {

    // ────────────────────────────────────────────────────────────
    // 辅助
    // ────────────────────────────────────────────────────────────
    static std::unique_ptr<LineEntity> MakeLine(
        Object::ObjectID id,
        const Point3& from = { 0,0,0 },
        const Point3& to = { 1,1,0 })
    {
        return std::make_unique<LineEntity>(id, from, to);
    }

    // ────────────────────────────────────────────────────────────
    // Fixture
    // ────────────────────────────────────────────────────────────
    class AddEntityCommandTest : public ::testing::Test {
    protected:
        Scene scene;
    };

    // ════════════════════════════════════════════════════════════
    // 1. Execute
    // ════════════════════════════════════════════════════════════
    TEST_F(AddEntityCommandTest, Execute_AddsEntityToScene) {
        AddEntityCommand cmd(scene, MakeLine(1));
        cmd.Execute();
        EXPECT_TRUE(scene.Has(1));
        EXPECT_EQ(scene.EntityCount(), 1);
    }

    TEST_F(AddEntityCommandTest, Execute_MarksDirty) {
        AddEntityCommand cmd(scene, MakeLine(2));
        cmd.Execute();
        EXPECT_TRUE(scene.IsDirty());
    }

    TEST_F(AddEntityCommandTest, Execute_TransfersOwnership) {
        // Execute 后命令内部 m_entity 应为 nullptr（所有权转给 Scene）
        auto entity = MakeLine(3);
        LineEntity* raw = entity.get();
        AddEntityCommand cmd(scene, std::move(entity));
        cmd.Execute();
        // Scene 里拿到的指针与原始指针相同
        EXPECT_EQ(scene.GetEntity(3), raw);
    }

    TEST_F(AddEntityCommandTest, Execute_GetDescription) {
        AddEntityCommand cmd(scene, MakeLine(4));
        EXPECT_EQ(cmd.GetDescription(), "Add Entity");
    }

    // ════════════════════════════════════════════════════════════
    // 2. Undo
    // ════════════════════════════════════════════════════════════
    TEST_F(AddEntityCommandTest, Undo_RemovesEntityFromScene) {
        AddEntityCommand cmd(scene, MakeLine(10));
        cmd.Execute();
        cmd.Undo();
        EXPECT_FALSE(scene.Has(10));
        EXPECT_EQ(scene.EntityCount(), 0);
    }

    TEST_F(AddEntityCommandTest, Undo_RestoresOwnershipToCommand) {
        AddEntityCommand cmd(scene, MakeLine(11));
        cmd.Execute();
        scene.ClearDirty();
        cmd.Undo();
        // Undo 之后 Scene 里没有该实体
        EXPECT_EQ(scene.GetEntity(11), nullptr);
    }

    TEST_F(AddEntityCommandTest, Undo_WithoutExecute_DoesNotCrash) {
        // Undo 在未 Execute 前调用：Scene 中没有该 ID，RemoveEntity 返回 nullptr
        AddEntityCommand cmd(scene, MakeLine(12));
        EXPECT_NO_THROW(cmd.Undo());
        EXPECT_EQ(scene.EntityCount(), 0);
    }

    // ════════════════════════════════════════════════════════════
    // 3. Execute → Undo → Execute（Redo）
    // ════════════════════════════════════════════════════════════
    TEST_F(AddEntityCommandTest, ExecuteUndoExecute_EntityBackInScene) {
        AddEntityCommand cmd(scene, MakeLine(20));
        cmd.Execute();
        EXPECT_TRUE(scene.Has(20));

        cmd.Undo();
        EXPECT_FALSE(scene.Has(20));

        // 第二次 Execute（Redo 路径）：m_entity 已被 Undo 取回
        cmd.Execute();
        EXPECT_TRUE(scene.Has(20));
    }

    TEST_F(AddEntityCommandTest, ExecuteUndoExecute_MultipleRounds) {
        AddEntityCommand cmd(scene, MakeLine(21));
        for (int i = 0; i < 5; ++i) {
            cmd.Execute();
            EXPECT_TRUE(scene.Has(21));
            cmd.Undo();
            EXPECT_FALSE(scene.Has(21));
        }
    }

    // ════════════════════════════════════════════════════════════
    // 4. 边界：nullptr 实体
    // ════════════════════════════════════════════════════════════
    TEST_F(AddEntityCommandTest, NullEntity_ExecuteDoesNothing) {
        AddEntityCommand cmd(scene, nullptr);
        EXPECT_NO_THROW(cmd.Execute());
        EXPECT_EQ(scene.EntityCount(), 0);
        EXPECT_FALSE(scene.IsDirty());
    }

    // ════════════════════════════════════════════════════════════
    // 5. DirtyCallback 触发
    // ════════════════════════════════════════════════════════════
    TEST_F(AddEntityCommandTest, Execute_TriggersDirtyCallback) {
        int count = 0;
        scene.SetDirtyCallback([&]() { ++count; });
        AddEntityCommand cmd(scene, MakeLine(30));
        cmd.Execute();
        EXPECT_EQ(count, 1);
    }

    TEST_F(AddEntityCommandTest, Undo_TriggersDirtyCallback) {
        AddEntityCommand cmd(scene, MakeLine(31));
        cmd.Execute();
        int count = 0;
        scene.SetDirtyCallback([&]() { ++count; });
        cmd.Undo();
        EXPECT_EQ(count, 1);
    }

} // namespace MiniCAD