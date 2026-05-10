#pragma once 
#include "Vec3.hpp" 
#include "Mat4.hpp" 

namespace MiniCAD::Math
{ 
    struct Transform
    {
        Vec3   Translation    = { 0,0,0 };
        Vec3   Scale          = { 1,1,1 }; 
        double RotationZRad = 0.0;
        Mat4   ToMatrix() const { return Mat4::Translation(Translation) * Mat4::RotationZ(RotationZRad) * Mat4::Scale(Scale); }
        Point3 TransformPoint(const Point3& p) const { return ToMatrix() * p; }
        Vec3   TransformVector(const Vec3& v)  const { return ToMatrix() * v; }
    };
}  

