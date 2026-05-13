#pragma once
#include "Point2.hpp"
#include "Point3.hpp"
#include "Vec2.hpp"
#include "Vec3.hpp"
#include "Constants.hpp"
#include <cmath>
#include <algorithm>
#include "Box2.hpp"
#include "Circle2.hpp"
namespace MiniCAD::Math
{  
	/*
	* Intersects  A ∩ B ≠ ∅  →  Intersects（相交成立）
	*/
	inline bool   NearlyEqual(double a, double b, double eps = LengthEPS) { return std::abs(a - b) <= eps; }  

	// Length
	inline double Length(const Vec2& v)   { return v.Length(); }
	inline double Length(const Vec3& v)   { return v.Length(); }
	inline double LengthSq(const Vec2& v) { return v.LengthSq(); }
	inline double LengthSq(const Vec3& v) { return v.LengthSq(); }

	// Normalize
	inline Vec2 Normalize(const Vec2& v) { return v.Normalized(); }
	inline Vec3 Normalize(const Vec3& v) { return v.Normalized(); }

	inline bool NormalizeSafe(Vec2& v) { double l = v.Length(); if (l < LengthEPS)return false; v /= l; return true; }
	inline bool NormalizeSafe(Vec3& v) { double l = v.Length(); if (l < LengthEPS)return false; v /= l; return true; }

	// Distance
	inline double Distance(const Vec2& a, const Vec2& b)     { return (a - b).Length(); }
	inline double Distance(const Vec3& a, const Vec3& b)     { return (a - b).Length(); }
	inline double Distance(const Point2& a, const Point2& b) { return (a - b).Length(); }
	inline double Distance(const Point3& a, const Point3& b) { return (a - b).Length(); }

	inline double DistanceSq(const Vec2& a, const Vec2& b)     { return (a - b).LengthSq(); }
	inline double DistanceSq(const Vec3& a, const Vec3& b)     { return (a - b).LengthSq(); }
	inline double DistanceSq(const Point2& a, const Point2& b) { return (a - b).LengthSq(); }
	inline double DistanceSq(const Point3& a, const Point3& b) { return (a - b).LengthSq(); }

	// Midpoint
	inline Point2 Midpoint(const Point2& a, const Point2& b) { return{ (a.x + b.x) * 0.5,(a.y + b.y) * 0.5 }; }
	inline Point3 Midpoint(const Point3& a, const Point3& b) { return{ (a.x + b.x) * 0.5,(a.y + b.y) * 0.5,(a.z + b.z) * 0.5 }; }

	// Lerp
	inline Vec2   Lerp(const Vec2& a, const Vec2& b, double t)     { return a + (b - a) * t; }
	inline Vec3   Lerp(const Vec3& a, const Vec3& b, double t)     { return a + (b - a) * t; }
	inline Point2 Lerp(const Point2& a, const Point2& b, double t) { return a + (b - a) * t; }
	inline Point3 Lerp(const Point3& a, const Point3& b, double t) { return a + (b - a) * t; }

	// Dot
	inline constexpr double Dot(const Vec2& a, const Vec2& b) { return a.x * b.x + a.y * b.y; }
	inline constexpr double Dot(const Vec3& a, const Vec3& b) { return a.x * b.x + a.y * b.y + a.z * b.z; }

	// Cross
	inline constexpr double Cross(const Vec2& a, const Vec2& b) { return a.x * b.y - a.y * b.x; }
	inline constexpr Vec3   Cross(const Vec3& a, const Vec3& b) { return{ a.y * b.z - a.z * b.y,a.z * b.x - a.x * b.z,a.x * b.y - a.y * b.x }; }

	// Perp
	inline constexpr Vec2 Perp(const Vec2& v) { return{ -v.y,v.x }; }

	// Project 
	inline Vec2 Project(const Vec2& a, const Vec2& b) { double d = Dot(b, b); if (d < LengthEPS)return{}; return b * (Dot(a, b) / d); }
	inline Vec3 Project(const Vec3& a, const Vec3& b) { double d = Dot(b, b); if (d < LengthEPS)return{}; return b * (Dot(a, b) / d); }

	// Reflect
	inline Vec2 Reflect(const Vec2& v, const Vec2& n) { return v - 2.0 * Dot(v, n) * n; }
	inline Vec3 Reflect(const Vec3& v, const Vec3& n) { return v - 2.0 * Dot(v, n) * n; }

	// 最近点
	inline Point3 ClosestPointOnSegment(const Point3& p, const Point3& a, const Point3& b)
	{
		Vec3 ab = b - a;

		double lenSq = Dot(ab, ab);

		if (lenSq < LengthEPS * LengthEPS)
			return a;

		double t = std::clamp(Dot(p - a, ab) / lenSq, 0.0, 1.0);

		return a + ab * t;
	}

	inline Point2 ClosestPointOnSegment(const Point2& p, const Point2& a, const Point2& b)
	{
		double dx    = b.x - a.x;
		double dy    = b.y - a.y;
		double lenSq = dx * dx + dy * dy;

		if (lenSq < LengthEPS * LengthEPS) return a;

		double t = std::clamp(((p.x - a.x) * dx + (p.y - a.y) * dy) / lenSq, 0.0, 1.0);
		return { a.x + t * dx, a.y + t * dy };
	}

	// Segment Intersect（叉积符号判断，不含端点共线情况）
	inline bool SegmentIntersect(const Point2& p1, const Point2& p2, const Point2& q1, const Point2& q2)
	{
		double d1 = Cross(p2 - p1, q1 - p1);
		double d2 = Cross(p2 - p1, q2 - p1);
		double d3 = Cross(q2 - q1, p1 - q1);
		double d4 = Cross(q2 - q1, p2 - q1);
		return (d1 * d2 < 0.0) && (d3 * d4 < 0.0);
	}

 

	// 线段与圆相交 
	inline bool SegmentIntersectsCircle( const Point2& a, const Point2& b, const Point2& center, double radius)
	{
		Point2 closest = ClosestPointOnSegment(center, a, b);

		double dx = closest.x - center.x;
		double dy = closest.y - center.y;

		return (dx * dx + dy * dy) <= radius * radius;
	}

	// 圆与包围盒边相交
	inline bool CircleIntersectsBoxEdges( const Point2& center, double radius, 	const Box2& box)
	{
		Point2 p0{ box.Min.x, box.Min.y };
		Point2 p1{ box.Max.x, box.Min.y };
		Point2 p2{ box.Max.x, box.Max.y };
		Point2 p3{ box.Min.x, box.Max.y };

		return
			SegmentIntersectsCircle(p0, p1, center, radius) ||
			SegmentIntersectsCircle(p1, p2, center, radius) ||
			SegmentIntersectsCircle(p2, p3, center, radius) ||
			SegmentIntersectsCircle(p3, p0, center, radius);
	}
	 

	// 所有点在二维包围盒内
	inline bool AllPointsInBox2(const Point2* pts, size_t n, const Box2& box)
	{
		for (size_t i = 0; i < n; ++i)
		{
			if (!box.Contains(pts[i]))
				return false;
		}
		return true;
	} 

	// 所有点在二维包围球内
	inline bool AllPointsInCircle2(const Point2* pts, size_t n, const Circle2& circle)
	{
		for (size_t i = 0; i < n; ++i)
		{
			if (!circle.Contains(pts[i]))
				return false;
		}
		return true;
	} 
	  
	// 任意点在包围盒
	inline bool AnyPointsInBox2(const Point2* pts, size_t n, const Box2& box)
	{
		for (size_t i = 0; i < n; ++i)
		{
			if (box.Contains(pts[i]))
				return true;
		}
		return false;
	}

	// 任意点在包围球
	inline bool AnyPointsInCircle2(const Point2* pts, size_t n, const Circle2& circle)
	{
		for (size_t i = 0; i < n; ++i)
		{
			if (!circle.Contains(pts[i]))
				return true;
		}
		return false;
	}

	// 线段与包围盒相交
	inline bool SegmentIntersectsBox2(const Point2& a, 	const Point2& b, const Box2& box) 
	{ 
		// 1. 端点在盒内
		if (box.Contains(a) || box.Contains(b))
			return true;

		// 2. Cohen–Sutherland 类似快速剪裁（slab test）
		double minX = std::min(a.x, b.x);
		double maxX = std::max(a.x, b.x);
		double minY = std::min(a.y, b.y);
		double maxY = std::max(a.y, b.y);

		if (maxX < box.Min.x || minX > box.Max.x ||
			maxY < box.Min.y || minY > box.Max.y)
			return false;

		// 3. 边相交测试
		Point2 r1{ box.Min.x, box.Min.y };
		Point2 r2{ box.Max.x, box.Min.y };
		Point2 r3{ box.Max.x, box.Max.y };
		Point2 r4{ box.Min.x, box.Max.y };

		return SegmentIntersect(a, b, r1, r2) ||
			   SegmentIntersect(a, b, r2, r3) ||
			   SegmentIntersect(a, b, r3, r4) ||
			   SegmentIntersect(a, b, r4, r1);

	}

}
