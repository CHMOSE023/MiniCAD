// ============================================================
// test_CommandStack.cpp
//
// 编译示例（在项目根目录下执行）:
//   g++ -std=c++17 -I. test_CommandStack.cpp doc/UndoRedo/CommandStack.cpp \
//       -lgtest -lgtest_main -pthread -o test_cs && ./test_cs
// ============================================================

#include <gtest/gtest.h>
#include "doc/UndoRedo/CommandStack.h"

#include <string>
#include <vector>

using namespace MiniCAD;

// ============================================================
// Mock 命令：记录每次 Execute / Undo / Redo 的调用
// ============================================================
struct MockCommand : public ICommand {
    std::string               desc;
    std::vector<std::string>& log;
    int execCount = 0;
    int undoCount = 0;
    int redoCount = 0;

    MockCommand(std::string d, std::vector<std::string>& l)
        : desc(std::move(d)), log(l) {
    }

    void Execute() override {
        ++execCount;
        log.push_back("exec:" + desc);
    }
    void Undo() override {
        ++undoCount;
        log.push_back("undo:" + desc);
    }
    void Redo() override {
        ++redoCount;
        log.push_back("redo:" + desc);
    }
    std::string GetDescription() const override { return desc; }
};

// 工厂函数：同时返回 unique_ptr 和裸指针，便于事后断言
static std::pair<std::unique_ptr<ICommand>, MockCommand*>
MakeMock(std::string desc, std::vector<std::string>& log) {
    auto* raw = new MockCommand(std::move(desc), log);
    return { std::unique_ptr<ICommand>(raw), raw };
}

// ============================================================
// Suite 1: ICommand 接口默认行为
// ============================================================
TEST(ICommandTest, DefaultRedoCallsExecute) {
    // 不覆盖 Redo() 时，默认实现应转发给 Execute()
    struct MinimalCmd : ICommand {
        int execCount = 0;
        void Execute() override { ++execCount; }
        void Undo()    override {}
        std::string GetDescription() const override { return ""; }
        // 不覆盖 Redo，使用 ICommand 默认实现
    };
    MinimalCmd cmd;
    cmd.Execute();
    EXPECT_EQ(cmd.execCount, 1);
    cmd.Redo();   // 默认 Redo = Execute
    EXPECT_EQ(cmd.execCount, 2);
}

// ============================================================
// Suite 2: 初始状态
// ============================================================
TEST(CommandStackInitTest, DefaultCapacity) {
    CommandStack cs;
    EXPECT_EQ(cs.Capacity(), CommandStack::DEFAULT_CAPACITY);
}

TEST(CommandStackInitTest, CustomCapacity) {
    CommandStack cs(10);
    EXPECT_EQ(cs.Capacity(), 10);
}

TEST(CommandStackInitTest, EmptyStackFlags) {
    CommandStack cs;
    EXPECT_FALSE(cs.CanUndo());
    EXPECT_FALSE(cs.CanRedo());
    EXPECT_EQ(cs.UndoCount(), 0);
    EXPECT_EQ(cs.RedoCount(), 0);
}

TEST(CommandStackInitTest, CurrentDescriptionOnEmpty) {
    CommandStack cs;
    EXPECT_EQ(cs.CurrentDescription(), "");
}

// ============================================================
// Suite 3: Push
// ============================================================
TEST(CommandStackPushTest, ExecutesCommandOnPush) {
    std::vector<std::string> log;
    CommandStack cs;
    auto [cmd, raw] = MakeMock("A", log);
    cs.Push(std::move(cmd));
    EXPECT_EQ(raw->execCount, 1);
}

TEST(CommandStackPushTest, CanUndoAfterPush) {
    std::vector<std::string> log;
    CommandStack cs;
    auto [cmd, raw] = MakeMock("A", log);
    cs.Push(std::move(cmd));
    EXPECT_TRUE(cs.CanUndo());
    EXPECT_FALSE(cs.CanRedo());
}

TEST(CommandStackPushTest, CountsAfterMultiplePush) {
    std::vector<std::string> log;
    CommandStack cs;
    for (int i = 0; i < 3; ++i) {
        auto [cmd, raw] = MakeMock("cmd" + std::to_string(i), log);
        cs.Push(std::move(cmd));
    }
    EXPECT_EQ(cs.UndoCount(), 3);
    EXPECT_EQ(cs.RedoCount(), 0);
}

TEST(CommandStackPushTest, CurrentDescriptionIsLatestCommand) {
    std::vector<std::string> log;
    CommandStack cs;
    auto [c1, r1] = MakeMock("First", log);
    auto [c2, r2] = MakeMock("Second", log);
    cs.Push(std::move(c1));
    cs.Push(std::move(c2));
    EXPECT_EQ(cs.CurrentDescription(), "Second");
}

TEST(CommandStackPushTest, PushClearsRedoStack) {
    std::vector<std::string> log;
    CommandStack cs;
    auto [c1, r1] = MakeMock("A", log);
    auto [c2, r2] = MakeMock("B", log);
    auto [c3, r3] = MakeMock("C", log);
    cs.Push(std::move(c1));
    cs.Push(std::move(c2));
    cs.Undo();                     // Redo 栈此时有 B
    EXPECT_EQ(cs.RedoCount(), 1);
    cs.Push(std::move(c3));        // Push 应清空 Redo 栈
    EXPECT_EQ(cs.RedoCount(), 0);
    EXPECT_EQ(cs.UndoCount(), 2);
}

// ============================================================
// Suite 4: Undo
// ============================================================
TEST(CommandStackUndoTest, CallsUndoOnCommand) {
    std::vector<std::string> log;
    CommandStack cs;
    auto [cmd, raw] = MakeMock("A", log);
    cs.Push(std::move(cmd));
    cs.Undo();
    EXPECT_EQ(raw->undoCount, 1);
}

TEST(CommandStackUndoTest, UpdatesCanRedoAfterUndo) {
    std::vector<std::string> log;
    CommandStack cs;
    auto [cmd, raw] = MakeMock("A", log);
    cs.Push(std::move(cmd));
    cs.Undo();
    EXPECT_FALSE(cs.CanUndo());
    EXPECT_TRUE(cs.CanRedo());
}

TEST(CommandStackUndoTest, UndoCountDecreases) {
    std::vector<std::string> log;
    CommandStack cs;
    auto [c1, r1] = MakeMock("A", log);
    auto [c2, r2] = MakeMock("B", log);
    cs.Push(std::move(c1));
    cs.Push(std::move(c2));
    cs.Undo();
    EXPECT_EQ(cs.UndoCount(), 1);
    EXPECT_EQ(cs.RedoCount(), 1);
}

TEST(CommandStackUndoTest, ConsecutiveUndo) {
    std::vector<std::string> log;
    CommandStack cs;
    auto [c1, r1] = MakeMock("A", log);
    auto [c2, r2] = MakeMock("B", log);
    auto [c3, r3] = MakeMock("C", log);
    cs.Push(std::move(c1));
    cs.Push(std::move(c2));
    cs.Push(std::move(c3));
    cs.Undo(); cs.Undo(); cs.Undo();
    EXPECT_EQ(cs.UndoCount(), 0);
    EXPECT_EQ(cs.RedoCount(), 3);
    EXPECT_EQ(r1->undoCount, 1);
    EXPECT_EQ(r2->undoCount, 1);
    EXPECT_EQ(r3->undoCount, 1);
}

TEST(CommandStackUndoTest, UndoOnEmptyStackIsNoop) {
    CommandStack cs;
    EXPECT_NO_THROW(cs.Undo());
    EXPECT_EQ(cs.UndoCount(), 0);
}

TEST(CommandStackUndoTest, CurrentDescriptionAfterUndo) {
    std::vector<std::string> log;
    CommandStack cs;
    auto [c1, r1] = MakeMock("A", log);
    auto [c2, r2] = MakeMock("B", log);
    cs.Push(std::move(c1));
    cs.Push(std::move(c2));
    cs.Undo();
    EXPECT_EQ(cs.CurrentDescription(), "A");
}

// ============================================================
// Suite 5: Redo
// ============================================================
TEST(CommandStackRedoTest, CallsRedoOnCommand) {
    std::vector<std::string> log;
    CommandStack cs;
    auto [cmd, raw] = MakeMock("A", log);
    cs.Push(std::move(cmd));
    cs.Undo();
    cs.Redo();
    EXPECT_EQ(raw->redoCount, 1);
}

TEST(CommandStackRedoTest, UpdatesCountsAfterRedo) {
    std::vector<std::string> log;
    CommandStack cs;
    auto [cmd, raw] = MakeMock("A", log);
    cs.Push(std::move(cmd));
    cs.Undo();
    cs.Redo();
    EXPECT_EQ(cs.UndoCount(), 1);
    EXPECT_EQ(cs.RedoCount(), 0);
}

TEST(CommandStackRedoTest, ConsecutiveRedo) {
    std::vector<std::string> log;
    CommandStack cs;
    auto [c1, r1] = MakeMock("A", log);
    auto [c2, r2] = MakeMock("B", log);
    auto [c3, r3] = MakeMock("C", log);
    cs.Push(std::move(c1));
    cs.Push(std::move(c2));
    cs.Push(std::move(c3));
    cs.Undo(); cs.Undo(); cs.Undo();
    cs.Redo(); cs.Redo(); cs.Redo();
    EXPECT_EQ(cs.UndoCount(), 3);
    EXPECT_EQ(cs.RedoCount(), 0);
    EXPECT_EQ(r1->redoCount, 1);
    EXPECT_EQ(r2->redoCount, 1);
    EXPECT_EQ(r3->redoCount, 1);
}

TEST(CommandStackRedoTest, RedoOnEmptyRedoStackIsNoop) {
    std::vector<std::string> log;
    CommandStack cs;
    auto [cmd, raw] = MakeMock("A", log);
    cs.Push(std::move(cmd));
    EXPECT_NO_THROW(cs.Redo());
    EXPECT_EQ(raw->redoCount, 0);
}

// ============================================================
// Suite 6: Undo + Redo 交替操作
// ============================================================
TEST(CommandStackUndoRedoTest, UndoRedoUndoSequence) {
    std::vector<std::string> log;
    CommandStack cs;
    auto [c1, r1] = MakeMock("A", log);
    auto [c2, r2] = MakeMock("B", log);
    cs.Push(std::move(c1));
    cs.Push(std::move(c2));

    cs.Undo();  // undo B
    cs.Undo();  // undo A
    cs.Redo();  // redo A
    cs.Undo();  // undo A again

    EXPECT_EQ(r1->undoCount, 2);
    EXPECT_EQ(r1->redoCount, 1);
    EXPECT_EQ(cs.UndoCount(), 0);
    EXPECT_EQ(cs.RedoCount(), 2);
}

TEST(CommandStackUndoRedoTest, CallOrderInLog) {
    std::vector<std::string> log;
    CommandStack cs;
    auto [c1, r1] = MakeMock("A", log);
    auto [c2, r2] = MakeMock("B", log);
    cs.Push(std::move(c1));
    cs.Push(std::move(c2));
    cs.Undo();
    cs.Redo();

    std::vector<std::string> expected = {
        "exec:A", "exec:B", "undo:B", "redo:B"
    };
    EXPECT_EQ(log, expected);
}

// ============================================================
// Suite 7: Clear
// ============================================================
TEST(CommandStackClearTest, ClearResetsAll) {
    std::vector<std::string> log;
    CommandStack cs;
    auto [c1, r1] = MakeMock("A", log);
    auto [c2, r2] = MakeMock("B", log);
    cs.Push(std::move(c1));
    cs.Push(std::move(c2));
    cs.Undo();
    cs.Clear();
    EXPECT_FALSE(cs.CanUndo());
    EXPECT_FALSE(cs.CanRedo());
    EXPECT_EQ(cs.UndoCount(), 0);
    EXPECT_EQ(cs.RedoCount(), 0);
    EXPECT_EQ(cs.CurrentDescription(), "");
}

TEST(CommandStackClearTest, PushAfterClearWorks) {
    std::vector<std::string> log;
    CommandStack cs;
    auto [c1, r1] = MakeMock("A", log);
    cs.Push(std::move(c1));
    cs.Clear();

    auto [c2, r2] = MakeMock("B", log);
    cs.Push(std::move(c2));
    EXPECT_EQ(cs.UndoCount(), 1);
    EXPECT_EQ(cs.CurrentDescription(), "B");
}

// ============================================================
// Suite 8: 容量限制与淘汰
// ============================================================
TEST(CommandStackCapacityTest, DoesNotExceedCapacity) {
    std::vector<std::string> log;
    CommandStack cs(3);
    for (int i = 0; i < 5; ++i) {
        auto [cmd, raw] = MakeMock("cmd" + std::to_string(i), log);
        cs.Push(std::move(cmd));
    }
    EXPECT_EQ(cs.UndoCount(), 3);
    EXPECT_EQ(cs.RedoCount(), 0);
}

TEST(CommandStackCapacityTest, OldestCommandEvicted) {
    std::vector<std::string> log;
    CommandStack cs(3);
    for (int i = 0; i < 4; ++i) {
        auto [cmd, raw] = MakeMock("cmd" + std::to_string(i), log);
        cs.Push(std::move(cmd));
    }
    // cmd0 已被淘汰，全部 Undo 后 UndoCount 应为 0
    cs.Undo(); cs.Undo(); cs.Undo();
    EXPECT_EQ(cs.UndoCount(), 0);
    EXPECT_EQ(cs.RedoCount(), 3);
}

TEST(CommandStackCapacityTest, CapacityOneKeepsOnlyLatest) {
    std::vector<std::string> log;
    CommandStack cs(1);
    auto [c1, r1] = MakeMock("A", log);
    auto [c2, r2] = MakeMock("B", log);
    cs.Push(std::move(c1));
    cs.Push(std::move(c2));
    EXPECT_EQ(cs.UndoCount(), 1);
    EXPECT_EQ(cs.CurrentDescription(), "B");
}
