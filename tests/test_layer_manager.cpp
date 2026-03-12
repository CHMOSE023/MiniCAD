// ============================================================
// MiniCAD — tests/Scene/test_LayerManager.cpp
// 覆盖：构造 / 增删改查 / 默认图层保护 / 活跃图层 / 并发
// ============================================================
#include <gtest/gtest.h>
#include <thread>
#include <vector>
#include <unordered_set>
#include <chrono>
#include "app/Scene/LayerManager.h"

namespace MiniCAD {

    // ────────────────────────────────────────────────────────────
    // Fixture
    // ────────────────────────────────────────────────────────────
    class LayerManagerTest : public ::testing::Test {
    protected:
        LayerManager mgr;
    };

    // ════════════════════════════════════════════════════════════
    // 1. 构造 & 默认状态
    // ════════════════════════════════════════════════════════════
    TEST_F(LayerManagerTest, Constructor_DefaultLayerExists) {
        Layer* def = mgr.GetLayer(LayerManager::DEFAULT_LAYER_ID);
        ASSERT_NE(def, nullptr);
        EXPECT_EQ(def->GetName(), "Default");
    }

    TEST_F(LayerManagerTest, Constructor_DefaultActiveLayer) {
        EXPECT_EQ(mgr.GetActiveLayerID(), LayerManager::DEFAULT_LAYER_ID);
    }

    TEST_F(LayerManagerTest, Constructor_GetAllIDs_ContainsDefault) {
        auto ids = mgr.GetAllLayerIDs();
        ASSERT_EQ(ids.size(), 1u);
        EXPECT_EQ(ids[0], LayerManager::DEFAULT_LAYER_ID);
    }

    // ════════════════════════════════════════════════════════════
    // 2. AddLayer
    // ════════════════════════════════════════════════════════════
    TEST_F(LayerManagerTest, AddLayer_ReturnsUniqueIDs) {
        Layer::LayerID id1 = mgr.AddLayer("A");
        Layer::LayerID id2 = mgr.AddLayer("B");
        EXPECT_NE(id1, id2);
    }

    TEST_F(LayerManagerTest, AddLayer_IDNotEqualToDefault) {
        Layer::LayerID id = mgr.AddLayer("X");
        EXPECT_NE(id, LayerManager::DEFAULT_LAYER_ID);
    }

    TEST_F(LayerManagerTest, AddLayer_LayerIsRetrievable) {
        Layer::LayerID id = mgr.AddLayer("MyLayer");
        Layer* layer = mgr.GetLayer(id);
        ASSERT_NE(layer, nullptr);
        EXPECT_EQ(layer->GetName(), "MyLayer");
        EXPECT_EQ(layer->GetID(), id);
    }

    TEST_F(LayerManagerTest, AddLayer_IncreasesCount) {
        mgr.AddLayer("L1");
        mgr.AddLayer("L2");
        EXPECT_EQ(mgr.GetAllLayerIDs().size(), 3u); // default + 2
    }

    TEST_F(LayerManagerTest, AddLayer_EmptyName_Allowed) {
        Layer::LayerID id = mgr.AddLayer("");
        ASSERT_NE(mgr.GetLayer(id), nullptr);
        EXPECT_EQ(mgr.GetLayer(id)->GetName(), "");
    }

    TEST_F(LayerManagerTest, AddLayer_DuplicateName_AllowedWithDifferentID) {
        Layer::LayerID id1 = mgr.AddLayer("Same");
        Layer::LayerID id2 = mgr.AddLayer("Same");
        EXPECT_NE(id1, id2);
        EXPECT_NE(mgr.GetLayer(id1), mgr.GetLayer(id2));
    }

    // ════════════════════════════════════════════════════════════
    // 3. RemoveLayer
    // ════════════════════════════════════════════════════════════
    TEST_F(LayerManagerTest, RemoveLayer_ExistingLayer_ReturnsTrue) {
        Layer::LayerID id = mgr.AddLayer("Temp");
        EXPECT_TRUE(mgr.RemoveLayer(id));
    }

    TEST_F(LayerManagerTest, RemoveLayer_ExistingLayer_NoLongerRetrievable) {
        Layer::LayerID id = mgr.AddLayer("Temp");
        mgr.RemoveLayer(id);
        EXPECT_EQ(mgr.GetLayer(id), nullptr);
    }

    TEST_F(LayerManagerTest, RemoveLayer_NonExistentID_ReturnsFalse) {
        EXPECT_FALSE(mgr.RemoveLayer(9999));
    }

    TEST_F(LayerManagerTest, RemoveLayer_DefaultLayer_ReturnsFalse) {
        EXPECT_FALSE(mgr.RemoveLayer(LayerManager::DEFAULT_LAYER_ID));
    }

    TEST_F(LayerManagerTest, RemoveLayer_DefaultLayer_StillExists) {
        mgr.RemoveLayer(LayerManager::DEFAULT_LAYER_ID);
        EXPECT_NE(mgr.GetLayer(LayerManager::DEFAULT_LAYER_ID), nullptr);
    }

    TEST_F(LayerManagerTest, RemoveLayer_Twice_SecondReturnsFalse) {
        Layer::LayerID id = mgr.AddLayer("Once");
        mgr.RemoveLayer(id);
        EXPECT_FALSE(mgr.RemoveLayer(id));
    }

    TEST_F(LayerManagerTest, RemoveLayer_DecreasesCount) {
        Layer::LayerID id = mgr.AddLayer("ToRemove");
        size_t before = mgr.GetAllLayerIDs().size();
        mgr.RemoveLayer(id);
        EXPECT_EQ(mgr.GetAllLayerIDs().size(), before - 1);
    }

    // ════════════════════════════════════════════════════════════
    // 4. GetLayer (const & non-const)
    // ════════════════════════════════════════════════════════════
    TEST_F(LayerManagerTest, GetLayer_NonConst_ReturnsWritable) {
        Layer::LayerID id = mgr.AddLayer("Writable");
        Layer* layer = mgr.GetLayer(id);
        ASSERT_NE(layer, nullptr);
        layer->SetName("Modified");
        EXPECT_EQ(mgr.GetLayer(id)->GetName(), "Modified");
    }

    TEST_F(LayerManagerTest, GetLayer_Const_ReturnsReadOnly) {
        Layer::LayerID id = mgr.AddLayer("ReadOnly");
        const LayerManager& cmgr = mgr;
        const Layer* layer = cmgr.GetLayer(id);
        ASSERT_NE(layer, nullptr);
        EXPECT_EQ(layer->GetName(), "ReadOnly");
    }

    TEST_F(LayerManagerTest, GetLayer_InvalidID_ReturnsNullptr) {
        EXPECT_EQ(mgr.GetLayer(99999), nullptr);
    }

    TEST_F(LayerManagerTest, GetLayer_Const_InvalidID_ReturnsNullptr) {
        const LayerManager& cmgr = mgr;
        EXPECT_EQ(cmgr.GetLayer(99999), nullptr);
    }

    // ════════════════════════════════════════════════════════════
    // 5. GetAllLayerIDs
    // ════════════════════════════════════════════════════════════
    TEST_F(LayerManagerTest, GetAllLayerIDs_NoDuplicates) {
        mgr.AddLayer("A");
        mgr.AddLayer("B");
        mgr.AddLayer("C");

        auto ids = mgr.GetAllLayerIDs();
        std::unordered_set<Layer::LayerID> unique(ids.begin(), ids.end());
        EXPECT_EQ(unique.size(), ids.size());
    }

    TEST_F(LayerManagerTest, GetAllLayerIDs_AfterAddAndRemove_Consistent) {
        Layer::LayerID id1 = mgr.AddLayer("X");
        Layer::LayerID id2 = mgr.AddLayer("Y");
        mgr.RemoveLayer(id1);

        auto ids = mgr.GetAllLayerIDs();
        EXPECT_EQ(ids.size(), 2u); // default + id2

        bool hasID2 = std::find(ids.begin(), ids.end(), id2) != ids.end();
        bool hasID1 = std::find(ids.begin(), ids.end(), id1) != ids.end();
        EXPECT_TRUE(hasID2);
        EXPECT_FALSE(hasID1);
    }

    // ════════════════════════════════════════════════════════════
    // 6. SetActiveLayerID / GetActiveLayerID
    // ════════════════════════════════════════════════════════════
    TEST_F(LayerManagerTest, SetActiveLayerID_ValidID_Updates) {
        Layer::LayerID id = mgr.AddLayer("Active");
        mgr.SetActiveLayerID(id);
        EXPECT_EQ(mgr.GetActiveLayerID(), id);
    }

    TEST_F(LayerManagerTest, SetActiveLayerID_InvalidID_NoChange) {
        mgr.SetActiveLayerID(9999);
        EXPECT_EQ(mgr.GetActiveLayerID(), LayerManager::DEFAULT_LAYER_ID);
    }

    TEST_F(LayerManagerTest, SetActiveLayerID_DefaultLayer_Allowed) {
        Layer::LayerID id = mgr.AddLayer("Temp");
        mgr.SetActiveLayerID(id);
        mgr.SetActiveLayerID(LayerManager::DEFAULT_LAYER_ID);
        EXPECT_EQ(mgr.GetActiveLayerID(), LayerManager::DEFAULT_LAYER_ID);
    }

    TEST_F(LayerManagerTest, SetActiveLayerID_AfterRemove_ActiveBecomesStaleBehavior) {
        // 设置某层为活跃，然后将其删除：active ID 仍保留旧值（调用方负责维护）
        Layer::LayerID id = mgr.AddLayer("WillBeRemoved");
        mgr.SetActiveLayerID(id);
        mgr.RemoveLayer(id);
        // active ID 记录的是旧 ID，GetLayer 返回 nullptr
        EXPECT_EQ(mgr.GetActiveLayerID(), id);
        EXPECT_EQ(mgr.GetLayer(mgr.GetActiveLayerID()), nullptr);
    }

    // ════════════════════════════════════════════════════════════
    // 7. 并发 AddLayer（ID 唯一性）
    // ════════════════════════════════════════════════════════════
   /* TEST_F(LayerManagerTest, ConcurrentAddLayer_AllIDsUnique) {
        constexpr int kThreads = 8;
        constexpr int kPerThread = 100;

        std::vector<std::vector<Layer::LayerID>> results(kThreads);
        std::vector<std::thread> threads;
        threads.reserve(kThreads);

        for (int t = 0; t < kThreads; ++t) {
            threads.emplace_back([&, t]() {
                for (int i = 0; i < kPerThread; ++i) {
                    results[t].push_back(mgr.AddLayer("T" + std::to_string(t)));
                }
                });
        }
        for (auto& th : threads) th.join();

        std::unordered_set<Layer::LayerID> all;
        for (auto& vec : results)
            for (auto id : vec)
                all.insert(id);

        EXPECT_EQ(all.size(), static_cast<size_t>(kThreads * kPerThread));
    }*/

    // ════════════════════════════════════════════════════════════
    // 8. 性能：大量图层增删
    // ════════════════════════════════════════════════════════════
    TEST_F(LayerManagerTest, Performance_AddRemove_10000Layers) {
        constexpr int kCount = 10000;
        auto start = std::chrono::steady_clock::now();

        std::vector<Layer::LayerID> ids;
        ids.reserve(kCount);
        for (int i = 0; i < kCount; ++i)
            ids.push_back(mgr.AddLayer("Layer" + std::to_string(i)));
        for (auto id : ids)
            mgr.RemoveLayer(id);

        auto elapsed = std::chrono::steady_clock::now() - start;
        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count();

        EXPECT_LT(ms, 500) << "10000 次增删耗时超过 500ms: " << ms << "ms";
        // 只剩默认图层
        EXPECT_EQ(mgr.GetAllLayerIDs().size(), 1u);
    }

} // namespace MiniCAD
