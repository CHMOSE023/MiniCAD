// ============================================================
// MiniCAD — tests/Scene/test_Scene.cpp
// 覆盖：增删改查 / DirtyFlag / DirtyCallback / QueryByBox / 并发
// 注意：Object 与 LineEntity 需提供最小可编译实现，
//       或在项目中已有对应头文件。
// ============================================================
#include <gtest/gtest.h>
#include <atomic>
#include <thread>
#include <vector>
#include <chrono>
#include "app/Scene/Scene.h"
#include "core/Entity/LineEntity.h"  // 需要 IsKindOf<LineEntity> / GetBoundingBox
#include <memory>
#include <core/Object/Object.hpp>
#include <math/Point.hpp>

namespace MiniCAD {

    // ────────────────────────────────────────────────────────────
    // 辅助：构造 LineEntity，签名与 LineEntity(ObjectID, Point3, Point3) 对齐
    // ────────────────────────────────────────────────────────────
    static std::unique_ptr<LineEntity> MakeLine(
        Object::ObjectID id,
        const Point3& from,
        const Point3& to)
    {
        return std::make_unique<LineEntity>(id, from, to);
    }

    // ────────────────────────────────────────────────────────────
    // Fixture
    // ────────────────────────────────────────────────────────────
    class SceneTest : public ::testing::Test {
    protected:
        Scene scene;
    };

    // ════════════════════════════════════════════════════════════
    // 1. 初始状态
    // ════════════════════════════════════════════════════════════
    TEST_F(SceneTest, InitialState_Empty) {
        EXPECT_EQ(scene.EntityCount(), 0);
        EXPECT_TRUE(scene.GetAllIDs().empty());
    }

    TEST_F(SceneTest, InitialState_NotDirty) {
        EXPECT_FALSE(scene.IsDirty());
    }

    // ════════════════════════════════════════════════════════════
    // 2. AddEntity
    // ════════════════════════════════════════════════════════════
    TEST_F(SceneTest, AddEntity_IncrementsCount) {
        scene.AddEntity(MakeLine(1, { 0,0,0 }, { 1,1,0 }));
        EXPECT_EQ(scene.EntityCount(), 1);
    }

    TEST_F(SceneTest, AddEntity_IsRetrievable) {
        scene.AddEntity(MakeLine(10, { 0,0,0 }, { 1,1,0 }));
        EXPECT_NE(scene.GetEntity(10), nullptr);
    }

    TEST_F(SceneTest, AddEntity_NullEntity_Ignored) {
        scene.AddEntity(nullptr);
        EXPECT_EQ(scene.EntityCount(), 0);
        EXPECT_FALSE(scene.IsDirty());
    }

    TEST_F(SceneTest, AddEntity_MarksDirty) {
        scene.AddEntity(MakeLine(1, { 0,0,0 }, { 1,1,0 }));
        EXPECT_TRUE(scene.IsDirty());
    }

    TEST_F(SceneTest, AddEntity_OverwritesSameID) {
        // 用相同 ID 再次 Add：覆盖旧实体
        scene.AddEntity(MakeLine(5, { 0,0,0 }, { 1,0,0 }));
        scene.AddEntity(MakeLine(5, { 0,0,0 }, { 0,1,0 }));
        EXPECT_EQ(scene.EntityCount(), 1);
    }

    // ════════════════════════════════════════════════════════════
    // 3. RemoveEntity
    // ════════════════════════════════════════════════════════════
    TEST_F(SceneTest, RemoveEntity_ReturnsOwnership) {
        scene.AddEntity(MakeLine(2, { 0,0,0 }, { 1,1,0 }));
        auto ptr = scene.RemoveEntity(2);
        EXPECT_NE(ptr, nullptr);
        EXPECT_EQ(ptr->GetID(), 2u);
    }

    TEST_F(SceneTest, RemoveEntity_NoLongerRetrievable) {
        scene.AddEntity(MakeLine(3, { 0,0,0 }, { 1,1,0 }));
        scene.RemoveEntity(3);
        EXPECT_EQ(scene.GetEntity(3), nullptr);
    }

    TEST_F(SceneTest, RemoveEntity_NonExistentID_ReturnsNull) {
        auto ptr = scene.RemoveEntity(9999);
        EXPECT_EQ(ptr, nullptr);
    }

    TEST_F(SceneTest, RemoveEntity_DecrementsCount) {
        scene.AddEntity(MakeLine(4, { 0,0,0 }, { 1,1,0 }));
        scene.ClearDirty();
        scene.RemoveEntity(4);
        EXPECT_EQ(scene.EntityCount(), 0);
    }

    TEST_F(SceneTest, RemoveEntity_MarksDirty) {
        scene.AddEntity(MakeLine(6, { 0,0,0 }, { 1,1,0 }));
        scene.ClearDirty();
        scene.RemoveEntity(6);
        EXPECT_TRUE(scene.IsDirty());
    }

    // ════════════════════════════════════════════════════════════
    // 4. GetEntity (const & non-const)
    // ════════════════════════════════════════════════════════════
    TEST_F(SceneTest, GetEntity_NonConst_Exists) {
        scene.AddEntity(MakeLine(7, { 0,0,0 }, { 1,1,0 }));
        Object* obj = scene.GetEntity(7);
        ASSERT_NE(obj, nullptr);
        EXPECT_EQ(obj->GetID(), 7u);
    }

    TEST_F(SceneTest, GetEntity_Const_Exists) {
        scene.AddEntity(MakeLine(8, { 0,0,0 }, { 1,1,0 }));
        const Scene& cs = scene;
        const Object* obj = cs.GetEntity(8);
        ASSERT_NE(obj, nullptr);
        EXPECT_EQ(obj->GetID(), 8u);
    }

    TEST_F(SceneTest, GetEntity_MissingID_ReturnsNull) {
        EXPECT_EQ(scene.GetEntity(1234), nullptr);
    }

    // ════════════════════════════════════════════════════════════
    // 5. Has
    // ════════════════════════════════════════════════════════════
    TEST_F(SceneTest, Has_ExistingID_True) {
        scene.AddEntity(MakeLine(9, { 0,0,0 }, { 1,1,0 }));
        EXPECT_TRUE(scene.Has(9));
    }

    TEST_F(SceneTest, Has_MissingID_False) {
        EXPECT_FALSE(scene.Has(9999));
    }

    TEST_F(SceneTest, Has_AfterRemove_False) {
        scene.AddEntity(MakeLine(11, { 0,0,0 }, { 1,1,0 }));
        scene.RemoveEntity(11);
        EXPECT_FALSE(scene.Has(11));
    }

    // ════════════════════════════════════════════════════════════
    // 6. GetAllIDs
    // ════════════════════════════════════════════════════════════
    TEST_F(SceneTest, GetAllIDs_MatchesCount) {
        scene.AddEntity(MakeLine(20, { 0,0,0 }, { 1,0,0 }));
        scene.AddEntity(MakeLine(21, { 0,0,0 }, { 0,1,0 }));
        auto ids = scene.GetAllIDs();
        EXPECT_EQ(ids.size(), 2u);
    }

    TEST_F(SceneTest, GetAllIDs_ContainsAllAddedIDs) {
        scene.AddEntity(MakeLine(30, { 0,0,0 }, { 1,0,0 }));
        scene.AddEntity(MakeLine(31, { 0,0,0 }, { 0,1,0 }));
        auto ids = scene.GetAllIDs();
        bool has30 = std::find(ids.begin(), ids.end(), 30u) != ids.end();
        bool has31 = std::find(ids.begin(), ids.end(), 31u) != ids.end();
        EXPECT_TRUE(has30);
        EXPECT_TRUE(has31);
    }

    // ════════════════════════════════════════════════════════════
    // 7. DirtyFlag & Callback
    // ════════════════════════════════════════════════════════════
    TEST_F(SceneTest, ClearDirty_ClearsFlag) {
        scene.AddEntity(MakeLine(40, { 0,0,0 }, { 1,1,0 }));
        EXPECT_TRUE(scene.IsDirty());
        scene.ClearDirty();
        EXPECT_FALSE(scene.IsDirty());
    }

    TEST_F(SceneTest, DirtyCallback_CalledOnAdd) {
        int callCount = 0;
        scene.SetDirtyCallback([&]() { ++callCount; });
        scene.AddEntity(MakeLine(50, { 0,0,0 }, { 1,1,0 }));
        EXPECT_EQ(callCount, 1);
    }

    TEST_F(SceneTest, DirtyCallback_CalledOnRemove) {
        scene.AddEntity(MakeLine(51, { 0,0,0 }, { 1,1,0 }));
        int callCount = 0;
        scene.SetDirtyCallback([&]() { ++callCount; });
        scene.RemoveEntity(51);
        EXPECT_EQ(callCount, 1);
    }

    TEST_F(SceneTest, DirtyCallback_NotCalledOnNullAdd) {
        int callCount = 0;
        scene.SetDirtyCallback([&]() { ++callCount; });
        scene.AddEntity(nullptr);
        EXPECT_EQ(callCount, 0);
    }

    TEST_F(SceneTest, DirtyCallback_NotCalledOnMissingRemove) {
        int callCount = 0;
        scene.SetDirtyCallback([&]() { ++callCount; });
        scene.RemoveEntity(9999);
        EXPECT_EQ(callCount, 0);
    }

    TEST_F(SceneTest, DirtyCallback_ReplacedCallback_OldNotCalled) {
        int old = 0, fresh = 0;
        scene.SetDirtyCallback([&]() { ++old; });
        scene.SetDirtyCallback([&]() { ++fresh; });
        scene.AddEntity(MakeLine(52, { 0,0,0 }, { 1,1,0 }));
        EXPECT_EQ(old, 0);
        EXPECT_EQ(fresh, 1);
    }

    TEST_F(SceneTest, MarkDirty_ManuallySetsDirty) {
        EXPECT_FALSE(scene.IsDirty());
        scene.MarkDirty();
        EXPECT_TRUE(scene.IsDirty());
    }

    // ════════════════════════════════════════════════════════════
    // 8. QueryByBox
    // ════════════════════════════════════════════════════════════
    TEST_F(SceneTest, QueryByBox_EmptyScene_ReturnsEmpty) {
        Box box({ 0,0,0 }, { 10,10,0 });
        EXPECT_TRUE(scene.QueryByBox(box).empty());
    }

    TEST_F(SceneTest, QueryByBox_EntityInsideBox_Found) {
        scene.AddEntity(MakeLine(60, { 1,1,0 }, { 2,2,0 }));
        Box box({ 0,0,0 }, { 5,5,0 });
        auto result = scene.QueryByBox(box);
        EXPECT_EQ(result.size(), 1u);
        EXPECT_EQ(result[0], 60u);
    }

    TEST_F(SceneTest, QueryByBox_EntityOutsideBox_NotFound) {
        scene.AddEntity(MakeLine(61, { 10,10,0 }, { 20,20,0 }));
        Box box({ 0,0,0 }, { 5,5,0 });
        EXPECT_TRUE(scene.QueryByBox(box).empty());
    }

    TEST_F(SceneTest, QueryByBox_MultipleEntities_PartialMatch) {
        scene.AddEntity(MakeLine(70, { 1,1,0 }, { 2,2,0 }));     // 在框内
        scene.AddEntity(MakeLine(71, { 50,50,0 }, { 60,60,0 })); // 在框外
        Box box({ 0,0,0 }, { 5,5,0 });
        auto result = scene.QueryByBox(box);
        EXPECT_EQ(result.size(), 1u);
        EXPECT_EQ(result[0], 70u);
    }

    TEST_F(SceneTest, QueryByBox_EntityOnBorder_Found) {
        // 线段恰好在 box 边界上，Intersects 应返回 true
        scene.AddEntity(MakeLine(72, { 0,0,0 }, { 5,0,0 }));
        Box box({ 0,0,0 }, { 5,5,0 });
        auto result = scene.QueryByBox(box);
        EXPECT_FALSE(result.empty());
    }

    // ════════════════════════════════════════════════════════════
    // 9. 并发读（多线程同时 GetEntity / Has）
    // ════════════════════════════════════════════════════════════
    TEST_F(SceneTest, ConcurrentRead_NoDataRace) {
        for (int i = 0; i < 100; ++i)
            scene.AddEntity(MakeLine(static_cast<Object::ObjectID>(i), { 0,0,0 }, { 1,1,0 }));
        scene.ClearDirty();

        constexpr int kThreads = 8;
        std::vector<std::thread> threads;
        threads.reserve(kThreads);

        for (int t = 0; t < kThreads; ++t) {
            threads.emplace_back([&]() {
                for (int i = 0; i < 100; ++i) {
                    (void)scene.GetEntity(static_cast<Object::ObjectID>(i));
                    (void)scene.Has(static_cast<Object::ObjectID>(i));
                    (void)scene.EntityCount();
                }
                });
        }
        for (auto& th : threads) th.join();
        SUCCEED();
    }

    // ════════════════════════════════════════════════════════════
    // 10. 性能：大量实体增删
    // ════════════════════════════════════════════════════════════
    TEST_F(SceneTest, Performance_AddRemove_10000Entities) {
        constexpr int kCount = 10000;
        auto start = std::chrono::steady_clock::now();

        for (int i = 0; i < kCount; ++i)
            scene.AddEntity(MakeLine(static_cast<Object::ObjectID>(i), { 0,0,0 }, { 1,1,0 }));
        for (int i = 0; i < kCount; ++i)
            scene.RemoveEntity(static_cast<Object::ObjectID>(i));

        auto elapsed = std::chrono::steady_clock::now() - start;
        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count();

        EXPECT_LT(ms, 500) << "10000 次增删耗时超过 500ms: " << ms << "ms";
        EXPECT_EQ(scene.EntityCount(), 0);
    }

} // namespace MiniCAD