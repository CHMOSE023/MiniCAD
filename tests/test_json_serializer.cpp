// tests/doc/Archive/JsonSerializerTest.cpp
#include <gtest/gtest.h>
#include "doc/Archive/JsonSerializer.h"

using namespace MiniCAD;

// ════════════════════════════════════════════
// 基础读写往返
// ════════════════════════════════════════════

TEST(JsonSerializer, WriteReadInt) {
    JsonSerializer s;
    s.Write("count", 42);
    EXPECT_EQ(s.ReadInt("count"), 42);
}

TEST(JsonSerializer, WriteReadInt_Negative) {
    JsonSerializer s;
    s.Write("val", -99);
    EXPECT_EQ(s.ReadInt("val"), -99);
}

TEST(JsonSerializer, WriteReadReal) {
    JsonSerializer s;
    s.Write("pi", 3.14f);
    EXPECT_NEAR(s.ReadReal("pi"), 3.14f, 1e-5f);
}

TEST(JsonSerializer, WriteReadString) {
    JsonSerializer s;
    s.Write("type", std::string_view("LineEntity"));
    EXPECT_EQ(s.ReadString("type"), "LineEntity");
}

TEST(JsonSerializer, WriteReadString_EmptyValue) {
    JsonSerializer s;
    s.Write("empty", std::string_view(""));
    EXPECT_EQ(s.ReadString("empty"), "");
}

TEST(JsonSerializer, WriteReadVec3) {
    JsonSerializer s;
    s.Write("pos", Vec3(1.f, 2.f, 3.f));
    Vec3 v = s.ReadVec3("pos");
    EXPECT_NEAR(v.x, 1.f, 1e-5f);
    EXPECT_NEAR(v.y, 2.f, 1e-5f);
    EXPECT_NEAR(v.z, 3.f, 1e-5f);
}

TEST(JsonSerializer, WriteReadVec3_Negative) {
    JsonSerializer s;
    s.Write("dir", Vec3(-1.f, -2.5f, 0.f));
    Vec3 v = s.ReadVec3("dir");
    EXPECT_NEAR(v.x, -1.f, 1e-5f);
    EXPECT_NEAR(v.y, -2.5f, 1e-5f);
    EXPECT_NEAR(v.z, 0.f, 1e-5f);
}

TEST(JsonSerializer, WriteReadVec3_Zero) {
    JsonSerializer s;
    s.Write("origin", Vec3(0.f, 0.f, 0.f));
    Vec3 v = s.ReadVec3("origin");
    EXPECT_FLOAT_EQ(v.x, 0.f);
    EXPECT_FLOAT_EQ(v.y, 0.f);
    EXPECT_FLOAT_EQ(v.z, 0.f);
}

// ════════════════════════════════════════════
// 缺失 key 的默认值行为
// ════════════════════════════════════════════

TEST(JsonSerializer, MissingKey_Int_ReturnsZero) {
    JsonSerializer s;
    EXPECT_EQ(s.ReadInt("nonexistent"), 0);
}

TEST(JsonSerializer, MissingKey_Real_ReturnsZero) {
    JsonSerializer s;
    EXPECT_FLOAT_EQ(s.ReadReal("nonexistent"), 0.f);
}

TEST(JsonSerializer, MissingKey_String_ReturnsEmpty) {
    JsonSerializer s;
    EXPECT_EQ(s.ReadString("nonexistent"), "");
}

TEST(JsonSerializer, MissingKey_Vec3_ReturnsZero) {
    JsonSerializer s;
    Vec3 v = s.ReadVec3("nonexistent");
    EXPECT_FLOAT_EQ(v.x, 0.f);
    EXPECT_FLOAT_EQ(v.y, 0.f);
    EXPECT_FLOAT_EQ(v.z, 0.f);
}

// ════════════════════════════════════════════
// Flush / Load 往返（最重要的集成路径）
// ════════════════════════════════════════════

TEST(JsonSerializer, FlushLoad_Int) {
    JsonSerializer s1;
    s1.Write("id", 7);
    std::string json = s1.Flush();

    JsonSerializer s2;
    ASSERT_TRUE(s2.Load(json));
    EXPECT_EQ(s2.ReadInt("id"), 7);
}

TEST(JsonSerializer, FlushLoad_String) {
    JsonSerializer s1;
    s1.Write("type", std::string_view("LineEntity"));
    std::string json = s1.Flush();

    JsonSerializer s2;
    s2.Load(json);
    EXPECT_EQ(s2.ReadString("type"), "LineEntity");
}

TEST(JsonSerializer, FlushLoad_Vec3) {
    // ⚠️ 这个测试很可能暴露 Load 解析 Vec3 的 bug
    JsonSerializer s1;
    s1.Write("start", Vec3(1.f, 2.f, 3.f));
    std::string json = s1.Flush();

    JsonSerializer s2;
    s2.Load(json);
    Vec3 v = s2.ReadVec3("start");
    EXPECT_NEAR(v.x, 1.f, 1e-4f);
    EXPECT_NEAR(v.y, 2.f, 1e-4f);
    EXPECT_NEAR(v.z, 3.f, 1e-4f);
}

TEST(JsonSerializer, FlushLoad_MultipleFields) {
    // 模拟 LineEntity 完整序列化往返
    JsonSerializer s1;
    s1.Write("type", std::string_view("LineEntity"));
    s1.Write("id", 42);
    s1.Write("start", Vec3(0.f, 0.f, 0.f));
    s1.Write("end", Vec3(3.f, 4.f, 0.f));
    std::string json = s1.Flush();

    JsonSerializer s2;
    ASSERT_TRUE(s2.Load(json));
    EXPECT_EQ(s2.ReadString("type"), "LineEntity");
    EXPECT_EQ(s2.ReadInt("id"), 42);

    Vec3 end = s2.ReadVec3("end");
    EXPECT_NEAR(end.x, 3.f, 1e-4f);
    EXPECT_NEAR(end.y, 4.f, 1e-4f);
}

TEST(JsonSerializer, Load_EmptyJson_ReturnsFalse) {
    JsonSerializer s;
    EXPECT_FALSE(s.Load(""));
}

TEST(JsonSerializer, Load_MalformedJson_DoesNotCrash) {
    JsonSerializer s;
    // 不应崩溃，结果未定义但不能 crash
    s.Load("{{{bad json!!! :::}}}");
}

TEST(JsonSerializer, Flush_ProducesValidStructure) {
    JsonSerializer s;
    s.Write("x", 1);
    std::string json = s.Flush();
    // 必须以 { 开头，} 结尾
    EXPECT_EQ(json.front(), '{');
    EXPECT_EQ(json.back(), '}');
}

// ════════════════════════════════════════════
// WriteVec3 直接接口
// ════════════════════════════════════════════

TEST(JsonSerializer, WriteVec3Direct_RoundTrip) {
    JsonSerializer s;
    s.WriteVec3("pt", 5.f, -3.f, 0.5f);
    Vec3 v = s.ReadVec3("pt");
    EXPECT_NEAR(v.x, 5.f, 1e-5f);
    EXPECT_NEAR(v.y, -3.f, 1e-5f);
    EXPECT_NEAR(v.z, 0.5f, 1e-5f);
}

// ════════════════════════════════════════════
// 极值 / 精度
// ════════════════════════════════════════════

TEST(JsonSerializer, LargeInt) {
    JsonSerializer s;
    s.Write("big", 2147483647); // INT_MAX
    EXPECT_EQ(s.ReadInt("big"), 2147483647);
}

TEST(JsonSerializer, Vec3_LargeValues) {
    JsonSerializer s;
    s.Write("far", Vec3(1e6f, -1e6f, 0.f));
    Vec3 v = s.ReadVec3("far");
    EXPECT_NEAR(v.x, 1e6f, 1.f);
    EXPECT_NEAR(v.y, -1e6f, 1.f);
}