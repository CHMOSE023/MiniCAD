#include <gtest/gtest.h> 
#include "math/Box.hpp"
#include "math/Matrix.hpp"
#include "math/Vector.hpp"
#include "math/Quaternion.hpp"
  
using namespace MiniCAD;

constexpr Real EPS = 1e-6;

TEST(Point3Test, DefaultConstructor) {
    Point3 p;
    EXPECT_NEAR(p.x, 0.0, EPS);
    EXPECT_NEAR(p.y, 0.0, EPS);
    EXPECT_NEAR(p.z, 0.0, EPS);
}

TEST(Point3Test, Origin) {
    Point3 p = Point3::Origin();
    EXPECT_NEAR(p.x, 0.0, EPS);
    EXPECT_NEAR(p.y, 0.0, EPS);
    EXPECT_NEAR(p.z, 0.0, EPS);
}

TEST(Point3Test, AddSubtractVec3) {
    Point3 p{ 1,2,3 };
    Vec3 v{ 0.5, -1.0, 2.0 };

    Point3 pAdd = p + v;
    EXPECT_NEAR(pAdd.x, 1.5, EPS);
    EXPECT_NEAR(pAdd.y, 1.0, EPS);
    EXPECT_NEAR(pAdd.z, 5.0, EPS);

    Point3 pSub = p - v;
    EXPECT_NEAR(pSub.x, 0.5, EPS);
    EXPECT_NEAR(pSub.y, 3.0, EPS);
    EXPECT_NEAR(pSub.z, 1.0, EPS);

    p += v;
    EXPECT_NEAR(p.x, 1.5, EPS);
    EXPECT_NEAR(p.y, 1.0, EPS);
    EXPECT_NEAR(p.z, 5.0, EPS);
}

TEST(Point3Test, SubtractPoint3) {
    Point3 p1{ 1,2,3 };
    Point3 p2{ 0.5, 1.0, -1.0 };

    Vec3 diff = p1 - p2;
    EXPECT_NEAR(diff.x, 0.5, EPS);
    EXPECT_NEAR(diff.y, 1.0, EPS);
    EXPECT_NEAR(diff.z, 4.0, EPS);
}

TEST(Point3Test, Equality) {
    Point3 p1{ 1,2,3 };
    Point3 p2{ 1 + 1e-10, 2 - 1e-10, 3 + 1e-10 };
    Point3 p3{ 2,2,3 };

    EXPECT_TRUE(p1 == p2);
    EXPECT_FALSE(p1 == p3);
    EXPECT_TRUE(p1 != p3);
}

TEST(Point3Test, ToVec3) {
    Point3 p{ 1,2,3 };
    Vec3 v = p.ToVec3();
    EXPECT_NEAR(v.x, 1.0, EPS);
    EXPECT_NEAR(v.y, 2.0, EPS);
    EXPECT_NEAR(v.z, 3.0, EPS);
}

TEST(Point3Test, DistanceTo) {
    Point3 p1{ 0,0,0 };
    Point3 p2{ 1,2,2 };
    Real dist = p1.DistanceTo(p2);
    EXPECT_NEAR(dist, 3.0, EPS); // sqrt(1+4+4) = 3
}

TEST(BoxTest, BoxCenter)
{
    Box b({ 0,0,0 }, { 10,10,10 });
    Point3 c = b.Center();

    EXPECT_TRUE(RealEqual(c.x, 5.0f));
    EXPECT_TRUE(RealEqual(c.y, 5.0f));
    EXPECT_TRUE(RealEqual(c.z, 5.0f));
}

TEST(BoxTest, BoxContains)
{
    Box b({ 0,0,0 }, { 10,10,10 });

    EXPECT_TRUE(b.Contains({ 5,5,5 }));
    EXPECT_TRUE(b.Contains({ 0,0,0 }));
    EXPECT_TRUE(b.Contains({ 10,10,10 }));

    EXPECT_FALSE(b.Contains({ -1,5,5 }));
}

TEST(BoxTest, BoxExpand)
{
    Box b;

    b.Expand({ 1,2,3 });
    b.Expand({ 4,5,6 });

    EXPECT_TRUE(b.IsValid());

    EXPECT_TRUE(RealEqual(b.min.x, 1));
    EXPECT_TRUE(RealEqual(b.min.y, 2));
    EXPECT_TRUE(RealEqual(b.min.z, 3));

    EXPECT_TRUE(RealEqual(b.max.x, 4));
    EXPECT_TRUE(RealEqual(b.max.y, 5));
    EXPECT_TRUE(RealEqual(b.max.z, 6));
}

TEST(VecTest, Vec3Add)
{
    Vec3 a(1, 2, 3);
    Vec3 b(4, 5, 6);

    Vec3 c = a + b;

    EXPECT_TRUE(RealEqual(c.x, 5));
    EXPECT_TRUE(RealEqual(c.y, 7));
    EXPECT_TRUE(RealEqual(c.z, 9));
}

TEST(VecTest, DotProduct)
{
    Vec3 a(1, 0, 0);
    Vec3 b(0, 1, 0);

    Real d = a.Dot(b);

    EXPECT_TRUE(RealZero(d));
}

TEST(VecTest, CrossProduct)
{
    Vec3 a(1, 0, 0);
    Vec3 b(0, 1, 0);

    Vec3 c = a.Cross(b);

    EXPECT_TRUE(c == Vec3(0, 0, 1));
}

TEST(VecTest, Length)
{
    Vec3 v(3, 4, 0);

    EXPECT_TRUE(RealEqual(v.Length(), 5));
}

TEST(VecTest, Normalize)
{
    Vec3 v(10, 0, 0);
    Vec3 n = v.Normalized();

    EXPECT_TRUE(n == Vec3(1, 0, 0));
}


TEST(Mat4Test, Identity) {
    Mat4 I = Mat4::Identity();
    for (int c = 0; c < 4; c++)
        for (int r = 0; r < 4; r++)
            if (c == r)
                EXPECT_TRUE(RealEqual(I.m[c][r], 1.0));
            else
                EXPECT_TRUE(RealZero(I.m[c][r]));
}

TEST(Mat4Test, Translation) {
    Mat4 T = Mat4::Translation(1.0, 2.0, 3.0);
    Vec3 p(0, 0, 0);
    Vec3 p2(
        T.m[3][0] + T.m[0][0] * p.x + T.m[1][0] * p.y + T.m[2][0] * p.z,
        T.m[3][1] + T.m[0][1] * p.x + T.m[1][1] * p.y + T.m[2][1] * p.z,
        T.m[3][2] + T.m[0][2] * p.x + T.m[1][2] * p.y + T.m[2][2] * p.z
    );
    EXPECT_TRUE(p2 == Vec3(1, 2, 3));
}

TEST(Mat4Test, Scale) {
    Mat4 S = Mat4::Scale(2.0, 3.0, 4.0);
    EXPECT_TRUE(RealEqual(S.m[0][0], 2.0));
    EXPECT_TRUE(RealEqual(S.m[1][1], 3.0));
    EXPECT_TRUE(RealEqual(S.m[2][2], 4.0));
    EXPECT_TRUE(RealEqual(S.m[3][3], 1.0));
}

TEST(Mat4Test, Rotation) {
    Vec3 axis(0, 0, 1); // Z轴
    Real angle = MINICAD_PI / 2.0; // 90度
    Mat4 R = Mat4::Rotation(axis, angle);
    // 旋转矩阵 Z轴 90度
    EXPECT_TRUE(RealEqual(R.m[0][0], 0.0));
    EXPECT_TRUE(RealEqual(R.m[0][1], 1.0));
    EXPECT_TRUE(RealEqual(R.m[1][0], -1.0));
    EXPECT_TRUE(RealEqual(R.m[1][1], 0.0));
}

TEST(Mat4Test, Multiply) {
    Mat4 A = Mat4::Translation(1, 2, 3);
    Mat4 B = Mat4::Scale(2, 2, 2);
    Mat4 C = A * B;

    // 检查 C(3,0-2) == translation * scale
    EXPECT_TRUE(RealEqual(C.m[3][0], 1));
    EXPECT_TRUE(RealEqual(C.m[3][1], 2));
    EXPECT_TRUE(RealEqual(C.m[3][2], 3));
}

TEST(Mat4Test, Transpose) {
    Mat4 M = Mat4::Identity();
    M.m[0][1] = 5;
    Mat4 T = M.Transpose();
    EXPECT_TRUE(RealEqual(T.m[1][0], 5));
}

TEST(Mat4Test, Determinant) {
    Mat4 M = Mat4::Scale(2, 3, 4);
    Real det = M.Determinant();
    EXPECT_TRUE(RealEqual(det, 2 * 3 * 4)); // scale矩阵行列式 = sx*sy*sz
}

TEST(Mat4Test, Inverse) {
    Mat4 M = Mat4::Translation(1, 2, 3);
    Mat4 inv = M.Inverse();
    Mat4 I = M * inv;

    for (int c = 0; c < 4; c++)
        for (int r = 0; r < 4; r++)
            if (c == r)
                EXPECT_TRUE(RealEqual(I.m[c][r], 1.0));
            else
                EXPECT_TRUE(RealZero(I.m[c][r]));
}

TEST(QuaternionTest, Identity) {
    Quaternion q = Quaternion::Identity();
    EXPECT_NEAR(q.x, 0.0, EPS);
    EXPECT_NEAR(q.y, 0.0, EPS);
    EXPECT_NEAR(q.z, 0.0, EPS);
    EXPECT_NEAR(q.w, 1.0, EPS);
}

TEST(QuaternionTest, FromAxisAngle) {
    Vec3 axis{ 1.0, 0.0, 0.0 };
    Real angle = MINICAD_PI; // 180 degrees
    Quaternion q = Quaternion::FromAxisAngle(axis, angle);

    EXPECT_NEAR(q.x, std::sin(angle / 2.0), EPS);
    EXPECT_NEAR(q.y, 0.0, EPS);
    EXPECT_NEAR(q.z, 0.0, EPS);
    EXPECT_NEAR(q.w, std::cos(angle / 2.0), EPS);
}

TEST(QuaternionTest, LengthAndNormalized) {
    Quaternion q{ 1,2,3,4 };
    Real len = q.Length();
    Quaternion nq = q.Normalized();

    EXPECT_NEAR(nq.Length(), 1.0, EPS);
    EXPECT_NEAR(nq.x, q.x / len, EPS);
    EXPECT_NEAR(nq.y, q.y / len, EPS);
    EXPECT_NEAR(nq.z, q.z / len, EPS);
    EXPECT_NEAR(nq.w, q.w / len, EPS);
}

TEST(QuaternionTest, Conjugate) {
    Quaternion q{ 1, -2, 3, -4 };
    Quaternion conj = q.Conjugate();
    EXPECT_EQ(conj.x, -q.x); // -1
    EXPECT_EQ(conj.y, -q.y); // 2
    EXPECT_EQ(conj.z, -q.z); // -3
    EXPECT_EQ(conj.w, q.w);  // 保持原来的 w，即 -4
}

TEST(QuaternionTest, Multiplication) {
    Quaternion a{ 1,0,0,0 };
    Quaternion b{ 0,1,0,0 };
    Quaternion c = a * b;

    // 手算: (w*x' + x*w' + y*z' - z*y', ...)
    EXPECT_NEAR(c.x, 0.0, EPS);
    EXPECT_NEAR(c.y, 0.0, EPS);
    EXPECT_NEAR(c.z, 1.0, EPS);
    EXPECT_NEAR(c.w, 0.0, EPS);
}

TEST(QuaternionTest, Slerp) {
    Quaternion a = Quaternion::Identity();
    Quaternion b = Quaternion::FromAxisAngle({ 0,1,0 }, MINICAD_PI);

    Quaternion mid = Quaternion::Slerp(a, b, 0.5);
    EXPECT_NEAR(mid.Length(), 1.0, EPS);

    // 检查方向是否在 a-b 中间
    EXPECT_GT(mid.w, 0.0);
}

TEST(QuaternionTest, ToMatrix) {
    Quaternion q = Quaternion::Identity();
    Mat4 m = q.ToMatrix();

    // Identity quaternion should produce identity matrix
    for (int i = 0; i < 4; i++)
        for (int j = 0; j < 4; j++)
            EXPECT_NEAR(m.m[i][j], i == j ? 1.0 : 0.0, EPS);
}