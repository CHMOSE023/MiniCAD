// DWG 读取：头段（对应 ACadSharp DwgHeaderReader）。主体由 ACadSharp 源码逐行移植，变量顺序不能改
#include "Dwg/Read/DwgReaderImpl.h"
#include <cmath>

namespace MiniDWG::DwgRead
{
    namespace
    {
        constexpr std::uint8_t kHeaderStart[16] = {
            0xCF, 0x7B, 0x1F, 0x23, 0xFD, 0xDE, 0x38, 0xA9, 0x5F, 0x7C, 0x68, 0xB8, 0x4E, 0x6D, 0x33, 0x5F,
        };
    }

    // 线宽在 DWG 中存为序号（与 ACadSharp CadUtils.ToValue 一致）
    LineWeightType LineWeightFromIndex(std::uint8_t index)
    {
        static constexpr std::int16_t kWeights[] = {
            0, 5, 9, 13, 15, 18, 20, 25, 30, 35, 40, 50, 53, 60, 70, 80, 90, 100, 106, 120, 140, 158, 200, 211,
        };
        switch (index)
        {
        case 28:
        case 29: return LineWeightType::ByLayer;
        case 30: return LineWeightType::ByBlock;
        case 31: return LineWeightType::Default;
        default:
            return index < std::size(kWeights) ? static_cast<LineWeightType>(kWeights[index]) : LineWeightType::Default;
        }
    }

    std::vector<Handle> HeaderHandles::All() const
    {
        return {
            CMATERIAL, CLAYER, TEXTSTYLE, CELTYPE, DIMSTYLE, CMLSTYLE, UCSNAME_PSPACE, UCSNAME_MSPACE, PUCSORTHOREF,
            PUCSBASE, UCSORTHOREF, UCSBASE, DIMTXSTY, DIMLDRBLK, DIMBLK, DIMBLK1, DIMBLK2, DIMLTYPE, DIMLTEX1, DIMLTEX2,
            BLOCK_CONTROL_OBJECT, LAYER_CONTROL_OBJECT, STYLE_CONTROL_OBJECT, LINETYPE_CONTROL_OBJECT,
            VIEW_CONTROL_OBJECT, UCS_CONTROL_OBJECT, VPORT_CONTROL_OBJECT, APPID_CONTROL_OBJECT, DIMSTYLE_CONTROL_OBJECT,
            VIEWPORT_ENTITY_HEADER_CONTROL_OBJECT, DICTIONARY_ACAD_GROUP, DICTIONARY_ACAD_MLINESTYLE,
            DICTIONARY_NAMED_OBJECTS, DICTIONARY_LAYOUTS, DICTIONARY_PLOTSETTINGS, DICTIONARY_PLOTSTYLES,
            DICTIONARY_MATERIALS, DICTIONARY_COLORS, DICTIONARY_VISUALSTYLE, CPSNID, PAPER_SPACE, MODEL_SPACE, BYLAYER,
            BYBLOCK, CONTINUOUS, INTERFEREOBJVS, INTERFEREVPVS, DRAGVS,
        };
    }

    bool ReadHeaderSection(std::span<const std::uint8_t> section, CadVersion version, int maintenanceVersion,
                           Codec::CodePage codePage, CadHeader& h, HeaderHandles& handles,
                           const NotificationHandler& notify)
    {
        const bool R13_14Only = version == CadVersion::AC1012 || version == CadVersion::AC1014;
        const bool R13_15Only = version <= CadVersion::AC1015;
        const bool R2000Plus = version >= CadVersion::AC1015;
        const bool R2004Pre = version < CadVersion::AC1018;
        const bool R2004Plus = version >= CadVersion::AC1018;
        const bool R2007Plus = version >= CadVersion::AC1021;
        const bool R2010Plus = version >= CadVersion::AC1024;
        const bool R2013Plus = version >= CadVersion::AC1027;
        const bool R2018Plus = version >= CadVersion::AC1032;

        DwgBitReader m(section, version, codePage);
        if (!m.CheckSentinel(kHeaderStart) && notify)
            notify(NotificationType::Warning, "头段的开始哨兵不正确");

        // RL：段大小；R2010/R2013 维护版本大于 3 或 R2018 起另有 4 字节
        const std::int64_t size = m.ReadRawLong();
        if ((R2010Plus && maintenanceVersion > 3) || R2018Plus)
            m.ReadRawLong();

        // R2007 起字符串与句柄位于数据末尾的独立区域
        DwgBitReader text = m;
        DwgBitReader refs = m;
        DwgStreams s{ &m, &m, &m };
        if (R2007Plus)
        {
            const std::uint64_t initialPos = m.PositionInBits();
            const std::uint64_t sizeInBits = static_cast<std::uint32_t>(m.ReadRawLong());
            const std::uint64_t lastBit = initialPos + sizeInBits - 1;
            text.SetPositionByFlag(lastBit);
            refs.SetPositionInBits(lastBit + 1);
            s = DwgStreams{ &m, &text, &refs };
        }
        (void)size;

        if (R2013Plus)
            h.RequiredVersions = m.ReadBitLongLong();
        double unknownbd1 = m.ReadBitDouble();
        (void)unknownbd1;
        double unknownbd2 = m.ReadBitDouble();
        (void)unknownbd2;
        double unknownbd3 = m.ReadBitDouble();
        (void)unknownbd3;
        double unknownbd4 = m.ReadBitDouble();
        (void)unknownbd4;
        std::string unknowntv = s.ReadVariableText();
        unknowntv = s.ReadVariableText();
        unknowntv = s.ReadVariableText();
        unknowntv = s.ReadVariableText();
        auto unknownbl = m.ReadBitLong();
        unknownbl = m.ReadBitLong();
        if (R13_14Only)
        {
            short unknowns = m.ReadBitShort();
            (void)unknowns;
        }
        if (R2004Pre)
        {
            Handle pointerViewPort = s.ReadHandle();
            (void)pointerViewPort;
        }
        h.AssociatedDimensions = m.ReadBit();
        h.UpdateDimensionsWhileDragging = m.ReadBit();
        if (R13_14Only)
        {
            (void)(m.ReadBit());    // DIMSAV：未建模
        }
        h.PolylineLineTypeGeneration = m.ReadBit();
        h.OrthoMode = m.ReadBit();
        h.RegenerationMode = m.ReadBit();
        h.FillMode = m.ReadBit();
        h.QuickTextMode = m.ReadBit();
        h.PaperSpaceLineTypeScaling = m.ReadBit() ? SpaceLineTypeScaling::Normal : SpaceLineTypeScaling::Viewport;
        h.LimitCheckingOn = m.ReadBit();
        if (R13_14Only)
            h.BlipMode = m.ReadBit();
        if (R2004Plus)
            m.ReadBit();
        h.UserTimer = m.ReadBit();
        h.SketchPolylines = m.ReadBit();
        h.AngularDirection = (AngularDirection)(m.ReadBit() ? 1 : 0);
        h.ShowSplineControlPoints = m.ReadBit();
        if (R13_14Only)
        {
            m.ReadBit();
            m.ReadBit();
        }
        h.MirrorText = m.ReadBit();
        h.WorldView = m.ReadBit();
        if (R13_14Only)
            m.ReadBit();
        h.ShowModelSpace = m.ReadBit();
        h.PaperSpaceLimitsChecking = m.ReadBit();
        h.RetainXRefDependentVisibilitySettings = m.ReadBit();
        if (R13_14Only)
            m.ReadBit();
        h.DisplaySilhouetteCurves = m.ReadBit();
        h.CreateEllipseAsPolyline = m.ReadBit();
        h.ProxyGraphics = (m.ReadBitShort() != 0);
        if (R13_14Only)
        {
            m.ReadBitShort();
        }
        h.SpatialIndexMaxTreeDepth = m.ReadBitShort();
        h.LinearUnitFormat = static_cast<LinearUnitFormat>(m.ReadBitShort());
        auto linearUnitPrecision = m.ReadBitShort();
        if (linearUnitPrecision >= 0 && linearUnitPrecision <= 8)
        {
            h.LinearUnitPrecision = linearUnitPrecision;
        }
        h.AngularUnit = static_cast<AngularUnitFormat>(m.ReadBitShort());
        auto angularUnitPrecision = m.ReadBitShort();
        if (angularUnitPrecision >= 0 && angularUnitPrecision <= 8)
        {
            h.AngularUnitPrecision = angularUnitPrecision;
        }
        if (R13_14Only)
        {
            h.ObjectSnapMode = static_cast<ObjectSnapMode>(m.ReadBitShort());
        }
        h.AttributeVisibility = static_cast<AttributeVisibilityMode>(m.ReadBitShort());
        if (R13_14Only)
            m.ReadBitShort();
        h.PointDisplayMode = m.ReadBitShort();
        if (R13_14Only)
            m.ReadBitShort();
        if (R2004Plus)
        {
            m.ReadBitLong();
            m.ReadBitLong();
            m.ReadBitLong();
        }
        h.UserShort1 = m.ReadBitShort();
        h.UserShort2 = m.ReadBitShort();
        h.UserShort3 = m.ReadBitShort();
        h.UserShort4 = m.ReadBitShort();
        h.UserShort5 = m.ReadBitShort();
        h.NumberOfSplineSegments = m.ReadBitShort();
        h.SurfaceDensityU = m.ReadBitShort();
        h.SurfaceDensityV = m.ReadBitShort();
        h.SurfaceType = m.ReadBitShort();
        h.SurfaceMeshTabulationCount1 = m.ReadBitShort();
        h.SurfaceMeshTabulationCount2 = m.ReadBitShort();
        h.SplineType = static_cast<SplineType>(m.ReadBitShort());
        h.ShadeEdge = static_cast<ShadeEdgeType>(m.ReadBitShort());
        h.ShadeDiffuseToAmbientPercentage = m.ReadBitShort();
        h.UnitMode = m.ReadBitShort();
        h.MaxViewportCount = m.ReadBitShort();
        auto surfaceIsoLineCount = m.ReadBitShort();
        (void)surfaceIsoLineCount;  // ISOLINES：未建模
        h.CurrentMultiLineJustification = static_cast<VerticalAlignmentType>(m.ReadBitShort());
        auto textQuality = m.ReadBitShort();
        (void)textQuality;      // TEXTQLTY：未建模
        h.LineTypeScale = m.ReadBitDouble();
        h.TextHeightDefault = m.ReadBitDouble();
        h.TraceWidthDefault = m.ReadBitDouble();
        h.SketchIncrement = m.ReadBitDouble();
        h.FilletRadius = m.ReadBitDouble();
        h.ThicknessDefault = m.ReadBitDouble();
        h.AngleBase = m.ReadBitDouble();
        h.PointDisplaySize = m.ReadBitDouble();
        h.PolylineWidthDefault = m.ReadBitDouble();
        h.UserDouble1 = m.ReadBitDouble();
        h.UserDouble2 = m.ReadBitDouble();
        h.UserDouble3 = m.ReadBitDouble();
        h.UserDouble4 = m.ReadBitDouble();
        h.UserDouble5 = m.ReadBitDouble();
        h.ChamferDistance1 = m.ReadBitDouble();
        h.ChamferDistance2 = m.ReadBitDouble();
        h.ChamferLength = m.ReadBitDouble();
        h.ChamferAngle = m.ReadBitDouble();
        auto facetResolution = m.ReadBitDouble();
        if (facetResolution > 0 && facetResolution <= 10)
        {
            h.FacetResolution = facetResolution;
        }
        h.CurrentMultilineScale = m.ReadBitDouble();
        h.CurrentEntityLinetypeScale = m.ReadBitDouble();
        h.MenuFileName = s.ReadVariableText();
        h.CreateDateTime = JulianDate{ m.ReadJulianDate() };
        h.UpdateDateTime = JulianDate{ m.ReadJulianDate() };
        if (R2004Plus)
        {
            m.ReadBitLong();
            m.ReadBitLong();
            m.ReadBitLong();
        }
        h.TotalEditingTime = TimeSpanDays{ m.ReadTimeSpanDays() };
        h.UserElapsedTimeSpan = TimeSpanDays{ m.ReadTimeSpanDays() };
        h.CurrentEntityColor = s.ReadCmColor();
        h.HandleSeed = m.ReadHandle();
        handles.CLAYER = s.ReadHandle();
        handles.TEXTSTYLE = s.ReadHandle();
        handles.CELTYPE = s.ReadHandle();
        if (R2007Plus)
        {
            handles.CMATERIAL = s.ReadHandle();
        }
        handles.DIMSTYLE = s.ReadHandle();
        handles.CMLSTYLE = s.ReadHandle();
        if (R2000Plus)
        {
            h.ViewportDefaultViewScaleFactor = m.ReadBitDouble();
        }
        h.PaperSpaceInsertionBase = m.Read3BitDouble();
        h.PaperSpaceExtMin = m.Read3BitDouble();
        h.PaperSpaceExtMax = m.Read3BitDouble();
        h.PaperSpaceLimitsMin = m.Read2RawDouble();
        h.PaperSpaceLimitsMax = m.Read2RawDouble();
        h.PaperSpaceElevation = m.ReadBitDouble();
        h.PaperSpaceUcsOrigin = m.Read3BitDouble();
        h.PaperSpaceUcsXAxis = m.Read3BitDouble();
        h.PaperSpaceUcsYAxis = m.Read3BitDouble();
        handles.UCSNAME_PSPACE = s.ReadHandle();
        if (R2000Plus)
        {
            handles.PUCSORTHOREF = s.ReadHandle();
            int PUCSORTHOVIEW = m.ReadBitShort();
            (void)PUCSORTHOVIEW;
            handles.PUCSBASE = s.ReadHandle();
            h.PaperSpaceOrthographicTopDOrigin = m.Read3BitDouble();
            h.PaperSpaceOrthographicBottomDOrigin = m.Read3BitDouble();
            h.PaperSpaceOrthographicLeftDOrigin = m.Read3BitDouble();
            h.PaperSpaceOrthographicRightDOrigin = m.Read3BitDouble();
            h.PaperSpaceOrthographicFrontDOrigin = m.Read3BitDouble();
            h.PaperSpaceOrthographicBackDOrigin = m.Read3BitDouble();
        }
        h.ModelSpaceInsertionBase = m.Read3BitDouble();
        h.ModelSpaceExtMin = m.Read3BitDouble();
        h.ModelSpaceExtMax = m.Read3BitDouble();
        h.ModelSpaceLimitsMin = m.Read2RawDouble();
        h.ModelSpaceLimitsMax = m.Read2RawDouble();
        h.Elevation = m.ReadBitDouble();
        h.ModelSpaceOrigin = m.Read3BitDouble();
        h.ModelSpaceXAxis = m.Read3BitDouble();
        h.ModelSpaceYAxis = m.Read3BitDouble();
        handles.UCSNAME_MSPACE = s.ReadHandle();
        if (R2000Plus)
        {
            handles.UCSORTHOREF = s.ReadHandle();
            short UCSORTHOVIEW = m.ReadBitShort();
            (void)UCSORTHOVIEW;
            handles.UCSBASE = s.ReadHandle();
            h.ModelSpaceOrthographicTopDOrigin = m.Read3BitDouble();
            h.ModelSpaceOrthographicBottomDOrigin = m.Read3BitDouble();
            h.ModelSpaceOrthographicLeftDOrigin = m.Read3BitDouble();
            h.ModelSpaceOrthographicRightDOrigin = m.Read3BitDouble();
            h.ModelSpaceOrthographicFrontDOrigin = m.Read3BitDouble();
            h.ModelSpaceOrthographicBackDOrigin = m.Read3BitDouble();
            h.DimensionPostFix = s.ReadVariableText();
            h.DimensionAlternateDimensioningSuffix = s.ReadVariableText();
        }
        if (R13_14Only)
        {
            h.DimensionGenerateTolerances = m.ReadBit();
            h.DimensionLimitsGeneration = m.ReadBit();
            h.DimensionTextInsideHorizontal = m.ReadBit();
            h.DimensionTextOutsideHorizontal = m.ReadBit();
            h.DimensionSuppressFirstExtensionLine = m.ReadBit();
            h.DimensionSuppressSecondExtensionLine = m.ReadBit();
            h.DimensionAlternateUnitDimensioning = m.ReadBit();
            h.DimensionTextOutsideExtensions = m.ReadBit();
            h.DimensionSeparateArrowBlocks = m.ReadBit();
            h.DimensionTextInsideExtensions = m.ReadBit();
            h.DimensionSuppressOutsideExtensions = m.ReadBit();
            h.DimensionAlternateUnitDecimalPlaces = static_cast<short>(m.ReadByte());
            h.DimensionZeroHandling = static_cast<ZeroHandling>(m.ReadByte());
            h.DimensionSuppressFirstDimensionLine = m.ReadBit();
            h.DimensionSuppressSecondDimensionLine = m.ReadBit();
            h.DimensionToleranceAlignment = static_cast<ToleranceAlignment>(m.ReadByte());
            h.DimensionTextHorizontalAlignment = static_cast<DimensionTextHorizontalAlignment>(m.ReadByte());
            h.DimensionFit = static_cast<short>(m.ReadByte());
            h.DimensionCursorUpdate = m.ReadBit();
            h.DimensionToleranceZeroHandling = static_cast<ZeroHandling>(m.ReadByte());
            h.DimensionAlternateUnitZeroHandling = static_cast<ZeroHandling>(m.ReadByte());
            h.DimensionAlternateUnitToleranceZeroHandling = static_cast<ZeroHandling>(m.ReadByte());
            h.DimensionTextVerticalAlignment = static_cast<DimensionTextVerticalAlignment>(m.ReadByte());
            h.DimensionUnit = m.ReadBitShort();
            h.DimensionAngularDimensionDecimalPlaces = m.ReadBitShort();
            h.DimensionDecimalPlaces = m.ReadBitShort();
            h.DimensionToleranceDecimalPlaces = m.ReadBitShort();
            h.DimensionAlternateUnitFormat = static_cast<LinearUnitFormat>(m.ReadBitShort());
            h.DimensionAlternateUnitToleranceDecimalPlaces = m.ReadBitShort();
            handles.DIMTXSTY = s.ReadHandle();
        }
        h.DimensionScaleFactor = m.ReadBitDouble();
        h.DimensionArrowSize = m.ReadBitDouble();
        h.DimensionExtensionLineOffset = m.ReadBitDouble();
        h.DimensionLineIncrement = m.ReadBitDouble();
        h.DimensionExtensionLineExtension = m.ReadBitDouble();
        h.DimensionRounding = m.ReadBitDouble();
        h.DimensionLineExtension = m.ReadBitDouble();
        h.DimensionPlusTolerance = m.ReadBitDouble();
        h.DimensionMinusTolerance = m.ReadBitDouble();
        if (R2007Plus)
        {
            h.DimensionFixedExtensionLineLength = m.ReadBitDouble();
            auto dimensionJoggedRadiusDimensionTransverseSegmentAngle = m.ReadBitDouble();
            const double rounded = std::round(dimensionJoggedRadiusDimensionTransverseSegmentAngle * 1e6) / 1e6;
            if (rounded > 5 * kPi / 180 && rounded < kPi / 2)
            {
                h.DimensionJoggedRadiusDimensionTransverseSegmentAngle = dimensionJoggedRadiusDimensionTransverseSegmentAngle;
            }
            h.DimensionTextBackgroundFillMode = static_cast<DimensionTextBackgroundFillMode>(m.ReadBitShort());
            h.DimensionTextBackgroundColor = s.ReadCmColor();
        }
        if (R2000Plus)
        {
            h.DimensionGenerateTolerances = m.ReadBit();
            h.DimensionLimitsGeneration = m.ReadBit();
            h.DimensionTextInsideHorizontal = m.ReadBit();
            h.DimensionTextOutsideHorizontal = m.ReadBit();
            h.DimensionSuppressFirstExtensionLine = m.ReadBit();
            h.DimensionSuppressSecondExtensionLine = m.ReadBit();
            h.DimensionTextVerticalAlignment = static_cast<DimensionTextVerticalAlignment>(m.ReadBitShort());
            h.DimensionZeroHandling = static_cast<ZeroHandling>(m.ReadBitShort());
            h.DimensionAngularZeroHandling = static_cast<AngularZeroHandling>(m.ReadBitShort());
        }
        if (R2007Plus)
        {
            h.DimensionArcLengthSymbolPosition = static_cast<ArcLengthSymbolPosition>(m.ReadBitShort());
        }
        h.DimensionTextHeight = m.ReadBitDouble();
        h.DimensionCenterMarkSize = m.ReadBitDouble();
        h.DimensionTickSize = m.ReadBitDouble();
        h.DimensionAlternateUnitScaleFactor = m.ReadBitDouble();
        h.DimensionLinearScaleFactor = m.ReadBitDouble();
        h.DimensionTextVerticalPosition = m.ReadBitDouble();
        h.DimensionToleranceScaleFactor = m.ReadBitDouble();
        h.DimensionLineGap = m.ReadBitDouble();
        if (R13_14Only)
        {
            h.DimensionPostFix = s.ReadVariableText();
            h.DimensionAlternateDimensioningSuffix = s.ReadVariableText();
            h.DimensionBlockName = s.ReadVariableText();
            h.DimensionBlockNameFirst = s.ReadVariableText();
            h.DimensionBlockNameSecond = s.ReadVariableText();
        }
        if (R2000Plus)
        {
            h.DimensionAlternateUnitRounding = m.ReadBitDouble();
            h.DimensionAlternateUnitDimensioning = m.ReadBit();
            h.DimensionAlternateUnitDecimalPlaces = static_cast<short>(m.ReadBitShort());
            h.DimensionTextOutsideExtensions = m.ReadBit();
            h.DimensionSeparateArrowBlocks = m.ReadBit();
            h.DimensionTextInsideExtensions = m.ReadBit();
            h.DimensionSuppressOutsideExtensions = m.ReadBit();
        }
        h.DimensionLineColor = s.ReadCmColor();
        h.DimensionExtensionLineColor = s.ReadCmColor();
        h.DimensionTextColor = s.ReadCmColor();
        if (R2000Plus)
        {
            h.DimensionAngularDimensionDecimalPlaces = m.ReadBitShort();
            h.DimensionDecimalPlaces = m.ReadBitShort();
            h.DimensionToleranceDecimalPlaces = m.ReadBitShort();
            h.DimensionAlternateUnitFormat = static_cast<LinearUnitFormat>(m.ReadBitShort());
            h.DimensionAlternateUnitToleranceDecimalPlaces = m.ReadBitShort();
            h.DimensionAngularUnit = static_cast<AngularUnitFormat>(m.ReadBitShort());
            h.DimensionFractionFormat = static_cast<FractionFormat>(m.ReadBitShort());
            h.DimensionLinearUnitFormat = static_cast<LinearUnitFormat>(m.ReadBitShort());
            h.DimensionDecimalSeparator = static_cast<char>(m.ReadBitShort());
            h.DimensionTextMovement = static_cast<TextMovement>(m.ReadBitShort());
            h.DimensionTextHorizontalAlignment = static_cast<DimensionTextHorizontalAlignment>(m.ReadBitShort());
            h.DimensionSuppressFirstExtensionLine = m.ReadBit();
            h.DimensionSuppressSecondExtensionLine = m.ReadBit();
            h.DimensionToleranceAlignment = static_cast<ToleranceAlignment>(m.ReadBitShort());
            h.DimensionToleranceZeroHandling = static_cast<ZeroHandling>(m.ReadBitShort());
            h.DimensionAlternateUnitZeroHandling = static_cast<ZeroHandling>(m.ReadBitShort());
            h.DimensionAlternateUnitToleranceZeroHandling = static_cast<ZeroHandling>(m.ReadBitShort());
            h.DimensionCursorUpdate = m.ReadBit();
            h.DimensionDimensionTextArrowFit = static_cast<TextArrowFitType>(m.ReadBitShort());
        }
        if (R2007Plus)
        {
            h.DimensionIsExtensionLineLengthFixed = m.ReadBit();
        }
        if (R2010Plus)
        {
            h.DimensionTextDirection = m.ReadBit() ? TextDirection::RightToLeft : TextDirection::LeftToRight;
            h.DimensionAltMzf = m.ReadBitDouble();
            h.DimensionAltMzs = s.ReadVariableText();
            h.DimensionMzf = m.ReadBitDouble();
            h.DimensionMzs = s.ReadVariableText();
        }
        if (R2000Plus)
        {
            handles.DIMTXSTY = s.ReadHandle();
            handles.DIMLDRBLK = s.ReadHandle();
            handles.DIMBLK = s.ReadHandle();
            handles.DIMBLK1 = s.ReadHandle();
            handles.DIMBLK2 = s.ReadHandle();
        }
        if (R2007Plus)
        {
            handles.DIMLTYPE = s.ReadHandle();
            handles.DIMLTEX1 = s.ReadHandle();
            handles.DIMLTEX2 = s.ReadHandle();
        }
        if (R2000Plus)
        {
            h.DimensionLineWeight = static_cast<LineWeightType>(m.ReadBitShort());
            h.ExtensionLineWeight = static_cast<LineWeightType>(m.ReadBitShort());
        }
        handles.BLOCK_CONTROL_OBJECT = s.ReadHandle();
        handles.LAYER_CONTROL_OBJECT = s.ReadHandle();
        handles.STYLE_CONTROL_OBJECT = s.ReadHandle();
        handles.LINETYPE_CONTROL_OBJECT = s.ReadHandle();
        handles.VIEW_CONTROL_OBJECT = s.ReadHandle();
        handles.UCS_CONTROL_OBJECT = s.ReadHandle();
        handles.VPORT_CONTROL_OBJECT = s.ReadHandle();
        handles.APPID_CONTROL_OBJECT = s.ReadHandle();
        handles.DIMSTYLE_CONTROL_OBJECT = s.ReadHandle();
        if (R13_15Only)
        {
            handles.VIEWPORT_ENTITY_HEADER_CONTROL_OBJECT = s.ReadHandle();
        }
        handles.DICTIONARY_ACAD_GROUP = s.ReadHandle();
        handles.DICTIONARY_ACAD_MLINESTYLE = s.ReadHandle();
        handles.DICTIONARY_NAMED_OBJECTS = s.ReadHandle();
        if (R2000Plus)
        {
            (void)(m.ReadBitShort());    // StackedTextAlignment：未建模
            (void)(m.ReadBitShort());    // StackedTextSizePercentage：未建模
            h.HyperLinkBase = s.ReadVariableText();
            h.StyleSheetName = s.ReadVariableText();
            handles.DICTIONARY_LAYOUTS = s.ReadHandle();
            handles.DICTIONARY_PLOTSETTINGS = s.ReadHandle();
            handles.DICTIONARY_PLOTSTYLES = s.ReadHandle();
        }
        if (R2004Plus)
        {
            handles.DICTIONARY_MATERIALS = s.ReadHandle();
            handles.DICTIONARY_COLORS = s.ReadHandle();
        }
        if (R2007Plus)
        {
            handles.DICTIONARY_VISUALSTYLE = s.ReadHandle();
            if (R2013Plus)
                handles.DICTIONARY_VISUALSTYLE = s.ReadHandle();
        }
        if (R2000Plus)
        {
            int flags = m.ReadBitLong();
            h.CurrentEntityLineWeight = LineWeightFromIndex(static_cast<std::uint8_t>(flags & 0x1F));
            h.EndCaps = static_cast<std::int16_t>((flags & 0x60) >> 5);
            h.JoinStyle = static_cast<std::int16_t>((flags & 0x180) >> 7);
            h.DisplayLineWeight = (flags & 0x200) != 0;
            h.XEdit = (flags & 0x400) != 0;
            h.ExtendedNames = (flags & 0x800) != 0;
            h.PlotStyleMode = (flags & 0x2000) != 0 ? 1 : 0;
            // LOADOLEOBJECT（0x4000）：未建模
            h.InsUnits = static_cast<UnitsType>(m.ReadBitShort());
            h.CurrentEntityPlotStyle = static_cast<EntityPlotStyleType>(m.ReadBitShort());
            if (h.CurrentEntityPlotStyle == EntityPlotStyleType::ByObjectId)
            {
                handles.CPSNID = s.ReadHandle();
            }
            h.FingerPrintGuid = s.ReadVariableText();
            h.VersionGuid = s.ReadVariableText();
        }
        if (R2004Plus)
        {
            h.EntitySortingFlags = static_cast<ObjectSortingFlags>(m.ReadByte());
            h.IndexCreationFlags = static_cast<IndexCreationFlags>(m.ReadByte());
            h.HideText = m.ReadByte();
            h.ExternalReferenceClippingBoundaryType = static_cast<XClipFrameType>(m.ReadByte());
            h.DimensionAssociativity = static_cast<DimensionAssociationType>(m.ReadByte());
            h.HaloGapPercentage = m.ReadByte();
            (void)(Color(m.ReadBitShort()));    // ObscuredColor：未建模
            h.InterfereColor = Color(m.ReadBitShort());
            (void)(m.ReadByte());    // ObscuredType：未建模
            (void)(m.ReadByte());    // IntersectionDisplay：未建模
            h.ProjectName = s.ReadVariableText();
        }
        handles.PAPER_SPACE = s.ReadHandle();
        handles.MODEL_SPACE = s.ReadHandle();
        handles.BYLAYER = s.ReadHandle();
        handles.BYBLOCK = s.ReadHandle();
        handles.CONTINUOUS = s.ReadHandle();
        if (R2007Plus)
        {
            h.CameraDisplayObjects = m.ReadBit();
            m.ReadBitLong();
            m.ReadBitLong();
            m.ReadBitDouble();
            auto stepsPerSecond = m.ReadBitDouble();
            if (stepsPerSecond >= 1 && stepsPerSecond <= 30)
            {
                h.StepsPerSecond = stepsPerSecond;
            }
            h.StepSize = m.ReadBitDouble();
            h.Dw3DPrecision = m.ReadBitDouble();
            h.LensLength = m.ReadBitDouble();
            h.CameraHeight = m.ReadBitDouble();
            h.SolidsRetainHistory = m.ReadByte();
            h.ShowSolidsHistory = m.ReadByte();
            h.SweptSolidWidth = m.ReadBitDouble();
            h.SweptSolidHeight = m.ReadBitDouble();
            h.DraftAngleFirstCrossSection = m.ReadBitDouble();
            h.DraftAngleSecondCrossSection = m.ReadBitDouble();
            h.DraftMagnitudeFirstCrossSection = m.ReadBitDouble();
            h.DraftMagnitudeSecondCrossSection = m.ReadBitDouble();
            h.SolidLoftedShape = m.ReadBitShort();
            h.LoftedObjectNormals = m.ReadByte();
            h.Latitude = m.ReadBitDouble();
            h.Longitude = m.ReadBitDouble();
            h.NorthDirection = m.ReadBitDouble();
            h.TimeZone = m.ReadBitLong();
            h.DisplayLightGlyphs = m.ReadByte();
            m.ReadByte();
            h.DwgUnderlayFramesVisibility = m.ReadByte();
            h.DgnUnderlayFramesVisibility = m.ReadByte();
            m.ReadBit();
            h.InterfereColor = s.ReadCmColor();
            handles.INTERFEREOBJVS = s.ReadHandle();
            handles.INTERFEREVPVS = s.ReadHandle();
            handles.DRAGVS = s.ReadHandle();
            h.ShadowMode = static_cast<ShadowMode>(m.ReadByte());
            h.ShadowPlaneLocation = m.ReadBitDouble();
        }

        h.VersionString = std::string(VersionString(version));
        return !s.Failed();
    }
}
