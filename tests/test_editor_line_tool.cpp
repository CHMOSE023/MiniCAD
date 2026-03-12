// ============================================================
// MiniCAD — tests/app/EditorLineToolTest.cpp
// 测试对象：Editor 单例 + LineTool
// 测试框架：Google Test (gtest)
// ============================================================

#include <gtest/gtest.h>
#include <memory>

#include "app/Editor.h"
#include "app/Tools/LineTool.h"
#include "app/Tools/ITool.h"
#include "core/Entity/LineEntity.h"

using namespace MiniCAD;

// ════════════════════════════════════════════════════════════
//  测试辅助
// ════════════════════════════════════════════════════════════
namespace {

    // Scene 没有 Clear()，通过 RemoveEntity 逐一清空
    void ClearScene(Scene& scene) {
        auto ids = scene.GetAllIDs();
        for (auto id : ids)
            scene.RemoveEntity(id);
    }

    InputEvent MakeMouseDown(float x, float y, int btn = 0) {
        InputEvent e{};
        e.type = InputEvent::Type::MouseDown;
        e.screenPos = { x, y };
        e.button = btn;
        return e;
    }

    InputEvent MakeMouseMove(float x, float y) {
        InputEvent e{};
        e.type = InputEvent::Type::MouseMove;
        e.screenPos = { x, y };
        return e;
    }

    InputEvent MakeMouseUp(float x, float y, int btn = 0) {
        InputEvent e{};
        e.type = InputEvent::Type::MouseUp;
        e.screenPos = { x, y };
        e.button = btn;
        return e;
    }

    InputEvent MakeKeyDown(int keyCode) {
        InputEvent e{};
        e.type = InputEvent::Type::KeyDown;
        e.keyCode = keyCode;
        return e;
    }

} // anonymous namespace


// ════════════════════════════════════════════════════════════
//  EditorTest — Editor 单例行为
// ════════════════════════════════════════════════════════════
class EditorTest : public ::testing::Test {
protected:
    void SetUp() override {
        Editor::Instance().Shutdown();   // 确保干净状态
        Editor::Instance().Initialize();
    }
    void TearDown() override {
        Editor::Instance().Shutdown();
    }
};

TEST_F(EditorTest, InitializeDoesNotCrash) {
    EXPECT_NO_THROW(Editor::Instance().Initialize());
}

TEST_F(EditorTest, ShutdownClearsActiveTool) {
    Editor::Instance().SetActiveTool(std::make_unique<LineTool>());
    Editor::Instance().Shutdown();
    EXPECT_EQ(Editor::Instance().GetActiveTool(), nullptr);
}

TEST_F(EditorTest, SetActiveToolStoresToolCorrectly) {
    auto* raw = new LineTool();
    Editor::Instance().SetActiveTool(std::unique_ptr<ITool>(raw));
    EXPECT_EQ(Editor::Instance().GetActiveTool(), raw);
}

TEST_F(EditorTest, SetActiveToolNullptrClearsActiveTool) {
    Editor::Instance().SetActiveTool(std::make_unique<LineTool>());
    Editor::Instance().SetActiveTool(nullptr);
    EXPECT_EQ(Editor::Instance().GetActiveTool(), nullptr);
}

TEST_F(EditorTest, ReplaceActiveToolCallsCancelOnOldTool) {
    struct CancelTracker : public ITool {
        bool cancelCalled = false;
        void OnMouseDown(const Point2&, int) override {}
        void OnMouseMove(const Point2&)      override {}
        void OnMouseUp(const Point2&, int)   override {}
        void OnKeyDown(int)                  override {}
        void OnCancel() override { cancelCalled = true; }
        std::string GetName() const override { return "CancelTracker"; }
    };

    auto* tracker = new CancelTracker();
    Editor::Instance().SetActiveTool(std::unique_ptr<ITool>(tracker));
    Editor::Instance().SetActiveTool(std::make_unique<LineTool>());
    EXPECT_TRUE(tracker->cancelCalled);
}

TEST_F(EditorTest, InitiallyCannotUndoOrRedo) {
    EXPECT_FALSE(Editor::Instance().CanUndo());
    EXPECT_FALSE(Editor::Instance().CanRedo());
}

TEST_F(EditorTest, RedrawCallbackIsInvokedOnRequestRedraw) {
    bool called = false;
    Editor::Instance().SetRedrawCallback([&called]() { called = true; });
    Editor::Instance().RequestRedraw();
    EXPECT_TRUE(called);
}

TEST_F(EditorTest, NoRedrawCallbackDoesNotCrash) {
    Editor::Instance().SetRedrawCallback(nullptr);
    EXPECT_NO_THROW(Editor::Instance().RequestRedraw());
}

TEST_F(EditorTest, HandleInputWithNoActiveToolDoesNotCrash) {
    EXPECT_NO_THROW(Editor::Instance().HandleInput(MakeMouseDown(0, 0)));
}

TEST_F(EditorTest, EscapeKeyDispatchesToOnCancel) {
    struct EscTracker : public ITool {
        bool cancelCalled = false;
        void OnMouseDown(const Point2&, int) override {}
        void OnMouseMove(const Point2&)      override {}
        void OnMouseUp(const Point2&, int)   override {}
        void OnKeyDown(int)                  override {}
        void OnCancel() override { cancelCalled = true; }
        std::string GetName() const override { return "EscTracker"; }
    };

    auto* tracker = new EscTracker();
    Editor::Instance().SetActiveTool(std::unique_ptr<ITool>(tracker));
    Editor::Instance().HandleInput(MakeKeyDown(KEY_ESCAPE));
    EXPECT_TRUE(tracker->cancelCalled);
}

TEST_F(EditorTest, GetSceneReturnsSameReference) {
    EXPECT_EQ(&Editor::Instance().GetScene(), &Editor::Instance().GetScene());
}

TEST_F(EditorTest, GetLayerManagerReturnsSameReference) {
    EXPECT_EQ(&Editor::Instance().GetLayerManager(), &Editor::Instance().GetLayerManager());
}

TEST_F(EditorTest, GetSelectionSetReturnsSameReference) {
    EXPECT_EQ(&Editor::Instance().GetSelectionSet(), &Editor::Instance().GetSelectionSet());
}


// ════════════════════════════════════════════════════════════
//  LineToolTest — LineTool 状态机
// ════════════════════════════════════════════════════════════
class LineToolTest : public ::testing::Test {
protected:
    Editor& editor = Editor::Instance();

    void SetUp() override {
        // 单例 Scene 跨测试存活，必须先清空
        editor.Shutdown();
        ClearScene(editor.GetScene());
        editor.Initialize();
        editor.SetActiveTool(std::make_unique<LineTool>());
    }
    void TearDown() override {
        editor.Shutdown();
        ClearScene(editor.GetScene());
    }
};

// ── GetName ──────────────────────────────────────────────────

TEST_F(LineToolTest, GetNameReturnsLineTool) {
    EXPECT_EQ(editor.GetActiveTool()->GetName(), "LineTool");
}

// ── 第一次点击不创建实体 ─────────────────────────────────────

TEST_F(LineToolTest, FirstClickSetsStartPoint) {
    InputEvent evt{};
    evt.type = InputEvent::Type::MouseDown;
    evt.screenPos = { 10, 20 };
    evt.button = 0;
    editor.HandleInput(evt);

    EXPECT_EQ(editor.GetScene().EntityCount(), 0);
}

// ── 两次左键点击创建一条 LineEntity，坐标正确 ────────────────
// 注意：GetEntity 按 ObjectID 查找，不按下标；用 GetAllIDs() 取第一个

TEST_F(LineToolTest, SecondClickCreatesLine) {
    InputEvent evt1{};
    evt1.type = InputEvent::Type::MouseDown;
    evt1.screenPos = { 0, 0 };
    evt1.button = 0;

    InputEvent evt2{};
    evt2.type = InputEvent::Type::MouseDown;
    evt2.screenPos = { 100, 0 };
    evt2.button = 0;

    editor.HandleInput(evt1);
    editor.HandleInput(evt2);

    ASSERT_EQ(editor.GetScene().EntityCount(), 1);

    auto ids = editor.GetScene().GetAllIDs();
    ASSERT_EQ(ids.size(), 1u);

    Object* entity = editor.GetScene().GetEntity(ids[0]);
    ASSERT_NE(entity, nullptr);

    auto* line = dynamic_cast<LineEntity*>(entity);
    ASSERT_NE(line, nullptr);

    EXPECT_FLOAT_EQ(line->GetLine().start.x, 0.0f);
    EXPECT_FLOAT_EQ(line->GetLine().end.x, 100.0f);
}

// ── ESC 取消后不生成线 ───────────────────────────────────────

TEST_F(LineToolTest, EscapeCancelsDrawing) {
    InputEvent evt1{};
    evt1.type = InputEvent::Type::MouseDown;
    evt1.screenPos = { 10, 10 };
    evt1.button = 0;

    InputEvent esc{};
    esc.type = InputEvent::Type::KeyDown;
    esc.keyCode = KEY_ESCAPE;

    InputEvent evt2{};
    evt2.type = InputEvent::Type::MouseDown;
    evt2.screenPos = { 100, 100 };
    evt2.button = 0;

    editor.HandleInput(evt1);
    editor.HandleInput(esc);
    editor.HandleInput(evt2);  // ESC 后此点成为新的第一点，不提交

    EXPECT_EQ(editor.GetScene().EntityCount(), 0);
}

// ── 非左键 MouseDown 不进入第二阶段 ─────────────────────────

TEST_F(LineToolTest, RightClickDoesNotAdvanceState) {
    editor.HandleInput(MakeMouseDown(0, 0, 2));
    editor.HandleInput(MakeMouseDown(10, 10, 2));

    EXPECT_EQ(editor.GetScene().EntityCount(), 0);
}

// ── 提交后可以 Undo，场景实体数归零 ─────────────────────────

TEST_F(LineToolTest, CommittedLineCanBeUndone) {
    editor.HandleInput(MakeMouseDown(0, 0));
    editor.HandleInput(MakeMouseDown(1, 1));

    ASSERT_EQ(editor.GetScene().EntityCount(), 1);
    editor.Undo();
    EXPECT_EQ(editor.GetScene().EntityCount(), 0);
}

// ── Undo 后可以 Redo ─────────────────────────────────────────

TEST_F(LineToolTest, UndoneLineCanBeRedone) {
    editor.HandleInput(MakeMouseDown(0, 0));
    editor.HandleInput(MakeMouseDown(5, 5));
    editor.Undo();
    editor.Redo();

    EXPECT_EQ(editor.GetScene().EntityCount(), 1);
}

// ── 提交后状态重置，可以继续画第二条线 ──────────────────────

TEST_F(LineToolTest, ToolResetsAfterCommitSoSecondLineIsPossible) {
    editor.HandleInput(MakeMouseDown(0, 0));
    editor.HandleInput(MakeMouseDown(1, 1));  // 第一条
    editor.HandleInput(MakeMouseDown(5, 5));
    editor.HandleInput(MakeMouseDown(9, 9));  // 第二条

    EXPECT_EQ(editor.GetScene().EntityCount(), 2);
}

// ── ESC 在第一点前不影响后续正常画线 ────────────────────────

TEST_F(LineToolTest, CancelBeforeFirstPointDoesNotCommit) {
    editor.HandleInput(MakeKeyDown(KEY_ESCAPE));
    EXPECT_EQ(editor.GetScene().EntityCount(), 0);
}

// ── ESC 后工具重置，可重新开始 ───────────────────────────────

TEST_F(LineToolTest, CancelResetsToolSoItCanRestartNormally) {
    editor.HandleInput(MakeMouseDown(0, 0));
    editor.HandleInput(MakeKeyDown(KEY_ESCAPE));
    editor.HandleInput(MakeMouseDown(0, 0));
    editor.HandleInput(MakeMouseDown(10, 10));

    EXPECT_EQ(editor.GetScene().EntityCount(), 1);
}

// ── MouseMove 不提交命令 ─────────────────────────────────────

TEST_F(LineToolTest, MouseMoveDoesNotCommitCommand) {
    editor.HandleInput(MakeMouseDown(0, 0));
    editor.HandleInput(MakeMouseMove(50, 50));
    editor.HandleInput(MakeMouseMove(80, 20));

    EXPECT_EQ(editor.GetScene().EntityCount(), 0);
}

// ── MouseUp 不提交命令 ───────────────────────────────────────

TEST_F(LineToolTest, MouseUpDoesNotCommitCommand) {
    editor.HandleInput(MakeMouseDown(0, 0));
    editor.HandleInput(MakeMouseUp(10, 10));

    EXPECT_EQ(editor.GetScene().EntityCount(), 0);
}

// ── 零长度线（起点 == 终点）不崩溃 ──────────────────────────

TEST_F(LineToolTest, ZeroLengthLineDoesNotCrash) {
    EXPECT_NO_THROW({
        editor.HandleInput(MakeMouseDown(5, 5));
        editor.HandleInput(MakeMouseDown(5, 5));
        });
}

// ── 负坐标不崩溃 ─────────────────────────────────────────────

TEST_F(LineToolTest, NegativeCoordinatesDoNotCrash) {
    EXPECT_NO_THROW({
        editor.HandleInput(MakeMouseDown(-100.0f, -200.0f));
        editor.HandleInput(MakeMouseDown(100.0f,  200.0f));
        });
}

