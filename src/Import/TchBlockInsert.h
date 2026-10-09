#pragma once
#include "Database/UnknownObjects.h"
#include "Dwg/Read/DwgBitReader.h"
#include <array>
#include <cmath>

namespace MiniCAD::Tch
{
    struct TchBlockInsert
    {
        MiniDWG::XYZ Point, Normal;
        double XScale = 1, YScale = 1, ZScale = 1, Rotation = 0;
        MiniDWG::Handle BlockHandle = MiniDWG::kNullHandle;
    };

    // The Tch block-insert layout starts with the same base prefix as
    // OdDbBlockReference before reading its own version/mode. Preserve Raw;
    // recover only the standard geometry prefix, not proprietary semantics.
    inline bool DecodeBlockInsert(const MiniDWG::UnknownEntity& entity, TchBlockInsert& output)
    {
        using namespace MiniDWG;
        const auto& raw = entity.Raw.Dwg;
        if (entity.Raw.DxfName != "TCH_BLOCK_INSERT" || raw.Version < CadVersion::AC1015 ||
            raw.Version > CadVersion::AC1032 || raw.MainBits > raw.Main.size() * 8ULL ||
            raw.HandleBits > raw.Handles.size() * 8ULL || raw.HandleBits < 8)
            return false;
        DwgBitReader r(raw.Main, raw.Version, Codec::CodePage::Gbk);
        TchBlockInsert result;
        result.Point = r.Read3BitDouble();
        switch (r.Read2Bits())
        {
        case 0:
            result.XScale = r.ReadRawDouble();
            result.YScale = r.ReadBitDoubleWithDefault(result.XScale);
            result.ZScale = r.ReadBitDoubleWithDefault(result.XScale);
            break;
        case 1:
            result.YScale = r.ReadBitDoubleWithDefault(1);
            result.ZScale = r.ReadBitDoubleWithDefault(1);
            break;
        case 2:
            result.XScale = result.YScale = result.ZScale = r.ReadRawDouble();
            break;
        default: break;
        }
        result.Rotation = r.ReadBitDouble();
        result.Normal = r.Read3BitDouble();
        const bool attributes = r.ReadBit();
        if (attributes && raw.Version >= CadVersion::AC1018)
        {
            const auto owned = r.ReadBitLong();
            if (owned < 0 || owned > 100000) return false;
        }
        const auto version = r.ReadByte();
        const auto mode = r.ReadByte();
        // Mode 1 has additional database-dependent behavior; do not guess it.
        if (version > 4 || mode != 0 || r.Failed() || r.PositionInBits() > raw.MainBits)
            return false;
        DwgBitReader handles(raw.Handles, raw.Version, Codec::CodePage::Gbk);
        result.BlockHandle = handles.ReadHandle(entity.ObjectHandle);
        if (handles.Failed() || handles.PositionInBits() > raw.HandleBits || result.BlockHandle == kNullHandle)
            return false;
        const std::array values{result.Point.X, result.Point.Y, result.Point.Z, result.XScale,
            result.YScale, result.ZScale, result.Rotation, result.Normal.X, result.Normal.Y, result.Normal.Z};
        for (double value : values)
            if (!std::isfinite(value) || std::abs(value) > 1e12) return false;
        if (result.XScale == 0 || result.YScale == 0 || result.ZScale == 0 ||
            std::hypot(result.Normal.X, result.Normal.Y, result.Normal.Z) < 1e-12)
            return false;
        output = result;
        return true;
    }
}
