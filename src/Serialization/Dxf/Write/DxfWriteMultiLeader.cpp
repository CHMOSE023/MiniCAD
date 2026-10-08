// DXF 写出：多重引线（MULTILEADER）与多重引线样式（MLEADERSTYLE）。组码顺序与 AutoCAD 写的样例一致：
// R2004 及以前没有 270、271～273、295；R2010 起有 270 与 271～273；R2013 起有 295（样式的 298 同）
#include "Dxf/Write/DxfWriterImpl.h"

namespace MiniDWG::DxfWrite
{
    namespace
    {
        std::int64_t CmValue(const Color& c) { return static_cast<std::int32_t>(c.CmValue()); }
    }

    void Writer::WriteMultiLeaderContext(const MultiLeaderObjectContextData& c)
    {
        WriteString(300, "CONTEXT_DATA{");
        WriteReal(40, c.ScaleFactor);
        WriteXYZ(10, c.ContentBasePoint);
        WriteReal(41, c.TextHeight);
        WriteReal(140, c.ArrowheadSize);
        WriteReal(145, c.LandingGap);
        WriteInt(174, c.TextLeftAttachment);
        WriteInt(175, c.TextRightAttachment);
        WriteInt(176, c.TextAlignment);
        WriteInt(177, c.BlockContentConnection);
        WriteInt(290, c.HasTextContents ? 1 : 0);
        if (c.HasTextContents)
        {
            WriteString(304, c.TextLabel);
            WriteXYZ(11, c.TextNormal);
            WriteRefOrNull(340, c.TextStyleHandle);
            WriteXYZ(12, c.TextLocation);
            WriteXYZ(13, c.Direction);
            WriteReal(42, c.TextRotation);
            WriteReal(43, c.BoundaryWidth);
            WriteReal(44, c.BoundaryHeight);
            WriteReal(45, c.LineSpacingFactor);
            WriteInt(170, c.LineSpacing);
            WriteInt(90, CmValue(c.TextColor));
            WriteInt(171, c.TextAttachmentPoint);
            WriteInt(172, c.FlowDirection);
            WriteInt(91, CmValue(c.BackgroundFillColor));
            WriteReal(141, c.BackgroundScaleFactor);
            WriteInt(92, c.BackgroundTransparency);
            WriteInt(291, c.BackgroundFillEnabled ? 1 : 0);
            WriteInt(292, c.BackgroundMaskFillOn ? 1 : 0);
            WriteInt(173, c.ColumnType);
            WriteInt(293, c.TextHeightAutomatic ? 1 : 0);
            WriteReal(142, c.ColumnWidth);
            WriteReal(143, c.ColumnGutter);
            WriteInt(294, c.ColumnFlowReversed ? 1 : 0);
            for (double size : c.ColumnSizes)
                WriteReal(144, size);
            WriteInt(295, c.WordBreak ? 1 : 0);
        }
        WriteInt(296, c.HasContentsBlock ? 1 : 0);
        if (c.HasContentsBlock)
        {
            WriteRefOrNull(341, c.BlockContentHandle);
            WriteXYZ(14, c.BlockContentNormal);
            WriteXYZ(15, c.BlockContentLocation);
            WriteXYZ(16, c.BlockContentScale);
            WriteReal(46, c.BlockContentRotation);
            WriteInt(93, CmValue(c.BlockContentColor));
            for (double v : c.TransformationMatrix.M)
                WriteReal(47, v);
        }
        WriteXYZ(110, c.BasePoint);
        WriteXYZ(111, c.BaseDirection);
        WriteXYZ(112, c.BaseVertical);
        WriteInt(297, c.NormalReversed ? 1 : 0);

        for (const MultiLeaderObjectContextDataLeaderRoot& root : c.LeaderRoots)
        {
            WriteString(302, "LEADER{");
            WriteInt(290, root.ContentValid ? 1 : 0);
            WriteInt(291, root.Unknown ? 1 : 0);
            WriteXYZ(10, root.ConnectionPoint);
            WriteXYZ(11, root.Direction);
            for (const auto& pair : root.BreakStartEndPointsPairs)
            {
                WriteXYZ(12, pair.StartPoint);
                WriteXYZ(13, pair.EndPoint);
            }
            WriteInt(90, root.LeaderIndex);
            WriteReal(40, root.LandingDistance);
            for (const MultiLeaderObjectContextDataLeaderLine& line : root.Lines)
            {
                WriteString(304, "LEADER_LINE{");
                for (const XYZ& p : line.Points)
                    WriteXYZ(10, p);
                for (const MultiLeaderObjectContextDataBreakInfo& brk : line.BreakInfoEntries)
                {
                    WriteInt(90, brk.SegmentIndex);
                    for (const auto& pair : brk.StartEndPoints)
                    {
                        WriteXYZ(11, pair.StartPoint);
                        WriteXYZ(12, pair.EndPoint);
                    }
                }
                WriteInt(91, line.Index);
                // 替代引线样式的属性：AutoCAD 只在有替代时写
                if (line.OverrideFlags != LeaderLinePropertOverrideFlags{})
                {
                    WriteInt(170, line.PathType);
                    WriteInt(92, CmValue(line.LineColor));
                    WriteRefOrNull(340, line.LineTypeHandle);
                    WriteInt(171, line.LineWeight);
                    WriteReal(40, line.ArrowheadSize);
                    WriteRefOrNull(341, line.ArrowheadHandle);
                    WriteInt(93, line.OverrideFlags);
                }
                WriteString(305, "}");
            }
            if (AtLeast(CadVersion::AC1024))
                WriteInt(271, root.TextAttachmentDirection);
            WriteString(303, "}");
        }
        if (AtLeast(CadVersion::AC1024))
        {
            WriteInt(272, c.TextBottomAttachment);
            WriteInt(273, c.TextTopAttachment);
        }
        WriteString(301, "}");
    }

    void Writer::WriteMultiLeader(const MultiLeader& ml)
    {
        WriteSubclass("AcDbMLeader");
        if (AtLeast(CadVersion::AC1024))
            WriteInt(270, 2);
        WriteMultiLeaderContext(ml.ContextData);

        WriteRefOrNull(340, ml.StyleHandle);
        WriteInt(90, ml.PropertyOverrideFlags);
        WriteInt(170, ml.PathType);
        WriteInt(91, CmValue(ml.LineColor));
        WriteRefOrNull(341, ml.LeaderLineTypeHandle);
        WriteInt(171, ml.LeaderLineWeight);
        WriteInt(290, ml.EnableLanding ? 1 : 0);
        WriteInt(291, ml.EnableDogleg ? 1 : 0);
        WriteReal(41, ml.LandingDistance);
        WriteRef(342, ml.ArrowheadHandle);
        WriteReal(42, ml.ArrowheadSize);
        WriteInt(172, ml.ContentType);
        WriteRefOrNull(343, ml.TextStyleHandle);
        WriteInt(173, ml.TextLeftAttachment);
        WriteInt(95, ml.TextRightAttachment);
        WriteInt(174, ml.TextAngle);
        WriteInt(175, ml.TextAlignment);
        WriteInt(92, CmValue(ml.TextColor));
        WriteInt(292, ml.TextFrame ? 1 : 0);
        WriteRef(344, ml.BlockContentHandle);
        WriteInt(93, CmValue(ml.BlockContentColor));
        WriteXYZ(10, ml.BlockContentScale);
        WriteReal(43, ml.BlockContentRotation);
        WriteInt(176, ml.BlockContentConnection);
        WriteInt(293, ml.EnableAnnotationScale ? 1 : 0);
        for (const MultiLeaderBlockAttribute& a : ml.BlockAttributes)
        {
            WriteRefOrNull(330, a.AttributeDefinitionHandle);
            WriteInt(177, a.Index);
            WriteReal(44, a.Width);
            WriteString(302, a.Text);
        }
        WriteInt(294, ml.TextDirectionNegative ? 1 : 0);
        WriteInt(178, ml.TextAligninIPE);
        WriteInt(179, ml.TextAttachmentPoint);
        WriteReal(45, ml.ScaleFactor);
        if (AtLeast(CadVersion::AC1024))
        {
            WriteInt(271, ml.TextAttachmentDirection);
            WriteInt(272, ml.TextBottomAttachment);
            WriteInt(273, ml.TextTopAttachment);
        }
        if (AtLeast(CadVersion::AC1027))
            WriteInt(295, ml.ExtendedToText ? 1 : 0);
    }

    void Writer::WriteMultiLeaderStyle(const MultiLeaderStyle& s)
    {
        WriteSubclass("AcDbMLeaderStyle");
        if (AtLeast(CadVersion::AC1024))
            WriteInt(179, 2);
        WriteInt(170, s.ContentType);
        WriteInt(171, s.MultiLeaderDrawOrder);
        WriteInt(172, s.LeaderDrawOrder);
        WriteInt(90, s.MaxLeaderSegmentsPoints);
        WriteReal(40, s.FirstSegmentAngleConstraint);
        WriteReal(41, s.SecondSegmentAngleConstraint);
        WriteInt(173, s.PathType);
        WriteInt(91, CmValue(s.LineColor));
        WriteRefOrNull(340, s.LeaderLineTypeHandle);
        WriteInt(92, s.LeaderLineWeight);
        WriteInt(290, s.EnableLanding ? 1 : 0);
        WriteReal(42, s.LandingGap);
        WriteInt(291, s.EnableDogleg ? 1 : 0);
        WriteReal(43, s.LandingDistance);
        WriteString(3, s.Description);
        WriteRef(341, s.ArrowheadHandle);
        WriteReal(44, s.ArrowheadSize);
        WriteString(300, s.DefaultTextContents);
        WriteRefOrNull(342, s.TextStyleHandle);
        WriteInt(174, s.TextLeftAttachment);
        WriteInt(178, s.TextRightAttachment);
        WriteInt(175, s.TextAngle);
        WriteInt(176, s.TextAlignment);
        WriteInt(93, CmValue(s.TextColor));
        WriteReal(45, s.TextHeight);
        WriteInt(292, s.TextFrame ? 1 : 0);
        WriteInt(297, s.TextAlignAlwaysLeft ? 1 : 0);
        WriteReal(46, s.AlignSpace);
        WriteRef(343, s.BlockContentHandle);
        WriteInt(94, CmValue(s.BlockContentColor));
        WriteReal(47, s.BlockContentScaleX);
        WriteReal(49, s.BlockContentScaleY);
        WriteReal(140, s.BlockContentScaleZ);
        WriteInt(293, s.EnableBlockContentScale ? 1 : 0);
        WriteReal(141, s.BlockContentRotation);
        WriteInt(294, s.EnableBlockContentRotation ? 1 : 0);
        WriteInt(177, s.BlockContentConnection);
        WriteReal(142, s.ScaleFactor);
        WriteInt(295, s.OverwritePropertyValue ? 1 : 0);
        WriteInt(296, s.IsAnnotative ? 1 : 0);
        WriteReal(143, s.BreakGapSize);
        if (AtLeast(CadVersion::AC1024))
        {
            WriteInt(271, s.TextAttachmentDirection);
            WriteInt(272, s.TextBottomAttachment);
            WriteInt(273, s.TextTopAttachment);
        }
        if (AtLeast(CadVersion::AC1027))
            WriteInt(298, s.UnknownFlag298 ? 1 : 0);
    }
}
