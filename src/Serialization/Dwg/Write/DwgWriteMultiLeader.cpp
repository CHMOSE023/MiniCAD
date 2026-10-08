// DWG 写入：多重引线与多重引线样式，字段顺序与 DwgReadMultiLeader.cpp 逐项对应
#include "Dwg/Write/DwgWriterImpl.h"

namespace MiniDWG::DwgWrite
{
    void Writer::WriteMLeaderContext(const MultiLeaderObjectContextData& c)
    {
        DwgBitWriter& m = M();
        m.WriteBitLong(static_cast<std::int32_t>(c.LeaderRoots.size()));
        for (const MultiLeaderObjectContextDataLeaderRoot& root : c.LeaderRoots)
        {
            m.WriteBit(root.ContentValid);
            m.WriteBit(root.Unknown);
            m.Write3BitDouble(root.ConnectionPoint);
            m.Write3BitDouble(root.Direction);
            m.WriteBitLong(static_cast<std::int32_t>(root.BreakStartEndPointsPairs.size()));
            for (const auto& pair : root.BreakStartEndPointsPairs)
            {
                m.Write3BitDouble(pair.StartPoint);
                m.Write3BitDouble(pair.EndPoint);
            }
            m.WriteBitLong(root.LeaderIndex);
            m.WriteBitDouble(root.LandingDistance);
            m.WriteBitLong(static_cast<std::int32_t>(root.Lines.size()));
            for (const MultiLeaderObjectContextDataLeaderLine& line : root.Lines)
            {
                m.WriteBitLong(static_cast<std::int32_t>(line.Points.size()));
                for (const XYZ& p : line.Points)
                    m.Write3BitDouble(p);
                m.WriteBitLong(static_cast<std::int32_t>(line.BreakInfoEntries.size()));
                for (const MultiLeaderObjectContextDataBreakInfo& brk : line.BreakInfoEntries)
                {
                    m.WriteBitLong(brk.SegmentIndex);
                    m.WriteBitLong(static_cast<std::int32_t>(brk.StartEndPoints.size()));
                    for (const auto& pair : brk.StartEndPoints)
                    {
                        m.Write3BitDouble(pair.StartPoint);
                        m.Write3BitDouble(pair.EndPoint);
                    }
                }
                m.WriteBitLong(line.Index);
                if (R2010Plus())
                {
                    m.WriteBitShort(static_cast<std::int16_t>(line.PathType));
                    Cmc(line.LineColor);
                    H(DwgRef::HardPointer, line.LineTypeHandle);
                    m.WriteBitLong(static_cast<std::int32_t>(line.LineWeight));
                    m.WriteBitDouble(line.ArrowheadSize);
                    H(DwgRef::HardPointer, line.ArrowheadHandle);
                    m.WriteBitLong(static_cast<std::int32_t>(line.OverrideFlags));
                }
            }
            if (R2010Plus())
                m.WriteBitShort(static_cast<std::int16_t>(root.TextAttachmentDirection));
        }
        m.WriteBitDouble(c.ScaleFactor);
        m.Write3BitDouble(c.ContentBasePoint);
        m.WriteBitDouble(c.TextHeight);
        m.WriteBitDouble(c.ArrowheadSize);
        m.WriteBitDouble(c.LandingGap);
        m.WriteBitShort(static_cast<std::int16_t>(c.TextLeftAttachment));
        m.WriteBitShort(static_cast<std::int16_t>(c.TextRightAttachment));
        m.WriteBitShort(static_cast<std::int16_t>(c.TextAlignment));
        m.WriteBitShort(static_cast<std::int16_t>(c.BlockContentConnection));
        m.WriteBit(c.HasTextContents);
        if (c.HasTextContents)
        {
            Text(c.TextLabel);
            m.Write3BitDouble(c.TextNormal);
            H(DwgRef::HardPointer, c.TextStyleHandle);
            m.Write3BitDouble(c.TextLocation);
            m.Write3BitDouble(c.Direction);
            m.WriteBitDouble(c.TextRotation);
            m.WriteBitDouble(c.BoundaryWidth);
            m.WriteBitDouble(c.BoundaryHeight);
            m.WriteBitDouble(c.LineSpacingFactor);
            m.WriteBitShort(static_cast<std::int16_t>(c.LineSpacing));
            Cmc(c.TextColor);
            m.WriteBitShort(static_cast<std::int16_t>(c.TextAttachmentPoint));
            m.WriteBitShort(static_cast<std::int16_t>(c.FlowDirection));
            Cmc(c.BackgroundFillColor);
            m.WriteBitDouble(c.BackgroundScaleFactor);
            m.WriteBitLong(c.BackgroundTransparency);
            m.WriteBit(c.BackgroundFillEnabled);
            m.WriteBit(c.BackgroundMaskFillOn);
            m.WriteBitShort(c.ColumnType);
            m.WriteBit(c.TextHeightAutomatic);
            m.WriteBitDouble(c.ColumnWidth);
            m.WriteBitDouble(c.ColumnGutter);
            m.WriteBit(c.ColumnFlowReversed);
            m.WriteBitLong(static_cast<std::int32_t>(c.ColumnSizes.size()));
            for (double size : c.ColumnSizes)
                m.WriteBitDouble(size);
            m.WriteBit(c.WordBreak);
            m.WriteBit(false);
        }
        else
        {
            m.WriteBit(c.HasContentsBlock);
            if (c.HasContentsBlock)
            {
                H(DwgRef::SoftPointer, c.BlockContentHandle);
                m.Write3BitDouble(c.BlockContentNormal);
                m.Write3BitDouble(c.BlockContentLocation);
                m.Write3BitDouble(c.BlockContentScale);
                m.WriteBitDouble(c.BlockContentRotation);
                Cmc(c.BlockContentColor);
                for (double v : c.TransformationMatrix.M)
                    m.WriteBitDouble(v);
            }
        }
        m.Write3BitDouble(c.BasePoint);
        m.Write3BitDouble(c.BaseDirection);
        m.Write3BitDouble(c.BaseVertical);
        m.WriteBit(c.NormalReversed);
        if (R2010Plus())
        {
            m.WriteBitShort(static_cast<std::int16_t>(c.TextTopAttachment));
            m.WriteBitShort(static_cast<std::int16_t>(c.TextBottomAttachment));
        }
    }

    void Writer::WriteMultiLeader(const MultiLeader& ml)
    {
        DwgBitWriter& m = M();
        if (R2010Plus())
            m.WriteBitShort(2);
        WriteMLeaderContext(ml.ContextData);

        H(DwgRef::HardPointer, ml.StyleHandle);
        m.WriteBitLong(static_cast<std::int32_t>(ml.PropertyOverrideFlags));
        m.WriteBitShort(static_cast<std::int16_t>(ml.PathType));
        Cmc(ml.LineColor);
        H(DwgRef::HardPointer, ml.LeaderLineTypeHandle);
        m.WriteBitLong(static_cast<std::int32_t>(ml.LeaderLineWeight));
        m.WriteBit(ml.EnableLanding);
        m.WriteBit(ml.EnableDogleg);
        m.WriteBitDouble(ml.LandingDistance);
        H(DwgRef::HardPointer, ml.ArrowheadHandle);
        m.WriteBitDouble(ml.ArrowheadSize);
        m.WriteBitShort(static_cast<std::int16_t>(ml.ContentType));
        H(DwgRef::HardPointer, ml.TextStyleHandle);
        m.WriteBitShort(static_cast<std::int16_t>(ml.TextLeftAttachment));
        m.WriteBitShort(static_cast<std::int16_t>(ml.TextRightAttachment));
        m.WriteBitShort(static_cast<std::int16_t>(ml.TextAngle));
        m.WriteBitShort(static_cast<std::int16_t>(ml.TextAlignment));
        Cmc(ml.TextColor);
        m.WriteBit(ml.TextFrame);
        H(DwgRef::HardPointer, ml.BlockContentHandle);
        Cmc(ml.BlockContentColor);
        m.Write3BitDouble(ml.BlockContentScale);
        m.WriteBitDouble(ml.BlockContentRotation);
        m.WriteBitShort(static_cast<std::int16_t>(ml.BlockContentConnection));
        m.WriteBit(ml.EnableAnnotationScale);
        if (!R2010Plus())
            m.WriteBitLong(0);      // 箭头列表（未建模）
        m.WriteBitLong(static_cast<std::int32_t>(ml.BlockAttributes.size()));
        for (const MultiLeaderBlockAttribute& a : ml.BlockAttributes)
        {
            H(DwgRef::HardPointer, a.AttributeDefinitionHandle);
            Text(a.Text);
            m.WriteBitShort(a.Index);
            m.WriteBitDouble(a.Width);
        }
        m.WriteBit(ml.TextDirectionNegative);
        m.WriteBitShort(ml.TextAligninIPE);
        m.WriteBitShort(static_cast<std::int16_t>(ml.TextAttachmentPoint));
        m.WriteBitDouble(ml.ScaleFactor);
        if (R2010Plus())
        {
            m.WriteBitShort(static_cast<std::int16_t>(ml.TextAttachmentDirection));
            m.WriteBitShort(static_cast<std::int16_t>(ml.TextBottomAttachment));
            m.WriteBitShort(static_cast<std::int16_t>(ml.TextTopAttachment));
        }
        if (R2013Plus())
            m.WriteBit(ml.ExtendedToText);
    }

    void Writer::WriteMultiLeaderStyle(const MultiLeaderStyle& s)
    {
        DwgBitWriter& m = M();
        if (R2010Plus())
            m.WriteBitShort(2);
        m.WriteBitShort(static_cast<std::int16_t>(s.ContentType));
        m.WriteBitShort(static_cast<std::int16_t>(s.MultiLeaderDrawOrder));
        m.WriteBitShort(static_cast<std::int16_t>(s.LeaderDrawOrder));
        m.WriteBitLong(s.MaxLeaderSegmentsPoints);
        m.WriteBitDouble(s.FirstSegmentAngleConstraint);
        m.WriteBitDouble(s.SecondSegmentAngleConstraint);
        m.WriteBitShort(static_cast<std::int16_t>(s.PathType));
        Cmc(s.LineColor);
        H(DwgRef::HardPointer, s.LeaderLineTypeHandle);
        m.WriteBitLong(static_cast<std::int32_t>(s.LeaderLineWeight));
        m.WriteBit(s.EnableLanding);
        m.WriteBitDouble(s.LandingGap);
        m.WriteBit(s.EnableDogleg);
        m.WriteBitDouble(s.LandingDistance);
        Text(s.Description);
        H(DwgRef::HardPointer, s.ArrowheadHandle);
        m.WriteBitDouble(s.ArrowheadSize);
        Text(s.DefaultTextContents);
        H(DwgRef::HardPointer, s.TextStyleHandle);
        m.WriteBitShort(static_cast<std::int16_t>(s.TextLeftAttachment));
        m.WriteBitShort(static_cast<std::int16_t>(s.TextRightAttachment));
        m.WriteBitShort(static_cast<std::int16_t>(s.TextAngle));
        m.WriteBitShort(static_cast<std::int16_t>(s.TextAlignment));
        Cmc(s.TextColor);
        m.WriteBitDouble(s.TextHeight);
        m.WriteBit(s.TextFrame);
        m.WriteBit(s.TextAlignAlwaysLeft);
        m.WriteBitDouble(s.AlignSpace);
        H(DwgRef::HardPointer, s.BlockContentHandle);
        Cmc(s.BlockContentColor);
        m.Write3BitDouble(XYZ{ s.BlockContentScaleX, s.BlockContentScaleY, s.BlockContentScaleZ });
        m.WriteBit(s.EnableBlockContentScale);
        m.WriteBitDouble(s.BlockContentRotation);
        m.WriteBit(s.EnableBlockContentRotation);
        m.WriteBitShort(static_cast<std::int16_t>(s.BlockContentConnection));
        m.WriteBitDouble(s.ScaleFactor);
        m.WriteBit(s.IsAnnotative);
        m.WriteBit(s.OverwritePropertyValue);
        m.WriteBitDouble(s.BreakGapSize);
        if (R2010Plus())
        {
            m.WriteBitShort(static_cast<std::int16_t>(s.TextAttachmentDirection));
            m.WriteBitShort(static_cast<std::int16_t>(s.TextBottomAttachment));
            m.WriteBitShort(static_cast<std::int16_t>(s.TextTopAttachment));
        }
        if (R2013Plus())
            m.WriteBit(s.UnknownFlag298);
    }
}
