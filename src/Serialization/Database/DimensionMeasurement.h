#pragma once
#include "Database/CadDatabase.h"
#include <algorithm>
#include <cmath>

namespace MiniDWG
{
    namespace DimensionDetail
    {
        inline XYZ Sub(const XYZ& a, const XYZ& b) { return { a.X - b.X, a.Y - b.Y, a.Z - b.Z }; }
        inline double Dot(const XYZ& a, const XYZ& b) { return a.X * b.X + a.Y * b.Y + a.Z * b.Z; }
        inline double Length(const XYZ& a) { return std::sqrt(Dot(a, a)); }
        inline double AngleBetween(const XYZ& a, const XYZ& b)
        {
            const double la = Length(a), lb = Length(b);
            if (la == 0.0 || lb == 0.0)
                return 0.0;
            return std::acos(std::clamp(Dot(a, b) / (la * lb), -1.0, 1.0));
        }
    }

    // 标注的测量值（DXF 组码 42）：R12 文件没有，读入为 0，写出时由定义点计算（DXF、DWG 写出共用）
    inline double ComputeDimensionMeasurement(const Dimension& dim)
    {
        using namespace DimensionDetail;
        if (const auto* linear = dynamic_cast<const DimensionLinear*>(&dim))
        {
            const XYZ dir{ std::cos(linear->Rotation), std::sin(linear->Rotation), 0.0 };
            return std::abs(Dot(Sub(linear->SecondPoint, linear->FirstPoint), dir));
        }
        if (const auto* aligned = dynamic_cast<const DimensionAligned*>(&dim))
            return Length(Sub(aligned->SecondPoint, aligned->FirstPoint));
        if (const auto* radius = dynamic_cast<const DimensionRadius*>(&dim))
            return Length(Sub(radius->AngleVertex, radius->DefinitionPoint));
        if (const auto* diameter = dynamic_cast<const DimensionDiameter*>(&dim))
            return Length(Sub(diameter->AngleVertex, diameter->DefinitionPoint));
        if (const auto* angular3 = dynamic_cast<const DimensionAngular3Pt*>(&dim))
            return AngleBetween(Sub(angular3->FirstPoint, angular3->AngleVertex),
                                Sub(angular3->SecondPoint, angular3->AngleVertex));
        if (const auto* angular2 = dynamic_cast<const DimensionAngular2Line*>(&dim))
            return AngleBetween(Sub(angular2->SecondPoint, angular2->FirstPoint),
                                Sub(angular2->DefinitionPoint, angular2->AngleVertex));
        if (const auto* ordinate = dynamic_cast<const DimensionOrdinate*>(&dim))
        {
            const bool xType = HasFlag(ordinate->Flags, DimensionType::OrdinateTypeX);
            return xType ? ordinate->FeatureLocation.X - ordinate->DefinitionPoint.X
                         : ordinate->FeatureLocation.Y - ordinate->DefinitionPoint.Y;
        }
        if (const auto* arc = dynamic_cast<const DimensionArc*>(&dim))
        {
            double sweep = arc->EndAngle - arc->StartAngle;
            while (sweep < 0)
                sweep += 2 * kPi;
            return Length(Sub(arc->FirstPoint, arc->Center)) * sweep;
        }
        return 0.0;
    }
}
