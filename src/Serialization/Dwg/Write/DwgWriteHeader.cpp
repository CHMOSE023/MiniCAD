// DWG 写入：头段（对应 ACadSharp DwgHeaderWriter）。变量顺序与 DwgReadHeader.cpp 逐项对应，不能改
#include "Dwg/Write/DwgWriterImpl.h"

namespace MiniDWG::DwgWrite
{
    namespace
    {
        constexpr std::uint8_t kHeaderStart[16] = {
            0xCF, 0x7B, 0x1F, 0x23, 0xFD, 0xDE, 0x38, 0xA9, 0x5F, 0x7C, 0x68, 0xB8, 0x4E, 0x6D, 0x33, 0x5F,
        };
        constexpr std::uint8_t kHeaderEnd[16] = {
            0x30, 0x84, 0xE0, 0xDC, 0x02, 0x21, 0xC7, 0x56, 0xA0, 0x83, 0x97, 0x47, 0xB1, 0x92, 0xCC, 0xA0,
        };
    }

    std::vector<std::uint8_t> Writer::WriteHeaderSection()
    {
        const CadHeader& h = m_db.Header;
        const bool R13_14Only = false;
        const bool R13_15Only = m_version <= CadVersion::AC1015;

        DwgBitWriter m(m_version, m_codePage);
        DwgBitWriter text(m_version, m_codePage);
        DwgBitWriter refs(m_version, m_codePage);
        DwgBitWriter& t = R2007Plus() ? text : m;
        DwgBitWriter& r = R2007Plus() ? refs : m;
        auto ref = [&](DwgRef code, Handle handle) { r.WriteHandle(code, Ref(handle)); };
        auto hard = [&](Handle handle) { ref(DwgRef::HardPointer, handle); };
        auto dict = [&](std::string_view name) {
            const CadDictionary* root = m_db.RootDictionary();
            const CadObject* entry = root != nullptr ? m_db.FindDictionaryEntry(root, name) : nullptr;
            return entry != nullptr ? entry->ObjectHandle : kNullHandle;
        };
        auto mlineStyle = [&](std::string_view name) {
            const auto* styles = m_db.FindNamedDictionary("ACAD_MLINESTYLE");
            const CadObject* entry = styles != nullptr ? m_db.FindDictionaryEntry(styles, name) : nullptr;
            return entry != nullptr ? entry->ObjectHandle : kNullHandle;
        };
        auto layer = [&](std::string_view name) { return TableEntryByName(m_db.Layers(), name); };
        auto textStyle = [&](std::string_view name) { return TableEntryByName(m_db.TextStyles(), name); };
        auto lineType = [&](std::string_view name) { return TableEntryByName(m_db.LineTypes(), name); };
        auto block = [&](std::string_view name) { return TableEntryByName(m_db.BlockRecords(), name); };
        auto ucs = [&](std::string_view name) { return TableEntryByName(m_db.UCSs(), name); };
        auto tableHandle = [&](const CadTable* table) { return table != nullptr ? table->ObjectHandle : kNullHandle; };

        std::uint64_t sizePos = 0;
        if (R2007Plus())
        {
            sizePos = m.PositionInBits();
            m.WriteRawLong(0);
        }

        if (R2013Plus())
            m.WriteBitLongLong(h.RequiredVersions);
        m.WriteBitDouble(412148564080.0);
        m.WriteBitDouble(1.0);
        m.WriteBitDouble(1.0);
        m.WriteBitDouble(1.0);
        t.WriteVariableText("m");
        t.WriteVariableText("");
        t.WriteVariableText("");
        t.WriteVariableText("");
        m.WriteBitLong(24);
        m.WriteBitLong(0);
        if (R13_14Only)
            m.WriteBitShort(0);
        if (R2004Pre())
            hard(m_vxEntries.empty() ? kNullHandle : m_vxEntries.front().first);    // 当前视口实体头：总视口的那一项
        m.WriteBit(h.AssociatedDimensions);
        m.WriteBit(h.UpdateDimensionsWhileDragging);
        m.WriteBit(h.PolylineLineTypeGeneration);
        m.WriteBit(h.OrthoMode);
        m.WriteBit(h.RegenerationMode);
        m.WriteBit(h.FillMode);
        m.WriteBit(h.QuickTextMode);
        m.WriteBit(h.PaperSpaceLineTypeScaling == SpaceLineTypeScaling::Normal);
        m.WriteBit(h.LimitCheckingOn);
        if (R2004Plus())
            m.WriteBit(false);
        m.WriteBit(h.UserTimer);
        m.WriteBit(h.SketchPolylines);
        m.WriteBit(h.AngularDirection != static_cast<AngularDirection>(0));
        m.WriteBit(h.ShowSplineControlPoints);
        m.WriteBit(h.MirrorText);
        m.WriteBit(h.WorldView);
        m.WriteBit(h.ShowModelSpace);
        m.WriteBit(h.PaperSpaceLimitsChecking);
        m.WriteBit(h.RetainXRefDependentVisibilitySettings);
        m.WriteBit(h.DisplaySilhouetteCurves);
        m.WriteBit(h.CreateEllipseAsPolyline);
        m.WriteBitShort(h.ProxyGraphics ? 1 : 0);
        m.WriteBitShort(h.SpatialIndexMaxTreeDepth);
        m.WriteBitShort(static_cast<std::int16_t>(h.LinearUnitFormat));
        m.WriteBitShort(h.LinearUnitPrecision);
        m.WriteBitShort(static_cast<std::int16_t>(h.AngularUnit));
        m.WriteBitShort(h.AngularUnitPrecision);
        m.WriteBitShort(static_cast<std::int16_t>(h.AttributeVisibility));
        m.WriteBitShort(h.PointDisplayMode);
        if (R2004Plus())
        {
            m.WriteBitLong(0);
            m.WriteBitLong(0);
            m.WriteBitLong(0);
        }
        m.WriteBitShort(h.UserShort1);
        m.WriteBitShort(h.UserShort2);
        m.WriteBitShort(h.UserShort3);
        m.WriteBitShort(h.UserShort4);
        m.WriteBitShort(h.UserShort5);
        m.WriteBitShort(h.NumberOfSplineSegments);
        m.WriteBitShort(h.SurfaceDensityU);
        m.WriteBitShort(h.SurfaceDensityV);
        m.WriteBitShort(h.SurfaceType);
        m.WriteBitShort(h.SurfaceMeshTabulationCount1);
        m.WriteBitShort(h.SurfaceMeshTabulationCount2);
        m.WriteBitShort(static_cast<std::int16_t>(h.SplineType));
        m.WriteBitShort(static_cast<std::int16_t>(h.ShadeEdge));
        m.WriteBitShort(h.ShadeDiffuseToAmbientPercentage);
        m.WriteBitShort(h.UnitMode);
        m.WriteBitShort(h.MaxViewportCount);
        m.WriteBitShort(4);             // ISOLINES：未建模，AutoCAD 默认值
        m.WriteBitShort(static_cast<std::int16_t>(h.CurrentMultiLineJustification));
        m.WriteBitShort(50);            // TEXTQLTY：未建模，AutoCAD 默认值
        m.WriteBitDouble(h.LineTypeScale);
        m.WriteBitDouble(h.TextHeightDefault);
        m.WriteBitDouble(h.TraceWidthDefault);
        m.WriteBitDouble(h.SketchIncrement);
        m.WriteBitDouble(h.FilletRadius);
        m.WriteBitDouble(h.ThicknessDefault);
        m.WriteBitDouble(h.AngleBase);
        m.WriteBitDouble(h.PointDisplaySize);
        m.WriteBitDouble(h.PolylineWidthDefault);
        m.WriteBitDouble(h.UserDouble1);
        m.WriteBitDouble(h.UserDouble2);
        m.WriteBitDouble(h.UserDouble3);
        m.WriteBitDouble(h.UserDouble4);
        m.WriteBitDouble(h.UserDouble5);
        m.WriteBitDouble(h.ChamferDistance1);
        m.WriteBitDouble(h.ChamferDistance2);
        m.WriteBitDouble(h.ChamferLength);
        m.WriteBitDouble(h.ChamferAngle);
        m.WriteBitDouble(h.FacetResolution);
        m.WriteBitDouble(h.CurrentMultilineScale);
        m.WriteBitDouble(h.CurrentEntityLinetypeScale);
        t.WriteVariableText(h.MenuFileName);
        m.WriteJulianDate(h.CreateDateTime.Value);
        m.WriteJulianDate(h.UpdateDateTime.Value);
        if (R2004Plus())
        {
            m.WriteBitLong(0);
            m.WriteBitLong(0);
            m.WriteBitLong(0);
        }
        m.WriteTimeSpanDays(h.TotalEditingTime.Value);
        m.WriteTimeSpanDays(h.UserElapsedTimeSpan.Value);
        m.WriteCmColor(h.CurrentEntityColor);
        // HANDSEED 在数据流中（不在句柄流中）
        m.WriteHandle(DwgRef::Undefined, m_nextHandle);
        hard(layer(h.CurrentLayerName.empty() ? "0" : h.CurrentLayerName));
        hard(textStyle(h.CurrentTextStyleName));
        hard(lineType(h.CurrentLineTypeName));
        if (R2007Plus())
            hard(kNullHandle);      // CMATERIAL：材质未建模
        hard(TableEntryByName(m_db.DimensionStyles(), h.CurrentDimensionStyleName));
        hard(mlineStyle(h.CurrentMLineStyleName));
        if (R2000Plus())
            m.WriteBitDouble(h.ViewportDefaultViewScaleFactor);
        m.Write3BitDouble(h.PaperSpaceInsertionBase);
        m.Write3BitDouble(h.PaperSpaceExtMin);
        m.Write3BitDouble(h.PaperSpaceExtMax);
        m.Write2RawDouble(h.PaperSpaceLimitsMin);
        m.Write2RawDouble(h.PaperSpaceLimitsMax);
        m.WriteBitDouble(h.PaperSpaceElevation);
        m.Write3BitDouble(h.PaperSpaceUcsOrigin);
        m.Write3BitDouble(h.PaperSpaceUcsXAxis);
        m.Write3BitDouble(h.PaperSpaceUcsYAxis);
        hard(ucs(h.PaperSpaceName));
        if (R2000Plus())
        {
            hard(kNullHandle);      // PUCSORTHOREF
            m.WriteBitShort(0);     // PUCSORTHOVIEW
            hard(ucs(h.PaperSpaceBaseName));
            m.Write3BitDouble(h.PaperSpaceOrthographicTopDOrigin);
            m.Write3BitDouble(h.PaperSpaceOrthographicBottomDOrigin);
            m.Write3BitDouble(h.PaperSpaceOrthographicLeftDOrigin);
            m.Write3BitDouble(h.PaperSpaceOrthographicRightDOrigin);
            m.Write3BitDouble(h.PaperSpaceOrthographicFrontDOrigin);
            m.Write3BitDouble(h.PaperSpaceOrthographicBackDOrigin);
        }
        m.Write3BitDouble(h.ModelSpaceInsertionBase);
        m.Write3BitDouble(h.ModelSpaceExtMin);
        m.Write3BitDouble(h.ModelSpaceExtMax);
        m.Write2RawDouble(h.ModelSpaceLimitsMin);
        m.Write2RawDouble(h.ModelSpaceLimitsMax);
        m.WriteBitDouble(h.Elevation);
        m.Write3BitDouble(h.ModelSpaceOrigin);
        m.Write3BitDouble(h.ModelSpaceXAxis);
        m.Write3BitDouble(h.ModelSpaceYAxis);
        hard(ucs(h.UcsName));
        if (R2000Plus())
        {
            hard(kNullHandle);      // UCSORTHOREF
            m.WriteBitShort(0);     // UCSORTHOVIEW
            hard(ucs(h.UcsBaseName));
            m.Write3BitDouble(h.ModelSpaceOrthographicTopDOrigin);
            m.Write3BitDouble(h.ModelSpaceOrthographicBottomDOrigin);
            m.Write3BitDouble(h.ModelSpaceOrthographicLeftDOrigin);
            m.Write3BitDouble(h.ModelSpaceOrthographicRightDOrigin);
            m.Write3BitDouble(h.ModelSpaceOrthographicFrontDOrigin);
            m.Write3BitDouble(h.ModelSpaceOrthographicBackDOrigin);
            t.WriteVariableText(h.DimensionPostFix);
            t.WriteVariableText(h.DimensionAlternateDimensioningSuffix);
        }
        m.WriteBitDouble(h.DimensionScaleFactor);
        m.WriteBitDouble(h.DimensionArrowSize);
        m.WriteBitDouble(h.DimensionExtensionLineOffset);
        m.WriteBitDouble(h.DimensionLineIncrement);
        m.WriteBitDouble(h.DimensionExtensionLineExtension);
        m.WriteBitDouble(h.DimensionRounding);
        m.WriteBitDouble(h.DimensionLineExtension);
        m.WriteBitDouble(h.DimensionPlusTolerance);
        m.WriteBitDouble(h.DimensionMinusTolerance);
        if (R2007Plus())
        {
            m.WriteBitDouble(h.DimensionFixedExtensionLineLength);
            m.WriteBitDouble(h.DimensionJoggedRadiusDimensionTransverseSegmentAngle);
            m.WriteBitShort(static_cast<std::int16_t>(h.DimensionTextBackgroundFillMode));
            m.WriteCmColor(h.DimensionTextBackgroundColor);
        }
        if (R2000Plus())
        {
            m.WriteBit(h.DimensionGenerateTolerances);
            m.WriteBit(h.DimensionLimitsGeneration);
            m.WriteBit(h.DimensionTextInsideHorizontal);
            m.WriteBit(h.DimensionTextOutsideHorizontal);
            m.WriteBit(h.DimensionSuppressFirstExtensionLine);
            m.WriteBit(h.DimensionSuppressSecondExtensionLine);
            m.WriteBitShort(static_cast<std::int16_t>(h.DimensionTextVerticalAlignment));
            m.WriteBitShort(static_cast<std::int16_t>(h.DimensionZeroHandling));
            m.WriteBitShort(static_cast<std::int16_t>(h.DimensionAngularZeroHandling));
        }
        if (R2007Plus())
            m.WriteBitShort(static_cast<std::int16_t>(h.DimensionArcLengthSymbolPosition));
        m.WriteBitDouble(h.DimensionTextHeight);
        m.WriteBitDouble(h.DimensionCenterMarkSize);
        m.WriteBitDouble(h.DimensionTickSize);
        m.WriteBitDouble(h.DimensionAlternateUnitScaleFactor);
        m.WriteBitDouble(h.DimensionLinearScaleFactor);
        m.WriteBitDouble(h.DimensionTextVerticalPosition);
        m.WriteBitDouble(h.DimensionToleranceScaleFactor);
        m.WriteBitDouble(h.DimensionLineGap);
        if (R2000Plus())
        {
            m.WriteBitDouble(h.DimensionAlternateUnitRounding);
            m.WriteBit(h.DimensionAlternateUnitDimensioning);
            m.WriteBitShort(h.DimensionAlternateUnitDecimalPlaces);
            m.WriteBit(h.DimensionTextOutsideExtensions);
            m.WriteBit(h.DimensionSeparateArrowBlocks);
            m.WriteBit(h.DimensionTextInsideExtensions);
            m.WriteBit(h.DimensionSuppressOutsideExtensions);
        }
        m.WriteCmColor(h.DimensionLineColor);
        m.WriteCmColor(h.DimensionExtensionLineColor);
        m.WriteCmColor(h.DimensionTextColor);
        if (R2000Plus())
        {
            m.WriteBitShort(h.DimensionAngularDimensionDecimalPlaces);
            m.WriteBitShort(h.DimensionDecimalPlaces);
            m.WriteBitShort(h.DimensionToleranceDecimalPlaces);
            m.WriteBitShort(static_cast<std::int16_t>(h.DimensionAlternateUnitFormat));
            m.WriteBitShort(h.DimensionAlternateUnitToleranceDecimalPlaces);
            m.WriteBitShort(static_cast<std::int16_t>(h.DimensionAngularUnit));
            m.WriteBitShort(static_cast<std::int16_t>(h.DimensionFractionFormat));
            m.WriteBitShort(static_cast<std::int16_t>(h.DimensionLinearUnitFormat));
            m.WriteBitShort(static_cast<std::int16_t>(h.DimensionDecimalSeparator));
            m.WriteBitShort(static_cast<std::int16_t>(h.DimensionTextMovement));
            m.WriteBitShort(static_cast<std::int16_t>(h.DimensionTextHorizontalAlignment));
            m.WriteBit(h.DimensionSuppressFirstExtensionLine);
            m.WriteBit(h.DimensionSuppressSecondExtensionLine);
            m.WriteBitShort(static_cast<std::int16_t>(h.DimensionToleranceAlignment));
            m.WriteBitShort(static_cast<std::int16_t>(h.DimensionToleranceZeroHandling));
            m.WriteBitShort(static_cast<std::int16_t>(h.DimensionAlternateUnitZeroHandling));
            m.WriteBitShort(static_cast<std::int16_t>(h.DimensionAlternateUnitToleranceZeroHandling));
            m.WriteBit(h.DimensionCursorUpdate);
            m.WriteBitShort(static_cast<std::int16_t>(h.DimensionDimensionTextArrowFit));
        }
        if (R2007Plus())
            m.WriteBit(h.DimensionIsExtensionLineLengthFixed);
        if (R2010Plus())
        {
            m.WriteBit(h.DimensionTextDirection == TextDirection::RightToLeft);
            m.WriteBitDouble(h.DimensionAltMzf);
            t.WriteVariableText(h.DimensionAltMzs);
            m.WriteBitDouble(h.DimensionMzf);
            t.WriteVariableText(h.DimensionMzs);
        }
        if (R2000Plus())
        {
            hard(textStyle(h.DimensionTextStyleName));
            hard(block(h.ArrowBlockName));
            hard(block(h.DimensionBlockName));
            hard(block(h.DimensionBlockNameFirst));
            hard(block(h.DimensionBlockNameSecond));
        }
        if (R2007Plus())
        {
            hard(lineType(h.DimensionLineType));
            hard(lineType(h.DimensionTex1));
            hard(lineType(h.DimensionTex2));
        }
        if (R2000Plus())
        {
            m.WriteBitShort(static_cast<std::int16_t>(h.DimensionLineWeight));
            m.WriteBitShort(static_cast<std::int16_t>(h.ExtensionLineWeight));
        }
        ref(DwgRef::HardOwnership, tableHandle(m_db.BlockRecords()));
        ref(DwgRef::HardOwnership, tableHandle(m_db.Layers()));
        ref(DwgRef::HardOwnership, tableHandle(m_db.TextStyles()));
        ref(DwgRef::HardOwnership, tableHandle(m_db.LineTypes()));
        ref(DwgRef::HardOwnership, tableHandle(m_db.Views()));
        ref(DwgRef::HardOwnership, tableHandle(m_db.UCSs()));
        ref(DwgRef::HardOwnership, tableHandle(m_db.VPorts()));
        ref(DwgRef::HardOwnership, tableHandle(m_db.AppIds()));
        ref(DwgRef::HardOwnership, tableHandle(m_db.DimensionStyles()));
        if (R13_15Only)
            ref(DwgRef::HardOwnership, m_vEntityControl);
        hard(dict("ACAD_GROUP"));
        hard(dict("ACAD_MLINESTYLE"));
        ref(DwgRef::HardOwnership, m_db.RootDictionary() != nullptr ? m_db.RootDictionary()->ObjectHandle : kNullHandle);
        if (R2000Plus())
        {
            m.WriteBitShort(1);         // TSTACKALIGN：未建模，AutoCAD 默认值
            m.WriteBitShort(70);        // TSTACKSIZE
            t.WriteVariableText(h.HyperLinkBase);
            t.WriteVariableText(h.StyleSheetName);
            hard(dict("ACAD_LAYOUT"));
            hard(dict("ACAD_PLOTSETTINGS"));
            hard(dict("ACAD_PLOTSTYLENAME"));
        }
        if (R2004Plus())
        {
            hard(dict("ACAD_MATERIAL"));
            hard(dict("ACAD_COLOR"));
        }
        if (R2007Plus())
        {
            hard(dict("ACAD_VISUALSTYLE"));
            if (R2013Plus())
                hard(kNullHandle);
        }
        if (R2000Plus())
        {
            int flags = LineWeightToIndex(h.CurrentEntityLineWeight) & 0x1F;
            flags |= (h.EndCaps & 0x3) << 5;
            flags |= (h.JoinStyle & 0x3) << 7;
            if (h.DisplayLineWeight)
                flags |= 0x200;
            if (h.XEdit)
                flags |= 0x400;
            if (h.ExtendedNames)
                flags |= 0x800;
            if (h.PlotStyleMode == 1)
                flags |= 0x2000;
            m.WriteBitLong(flags);
            m.WriteBitShort(static_cast<std::int16_t>(h.InsUnits));
            m.WriteBitShort(static_cast<std::int16_t>(h.CurrentEntityPlotStyle));
            if (h.CurrentEntityPlotStyle == EntityPlotStyleType::ByObjectId)
                hard(kNullHandle);
            t.WriteVariableText(h.FingerPrintGuid);
            t.WriteVariableText(h.VersionGuid);
        }
        if (R2004Plus())
        {
            m.WriteByte(static_cast<std::uint8_t>(h.EntitySortingFlags));
            m.WriteByte(static_cast<std::uint8_t>(h.IndexCreationFlags));
            m.WriteByte(static_cast<std::uint8_t>(h.HideText));
            m.WriteByte(static_cast<std::uint8_t>(h.ExternalReferenceClippingBoundaryType));
            m.WriteByte(static_cast<std::uint8_t>(h.DimensionAssociativity));
            m.WriteByte(static_cast<std::uint8_t>(h.HaloGapPercentage));
            m.WriteBitShort(257);       // OBSCUREDCOLOR：未建模，AutoCAD 默认值
            m.WriteBitShort(h.InterfereColor.IsTrueColor() ? h.InterfereColor.ApproxIndex() : h.InterfereColor.Index());
            m.WriteByte(0);             // OBSCUREDLTYPE
            m.WriteByte(0);             // INTERSECTIONDISPLAY
            t.WriteVariableText(h.ProjectName);
        }
        hard(m_db.PaperSpace() != nullptr ? m_db.PaperSpace()->ObjectHandle : kNullHandle);
        hard(m_db.ModelSpace() != nullptr ? m_db.ModelSpace()->ObjectHandle : kNullHandle);
        hard(lineType("ByLayer"));
        hard(lineType("ByBlock"));
        hard(lineType("Continuous"));
        if (R2007Plus())
        {
            m.WriteBit(h.CameraDisplayObjects);
            m.WriteBitLong(0);
            m.WriteBitLong(0);
            m.WriteBitDouble(0.0);
            m.WriteBitDouble(h.StepsPerSecond);
            m.WriteBitDouble(h.StepSize);
            m.WriteBitDouble(h.Dw3DPrecision);
            m.WriteBitDouble(h.LensLength);
            m.WriteBitDouble(h.CameraHeight);
            m.WriteByte(static_cast<std::uint8_t>(h.SolidsRetainHistory));
            m.WriteByte(static_cast<std::uint8_t>(h.ShowSolidsHistory));
            m.WriteBitDouble(h.SweptSolidWidth);
            m.WriteBitDouble(h.SweptSolidHeight);
            m.WriteBitDouble(h.DraftAngleFirstCrossSection);
            m.WriteBitDouble(h.DraftAngleSecondCrossSection);
            m.WriteBitDouble(h.DraftMagnitudeFirstCrossSection);
            m.WriteBitDouble(h.DraftMagnitudeSecondCrossSection);
            m.WriteBitShort(h.SolidLoftedShape);
            m.WriteByte(static_cast<std::uint8_t>(h.LoftedObjectNormals));
            m.WriteBitDouble(h.Latitude);
            m.WriteBitDouble(h.Longitude);
            m.WriteBitDouble(h.NorthDirection);
            m.WriteBitLong(h.TimeZone);
            m.WriteByte(static_cast<std::uint8_t>(h.DisplayLightGlyphs));
            m.WriteByte('0');
            m.WriteByte(static_cast<std::uint8_t>(h.DwgUnderlayFramesVisibility));
            m.WriteByte(static_cast<std::uint8_t>(h.DgnUnderlayFramesVisibility));
            m.WriteBit(false);
            m.WriteCmColor(h.InterfereColor);
            hard(kNullHandle);      // INTERFEREOBJVS
            hard(kNullHandle);      // INTERFEREVPVS
            hard(kNullHandle);      // DRAGVS
            m.WriteByte(static_cast<std::uint8_t>(h.ShadowMode));
            m.WriteBitDouble(h.ShadowPlaneLocation);
        }
        // R14 起：4 个 BS -1（未知），R2004 起另有 BL 0、BL 0、B 0
        m.WriteBitShort(-1);
        m.WriteBitShort(-1);
        m.WriteBitShort(-1);
        m.WriteBitShort(-1);
        if (R2004Plus())
        {
            m.WriteBitLong(0);
            m.WriteBitLong(0);
            m.WriteBit(false);
        }

        const std::vector<std::uint8_t> body = MergeSectionStreams(m, text, refs, sizePos);
        const bool extraSize = (R2010Plus() && m_maintenanceVersion > 3) || R2018Plus();
        return WrapSection(kHeaderStart, kHeaderEnd, body, extraSize);
    }
}
