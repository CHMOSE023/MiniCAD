// DWG 读取：多重引线（MULTILEADER）与多重引线样式（MLEADERSTYLE），字段顺序与 ACadSharp（ODA 规范）一致
#include "Dwg/Read/DwgReaderImpl.h"

namespace MiniDWG::DwgRead
{
    void Reader::ReadMLeaderLine(MultiLeaderObjectContextDataLeaderLine& line)
    {
        DwgBitReader& r = m_objReader;
        const std::int32_t points = r.ReadBitLong();
        if (!r.CheckCount(points, 6))
            return;
        for (int i = 0; i < points; ++i)
            line.Points.push_back(r.Read3BitDouble());
        // 断点：BL 段序号 + BL 个数 + 起止点
        const std::int32_t breaks = r.ReadBitLong();
        if (!r.CheckCount(breaks, 4))
            return;
        for (int i = 0; i < breaks; ++i)
        {
            MultiLeaderObjectContextDataBreakInfo brk;
            brk.SegmentIndex = r.ReadBitLong();
            const std::int32_t pairs = r.ReadBitLong();
            if (!r.CheckCount(pairs, 12))
                return;
            for (int k = 0; k < pairs; ++k)
            {
                MultiLeaderObjectContextDataStartEndPointPair pair;
                pair.StartPoint = r.Read3BitDouble();
                pair.EndPoint = r.Read3BitDouble();
                brk.StartEndPoints.push_back(pair);
            }
            line.BreakInfoEntries.push_back(std::move(brk));
        }
        line.Index = r.ReadBitLong();
        if (R2010Plus())
        {
            line.PathType = static_cast<MultiLeaderPathType>(r.ReadBitShort());
            line.LineColor = m_s.ReadCmColor();
            line.LineTypeHandle = HandleRef();
            line.LineWeight = static_cast<LineWeightType>(r.ReadBitLong());
            line.ArrowheadSize = r.ReadBitDouble();
            line.ArrowheadHandle = HandleRef();
            line.OverrideFlags = static_cast<LeaderLinePropertOverrideFlags>(r.ReadBitLong());
        }
    }

    void Reader::ReadMLeaderRoot(MultiLeaderObjectContextDataLeaderRoot& root)
    {
        DwgBitReader& r = m_objReader;
        root.ContentValid = r.ReadBit();
        root.Unknown = r.ReadBit();
        root.ConnectionPoint = r.Read3BitDouble();
        root.Direction = r.Read3BitDouble();
        const std::int32_t pairs = r.ReadBitLong();
        if (!r.CheckCount(pairs, 12))
            return;
        for (int i = 0; i < pairs; ++i)
        {
            MultiLeaderObjectContextDataStartEndPointPair pair;
            pair.StartPoint = r.Read3BitDouble();
            pair.EndPoint = r.Read3BitDouble();
            root.BreakStartEndPointsPairs.push_back(pair);
        }
        root.LeaderIndex = r.ReadBitLong();
        root.LandingDistance = r.ReadBitDouble();
        const std::int32_t lines = r.ReadBitLong();
        if (!r.CheckCount(lines, 8))
            return;
        for (int i = 0; i < lines && !r.Failed(); ++i)
        {
            root.Lines.emplace_back();
            ReadMLeaderLine(root.Lines.back());
        }
        if (R2010Plus())
            root.TextAttachmentDirection = static_cast<TextAttachmentDirectionType>(r.ReadBitShort());
    }

    void Reader::ReadMLeaderContext(MultiLeaderObjectContextData& c)
    {
        DwgBitReader& r = m_objReader;
        const std::int32_t roots = r.ReadBitLong();
        if (!r.CheckCount(roots, 16))
            return;
        for (int i = 0; i < roots && !r.Failed(); ++i)
        {
            c.LeaderRoots.emplace_back();
            ReadMLeaderRoot(c.LeaderRoots.back());
        }
        c.ScaleFactor = r.ReadBitDouble();
        c.ContentBasePoint = r.Read3BitDouble();
        c.TextHeight = r.ReadBitDouble();
        c.ArrowheadSize = r.ReadBitDouble();
        c.LandingGap = r.ReadBitDouble();
        c.TextLeftAttachment = static_cast<TextAttachmentType>(r.ReadBitShort());
        c.TextRightAttachment = static_cast<TextAttachmentType>(r.ReadBitShort());
        c.TextAlignment = static_cast<TextAlignmentType>(r.ReadBitShort());
        c.BlockContentConnection = static_cast<BlockContentConnectionType>(r.ReadBitShort());
        c.HasTextContents = r.ReadBit();
        if (c.HasTextContents)
        {
            c.TextLabel = m_s.ReadVariableText();
            c.TextNormal = r.Read3BitDouble();
            c.TextStyleHandle = HandleRef();
            c.TextLocation = r.Read3BitDouble();
            c.Direction = r.Read3BitDouble();
            c.TextRotation = r.ReadBitDouble();
            c.BoundaryWidth = r.ReadBitDouble();
            c.BoundaryHeight = r.ReadBitDouble();
            c.LineSpacingFactor = r.ReadBitDouble();
            c.LineSpacing = static_cast<LineSpacingStyle>(r.ReadBitShort());
            c.TextColor = m_s.ReadCmColor();
            c.TextAttachmentPoint = static_cast<TextAttachmentPointType>(r.ReadBitShort());
            c.FlowDirection = static_cast<FlowDirectionType>(r.ReadBitShort());
            c.BackgroundFillColor = m_s.ReadCmColor();
            c.BackgroundScaleFactor = r.ReadBitDouble();
            c.BackgroundTransparency = r.ReadBitLong();
            c.BackgroundFillEnabled = r.ReadBit();
            c.BackgroundMaskFillOn = r.ReadBit();
            c.ColumnType = r.ReadBitShort();
            c.TextHeightAutomatic = r.ReadBit();
            c.ColumnWidth = r.ReadBitDouble();
            c.ColumnGutter = r.ReadBitDouble();
            c.ColumnFlowReversed = r.ReadBit();
            const std::int32_t sizes = r.ReadBitLong();
            if (!r.CheckCount(sizes, 2))
                return;
            for (int i = 0; i < sizes; ++i)
                c.ColumnSizes.push_back(r.ReadBitDouble());
            c.WordBreak = r.ReadBit();
            r.ReadBit();    // 含义未知
        }
        else
        {
            c.HasContentsBlock = r.ReadBit();
            if (c.HasContentsBlock)
            {
                c.BlockContentHandle = HandleRef();
                c.BlockContentNormal = r.Read3BitDouble();
                c.BlockContentLocation = r.Read3BitDouble();
                c.BlockContentScale = r.Read3BitDouble();
                c.BlockContentRotation = r.ReadBitDouble();
                c.BlockContentColor = m_s.ReadCmColor();
                for (double& v : c.TransformationMatrix.M)
                    v = r.ReadBitDouble();
            }
        }
        c.BasePoint = r.Read3BitDouble();
        c.BaseDirection = r.Read3BitDouble();
        c.BaseVertical = r.Read3BitDouble();
        c.NormalReversed = r.ReadBit();
        if (R2010Plus())
        {
            c.TextTopAttachment = static_cast<TextAttachmentType>(r.ReadBitShort());
            c.TextBottomAttachment = static_cast<TextAttachmentType>(r.ReadBitShort());
        }
    }

    // R2007 之前的箭头列表没有建模：有箭头时返回 nullptr，由调用方原样保留
    std::unique_ptr<CadObject> Reader::ReadMultiLeader(ObjectInfo& info)
    {
        auto ml = std::make_unique<MultiLeader>();
        ReadCommonEntityData(*ml, info);
        DwgBitReader& r = m_objReader;
        if (R2010Plus())
            r.ReadBitShort();   // 版本（2）
        ReadMLeaderContext(ml->ContextData);

        ml->StyleHandle = HandleRef();
        ml->PropertyOverrideFlags = static_cast<MultiLeaderPropertyOverrideFlags>(r.ReadBitLong());
        ml->PathType = static_cast<MultiLeaderPathType>(r.ReadBitShort());
        ml->LineColor = m_s.ReadCmColor();
        ml->LeaderLineTypeHandle = HandleRef();
        ml->LeaderLineWeight = static_cast<LineWeightType>(r.ReadBitLong());
        ml->EnableLanding = r.ReadBit();
        ml->EnableDogleg = r.ReadBit();
        ml->LandingDistance = r.ReadBitDouble();
        ml->ArrowheadHandle = HandleRef();
        ml->ArrowheadSize = r.ReadBitDouble();
        ml->ContentType = static_cast<LeaderContentType>(r.ReadBitShort());
        ml->TextStyleHandle = HandleRef();
        ml->TextLeftAttachment = static_cast<TextAttachmentType>(r.ReadBitShort());
        ml->TextRightAttachment = static_cast<TextAttachmentType>(r.ReadBitShort());
        ml->TextAngle = static_cast<TextAngleType>(r.ReadBitShort());
        ml->TextAlignment = static_cast<TextAlignmentType>(r.ReadBitShort());
        ml->TextColor = m_s.ReadCmColor();
        ml->TextFrame = r.ReadBit();
        ml->BlockContentHandle = HandleRef();
        ml->BlockContentColor = m_s.ReadCmColor();
        ml->BlockContentScale = r.Read3BitDouble();
        ml->BlockContentRotation = r.ReadBitDouble();
        ml->BlockContentConnection = static_cast<BlockContentConnectionType>(r.ReadBitShort());
        ml->EnableAnnotationScale = r.ReadBit();
        // R2010 之前（含 R2007）：箭头列表
        if (!R2010Plus() && r.ReadBitLong() != 0)
            return nullptr;
        const std::int32_t attributes = r.ReadBitLong();
        if (!r.CheckCount(attributes, 4))
            return nullptr;
        for (int i = 0; i < attributes; ++i)
        {
            MultiLeaderBlockAttribute a;
            a.AttributeDefinitionHandle = HandleRef();
            a.Text = m_s.ReadVariableText();
            a.Index = r.ReadBitShort();
            a.Width = r.ReadBitDouble();
            ml->BlockAttributes.push_back(std::move(a));
        }
        ml->TextDirectionNegative = r.ReadBit();
        ml->TextAligninIPE = r.ReadBitShort();
        ml->TextAttachmentPoint = static_cast<TextAttachmentPointType>(r.ReadBitShort());
        ml->ScaleFactor = r.ReadBitDouble();
        if (R2010Plus())
        {
            ml->TextAttachmentDirection = static_cast<TextAttachmentDirectionType>(r.ReadBitShort());
            ml->TextBottomAttachment = static_cast<TextAttachmentType>(r.ReadBitShort());
            ml->TextTopAttachment = static_cast<TextAttachmentType>(r.ReadBitShort());
        }
        if (R2013Plus())
            ml->ExtendedToText = r.ReadBit();
        return ml;
    }

    std::unique_ptr<CadObject> Reader::ReadMultiLeaderStyle()
    {
        auto s = std::make_unique<MultiLeaderStyle>();
        ReadCommonNonEntityData(*s);
        DwgBitReader& r = m_objReader;
        if (R2010Plus())
            r.ReadBitShort();   // 版本（2）
        s->ContentType = static_cast<LeaderContentType>(r.ReadBitShort());
        s->MultiLeaderDrawOrder = static_cast<MultiLeaderDrawOrderType>(r.ReadBitShort());
        s->LeaderDrawOrder = static_cast<LeaderDrawOrderType>(r.ReadBitShort());
        s->MaxLeaderSegmentsPoints = r.ReadBitLong();
        s->FirstSegmentAngleConstraint = r.ReadBitDouble();
        s->SecondSegmentAngleConstraint = r.ReadBitDouble();
        s->PathType = static_cast<MultiLeaderPathType>(r.ReadBitShort());
        s->LineColor = m_s.ReadCmColor();
        s->LeaderLineTypeHandle = HandleRef();
        s->LeaderLineWeight = static_cast<LineWeightType>(r.ReadBitLong());
        s->EnableLanding = r.ReadBit();
        s->LandingGap = r.ReadBitDouble();
        s->EnableDogleg = r.ReadBit();
        s->LandingDistance = r.ReadBitDouble();
        s->Description = m_s.ReadVariableText();
        s->ArrowheadHandle = HandleRef();
        s->ArrowheadSize = r.ReadBitDouble();
        s->DefaultTextContents = m_s.ReadVariableText();
        s->TextStyleHandle = HandleRef();
        s->TextLeftAttachment = static_cast<TextAttachmentType>(r.ReadBitShort());
        s->TextRightAttachment = static_cast<TextAttachmentType>(r.ReadBitShort());
        s->TextAngle = static_cast<TextAngleType>(r.ReadBitShort());
        s->TextAlignment = static_cast<TextAlignmentType>(r.ReadBitShort());
        s->TextColor = m_s.ReadCmColor();
        s->TextHeight = r.ReadBitDouble();
        s->TextFrame = r.ReadBit();
        s->TextAlignAlwaysLeft = r.ReadBit();
        s->AlignSpace = r.ReadBitDouble();
        s->BlockContentHandle = HandleRef();
        s->BlockContentColor = m_s.ReadCmColor();
        const XYZ scale = r.Read3BitDouble();
        s->BlockContentScaleX = scale.X;
        s->BlockContentScaleY = scale.Y;
        s->BlockContentScaleZ = scale.Z;
        s->EnableBlockContentScale = r.ReadBit();
        s->BlockContentRotation = r.ReadBitDouble();
        s->EnableBlockContentRotation = r.ReadBit();
        s->BlockContentConnection = static_cast<BlockContentConnectionType>(r.ReadBitShort());
        s->ScaleFactor = r.ReadBitDouble();
        // 两个位的顺序与 ACadSharp 相反（按同一张图的 DXF 核对：先是注释性 296，后是 295）
        s->IsAnnotative = r.ReadBit();
        s->OverwritePropertyValue = r.ReadBit();
        s->BreakGapSize = r.ReadBitDouble();
        if (R2010Plus())
        {
            s->TextAttachmentDirection = static_cast<TextAttachmentDirectionType>(r.ReadBitShort());
            s->TextBottomAttachment = static_cast<TextAttachmentType>(r.ReadBitShort());
            s->TextTopAttachment = static_cast<TextAttachmentType>(r.ReadBitShort());
        }
        if (R2013Plus())
            s->UnknownFlag298 = r.ReadBit();
        return s;
    }
}
