// DXF 读取：多重引线（MULTILEADER）与多重引线样式（MLEADERSTYLE）。
// 组码按 AutoCAD 写出的样例核对（ACadSharp 的读取漏了断点、块内容的变换矩阵、颜色的编码等）。
// MULTILEADER 中嵌套 CONTEXT_DATA{ … LEADER{ … LEADER_LINE{ … } … } … }，各层的组码含义不同，逐层手工解析。
// 角度（文字、块内容的旋转）在 DXF 中与 DWG 相同，都是弧度。
#include "Dxf/Read/DxfReaderImpl.h"

namespace MiniDWG::DxfRead
{
    namespace
    {
        // 点的分量：组码个位为 0～9 的 X（10～19、110～119 …）、Y（20～29 …）、Z（30～39 …）
        void SetComponent(XYZ& p, int code, double v)
        {
            switch ((code / 10) % 10)
            {
            case 1: p.X = v; break;
            case 2: p.Y = v; break;
            case 3: p.Z = v; break;
            default: break;
            }
        }

        bool IsBrace(const DxfGroup& g, std::string_view text) { return g.Value.AsString() == text; }

        Color CmColor(const DxfGroup& g) { return Color::FromCmValue(static_cast<std::uint32_t>(g.Value.AsInt())); }

        template <class E>
        E EnumOf(const DxfGroup& g) { return static_cast<E>(g.Value.AsInt()); }

        // LEADER_LINE{ … 305 }：顶点、断点（90 段序号开始一组，11/12 起止点）、序号与替代属性
        void ReadLeaderLine(MultiLeaderObjectContextDataLeaderLine& line, const Record& r, std::size_t& i)
        {
            MultiLeaderObjectContextDataBreakInfo* brk = nullptr;
            for (++i; i < r.Groups.size(); ++i)
            {
                const DxfGroup& g = r.Groups[i];
                switch (g.Code)
                {
                case 305:
                    return;
                case 10: line.Points.emplace_back(); line.Points.back().X = g.Value.AsDouble(); break;
                case 20: if (!line.Points.empty()) line.Points.back().Y = g.Value.AsDouble(); break;
                case 30: if (!line.Points.empty()) line.Points.back().Z = g.Value.AsDouble(); break;
                case 90:
                    line.BreakInfoEntries.emplace_back();
                    brk = &line.BreakInfoEntries.back();
                    brk->SegmentIndex = static_cast<std::int32_t>(g.Value.AsInt());
                    break;
                case 11: case 21: case 31:
                    if (brk != nullptr)
                    {
                        if (g.Code == 11)
                            brk->StartEndPoints.emplace_back();
                        if (!brk->StartEndPoints.empty())
                            SetComponent(brk->StartEndPoints.back().StartPoint, g.Code, g.Value.AsDouble());
                    }
                    break;
                case 12: case 22: case 32:
                    if (brk != nullptr && !brk->StartEndPoints.empty())
                        SetComponent(brk->StartEndPoints.back().EndPoint, g.Code, g.Value.AsDouble());
                    break;
                case 91: line.Index = static_cast<std::int32_t>(g.Value.AsInt()); break;
                case 170: line.PathType = EnumOf<MultiLeaderPathType>(g); break;
                case 92: line.LineColor = CmColor(g); break;
                case 340: line.LineTypeHandle = g.Value.AsHandle(); break;
                case 171: line.LineWeight = EnumOf<LineWeightType>(g); break;
                case 40: line.ArrowheadSize = g.Value.AsDouble(); break;
                case 341: line.ArrowheadHandle = g.Value.AsHandle(); break;
                case 93: line.OverrideFlags = EnumOf<LeaderLinePropertOverrideFlags>(g); break;
                default: break;
                }
            }
        }

        // LEADER{ … 303 }
        void ReadLeaderRoot(MultiLeaderObjectContextDataLeaderRoot& root, const Record& r, std::size_t& i)
        {
            for (++i; i < r.Groups.size(); ++i)
            {
                const DxfGroup& g = r.Groups[i];
                switch (g.Code)
                {
                case 303:
                    return;
                case 304:
                    root.Lines.emplace_back();
                    ReadLeaderLine(root.Lines.back(), r, i);
                    break;
                case 290: root.ContentValid = g.Value.AsInt() != 0; break;
                case 291: root.Unknown = g.Value.AsInt() != 0; break;
                case 10: case 20: case 30: SetComponent(root.ConnectionPoint, g.Code, g.Value.AsDouble()); break;
                case 11: case 21: case 31: SetComponent(root.Direction, g.Code, g.Value.AsDouble()); break;
                case 12: case 22: case 32:
                    if (g.Code == 12)
                        root.BreakStartEndPointsPairs.emplace_back();
                    if (!root.BreakStartEndPointsPairs.empty())
                        SetComponent(root.BreakStartEndPointsPairs.back().StartPoint, g.Code, g.Value.AsDouble());
                    break;
                case 13: case 23: case 33:
                    if (!root.BreakStartEndPointsPairs.empty())
                        SetComponent(root.BreakStartEndPointsPairs.back().EndPoint, g.Code, g.Value.AsDouble());
                    break;
                case 90: root.LeaderIndex = static_cast<std::int32_t>(g.Value.AsInt()); break;
                case 40: root.LandingDistance = g.Value.AsDouble(); break;
                case 271: root.TextAttachmentDirection = EnumOf<TextAttachmentDirectionType>(g); break;
                default: break;
                }
            }
        }

        // CONTEXT_DATA{ … 301 }
        void ReadContext(MultiLeaderObjectContextData& c, const Record& r, std::size_t& i)
        {
            int matrix = 0;
            for (++i; i < r.Groups.size(); ++i)
            {
                const DxfGroup& g = r.Groups[i];
                const double d = g.Value.AsDouble();
                switch (g.Code)
                {
                case 301:
                    return;
                case 302:
                    c.LeaderRoots.emplace_back();
                    ReadLeaderRoot(c.LeaderRoots.back(), r, i);
                    break;
                case 40: c.ScaleFactor = d; break;
                case 10: case 20: case 30: SetComponent(c.ContentBasePoint, g.Code, d); break;
                case 41: c.TextHeight = d; break;
                case 140: c.ArrowheadSize = d; break;
                case 145: c.LandingGap = d; break;
                case 174: c.TextLeftAttachment = EnumOf<TextAttachmentType>(g); break;
                case 175: c.TextRightAttachment = EnumOf<TextAttachmentType>(g); break;
                case 176: c.TextAlignment = EnumOf<TextAlignmentType>(g); break;
                case 177: c.BlockContentConnection = EnumOf<BlockContentConnectionType>(g); break;
                case 290: c.HasTextContents = g.Value.AsInt() != 0; break;
                case 304: c.TextLabel = g.Value.AsString(); break;
                case 11: case 21: case 31: SetComponent(c.TextNormal, g.Code, d); break;
                case 340: c.TextStyleHandle = g.Value.AsHandle(); break;
                case 12: case 22: case 32: SetComponent(c.TextLocation, g.Code, d); break;
                case 13: case 23: case 33: SetComponent(c.Direction, g.Code, d); break;
                case 42: c.TextRotation = d; break;
                case 43: c.BoundaryWidth = d; break;
                case 44: c.BoundaryHeight = d; break;
                case 45: c.LineSpacingFactor = d; break;
                case 170: c.LineSpacing = EnumOf<LineSpacingStyle>(g); break;
                case 90: c.TextColor = CmColor(g); break;
                case 171: c.TextAttachmentPoint = EnumOf<TextAttachmentPointType>(g); break;
                case 172: c.FlowDirection = EnumOf<FlowDirectionType>(g); break;
                case 91: c.BackgroundFillColor = CmColor(g); break;
                case 141: c.BackgroundScaleFactor = d; break;
                case 92: c.BackgroundTransparency = static_cast<std::int32_t>(g.Value.AsInt()); break;
                case 291: c.BackgroundFillEnabled = g.Value.AsInt() != 0; break;
                case 292: c.BackgroundMaskFillOn = g.Value.AsInt() != 0; break;
                case 173: c.ColumnType = static_cast<std::int16_t>(g.Value.AsInt()); break;
                case 293: c.TextHeightAutomatic = g.Value.AsInt() != 0; break;
                case 142: c.ColumnWidth = d; break;
                case 143: c.ColumnGutter = d; break;
                case 294: c.ColumnFlowReversed = g.Value.AsInt() != 0; break;
                case 144: c.ColumnSizes.push_back(d); break;
                case 295: c.WordBreak = g.Value.AsInt() != 0; break;
                case 296: c.HasContentsBlock = g.Value.AsInt() != 0; break;
                case 341: c.BlockContentHandle = g.Value.AsHandle(); break;
                case 14: case 24: case 34: SetComponent(c.BlockContentNormal, g.Code, d); break;
                case 15: case 25: case 35: SetComponent(c.BlockContentLocation, g.Code, d); break;
                case 16: case 26: case 36: SetComponent(c.BlockContentScale, g.Code, d); break;
                case 46: c.BlockContentRotation = d; break;
                case 93: c.BlockContentColor = CmColor(g); break;
                case 47:
                    // 16 个数：变换矩阵，按文件中的顺序保存（与 DWG 相同）
                    if (matrix < 16)
                        c.TransformationMatrix.M[static_cast<std::size_t>(matrix)] = d;
                    ++matrix;
                    break;
                case 110: case 120: case 130: SetComponent(c.BasePoint, g.Code, d); break;
                case 111: case 121: case 131: SetComponent(c.BaseDirection, g.Code, d); break;
                case 112: case 122: case 132: SetComponent(c.BaseVertical, g.Code, d); break;
                case 297: c.NormalReversed = g.Value.AsInt() != 0; break;
                case 272: c.TextBottomAttachment = EnumOf<TextAttachmentType>(g); break;
                case 273: c.TextTopAttachment = EnumOf<TextAttachmentType>(g); break;
                default: break;
                }
            }
        }
    }

    void Reader::ReadMultiLeader(ReadItem& item, const Record& r)
    {
        auto* ml = static_cast<MultiLeader*>(item.Object.get());
        MultiLeaderBlockAttribute* attribute = nullptr;
        Process(item, r, [&](const DxfGroup& g, std::size_t& i, const ParseState& state) -> bool {
            if (state.Subclass != "AcDbMLeader")
                return false;
            switch (g.Code)
            {
            case 100:
                return true;
            case 270:
                return true;    // 版本（2）
            case 300:
                if (IsBrace(g, "CONTEXT_DATA{"))
                    ReadContext(ml->ContextData, r, i);
                return true;
            case 91: ml->LineColor = CmColor(g); return true;
            case 92: ml->TextColor = CmColor(g); return true;
            case 93: ml->BlockContentColor = CmColor(g); return true;
            case 43: ml->BlockContentRotation = g.Value.AsDouble(); return true;
            case 94: case 345:
                return true;    // R2007 之前的箭头列表：未建模
            case 330:
                ml->BlockAttributes.emplace_back();
                attribute = &ml->BlockAttributes.back();
                attribute->AttributeDefinitionHandle = g.Value.AsHandle();
                return true;
            case 177: if (attribute) attribute->Index = static_cast<std::int16_t>(g.Value.AsInt()); return true;
            case 44: if (attribute) attribute->Width = g.Value.AsDouble(); return true;
            case 302: if (attribute) attribute->Text = g.Value.AsString(); return true;
            default:
                return false;   // 其余按元数据赋值
            }
        });
    }

    void Reader::ReadMultiLeaderStyle(ReadItem& item, const Record& r)
    {
        auto* style = static_cast<MultiLeaderStyle*>(item.Object.get());
        Process(item, r, [&](const DxfGroup& g, std::size_t&, const ParseState& state) -> bool {
            if (!state.SeenSubclass)
                return false;
            switch (g.Code)
            {
            case 179: return true;  // 版本（2）
            case 91: style->LineColor = CmColor(g); return true;
            case 93: style->TextColor = CmColor(g); return true;
            case 94: style->BlockContentColor = CmColor(g); return true;
            default: return false;
            }
        });
    }
}
