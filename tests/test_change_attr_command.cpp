// ============================================================
// MiniCAD — tests/Command/test_ChangeAttrCommand.cpp
// 覆盖：Execute / Undo / 旧属性保存 / 边界（非LineEntity / 不存在ID）
// ============================================================

#include <gtest/gtest.h>
#include "app/Command/ChangeAttrCommand.h"
#include "app/Scene/Scene.h"
#include "core/Entity/LineEntity.h"
#include "core/Entity/EntityAttr.h"
#include <memory>
#include <core/Object/Object.hpp>

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

    static EntityAttr MakeAttr(Color4 color, Real lineWidth = 1.0f,
        LineType lineType = LineType::SOLID)
    {
        EntityAttr a;
        a.color = color;
        a.lineWidth = lineWidth;
        a.lineType = lineType;
        return a;
    }

    // ────────────────────────────────────────────────────────────
    // Fixture
    // ────────────────────────────────────────────────────────────
    class ChangeAttrCommandTest : public ::testing::Test {
    protected:
        Scene    scene;
        static constexpr Object::ObjectID kID = 1;

        EntityAttr m_originalAttr;

        void SetUp() override {
            auto line = MakeLine(kID);
            // 设置初始属性
            m_originalAttr = MakeAttr(Color4::White(), 1.0f, LineType::SOLID);
            line->SetAttr(m_originalAttr);
            scene.AddEntity(std::move(line));
            scene.ClearDirty();
        }

        // 获取 Scene 中实体当前属性的便捷函数
        EntityAttr CurrentAttr() {
            auto* obj = scene.GetEntity(kID);
            EXPECT_NE(obj, nullptr);
            return static_cast<LineEntity*>(obj)->GetAttr();
        }
    };

    // ════════════════════════════════════════════════════════════
    // 1. Execute：应用新属性
    // ════════════════════════════════════════════════════════════
    TEST_F(ChangeAttrCommandTest, Execute_AppliesNewColor) {
        EntityAttr newAttr = MakeAttr(Color4::Red());
        ChangeAttrCommand cmd(scene, kID, newAttr);
        cmd.Execute();

        EntityAttr cur = CurrentAttr();
        EXPECT_EQ(cur.color.r, 255);
        EXPECT_EQ(cur.color.g, 0);
        EXPECT_EQ(cur.color.b, 0);
    }

    TEST_F(ChangeAttrCommandTest, Execute_AppliesNewLineWidth) {
        EntityAttr newAttr = MakeAttr(Color4::White(), 3.0f);
        ChangeAttrCommand cmd(scene, kID, newAttr);
        cmd.Execute();
        EXPECT_FLOAT_EQ(CurrentAttr().lineWidth, 3.0f);
    }

    TEST_F(ChangeAttrCommandTest, Execute_AppliesNewLineType) {
        EntityAttr newAttr = MakeAttr(Color4::White(), 1.0f, LineType::DASHED);
        ChangeAttrCommand cmd(scene, kID, newAttr);
        cmd.Execute();
        EXPECT_EQ(CurrentAttr().lineType, LineType::DASHED);
    }

    TEST_F(ChangeAttrCommandTest, Execute_MarksDirty) {
        ChangeAttrCommand cmd(scene, kID, MakeAttr(Color4::Red()));
        cmd.Execute();
        EXPECT_TRUE(scene.IsDirty());
    }

    TEST_F(ChangeAttrCommandTest, Execute_GetDescription) {
        ChangeAttrCommand cmd(scene, kID, MakeAttr(Color4::Red()));
        EXPECT_EQ(cmd.GetDescription(), "Change Attribute");
    }

    // ════════════════════════════════════════════════════════════
    // 2. Undo：还原旧属性
    // ════════════════════════════════════════════════════════════
    TEST_F(ChangeAttrCommandTest, Undo_RestoresOriginalColor) {
        ChangeAttrCommand cmd(scene, kID, MakeAttr(Color4::Red()));
        cmd.Execute();
        cmd.Undo();

        EntityAttr cur = CurrentAttr();
        EXPECT_EQ(cur.color.r, m_originalAttr.color.r);
        EXPECT_EQ(cur.color.g, m_originalAttr.color.g);
        EXPECT_EQ(cur.color.b, m_originalAttr.color.b);
    }

    TEST_F(ChangeAttrCommandTest, Undo_RestoresOriginalLineWidth) {
        ChangeAttrCommand cmd(scene, kID, MakeAttr(Color4::White(), 5.0f));
        cmd.Execute();
        cmd.Undo();
        EXPECT_FLOAT_EQ(CurrentAttr().lineWidth, m_originalAttr.lineWidth);
    }

    TEST_F(ChangeAttrCommandTest, Undo_RestoresOriginalLineType) {
        ChangeAttrCommand cmd(scene, kID, MakeAttr(Color4::White(), 1.0f, LineType::DOTTED));
        cmd.Execute();
        cmd.Undo();
        EXPECT_EQ(CurrentAttr().lineType, m_originalAttr.lineType);
    }

    TEST_F(ChangeAttrCommandTest, Undo_MarksDirty) {
        ChangeAttrCommand cmd(scene, kID, MakeAttr(Color4::Red()));
        cmd.Execute();
        scene.ClearDirty();
        cmd.Undo();
        EXPECT_TRUE(scene.IsDirty());
    }

    // ════════════════════════════════════════════════════════════
    // 3. Execute → Undo → Execute（Redo）
    // ════════════════════════════════════════════════════════════
    TEST_F(ChangeAttrCommandTest, ExecuteUndoExecute_ReappliesNewAttr) {
        EntityAttr newAttr = MakeAttr(Color4::Red(), 2.0f, LineType::DASHED);
        ChangeAttrCommand cmd(scene, kID, newAttr);

        cmd.Execute();
        EXPECT_EQ(CurrentAttr().color.r, 255);

        cmd.Undo();
        EXPECT_EQ(CurrentAttr().color.r, m_originalAttr.color.r);

        cmd.Execute(); // Redo
        EXPECT_EQ(CurrentAttr().color.r, 255);
        EXPECT_FLOAT_EQ(CurrentAttr().lineWidth, 2.0f);
        EXPECT_EQ(CurrentAttr().lineType, LineType::DASHED);
    }

    TEST_F(ChangeAttrCommandTest, MultipleUndoRedo_Consistent) {
        EntityAttr newAttr = MakeAttr(Color4::Black(), 4.0f);
        ChangeAttrCommand cmd(scene, kID, newAttr);

        for (int i = 0; i < 5; ++i) {
            cmd.Execute();
            EXPECT_EQ(CurrentAttr().color.r, 0); // Black

            cmd.Undo();
            EXPECT_EQ(CurrentAttr().color.r, m_originalAttr.color.r);
        }
    }

    // ════════════════════════════════════════════════════════════
    // 4. 构造时快照旧属性（构造后 Execute 前修改实体，旧属性仍正确）
    // ════════════════════════════════════════════════════════════
    TEST_F(ChangeAttrCommandTest, Constructor_SnapshotsOldAttrAtConstructionTime) {
        // 构造命令时记录旧属性
        EntityAttr newAttr = MakeAttr(Color4::Red());
        ChangeAttrCommand cmd(scene, kID, newAttr);

        // 构造后、Execute 前再次修改实体属性
        EntityAttr intermediate = MakeAttr(Color4::Black());
        static_cast<LineEntity*>(scene.GetEntity(kID))->SetAttr(intermediate);

        cmd.Execute(); // 应用 Red
        cmd.Undo();    // 应还原到构造时的 White，而非 intermediate 的 Black

        EntityAttr cur = CurrentAttr();
        EXPECT_EQ(cur.color.r, m_originalAttr.color.r);
        EXPECT_EQ(cur.color.g, m_originalAttr.color.g);
        EXPECT_EQ(cur.color.b, m_originalAttr.color.b);
    }

    // ════════════════════════════════════════════════════════════
    // 5. 边界：实体不存在
    // ════════════════════════════════════════════════════════════
    TEST_F(ChangeAttrCommandTest, Execute_NonExistentID_DoesNotCrash) {
        ChangeAttrCommand cmd(scene, 9999, MakeAttr(Color4::Red()));
        EXPECT_NO_THROW(cmd.Execute());
    }

    TEST_F(ChangeAttrCommandTest, Undo_NonExistentID_DoesNotCrash) {
        ChangeAttrCommand cmd(scene, 9999, MakeAttr(Color4::Red()));
        EXPECT_NO_THROW(cmd.Undo());
    }

    TEST_F(ChangeAttrCommandTest, Execute_NonExistentID_SceneUnchanged) {
        ChangeAttrCommand cmd(scene, 9999, MakeAttr(Color4::Red()));
        cmd.Execute();
        // kID=1 的实体属性不受影响
        EXPECT_EQ(CurrentAttr().color.r, m_originalAttr.color.r);
    }

    // ════════════════════════════════════════════════════════════
    // 6. DirtyCallback
    // ════════════════════════════════════════════════════════════
    TEST_F(ChangeAttrCommandTest, Execute_TriggersDirtyCallback) {
        int count = 0;
        scene.SetDirtyCallback([&]() { ++count; });
        ChangeAttrCommand cmd(scene, kID, MakeAttr(Color4::Red()));
        cmd.Execute();
        EXPECT_EQ(count, 1);
    }

    TEST_F(ChangeAttrCommandTest, Undo_TriggersDirtyCallback) {
        ChangeAttrCommand cmd(scene, kID, MakeAttr(Color4::Red()));
        cmd.Execute();
        int count = 0;
        scene.SetDirtyCallback([&]() { ++count; });
        cmd.Undo();
        EXPECT_EQ(count, 1);
    }

    TEST_F(ChangeAttrCommandTest, Execute_NonExistent_DoesNotTriggerDirtyCallback) {
        int count = 0;
        scene.SetDirtyCallback([&]() { ++count; });
        ChangeAttrCommand cmd(scene, 9999, MakeAttr(Color4::Red()));
        cmd.Execute();
        EXPECT_EQ(count, 0);
    }

} // namespace MiniCAD