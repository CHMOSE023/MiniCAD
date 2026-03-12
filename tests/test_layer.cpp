// ============================================================
// MiniCAD — tests/Scene/test_Layer.cpp
// 覆盖：构造 / getter / setter / 边界 / 并发读写
// ============================================================
#include <gtest/gtest.h>
#include <thread>
#include <vector>
#include "app/Scene/Layer.hpp"
#include "core/Entity/EntityAttr.h"

namespace MiniCAD {

    // ────────────────────────────────────────────────────────────
    // Fixture
    // ────────────────────────────────────────────────────────────
    class LayerTest : public ::testing::Test {
    protected:
        static constexpr Layer::LayerID kID = 42;
        Layer layer{ kID, "TestLayer" };
    };

    // ════════════════════════════════════════════════════════════
    // 1. 构造 & 初始状态
    // ════════════════════════════════════════════════════════════
    TEST_F(LayerTest, Constructor_SetsIDAndName) {
        EXPECT_EQ(layer.GetID(), kID);
        EXPECT_EQ(layer.GetName(), "TestLayer");
    }

    TEST_F(LayerTest, DefaultColor_IsWhite) {
        const Color4 c = layer.GetColor();
        EXPECT_EQ(c.r, 255);
        EXPECT_EQ(c.g, 255);
        EXPECT_EQ(c.b, 255);
        EXPECT_EQ(c.a, 255);
    }

    TEST_F(LayerTest, DefaultVisible_IsTrue) {
        EXPECT_TRUE(layer.IsVisible());
    }

    TEST_F(LayerTest, DefaultLocked_IsFalse) {
        EXPECT_FALSE(layer.IsLocked());
    }

    // ════════════════════════════════════════════════════════════
    // 2. SetName
    // ════════════════════════════════════════════════════════════
    TEST_F(LayerTest, SetName_UpdatesName) {
        layer.SetName("NewName");
        EXPECT_EQ(layer.GetName(), "NewName");
    }

    TEST_F(LayerTest, SetName_EmptyString) {
        layer.SetName("");
        EXPECT_EQ(layer.GetName(), "");
    }

    TEST_F(LayerTest, SetName_UnicodeString) {
        layer.SetName(u8"图层一");
        EXPECT_EQ(layer.GetName(), u8"图层一");
    }

    TEST_F(LayerTest, SetName_VeryLongString) {
        std::string longName(10000, 'A');
        layer.SetName(longName);
        EXPECT_EQ(layer.GetName(), longName);
    }

    // ════════════════════════════════════════════════════════════
    // 3. SetColor
    // ════════════════════════════════════════════════════════════
    TEST_F(LayerTest, SetColor_UpdatesColor) {
        Color4 red = Color4::Red();
        layer.SetColor(red);
        const Color4 c = layer.GetColor();
        EXPECT_EQ(c.r, 255);
        EXPECT_EQ(c.g, 0);
        EXPECT_EQ(c.b, 0);
        EXPECT_EQ(c.a, 255);
    }

    TEST_F(LayerTest, SetColor_Black) {
        layer.SetColor(Color4::Black());
        const Color4 c = layer.GetColor();
        EXPECT_EQ(c.r, 0);
        EXPECT_EQ(c.g, 0);
        EXPECT_EQ(c.b, 0);
        EXPECT_EQ(c.a, 255);
    }

    TEST_F(LayerTest, SetColor_ArbitraryRGBA) {
        Color4 custom(10, 20, 30, 128);
        layer.SetColor(custom);
        const Color4 c = layer.GetColor();
        EXPECT_EQ(c.r, 10);
        EXPECT_EQ(c.g, 20);
        EXPECT_EQ(c.b, 30);
        EXPECT_EQ(c.a, 128);
    }

    TEST_F(LayerTest, SetColor_Transparent) {
        Color4 transparent(0, 0, 0, 0);
        layer.SetColor(transparent);
        EXPECT_EQ(layer.GetColor().a, 0);
    }

    // ════════════════════════════════════════════════════════════
    // 4. SetVisible
    // ════════════════════════════════════════════════════════════
    TEST_F(LayerTest, SetVisible_False) {
        layer.SetVisible(false);
        EXPECT_FALSE(layer.IsVisible());
    }

    TEST_F(LayerTest, SetVisible_ToggleBackToTrue) {
        layer.SetVisible(false);
        layer.SetVisible(true);
        EXPECT_TRUE(layer.IsVisible());
    }

    TEST_F(LayerTest, SetVisible_IdempotentTrue) {
        layer.SetVisible(true);
        layer.SetVisible(true);
        EXPECT_TRUE(layer.IsVisible());
    }

    TEST_F(LayerTest, SetVisible_IdempotentFalse) {
        layer.SetVisible(false);
        layer.SetVisible(false);
        EXPECT_FALSE(layer.IsVisible());
    }

    // ════════════════════════════════════════════════════════════
    // 5. SetLocked
    // ════════════════════════════════════════════════════════════
    TEST_F(LayerTest, SetLocked_True) {
        layer.SetLocked(true);
        EXPECT_TRUE(layer.IsLocked());
    }

    TEST_F(LayerTest, SetLocked_ToggleBackToFalse) {
        layer.SetLocked(true);
        layer.SetLocked(false);
        EXPECT_FALSE(layer.IsLocked());
    }

    TEST_F(LayerTest, SetLocked_IdempotentTrue) {
        layer.SetLocked(true);
        layer.SetLocked(true);
        EXPECT_TRUE(layer.IsLocked());
    }

    // ════════════════════════════════════════════════════════════
    // 6. ID 边界
    // ════════════════════════════════════════════════════════════
    TEST_F(LayerTest, ZeroID_IsValid) {
        Layer zeroLayer(0, "Zero");
        EXPECT_EQ(zeroLayer.GetID(), 0u);
    }

    TEST_F(LayerTest, MaxID_IsValid) {
        Layer::LayerID maxID = std::numeric_limits<Layer::LayerID>::max();
        Layer maxLayer(maxID, "Max");
        EXPECT_EQ(maxLayer.GetID(), maxID);
    }

    // ════════════════════════════════════════════════════════════
    // 7. 多属性组合
    // ════════════════════════════════════════════════════════════
    TEST_F(LayerTest, AllAttributes_IndependentOfEachOther) {
        layer.SetName("Combined");
        layer.SetColor(Color4::Red());
        layer.SetVisible(false);
        layer.SetLocked(true);

        EXPECT_EQ(layer.GetName(), "Combined");
        EXPECT_EQ(layer.GetColor().r, 255);
        EXPECT_FALSE(layer.IsVisible());
        EXPECT_TRUE(layer.IsLocked());
        EXPECT_EQ(layer.GetID(), kID); // ID 不变
    }

    // ════════════════════════════════════════════════════════════
    // 8. 并发读（多线程同时读取，不应崩溃）
    // ════════════════════════════════════════════════════════════
    TEST_F(LayerTest, ConcurrentRead_NoDataRace) {
        layer.SetName("Concurrent");
        layer.SetColor(Color4::White());
        layer.SetVisible(true);

        constexpr int kThreads = 16;
        std::vector<std::thread> threads;
        threads.reserve(kThreads);

        for (int i = 0; i < kThreads; ++i) {
            threads.emplace_back([&]() {
                for (int j = 0; j < 1000; ++j) {
                    (void)layer.GetID();
                    (void)layer.GetName();
                    (void)layer.GetColor();
                    (void)layer.IsVisible();
                    (void)layer.IsLocked();
                }
                });
        }
        for (auto& t : threads) t.join();
        // 不崩溃即通过
        SUCCEED();
    }

} // namespace MiniCAD
