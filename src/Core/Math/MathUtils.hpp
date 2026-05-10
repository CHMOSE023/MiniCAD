#pragma once
#include <cmath>
#include "Point2.hpp"
#include "Point3.hpp"
#include "Vec2.hpp"
#include "Vec3.hpp"

namespace MiniCAD::Math
{
	constexpr double EPS = 1e-9;

	inline constexpr bool NearlyEqual(double a, double b, double eps = EPS) { return std::abs(a - b) <= eps; }

	// Length
	inline double Length(const Vec2& v)   { return v.Length(); }
	inline double Length(const Vec3& v)   { return v.Length(); }
	inline double LengthSq(const Vec2& v) { return v.LengthSq(); }
	inline double LengthSq(const Vec3& v) { return v.LengthSq(); }

	// Normalize
	inline Vec2 Normalize(const Vec2& v) { return v.Normalized(); }
	inline Vec3 Normalize(const Vec3& v) { return v.Normalized(); }

	inline bool NormalizeSafe(Vec2& v) { double l = v.Length(); if (l < EPS)return false; v /= l; return true; }
	inline bool NormalizeSafe(Vec3& v) { double l = v.Length(); if (l < EPS)return false; v /= l; return true; }

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
	inline Vec2 Lerp(const Vec2& a, const Vec2& b, double t)       { return a + (b - a) * t; }
	inline Vec3 Lerp(const Vec3& a, const Vec3& b, double t)       { return a + (b - a) * t; }
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
	inline Vec2 Project(const Vec2& a, const Vec2& b) { double d = Dot(b, b); if (d < EPS)return{}; return b * (Dot(a, b) / d); }
	inline Vec3 Project(const Vec3& a, const Vec3& b) { double d = Dot(b, b); if (d < EPS)return{}; return b * (Dot(a, b) / d); }

	// Reflect
	inline Vec2 Reflect(const Vec2& v, const Vec2& n) { return v - 2.0 * Dot(v, n) * n; }
	inline Vec3 Reflect(const Vec3& v, const Vec3& n) { return v - 2.0 * Dot(v, n) * n; }

}
