// ============================================================
// MiniCAD — tests/Command/test_DeleteEntityCommand.cpp
// 覆盖：Execute / Undo / Redo / 所有权 / 边界
// ============================================================
#include <gtest/gtest.h>
#include "app/Command/DeleteEntityCommand.h"
#include "app/Scene/Scene.h"
#include "core/Entity/LineEntity.h"

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
    // Fixture：每个测试预先在 Scene 里放好实体
    // ────────────────────────────────────────────────────────────
    class DeleteEntityCommandTest : public ::testing::Test {
    protected:
        Scene scene;

        void SetUp() override {
            scene.AddEntity(MakeLine(1));
            scene.AddEntity(MakeLine(2));
            scene.ClearDirty();
        }
    };

    // ════════════════════════════════════════════════════════════
    // 1. Execute
    // ════════════════════════════════════════════════════════════
    TEST_F(DeleteEntityCommandTest, Execute_RemovesEntityFromScene) {
        DeleteEntityCommand cmd(scene, 1);
        cmd.Execute();
        EXPECT_FALSE(scene.Has(1));
    }

    TEST_F(DeleteEntityCommandTest, Execute_DecrementsCount) {
        DeleteEntityCommand cmd(scene, 1);
        cmd.Execute();
        EXPECT_EQ(scene.EntityCount(), 1);
    }

    TEST_F(DeleteEntityCommandTest, Execute_MarksDirty) {
        DeleteEntityCommand cmd(scene, 1);
        cmd.Execute();
        EXPECT_TRUE(scene.IsDirty());
    }

    TEST_F(DeleteEntityCommandTest, Execute_TakesOwnershipOfEntity) {
        // Execute 后命令持有被删除实体，Scene 中不再有它
        DeleteEntityCommand cmd(scene, 1);
        cmd.Execute();
        EXPECT_EQ(scene.GetEntity(1), nullptr);
    }

    TEST_F(DeleteEntityCommandTest, Execute_NonExistentID_DoesNotCrash) {
        DeleteEntityCommand cmd(scene, 9999);
        EXPECT_NO_THROW(cmd.Execute());
        EXPECT_EQ(scene.EntityCount(), 2); // 原有实体不受影响
    }

    TEST_F(DeleteEntityCommandTest, Execute_GetDescription) {
        DeleteEntityCommand cmd(scene, 1);
        EXPECT_EQ(cmd.GetDescription(), "Delete Entity");
    }

    // ════════════════════════════════════════════════════════════
    // 2. Undo
    // ════════════════════════════════════════════════════════════
    TEST_F(DeleteEntityCommandTest, Undo_RestoresEntityToScene) {
        DeleteEntityCommand cmd(scene, 1);
        cmd.Execute();
        cmd.Undo();
        EXPECT_TRUE(scene.Has(1));
    }

    TEST_F(DeleteEntityCommandTest, Undo_RestoresCorrectID) {
        DeleteEntityCommand cmd(scene, 1);
        cmd.Execute();
        cmd.Undo();
        Object* obj = scene.GetEntity(1);
        ASSERT_NE(obj, nullptr);
        EXPECT_EQ(obj->GetID(), 1u);
    }

    TEST_F(DeleteEntityCommandTest, Undo_RestoresCount) {
        DeleteEntityCommand cmd(scene, 1);
        cmd.Execute();
        EXPECT_EQ(scene.EntityCount(), 1);
        cmd.Undo();
        EXPECT_EQ(scene.EntityCount(), 2);
    }

    TEST_F(DeleteEntityCommandTest, Undo_WithoutExecute_DoesNotCrash) {
        // m_entity 为 nullptr，Undo 应静默跳过
        DeleteEntityCommand cmd(scene, 1);
        EXPECT_NO_THROW(cmd.Undo());
        EXPECT_EQ(scene.EntityCount(), 2);
    }

    // ════════════════════════════════════════════════════════════
    // 3. Redo
    // ════════════════════════════════════════════════════════════
    TEST_F(DeleteEntityCommandTest, Redo_RemovesEntityAgain) {
        DeleteEntityCommand cmd(scene, 1);
        cmd.Execute();
        cmd.Undo();
        EXPECT_TRUE(scene.Has(1));
        cmd.Redo();
        EXPECT_FALSE(scene.Has(1));
    }

    TEST_F(DeleteEntityCommandTest, Redo_IsEquivalentToExecute) {
        DeleteEntityCommand cmd(scene, 1);
        cmd.Execute();
        cmd.Undo();
        cmd.Redo();
        EXPECT_EQ(scene.EntityCount(), 1);
    }

    // ════════════════════════════════════════════════════════════
    // 4. Execute → Undo → Redo 多轮
    // ════════════════════════════════════════════════════════════
    TEST_F(DeleteEntityCommandTest, MultipleUndoRedo_Consistent) {
        DeleteEntityCommand cmd(scene, 1);
        for (int i = 0; i < 5; ++i) {
            cmd.Execute();
            EXPECT_FALSE(scene.Has(1));
            cmd.Undo();
            EXPECT_TRUE(scene.Has(1));
        }
    }

    // ════════════════════════════════════════════════════════════
    // 5. DirtyCallback
    // ════════════════════════════════════════════════════════════
    TEST_F(DeleteEntityCommandTest, Execute_TriggersDirtyCallback) {
        int count = 0;
        scene.SetDirtyCallback([&]() { ++count; });
        DeleteEntityCommand cmd(scene, 1);
        cmd.Execute();
        EXPECT_EQ(count, 1);
    }

    TEST_F(DeleteEntityCommandTest, Undo_TriggersDirtyCallback) {
        DeleteEntityCommand cmd(scene, 1);
        cmd.Execute();
        int count = 0;
        scene.SetDirtyCallback([&]() { ++count; });
        cmd.Undo();
        EXPECT_EQ(count, 1);
    }

} // namespace MiniCAD