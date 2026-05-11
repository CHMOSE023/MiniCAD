#pragma once
#include "Point2.hpp"
#include "Point3.hpp"
#include "Vec2.hpp"
#include "Vec3.hpp"
#include "Constants.hpp"
#include <cmath>
#include <algorithm>
namespace MiniCAD::Math
{  
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
		double dx    = b.x - a.x;
		double dy    = b.y - a.y;
		double lenSq = dx * dx + dy * dy;

		if (lenSq < LengthEPS * LengthEPS) return a;

		double t = std::clamp(((p.x - a.x) * dx + (p.y - a.y) * dy) / lenSq, 0.0, 1.0);
		return { a.x + t * dx, a.y + t * dy, 0.0 };
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

	// Segment vs AABB（框选触碰检测）
	inline bool SegmentIntersectsRect(const Point2& a, const Point2& b, double xMin, double yMin, double xMax, double yMax)
	{
		auto inRect = [&](const Point2& p) {
				return p.x >= xMin && p.x <= xMax && p.y >= yMin && p.y <= yMax;
		};
		if (inRect(a) || inRect(b)) return true;
		Point2 r1{ xMin, yMin }, r2{ xMax, yMin };
		Point2 r3{ xMax, yMax }, r4{ xMin, yMax };
		return SegmentIntersect(a, b, r1, r2) || SegmentIntersect(a, b, r2, r3) || SegmentIntersect(a, b, r3, r4) || SegmentIntersect(a, b, r4, r1);
	}

	// Point vs AABB（点选容差检测）
	inline bool PointIntersectsRect(const Point2& p, double xMin, double yMin, double xMax, double yMax, double padding = 2.0)
	{
		return p.x >= xMin - padding && p.x <= xMax + padding && p.y >= yMin - padding && p.y <= yMax + padding;
	} 

}
