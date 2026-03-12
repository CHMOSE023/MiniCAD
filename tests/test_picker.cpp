// ============================================================
// MiniCAD — tests/Picking/test_Picker.cpp
// 覆盖：BoxSelect / PickAt / Tolerance / 边界
// 策略：ScreenToRayFn 通过 lambda 注入，不依赖 Camera
//       BoxSelect 直接验证 XY 平面框选行为
// ============================================================
#include <gtest/gtest.h>
#include "app/Picking/Picker.h"
#include "app/Scene/Scene.h"
#include "core/Entity/LineEntity.h"
#include "math/Ray.hpp"
#include <memory>
#include <core/Object/Object.hpp>
#include <math/Point.hpp>
#include <math/Box.hpp>

namespace MiniCAD {

    // ────────────────────────────────────────────────────────────
    // 辅助
    // ────────────────────────────────────────────────────────────
    static std::unique_ptr<LineEntity> MakeLine(
        Object::ObjectID id,
        const Point3& from,
        const Point3& to)
    {
        return std::make_unique<LineEntity>(id, from, to);
    }

    // 构造一个固定射线的 ScreenToRayFn（忽略屏幕坐标，总返回同一条射线）
    static Picker::ScreenToRayFn FixedRay(const Point3& origin, const Vec3& dir) {
        return [origin, dir](const Vec2&) -> Ray {
            return Ray(origin, dir.Normalized());
            };
    }

    // 构造按屏幕坐标映射为 XY 平面射线的 ScreenToRayFn
    // screenPos → origin=(x, y, 100), dir=(0, 0, -1)
    static Picker::ScreenToRayFn TopDownRay() {
        return [](const Vec2& screenPos) -> Ray {
            return Ray(
                Point3(screenPos.x, screenPos.y, 100.f),
                Vec3(0.f, 0.f, -1.f)
            );
            };
    }

    // ────────────────────────────────────────────────────────────
    // Fixture
    // ────────────────────────────────────────────────────────────
    class PickerTest : public ::testing::Test {
    protected:
        Scene scene;
    };

    // ════════════════════════════════════════════════════════════
    // 1. BoxSelect 基础
    // ════════════════════════════════════════════════════════════
    TEST_F(PickerTest, BoxSelect_EmptyScene_ReturnsEmpty) {
        Picker picker(scene, TopDownRay());
        Box2D rect(Vec2{ 0,0 }, Vec2{ 10,10 });
        EXPECT_TRUE(picker.BoxSelect(rect).empty());
    }

    TEST_F(PickerTest, BoxSelect_EntityInsideRect_Found) {
        scene.AddEntity(MakeLine(1, { 1,1,0 }, { 2,2,0 }));
        Picker picker(scene, TopDownRay());
        Box2D rect(Vec2{ 0,0 }, Vec2{ 5,5 });
        auto result = picker.BoxSelect(rect);
        ASSERT_EQ(result.size(), 1u);
        EXPECT_EQ(result[0], 1u);
    }

    TEST_F(PickerTest, BoxSelect_EntityOutsideRect_NotFound) {
        scene.AddEntity(MakeLine(2, { 20,20,0 }, { 30,30,0 }));
        Picker picker(scene, TopDownRay());
        Box2D rect(Vec2{ 0,0 }, Vec2{ 5,5 });
        EXPECT_TRUE(picker.BoxSelect(rect).empty());
    }

    TEST_F(PickerTest, BoxSelect_MultipleEntities_PartialMatch) {
        scene.AddEntity(MakeLine(3, { 1,1,0 }, { 2,2,0 }));   // 在框内
        scene.AddEntity(MakeLine(4, { 50,50,0 }, { 60,60,0 })); // 在框外
        Picker picker(scene, TopDownRay());
        Box2D rect(Vec2{ 0,0 }, Vec2{ 5,5 });
        auto result = picker.BoxSelect(rect);
        EXPECT_EQ(result.size(), 1u);
        EXPECT_EQ(result[0], 3u);
    }

    TEST_F(PickerTest, BoxSelect_AllEntitiesInRect_AllFound) {
        scene.AddEntity(MakeLine(5, { 1,1,0 }, { 2,2,0 }));
        scene.AddEntity(MakeLine(6, { 3,3,0 }, { 4,4,0 }));
        Picker picker(scene, TopDownRay());
        Box2D rect(Vec2{ 0,0 }, Vec2{ 10,10 });
        auto result = picker.BoxSelect(rect);
        EXPECT_EQ(result.size(), 2u);
    }

    TEST_F(PickerTest, BoxSelect_ZeroSizeRect_NoResult) {
        scene.AddEntity(MakeLine(7, { 1,1,0 }, { 2,2,0 }));
        Picker picker(scene, TopDownRay());
        Box2D rect(Vec2{ 5,5 }, Vec2{ 5,5 }); // 退化矩形
        // 线段不在此点上，期望为空
        EXPECT_TRUE(picker.BoxSelect(rect).empty());
    }

    // ════════════════════════════════════════════════════════════
    // 2. PickAt 基础
    // ════════════════════════════════════════════════════════════
    TEST_F(PickerTest, PickAt_EmptyScene_NoHit) {
        Picker picker(scene, TopDownRay());
        PickResult r = picker.PickAt(Vec2{ 5, 5 });
        EXPECT_FALSE(r.hit);
        EXPECT_EQ(r.entityId, Object::INVALID_ID);
    }

    TEST_F(PickerTest, PickAt_RayHitsLine_ReturnsHit) {
        // 线段从 (5,0,0) 到 (5,10,0)，射线从 (5,5,100) 垂直向下
        scene.AddEntity(MakeLine(10, { 5,0,0 }, { 5,10,0 }));
        Picker picker(scene, TopDownRay());
        PickResult r = picker.PickAt(Vec2{ 5, 5 });
        EXPECT_TRUE(r.hit);
        EXPECT_EQ(r.entityId, 10u);
    }

    TEST_F(PickerTest, PickAt_RayMissesLine_NoHit) {
        // 线段远离射线
        scene.AddEntity(MakeLine(11, { 100,100,0 }, { 200,200,0 }));
        Picker picker(scene, TopDownRay());
        PickResult r = picker.PickAt(Vec2{ 5, 5 });
        EXPECT_FALSE(r.hit);
    }

    TEST_F(PickerTest, PickAt_MultipleLines_ReturnsNearest) {
        // 两条线段都在 Z=0 平面，射线从 (5,5,100) 垂直向下
        // 线段20：经过 x=5，射线正好穿过，距离最小
        // 线段21：x=8，偏离射线较远
        scene.AddEntity(MakeLine(20, { 5,0,0 }, { 5,10,0 }));  // 近
        scene.AddEntity(MakeLine(21, { 8,0,0 }, { 8,10,0 }));  // 远
        Picker picker(scene, TopDownRay());
        PickResult r = picker.PickAt(Vec2{ 5, 5 });
        EXPECT_TRUE(r.hit);
        EXPECT_EQ(r.entityId, 20u);
    }

    // ════════════════════════════════════════════════════════════
    // 3. SetPickTolerance / GetPickTolerance
    // ════════════════════════════════════════════════════════════
    TEST_F(PickerTest, DefaultTolerance_Is5) {
        Picker picker(scene, TopDownRay());
        EXPECT_FLOAT_EQ(picker.GetPickTolerance(), 5.0f);
    }

    TEST_F(PickerTest, SetTolerance_UpdatesValue) {
        Picker picker(scene, TopDownRay());
        picker.SetPickTolerance(10.f);
        EXPECT_FLOAT_EQ(picker.GetPickTolerance(), 10.f);
    }

    TEST_F(PickerTest, SetTolerance_Zero_Allowed) {
        Picker picker(scene, TopDownRay());
        picker.SetPickTolerance(0.f);
        EXPECT_FLOAT_EQ(picker.GetPickTolerance(), 0.f);
    }

    // ════════════════════════════════════════════════════════════
    // 4. ScreenToRayFn 注入验证
    // ════════════════════════════════════════════════════════════
    TEST_F(PickerTest, CustomRayFn_CalledWithCorrectScreenPos) {
        Vec2 capturedPos{ -1, -1 };
        auto fn = [&](const Vec2& pos) -> Ray {
            capturedPos = pos;
            return Ray(Point3(0, 0, 100), Vec3(0, 0, -1));
            };
        scene.AddEntity(MakeLine(30, { 0,0,0 }, { 1,1,0 }));
        Picker picker(scene, fn);
        picker.PickAt(Vec2{ 42, 99 });
        EXPECT_FLOAT_EQ(capturedPos.x, 42.f);
        EXPECT_FLOAT_EQ(capturedPos.y, 99.f);
    }

    TEST_F(PickerTest, CustomRayFn_MissRay_NoHit) {
        // 线段在 x=[0,10], y=0, z=0
        // 射线从 (100,100,100) 朝 +X 方向，永远不会经过线段附近
        auto fn = FixedRay(Point3(100.f, 100.f, 100.f), Vec3(1.f, 0.f, 0.f));
        scene.AddEntity(MakeLine(31, { 0,0,0 }, { 10,0,0 }));
        Picker picker(scene, fn);
        PickResult r = picker.PickAt(Vec2{ 0, 0 });
        EXPECT_FALSE(r.hit);
    }

    // ════════════════════════════════════════════════════════════
    // 5. PickResult 字段验证
    // ════════════════════════════════════════════════════════════
    TEST_F(PickerTest, PickResult_HitDist_IsPositive) {
        scene.AddEntity(MakeLine(40, { 5,0,0 }, { 5,10,0 }));
        Picker picker(scene, TopDownRay());
        PickResult r = picker.PickAt(Vec2{ 5, 5 });
        EXPECT_TRUE(r.hit);
        EXPECT_GT(r.hitDist, 0.f);
    }

    TEST_F(PickerTest, PickResult_NoHit_DefaultValues) {
        Picker picker(scene, TopDownRay());
        PickResult r = picker.PickAt(Vec2{ 0, 0 });
        EXPECT_FALSE(r.hit);
        EXPECT_EQ(r.entityId, Object::INVALID_ID);
        EXPECT_FLOAT_EQ(r.hitDist, 1e38f);
    }

} // namespace MiniCAD