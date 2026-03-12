// ============================================================
// MiniCAD — tests/core/Entity/LineEntityTest.cpp
// 职责：LineEntity 单元测试
// 覆盖：构造、getter/setter、BoundingBox、Snapshot 往返、
//        Serialize/Deserialize（通过 JsonSerializer）
// ============================================================
#include <gtest/gtest.h>
#include "core/Entity/LineEntity.h"
#include "core/Object/Object.hpp"
#include "doc/Archive/JsonSerializer.h"
#include "math/MathDefs.hpp"
#include <core/GeomKernel/Line.hpp>
using namespace MiniCAD;

// ════════════════════════════════════════════════════════════
// 辅助
// ════════════════════════════════════════════════════════════

static LineEntity MakeLine(MiniCAD::Object::ObjectID id ,
    Real x0, Real y0, Real z0,
    Real x1, Real y1, Real z1)
{
    return LineEntity(id, Point3{ x0, y0, z0 }, Point3{ x1, y1, z1 });
}

// ════════════════════════════════════════════════════════════
// 1. 构造
// ════════════════════════════════════════════════════════════

TEST(LineEntity, Construct_StoresStartAndEnd)
{
    auto e = MakeLine(1, 1, 2, 3, 4, 5, 6);
    EXPECT_FLOAT_EQ(e.GetLine().start.x, 1.f);
    EXPECT_FLOAT_EQ(e.GetLine().start.y, 2.f);
    EXPECT_FLOAT_EQ(e.GetLine().start.z, 3.f);
    EXPECT_FLOAT_EQ(e.GetLine().end.x, 4.f);
    EXPECT_FLOAT_EQ(e.GetLine().end.y, 5.f);
    EXPECT_FLOAT_EQ(e.GetLine().end.z, 6.f);
}

TEST(LineEntity, Construct_StoresID)
{
    LineEntity e(42, { 0,0,0 }, { 1,1,1 });
    EXPECT_EQ(e.GetID(), 42);
}

TEST(LineEntity, Construct_NegativeCoords)
{
    auto e = MakeLine(1, -3, -2, -1, -6, -5, -4);
    EXPECT_FLOAT_EQ(e.GetLine().start.x, -3.f);
    EXPECT_FLOAT_EQ(e.GetLine().end.z, -4.f);
}

// ════════════════════════════════════════════════════════════
// 2. SetLine / GetLine
// ════════════════════════════════════════════════════════════

TEST(LineEntity, SetLine_UpdatesEndpoints)
{
    LineEntity e(1, { 0,0,0 }, { 1,1,1 });
    Line newLine{ Point3{7,8,9}, Point3{10,11,12} };
    e.SetLine(newLine);
    EXPECT_FLOAT_EQ(e.GetLine().start.x, 7.f);
    EXPECT_FLOAT_EQ(e.GetLine().end.z, 12.f);
}

TEST(LineEntity, SetLine_DoesNotChangeID)
{
    LineEntity e(99, { 0,0,0 }, { 1,1,1 });
    e.SetLine(Line{ {5,5,5},{6,6,6} });
    EXPECT_EQ(e.GetID(), 99);
}

// ════════════════════════════════════════════════════════════
// 3. BoundingBox
// ════════════════════════════════════════════════════════════

TEST(LineEntity, BBox_AxisAligned)
{
    auto e = MakeLine(1, 1, 2, 3, 4, 5, 6);
    Box b = e.GetBoundingBox();
    EXPECT_FLOAT_EQ(b.min.x, 1.f);  EXPECT_FLOAT_EQ(b.max.x, 4.f);
    EXPECT_FLOAT_EQ(b.min.y, 2.f);  EXPECT_FLOAT_EQ(b.max.y, 5.f);
    EXPECT_FLOAT_EQ(b.min.z, 3.f);  EXPECT_FLOAT_EQ(b.max.z, 6.f);
}

TEST(LineEntity, BBox_ReversedOrder_MinMaxStillCorrect)
{
    // 终点坐标比起点小，min/max 不应依赖传入顺序
    auto e = MakeLine(1, 5, 5, 5, 1, 1, 1);
    Box b = e.GetBoundingBox();
    EXPECT_LE(b.min.x, b.max.x);
    EXPECT_LE(b.min.y, b.max.y);
    EXPECT_LE(b.min.z, b.max.z);
    EXPECT_FLOAT_EQ(b.min.x, 1.f);
    EXPECT_FLOAT_EQ(b.max.x, 5.f);
}

TEST(LineEntity, BBox_DegenerateLine_IsPoint)
{
    // 零长度线段，包围盒应退化为一个点
    auto e = MakeLine(1, 3, 3, 3, 3, 3, 3);
    Box b = e.GetBoundingBox();
    EXPECT_FLOAT_EQ(b.min.x, b.max.x);
    EXPECT_FLOAT_EQ(b.min.y, b.max.y);
    EXPECT_FLOAT_EQ(b.min.z, b.max.z);
}

TEST(LineEntity, BBox_NegativeCoords)
{
    auto e = MakeLine(1, -4, -5, -6, -1, -2, -3);
    Box b = e.GetBoundingBox();
    EXPECT_FLOAT_EQ(b.min.x, -4.f);
    EXPECT_FLOAT_EQ(b.max.x, -1.f);
}

TEST(LineEntity, BBox_CrossesOrigin)
{
    auto e = MakeLine(1, -1, -1, -1, 1, 1, 1);
    Box b = e.GetBoundingBox();
    EXPECT_FLOAT_EQ(b.min.x, -1.f);
    EXPECT_FLOAT_EQ(b.max.x, 1.f);
}

// ════════════════════════════════════════════════════════════
// 4. Snapshot / RestoreSnapshot
// ════════════════════════════════════════════════════════════

TEST(LineEntity, Snapshot_SizeIs6Reals)
{
    // Snapshot 必须正好是 6 个 Real 字节
    // ⚠️  如果 Real=double，size 应为 48；如果 Real=float，size 应为 24
    //     这里用 sizeof(Real) 自适应，避免 Real 类型变化时失效
    LineEntity e(1, { 0,0,0 }, { 1,1,1 });
    EXPECT_EQ(e.Snapshot().size(), 6 * sizeof(Real));
}

TEST(LineEntity, Snapshot_RoundTrip_Basic)
{
    auto original = MakeLine(1, 1, 2, 3, 4, 5, 6);
    auto snap = original.Snapshot();

    LineEntity restored(1, { 0,0,0 }, { 0,0,0 });
    restored.RestoreSnapshot(snap);

    EXPECT_FLOAT_EQ(restored.GetLine().start.x, 1.f);
    EXPECT_FLOAT_EQ(restored.GetLine().start.y, 2.f);
    EXPECT_FLOAT_EQ(restored.GetLine().start.z, 3.f);
    EXPECT_FLOAT_EQ(restored.GetLine().end.x, 4.f);
    EXPECT_FLOAT_EQ(restored.GetLine().end.y, 5.f);
    EXPECT_FLOAT_EQ(restored.GetLine().end.z, 6.f);
}

TEST(LineEntity, Snapshot_RoundTrip_NegativeCoords)
{
    auto original = MakeLine(1, -1, -2, -3, -4, -5, -6);
    LineEntity restored(1, { 0,0,0 }, { 0,0,0 });
    restored.RestoreSnapshot(original.Snapshot());
    EXPECT_FLOAT_EQ(restored.GetLine().start.x, -1.f);
    EXPECT_FLOAT_EQ(restored.GetLine().end.z, -6.f);
}

TEST(LineEntity, Snapshot_RoundTrip_AfterSetLine)
{
    // 修改后的快照应反映新值
    LineEntity e(1, { 0,0,0 }, { 1,1,1 });
    e.SetLine(Line{ {7,8,9},{10,11,12} });
    auto snap = e.Snapshot();

    LineEntity restored(1, { 0,0,0 }, { 0,0,0 });
    restored.RestoreSnapshot(snap);
    EXPECT_FLOAT_EQ(restored.GetLine().start.x, 7.f);
    EXPECT_FLOAT_EQ(restored.GetLine().end.z, 12.f);
}

TEST(LineEntity, RestoreSnapshot_TooShort_DoesNotCorrupt)
{
    // 数据不足时应静默忽略，原数据保持不变
    LineEntity e(1, { 1,2,3 }, { 4,5,6 });
    std::vector<uint8_t> bad(4);  // 远小于 6*sizeof(Real)
    e.RestoreSnapshot(bad);
    // 原值不变
    EXPECT_FLOAT_EQ(e.GetLine().start.x, 1.f);
    EXPECT_FLOAT_EQ(e.GetLine().end.z, 6.f);
}

TEST(LineEntity, RestoreSnapshot_ExactSize_DoesNotCrash)
{
    LineEntity e(1, { 0,0,0 }, { 0,0,0 });
    std::vector<uint8_t> exact(6 * sizeof(Real), 0);
    EXPECT_NO_FATAL_FAILURE(e.RestoreSnapshot(exact));
}

TEST(LineEntity, RestoreSnapshot_OversizeData_DoesNotCrash)
{
    LineEntity e(1, { 0,0,0 }, { 0,0,0 });
    std::vector<uint8_t> big(256, 0);
    EXPECT_NO_FATAL_FAILURE(e.RestoreSnapshot(big));
}

// ════════════════════════════════════════════════════════════
// 5. Serialize / Deserialize（通过 JsonSerializer）
// ════════════════════════════════════════════════════════════

TEST(LineEntity, Serialize_WritesType)
{
    LineEntity e(1, { 0,0,0 }, { 1,1,1 });
    JsonSerializer s;
    e.Serialize(s);
    EXPECT_EQ(s.ReadString("type"), "LineEntity");
}

TEST(LineEntity, Serialize_WritesID)
{
    LineEntity e(55, { 0,0,0 }, { 1,1,1 });
    JsonSerializer s;
    e.Serialize(s);
    EXPECT_EQ(s.ReadInt("id"), 55);
}

TEST(LineEntity, Serialize_WritesStartAndEnd)
{
    auto e = MakeLine(1, 1, 2, 3, 4, 5, 6);
    JsonSerializer s;
    e.Serialize(s);

    Vec3 sv = s.ReadVec3("start");
    Vec3 ev = s.ReadVec3("end");
    EXPECT_NEAR(sv.x, 1.f, 1e-4f);
    EXPECT_NEAR(sv.y, 2.f, 1e-4f);
    EXPECT_NEAR(sv.z, 3.f, 1e-4f);
    EXPECT_NEAR(ev.x, 4.f, 1e-4f);
    EXPECT_NEAR(ev.z, 6.f, 1e-4f);
}

TEST(LineEntity, Deserialize_RestoresEndpoints)
{
    JsonSerializer s;
    s.Write("start", Vec3(7, 8, 9));
    s.Write("end", Vec3(10, 11, 12));

    LineEntity e(1, { 0,0,0 }, { 0,0,0 });
    e.Deserialize(s);
    EXPECT_NEAR(e.GetLine().start.x, 7.f, 1e-4f);
    EXPECT_NEAR(e.GetLine().start.y, 8.f, 1e-4f);
    EXPECT_NEAR(e.GetLine().start.z, 9.f, 1e-4f);
    EXPECT_NEAR(e.GetLine().end.x, 10.f, 1e-4f);
    EXPECT_NEAR(e.GetLine().end.z, 12.f, 1e-4f);
}

TEST(LineEntity, SerializeDeserialize_RoundTrip)
{
    // 完整往返：序列化 → Flush → Load → 反序列化
    auto original = MakeLine(42, 1, 2, 3, 4, 5, 6);

    JsonSerializer s1;
    original.Serialize(s1);
    std::string json = s1.Flush();

    JsonSerializer s2;
    ASSERT_TRUE(s2.Load(json));

    LineEntity restored(42, { 0,0,0 }, { 0,0,0 });
    restored.Deserialize(s2);

    EXPECT_NEAR(restored.GetLine().start.x, 1.f, 1e-4f);
    EXPECT_NEAR(restored.GetLine().start.y, 2.f, 1e-4f);
    EXPECT_NEAR(restored.GetLine().start.z, 3.f, 1e-4f);
    EXPECT_NEAR(restored.GetLine().end.x, 4.f, 1e-4f);
    EXPECT_NEAR(restored.GetLine().end.y, 5.f, 1e-4f);
    EXPECT_NEAR(restored.GetLine().end.z, 6.f, 1e-4f);
}

TEST(LineEntity, SerializeDeserialize_NegativeCoords)
{
    auto original = MakeLine(1, -1, -2, -3, -4, -5, -6);

    JsonSerializer s1;
    original.Serialize(s1);

    JsonSerializer s2;
    s2.Load(s1.Flush());

    LineEntity restored(1, { 0,0,0 }, { 0,0,0 });
    restored.Deserialize(s2);
    EXPECT_NEAR(restored.GetLine().start.x, -1.f, 1e-4f);
    EXPECT_NEAR(restored.GetLine().end.z, -6.f, 1e-4f);
}
