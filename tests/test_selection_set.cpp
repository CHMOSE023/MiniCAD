// ============================================================
// MiniCAD — tests/Picking/test_SelectionSet.cpp
// 覆盖：Select / Deselect / Toggle / Clear / Callback / 边界
// ============================================================
#include <gtest/gtest.h>
#include "app/Picking/SelectionSet.h"
#include <limits>

namespace MiniCAD {

    // ────────────────────────────────────────────────────────────
    // Fixture
    // ────────────────────────────────────────────────────────────
    class SelectionSetTest : public ::testing::Test {
    protected:
        SelectionSet sel;
    };

    // ════════════════════════════════════════════════════════════
    // 1. 初始状态
    // ════════════════════════════════════════════════════════════
    TEST_F(SelectionSetTest, InitialState_Empty) {
        EXPECT_EQ(sel.Count(), 0);
        EXPECT_TRUE(sel.GetSelected().empty());
    }

    TEST_F(SelectionSetTest, InitialState_NothingSelected) {
        EXPECT_FALSE(sel.IsSelected(1));
        EXPECT_FALSE(sel.IsSelected(0));
    }

    // ════════════════════════════════════════════════════════════
    // 2. Select
    // ════════════════════════════════════════════════════════════
    TEST_F(SelectionSetTest, Select_AddsID) {
        sel.Select(1);
        EXPECT_TRUE(sel.IsSelected(1));
        EXPECT_EQ(sel.Count(), 1);
    }

    TEST_F(SelectionSetTest, Select_Multiple_AllPresent) {
        sel.Select(1);
        sel.Select(2);
        sel.Select(3);
        EXPECT_TRUE(sel.IsSelected(1));
        EXPECT_TRUE(sel.IsSelected(2));
        EXPECT_TRUE(sel.IsSelected(3));
        EXPECT_EQ(sel.Count(), 3);
    }

    TEST_F(SelectionSetTest, Select_Duplicate_NotAddedTwice) {
        sel.Select(1);
        sel.Select(1);
        EXPECT_EQ(sel.Count(), 1);
    }

    TEST_F(SelectionSetTest, Select_Duplicate_CallbackOnlyOnce) {
        int count = 0;
        sel.SetChangedCallback([&]() { ++count; });
        sel.Select(1);
        sel.Select(1); // 重复，不触发
        EXPECT_EQ(count, 1);
    }

    // ════════════════════════════════════════════════════════════
    // 3. Deselect
    // ════════════════════════════════════════════════════════════
    TEST_F(SelectionSetTest, Deselect_ExistingID_Removes) {
        sel.Select(1);
        sel.Deselect(1);
        EXPECT_FALSE(sel.IsSelected(1));
        EXPECT_EQ(sel.Count(), 0);
    }

    TEST_F(SelectionSetTest, Deselect_NonExistentID_NoChange) {
        sel.Select(1);
        sel.Deselect(99);
        EXPECT_EQ(sel.Count(), 1);
    }

    TEST_F(SelectionSetTest, Deselect_NonExistentID_NoCallback) {
        int count = 0;
        sel.SetChangedCallback([&]() { ++count; });
        sel.Deselect(99); // 不存在，不触发
        EXPECT_EQ(count, 0);
    }

    TEST_F(SelectionSetTest, Deselect_OnlyRemovesTarget) {
        sel.Select(1);
        sel.Select(2);
        sel.Select(3);
        sel.Deselect(2);
        EXPECT_TRUE(sel.IsSelected(1));
        EXPECT_FALSE(sel.IsSelected(2));
        EXPECT_TRUE(sel.IsSelected(3));
        EXPECT_EQ(sel.Count(), 2);
    }

    // ════════════════════════════════════════════════════════════
    // 4. ToggleSelect
    // ════════════════════════════════════════════════════════════
    TEST_F(SelectionSetTest, Toggle_UnselectedID_Selects) {
        sel.ToggleSelect(1);
        EXPECT_TRUE(sel.IsSelected(1));
    }

    TEST_F(SelectionSetTest, Toggle_SelectedID_Deselects) {
        sel.Select(1);
        sel.ToggleSelect(1);
        EXPECT_FALSE(sel.IsSelected(1));
    }

    TEST_F(SelectionSetTest, Toggle_TwiceRestoresOriginalState) {
        sel.ToggleSelect(1);
        sel.ToggleSelect(1);
        EXPECT_FALSE(sel.IsSelected(1));
        EXPECT_EQ(sel.Count(), 0);
    }

    TEST_F(SelectionSetTest, Toggle_CallbackCalledEachTime) {
        int count = 0;
        sel.SetChangedCallback([&]() { ++count; });
        sel.ToggleSelect(1); // Select → +1
        sel.ToggleSelect(1); // Deselect → +1
        EXPECT_EQ(count, 2);
    }

    // ════════════════════════════════════════════════════════════
    // 5. Clear
    // ════════════════════════════════════════════════════════════
    TEST_F(SelectionSetTest, Clear_RemovesAll) {
        sel.Select(1);
        sel.Select(2);
        sel.Select(3);
        sel.Clear();
        EXPECT_EQ(sel.Count(), 0);
        EXPECT_FALSE(sel.IsSelected(1));
        EXPECT_FALSE(sel.IsSelected(2));
        EXPECT_FALSE(sel.IsSelected(3));
    }

    TEST_F(SelectionSetTest, Clear_EmptySet_NoCallback) {
        int count = 0;
        sel.SetChangedCallback([&]() { ++count; });
        sel.Clear(); // 已经为空，不触发
        EXPECT_EQ(count, 0);
    }

    TEST_F(SelectionSetTest, Clear_NonEmpty_TriggersCallback) {
        sel.Select(1);
        int count = 0;
        sel.SetChangedCallback([&]() { ++count; });
        sel.Clear();
        EXPECT_EQ(count, 1);
    }

    // ════════════════════════════════════════════════════════════
    // 6. GetSelected
    // ════════════════════════════════════════════════════════════
    TEST_F(SelectionSetTest, GetSelected_ReturnsAllIDs) {
        sel.Select(10);
        sel.Select(20);
        const auto& ids = sel.GetSelected();
        EXPECT_EQ(ids.size(), 2u);
        bool has10 = std::find(ids.begin(), ids.end(), 10u) != ids.end();
        bool has20 = std::find(ids.begin(), ids.end(), 20u) != ids.end();
        EXPECT_TRUE(has10);
        EXPECT_TRUE(has20);
    }

    TEST_F(SelectionSetTest, GetSelected_AfterDeselect_Updated) {
        sel.Select(1);
        sel.Select(2);
        sel.Deselect(1);
        const auto& ids = sel.GetSelected();
        EXPECT_EQ(ids.size(), 1u);
        EXPECT_EQ(ids[0], 2u);
    }

    // ════════════════════════════════════════════════════════════
    // 7. Callback 替换
    // ════════════════════════════════════════════════════════════
    TEST_F(SelectionSetTest, SetChangedCallback_ReplacesOld) {
        int old = 0, fresh = 0;
        sel.SetChangedCallback([&]() { ++old; });
        sel.SetChangedCallback([&]() { ++fresh; });
        sel.Select(1);
        EXPECT_EQ(old, 0);
        EXPECT_EQ(fresh, 1);
    }

    // ════════════════════════════════════════════════════════════
    // 8. 边界：ID = 0 / INVALID_ID
    // ════════════════════════════════════════════════════════════
    TEST_F(SelectionSetTest, Select_ZeroID_Allowed) {
        sel.Select(0);
        EXPECT_TRUE(sel.IsSelected(0));
    }

    TEST_F(SelectionSetTest, Select_MaxID_Allowed) {
        SelectionSet::ObjectID maxID = std::numeric_limits<SelectionSet::ObjectID>::max();
        sel.Select(maxID);
        EXPECT_TRUE(sel.IsSelected(maxID));
    }

} // namespace MiniCAD