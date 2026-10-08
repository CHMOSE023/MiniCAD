// 由 tools/dxfgen/dxfgen.py 从 ACadSharp 源码生成，不要手工修改。
#pragma once
#include "Database/EnumFlags.hpp"
#include <cstdint>

namespace MiniDWG
{
    // Types/Units/AngularDirection.cs
    enum class AngularDirection : std::int16_t
    {
        CounterClockWise = 0,
        ClockWise = 1,
    };

    // Types/Units/AngularUnitFormat.cs
    enum class AngularUnitFormat : std::int16_t
    {
        DecimalDegrees,
        DegreesMinutesSeconds,
        Gradians,
        Radians,
        SurveyorsUnits,
    };

    // Tables/ZeroHandling.cs
    enum class AngularZeroHandling : std::uint8_t
    {
        DisplayAll = 0,
        SuppressLeadingZeroes = 1,
        SupressTrailingZeroes = 2,
        SupressAll = 3,
    };

    // Tables/ArcLengthSymbolPosition.cs
    enum class ArcLengthSymbolPosition : std::int16_t
    {
        BeforeDimensionText,
        AboveDimensionText,
        None,
    };

    // Objects/AssociativityFlags.cs
    enum class AssociativityFlags : std::int16_t
    {
        None = 0,
        FirstPointReference = 1,
        SecondPointReference = 2,
        ThirdPointReference = 4,
        FourthPointReference = 8,
    };
    MINIDWG_ENUM_FLAGS(AssociativityFlags)

    // Entities/AttachmentPointType.cs
    enum class AttachmentPointType : std::int16_t
    {
        TopLeft = 1,
        TopCenter = 2,
        TopRight = 3,
        MiddleLeft = 4,
        MiddleCenter = 5,
        MiddleRight = 6,
        BottomLeft = 7,
        BottomCenter = 8,
        BottomRight = 9,
    };

    // Entities/AttributeFlags.cs
    enum class AttributeFlags : std::int32_t
    {
        None = 0,
        Hidden = 1,
        Constant = 2,
        Verify = 4,
        Preset = 8,
    };
    MINIDWG_ENUM_FLAGS(AttributeFlags)

    // Entities/AttributeType.cs
    enum class AttributeType : std::int32_t
    {
        SingleLine = 1,
        MultiLine = 2,
        ConstantMultiLine = 4,
    };

    // Header/AttributeVisibilityMode.cs
    enum class AttributeVisibilityMode : std::int32_t
    {
        None = 0,
        Normal = 1,
        All = 2,
    };

    // Entities/BackgroundFillFlags.cs
    enum class BackgroundFillFlags : std::uint8_t
    {
        None = 0,
        UseBackgroundFillColor = 1,
        UseDrawingWindowColor = 2,
        TextFrame = 16,
    };
    MINIDWG_ENUM_FLAGS(BackgroundFillFlags)

    // Tables/BlockContentConnectionType.cs
    enum class BlockContentConnectionType : std::int16_t
    {
        BlockExtents = 0,
        BasePoint = 1,
    };

    // Blocks/BlockTypeFlags.cs
    enum class BlockTypeFlags : std::int32_t
    {
        None = 0,
        Anonymous = 1,
        NonConstantAttributeDefinitions = 2,
        XRef = 4,
        XRefOverlay = 8,
        XRefDependent = 16,
        XRefResolved = 32,
        Referenced = 64,
    };
    MINIDWG_ENUM_FLAGS(BlockTypeFlags)

    // Entities/BoundaryPathFlags.cs
    enum class BoundaryPathFlags : std::int32_t
    {
        Default = 0,
        External = 1,
        Polyline = 2,
        Derived = 4,
        Textbox = 8,
        Outermost = 16,
        NotClosed = 32,
        SelfIntersecting = 64,
        TextIsland = 128,
        Duplicate = 256,
        IsAnnotative = 512,
        DoesNotSupportScale = 1024,
        ForceAnnoAllVisible = 2048,
        OrientToPaper = 4096,
        IsAnnotativeBlock = 8192,
    };
    MINIDWG_ENUM_FLAGS(BoundaryPathFlags)

    // Types/ObjectType.cs
    enum class CadObjectType : std::int16_t
    {
        UNLISTED = -999,
        INVALID = -1,
        UNDEFINED = 0,
        TEXT = 1,
        ATTRIB = 2,
        ATTDEF = 3,
        BLOCK = 4,
        ENDBLK = 5,
        SEQEND = 6,
        INSERT = 7,
        MINSERT = 8,
        UNKNOW_9 = 9,
        VERTEX_2D = 0x0A,
        VERTEX_3D = 0x0B,
        VERTEX_MESH = 0x0C,
        VERTEX_PFACE = 0x0D,
        VERTEX_PFACE_FACE = 0x0E,
        POLYLINE_2D = 0x0F,
        POLYLINE_3D = 0x10,
        ARC = 0x11,
        CIRCLE = 0x12,
        LINE = 0x13,
        DIMENSION_ORDINATE = 0x14,
        DIMENSION_LINEAR = 0x15,
        DIMENSION_ALIGNED = 0x16,
        DIMENSION_ANG_3_Pt = 0x17,
        DIMENSION_ANG_2_Ln = 0x18,
        DIMENSION_RADIUS = 0x19,
        DIMENSION_DIAMETER = 0x1A,
        POINT = 0x1B,
        FACE3D = 0x1C,
        POLYLINE_PFACE = 0x1D,
        POLYLINE_MESH = 0x1E,
        SOLID = 0x1F,
        TRACE = 0x20,
        SHAPE = 0x21,
        VIEWPORT = 0x22,
        ELLIPSE = 0x23,
        SPLINE = 0x24,
        REGION = 0x25,
        SOLID3D = 0x26,
        BODY = 0x27,
        RAY = 0x28,
        XLINE = 0x29,
        DICTIONARY = 0x2A,
        OLEFRAME = 0x2B,
        MTEXT = 0x2C,
        LEADER = 0x2D,
        TOLERANCE = 0x2E,
        MLINE = 0x2F,
        BLOCK_CONTROL_OBJ = 0x30,
        BLOCK_HEADER = 0x31,
        LAYER_CONTROL_OBJ = 0x32,
        LAYER = 0x33,
        STYLE_CONTROL_OBJ = 0x34,
        STYLE = 0x35,
        UNKNOW_36 = 0x36,
        UNKNOW_37 = 0x37,
        LTYPE_CONTROL_OBJ = 0x38,
        LTYPE = 0x39,
        UNKNOW_3A = 0x3A,
        UNKNOW_3B = 0x3B,
        VIEW_CONTROL_OBJ = 0x3C,
        VIEW = 0x3D,
        UCS_CONTROL_OBJ = 0x3E,
        UCS = 0x3F,
        VPORT_CONTROL_OBJ = 0x40,
        VPORT = 0x41,
        APPID_CONTROL_OBJ = 0x42,
        APPID = 0x43,
        DIMSTYLE_CONTROL_OBJ = 0x44,
        DIMSTYLE = 0x45,
        VP_ENT_HDR_CTRL_OBJ = 0x46,
        VP_ENT_HDR = 0x47,
        GROUP = 0x48,
        MLINESTYLE = 0x49,
        OLE2FRAME = 0x4A,
        DUMMY = 0x4B,
        LONG_TRANSACTION = 0x4C,
        LWPOLYLINE = 0x4D,
        HATCH = 0x4E,
        XRECORD = 0x4F,
        ACDBPLACEHOLDER = 0x50,
        VBA_PROJECT = 0x51,
        LAYOUT = 0x52,
        ACAD_PROXY_ENTITY = 0x1f2,
        ACAD_PROXY_OBJECT = 0x1f3,
    };

    // Entities/ClipMode.cs
    enum class ClipMode : std::uint8_t
    {
        Outside = 0,
        Inside = 1,
    };

    // Entities/ClipType.cs
    enum class ClipType : std::int16_t
    {
        Rectangular = 1,
        Polygonal = 2,
    };

    // Entities/ColumnType.cs
    enum class ColumnType : std::int16_t
    {
        NoColumns = 0,
        StaticColumns = 1,
        DynamicColumns = 2,
    };

    // Tables/DefaultLightingType.cs
    enum class DefaultLightingType : std::int16_t
    {
        OneDistantLight,
        TwoDistantLights,
    };

    // Objects/DictionaryCloningFlags.cs
    enum class DictionaryCloningFlags : std::int16_t
    {
        NotApplicable = 0,
        KeepExisting = 1,
        UseClone = 2,
        XrefName = 3,
        Name = 4,
        UnmangleName = 5,
    };
    MINIDWG_ENUM_FLAGS(DictionaryCloningFlags)

    // Header/DimensionAssociation.cs
    enum class DimensionAssociationType : std::int16_t
    {
        CreateExplodedDimensions = 0,
        CreateNonAssociativeDimensions = 1,
        CreateAssociativeDimensions = 2,
    };

    // Tables/DimensionTextBackgroundFillMode.cs
    enum class DimensionTextBackgroundFillMode : std::int16_t
    {
        NoBackground,
        DrawingBackgroundColor,
        DimensionTextBackgroundColor,
    };

    // Tables/DimensionTextHorizontalAlignment.cs
    enum class DimensionTextHorizontalAlignment : std::uint8_t
    {
        Centered = 0,
        Left = 1,
        Right = 2,
        OverFirstExtLine = 3,
        OverSecondExtLine = 4,
    };

    // Tables/DimensionTextVerticalAlignment.cs
    enum class DimensionTextVerticalAlignment : std::int32_t
    {
        Centered = 0,
        Above = 1,
        Outside = 2,
        JIS = 3,
        Below = 4,
    };

    // Entities/DimensionType.cs
    enum class DimensionType : std::int32_t
    {
        Linear = 0,
        Aligned = 1,
        Angular = 2,
        Diameter = 3,
        Radius = 4,
        Angular3Point = 5,
        Ordinate = 6,
        BlockReference = 32,
        OrdinateTypeX = 64,
        TextUserDefinedLocation = 128,
    };
    MINIDWG_ENUM_FLAGS(DimensionType)

    // Entities/DrawingDirectionType.cs
    enum class DrawingDirectionType : std::int16_t
    {
        LeftToRight = 1,
        RightToLeft = 2,
        TopToBottom = 3,
        BottomToTop = 4,
        ByStyle = 5,
    };

    // Types/DxfReferenceType.cs
    enum class DxfReferenceType : std::uint8_t
    {
        None = 0,
        Handle = 1,
        Name = 2,
        Count = 4,
        Optional = 8,
        Ignored = 16,
        IsAngle = 32,
        Unprocess = 64,
    };
    MINIDWG_ENUM_FLAGS(DxfReferenceType)

    // Header/EntityPlotStyleType.cs
    enum class EntityPlotStyleType : std::int16_t
    {
        ByLayer = 0,
        ByBlock = 1,
        ByDictionaryDefault = 2,
        ByObjectId = 3,
    };

    // FlowDirectionType.cs
    enum class FlowDirectionType : std::int16_t
    {
        Horizontal = 1,
        Vertical = 3,
        ByStyle = 5,
        ByStyleBadDoc = 6,
    };

    // Tables/FontFlags.cs
    enum class FontFlags : std::int32_t
    {
        Regular = 0,
        Italic = 1,
        Bold = 2,
    };
    MINIDWG_ENUM_FLAGS(FontFlags)

    // Tables/FractionFormat.cs
    enum class FractionFormat : std::int16_t
    {
        Horizontal,
        Diagonal,
        None,
    };

    // Tables/GridFlags.cs
    enum class GridFlags : std::int16_t
    {
        _0 = 0,
        _1 = 1,
        _2 = 2,
        _3 = 4,
        _4 = 8,
    };
    MINIDWG_ENUM_FLAGS(GridFlags)

    // Entities/Hatch.BoundaryPath.Edge.cs
    enum class HatchBoundaryPathEdgeType : std::int32_t
    {
        Polyline = 0,
        Line = 1,
        CircularArc = 2,
        EllipticArc = 3,
        Spline = 4,
    };

    // Entities/HatchPatternType.cs
    enum class HatchPatternType : std::int32_t
    {
        PatternFill = 0,
        SolidFill = 1,
        Custom = 2,
    };

    // Entities/HatchStyleType.cs
    enum class HatchStyleType : std::int32_t
    {
        Normal = 0,
        Outer = 1,
        Ignore = 2,
    };

    // Entities/HookLineDirection.cs
    enum class HookLineDirection : std::int16_t
    {
        Opposite = 0,
        Same = 1,
    };

    // Entities/ImageDisplayFlags.cs
    enum class ImageDisplayFlags : std::int16_t
    {
        None = 0,
        ShowImage = 1,
        ShowNotAlignedImage = 2,
        UseClippingBoundary = 4,
        TransparencyIsOn = 8,
    };
    MINIDWG_ENUM_FLAGS(ImageDisplayFlags)

    // Objects/ImageDisplayQuality.cs
    enum class ImageDisplayQuality : std::int32_t
    {
        Draft = 0,
        High = 1,
    };

    // Types/Units/ImageUnits.cs
    enum class ImageUnits : std::int16_t
    {
        Unitless = 0,
        Millimeters = 1,
        Centimeters = 2,
        Meters = 3,
        Kilometers = 4,
        Inches = 5,
        Feet = 6,
        Yards = 7,
        Miles = 8,
    };

    // Header/IndexCreationFlags.cs
    enum class IndexCreationFlags : std::uint8_t
    {
        NoIndex = 0b0,
        LayerIndex = 0b1,
        SpatialIndex = 0b10,
        LayerAndSpatialIndex = 0b11,
    };
    MINIDWG_ENUM_FLAGS(IndexCreationFlags)

    // Entities/InvisibleEdgeFlags.cs
    enum class InvisibleEdgeFlags : std::int32_t
    {
        None = 0,
        First = 1,
        Second = 2,
        Third = 4,
        Fourth = 8,
    };
    MINIDWG_ENUM_FLAGS(InvisibleEdgeFlags)

    // Entities/KnotParametrization.cs
    enum class KnotParametrization : std::uint16_t
    {
        Chord = 0,
        SquareRoot = 1,
        Uniform = 2,
        Custom = 15,
    };

    // Objects/LayoutFlags.cs
    enum class LayoutFlags : std::int16_t
    {
        None = 0,
        PaperSpaceLinetypeScaling = 1,
        LimitsChecking = 2,
    };
    MINIDWG_ENUM_FLAGS(LayoutFlags)

    // Objects/LeaderContentType.cs
    enum class LeaderContentType : std::int16_t
    {
        None = 0,
        Block = 1,
        MText = 2,
        Tolerance = 3,
    };

    // Entities/LeaderCreationType.cs
    enum class LeaderCreationType : std::int16_t
    {
        CreatedWithTextAnnotation = 0,
        CreatedWithToleranceAnnotation = 1,
        CreatedWithBlockReferenceAnnotation = 2,
        CreatedWithoutAnnotation = 3,
    };

    // Objects/LeaderDrawOrderType.cs
    enum class LeaderDrawOrderType : std::int32_t
    {
        LeaderHeadFirst = 0,
        LeaderTailFirst = 1,
    };

    // Objects/LeaderLinePropertOverrideFlags.cs
    enum class LeaderLinePropertOverrideFlags : std::int32_t
    {
        None = 0,
        PathType = 1,
        LineColor = 2,
        LineType = 4,
        LineWeight = 8,
        ArrowheadSize = 16,
        Arrowhead = 32,
    };
    MINIDWG_ENUM_FLAGS(LeaderLinePropertOverrideFlags)

    // Entities/LeaderPathType.cs
    enum class LeaderPathType : std::int32_t
    {
        StraightLineSegments = 0,
        Spline = 1,
    };

    // Entities/LightingType.cs
    enum class LightingType : std::uint8_t
    {
        OneDistantLight,
        TwoDistantLights,
    };

    // LineSpacingStyle.cs
    enum class LineSpacingStyle : std::int16_t
    {
        AtLeast = 1,
        Exactly = 2,
    };

    // Entities/LineSpacingStyleType.cs
    enum class LineSpacingStyleType : std::int16_t
    {
        None,
        AtLeast,
        Exact,
    };

    // Tables/LinetypeShapeFlags.cs
    enum class LineTypeShapeFlags : std::int16_t
    {
        None = 0,
        RotationIsAbsolute = 1,
        Text = 2,
        Shape = 4,
    };
    MINIDWG_ENUM_FLAGS(LineTypeShapeFlags)

    // Types/LineWeightType.cs
    enum class LineWeightType : std::int16_t
    {
        ByDIPs = -4,
        Default = -3,
        ByBlock = -2,
        ByLayer = -1,
        W0 = 0,
        W5 = 5,
        W9 = 9,
        W13 = 13,
        W15 = 15,
        W18 = 18,
        W20 = 20,
        W25 = 25,
        W30 = 30,
        W35 = 35,
        W40 = 40,
        W50 = 50,
        W53 = 53,
        W60 = 60,
        W70 = 70,
        W80 = 80,
        W90 = 90,
        W100 = 100,
        W106 = 106,
        W120 = 120,
        W140 = 140,
        W158 = 158,
        W200 = 200,
        W211 = 211,
    };

    // Types/Units/LinearUnitFormat.cs
    enum class LinearUnitFormat : std::int16_t
    {
        None = 0,
        Scientific = 1,
        Decimal = 2,
        Engineering = 3,
        Architectural = 4,
        Fractional = 5,
        WindowsDesktop = 6,
    };

    // Entities/LwPolylineFlags.cs
    enum class LwPolylineFlags : std::int32_t
    {
        Default = 0,
        Closed = 1,
        Plinegen = 128,
    };
    MINIDWG_ENUM_FLAGS(LwPolylineFlags)

    // Entities/MLineFlags.cs
    enum class MLineFlags : std::int32_t
    {
        None = 0,
        HasVertices = 1,
        Closed = 2,
        NoStartCaps = 4,
        NoEndCaps = 8,
    };
    MINIDWG_ENUM_FLAGS(MLineFlags)

    // Entities/MLineJustification.cs
    enum class MLineJustification : std::int32_t
    {
        Top = 0,
        Zero = 1,
        Bottom = 2,
    };

    // Objects/MLineStyleFlags.cs
    enum class MLineStyleFlags : std::int32_t
    {
        None = 0,
        FillOn = 1,
        DisplayJoints = 2,
        StartSquareCap = 16,
        StartInnerArcsCap = 32,
        StartRoundCap = 64,
        EndSquareCap = 256,
        EndInnerArcsCap = 512,
        EndRoundCap = 1024,
    };
    MINIDWG_ENUM_FLAGS(MLineStyleFlags)

    // Header/MeasurementUnits.cs
    enum class MeasurementUnits : std::int16_t
    {
        English = 0,
        Metric = 1,
    };

    // Objects/MultiLeaderDrawOrderType.cs
    enum class MultiLeaderDrawOrderType : std::int32_t
    {
        ContentFirst = 0,
        LeaderFirst = 1,
    };

    // MultiLeaderPathType.cs
    enum class MultiLeaderPathType : std::int16_t
    {
        Invisible = 0,
        StraightLineSegments = 1,
        Spline = 2,
    };

    // Entities/MultiLeaderPropertyOverrideFlags.cs
    enum class MultiLeaderPropertyOverrideFlags : std::int32_t
    {
        None = 0,
        PathType = 0x1,
        LineColor = 0x2,
        LeaderLineType = 0x4,
        LeaderLineWeight = 0x8,
        EnableLanding = 0x10,
        LandingGap = 0x20,
        EnableDogleg = 0x40,
        LandingDistance = 0x80,
        Arrowhead = 0x100,
        ArrowheadSize = 0x200,
        ContentType = 0x400,
        TextStyle = 0x800,
        TextLeftAttachment = 0x1000,
        TextAngle = 0x2000,
        TextAlignment = 0x4000,
        TextColor = 0x8000,
        TextHeight = 0x10000,
        TextFrame = 0x20000,
        EnableUseDefaultMText = 0x40000,
        BlockContent = 0x80000,
        BlockContentColor = 0x100000,
        BlockContentScale = 0x200000,
        BlockContentRotation = 0x400000,
        BlockContentConnection = 0x800000,
        ScaleFactor = 0x1000000,
        TextRightAttachment = 0x2000000,
        TextSwitchAlignmentType = 0x4000000,
        TextAttachmentDirection = 0x8000000,
        TextTopAttachment = 0x10000000,
        TextBottomAttachment = 0x20000000,
    };
    MINIDWG_ENUM_FLAGS(MultiLeaderPropertyOverrideFlags)

    // Objects/ObjectOsnapType.cs
    enum class ObjectOsnapType : std::int16_t
    {
        None = 0,
        Endpoint = 1,
        Midpoint = 2,
        Center = 3,
        Node = 4,
        Quadrant = 5,
        Intersection = 6,
        Insertion = 7,
        Perpendicular = 8,
        Tangent = 9,
        Nearest = 10,
        ApparentIntersection = 11,
        Parallel = 12,
        StartPoint = 13,
    };

    // Header/ObjectSnapMode.cs
    enum class ObjectSnapMode : std::uint16_t
    {
        None = 0,
        EndPoint = 1,
        MidPoint = 2,
        Center = 4,
        Node = 8,
        Quadrant = 16,
        Intersection = 32,
        Insertion = 64,
        Perpendicular = 128,
        Tangent = 256,
        Nearest = 512,
        ClearsAllObjectSnaps = 1024,
        ApparentIntersection = 2048,
        Extension = 4096,
        Parallel = 8192,
        AllModes = Parallel | Extension | ApparentIntersection | ClearsAllObjectSnaps | Nearest | Tangent | Perpendicular | Insertion | Intersection | Quadrant | Node | Center | MidPoint | EndPoint,
        SwitchedOff = 16384,
    };
    MINIDWG_ENUM_FLAGS(ObjectSnapMode)

    // Header/ObjectSortingFlags.cs
    enum class ObjectSortingFlags : std::uint8_t
    {
        Disabled = 0,
        Selection = 1,
        Snap = 2,
        Redraw = 4,
        Slide = 8,
        Regen = 16,
        Plotting = 32,
        Postscript = 64,
        All = Postscript | Plotting | Regen | Slide | Redraw | Snap | Selection,
    };
    MINIDWG_ENUM_FLAGS(ObjectSortingFlags)

    // Types/OrthographicType.cs
    enum class OrthographicType : std::int32_t
    {
        None,
        Top,
        Bottom,
        Front,
        Back,
        Left,
        Right,
    };

    // Objects/PlotFlags.cs
    enum class PlotFlags : std::int32_t
    {
        None = 0,
        PlotViewportBorders = 1,
        ShowPlotStyles = 2,
        PlotCentered = 4,
        PlotHidden = 8,
        UseStandardScale = 16,
        PlotPlotStyles = 32,
        ScaleLineweights = 64,
        PrintLineweights = 128,
        DrawViewportsFirst = 512,
        ModelType = 1024,
        UpdatePaper = 2048,
        ZoomToPaperOnUpdate = 4096,
        Initializing = 8192,
        PrevPlotInit = 16384,
    };
    MINIDWG_ENUM_FLAGS(PlotFlags)

    // Objects/PlotPaperUnits.cs
    enum class PlotPaperUnits : std::int32_t
    {
        Inches = 0,
        Millimeters = 1,
        Pixels = 2,
    };

    // Objects/PlotRotation.cs
    enum class PlotRotation : std::int32_t
    {
        NoRotation = 0,
        Degrees90 = 1,
        Degrees180 = 2,
        Degrees270 = 3,
    };

    // Types/PlotType.cs
    enum class PlotType : std::int32_t
    {
        LastScreenDisplay = 0,
        DrawingExtents = 1,
        DrawingLimits = 2,
        View = 3,
        Window = 4,
        LayoutInformation = 5,
    };

    // Entities/PolylineFlags.cs
    enum class PolylineFlags : std::int32_t
    {
        Default = 0,
        ClosedPolylineOrClosedPolygonMeshInM = 1,
        CurveFit = 2,
        SplineFit = 4,
        Polyline3D = 8,
        PolygonMesh = 16,
        ClosedPolygonMeshInN = 32,
        PolyfaceMesh = 64,
        ContinuousLinetypePattern = 128,
    };
    MINIDWG_ENUM_FLAGS(PolylineFlags)

    // Types/RenderMode.cs
    enum class RenderMode : std::int32_t
    {
        Optimized2D,
        Wireframe,
        HiddenLine,
        FlatShaded,
        GouraudShaded,
        FlatShadedWithWireframe,
        GouraudShadedWithWireframe,
    };

    // Objects/ResolutionUnit.cs
    enum class ResolutionUnit : std::uint8_t
    {
        None = 0,
        Centimeters = 2,
        Inches = 5,
    };

    // Objects/RotatedDimensionType.cs
    enum class RotatedDimensionType : std::int16_t
    {
        Unknown = 0,
        Parallel = 1,
        Perpendicular = 2,
    };

    // Types/ScaledType.cs
    enum class ScaledType : std::int32_t
    {
        ScaledToFit = 0,
        _1 = 1,
        _2 = 2,
        _3 = 3,
        _4 = 4,
        _5 = 5,
        _6 = 6,
        _7 = 7,
        _8 = 8,
        _9 = 9,
        _10 = 10,
        _11 = 11,
        _12 = 12,
        _13 = 13,
        _14 = 14,
        _15 = 15,
        _16 = 16,
        _17 = 17,
        _18 = 18,
        _19 = 19,
        _20 = 20,
        _21 = 21,
        _22 = 22,
        _23 = 23,
        _24 = 24,
        _25 = 25,
        _26 = 26,
        _27 = 27,
        _28 = 28,
        _29 = 29,
        _30 = 30,
        _31 = 31,
        _32 = 32,
    };

    // Header/ShadeEdgeType.cs
    enum class ShadeEdgeType : std::int16_t
    {
        FacesShadedEdgesNotHighlighted,
        FacesShadedEdgesHighlightedInBlack,
        FacesNotFilledEdgesInEntityColor,
        FacesInEntityColorEdgesInBlack,
    };

    // Objects/ShadePlotMode.cs
    enum class ShadePlotMode : std::int32_t
    {
        AsDisplayed = 0,
        Wireframe = 1,
        Hidden = 2,
        Rendered = 3,
    };

    // Objects/ShadePlotResolutionMode.cs
    enum class ShadePlotResolutionMode : std::uint16_t
    {
        Draft = 0,
        Preview = 1,
        Normal = 2,
        Presentation = 3,
        Maximum = 4,
        Custom = 5,
    };

    // Header/ShadowMode.cs
    enum class ShadowMode : std::uint8_t
    {
        CastsAndReceives = 0,
        Casts = 1,
        Receives = 2,
        Ignores = 3,
    };

    // Entities/SmoothSurfaceType.cs
    enum class SmoothSurfaceType : std::int16_t
    {
        NoSmooth = 0,
        Quadratic = 5,
        Cubic = 6,
        BezierSurface = 8,
    };

    // Header/SpaceLineTypeScaling.cs
    enum class SpaceLineTypeScaling : std::int16_t
    {
        Viewport = 0,
        Normal = 1,
    };

    // Entities/SplineFlags.cs
    enum class SplineFlags : std::uint16_t
    {
        None = 0,
        Closed = 1,
        Periodic = 2,
        Rational = 4,
        Planar = 8,
        Linear = 16,
    };
    MINIDWG_ENUM_FLAGS(SplineFlags)

    // Entities/SplineFlags1.cs
    enum class SplineFlags1 : std::uint16_t
    {
        None = 0,
        MethodFitPoints = 1,
        CVFrameShow = 2,
        Closed = 4,
        UseKnotParameter = 8,
    };
    MINIDWG_ENUM_FLAGS(SplineFlags1)

    // Header/SplineType.cs
    enum class SplineType : std::int16_t
    {
        None = 0,
        QuadraticBSpline = 5,
        CubicBSpline = 6,
        Bezier = 8,
    };

    // Tables/StandardFlags.cs
    enum class StandardFlags : std::int16_t
    {
        None = 0,
        XrefDependent = 16,
        XrefResolved = 32,
        Referenced = 64,
    };
    MINIDWG_ENUM_FLAGS(StandardFlags)

    // Objects/SubentType.cs
    enum class SubentType : std::int16_t
    {
        Unknown = 0,
        Edge = 1,
        Face = 2,
    };

    // TextAlignmentType.cs
    enum class TextAlignmentType : std::int16_t
    {
        Left = 0,
        Center = 1,
        Right = 2,
    };

    // TextAngleType.cs
    enum class TextAngleType : std::int16_t
    {
        ParllelToLastLeaderLine = 0,
        Horizontal = 1,
        Optimized = 2,
    };

    // Tables/TextArrowFitType.cs
    enum class TextArrowFitType : std::uint8_t
    {
        Both = 0,
        ArrowsFirst = 1,
        TextFirst = 2,
        BestFit = 3,
    };

    // TextAttachmentDirectionType.cs
    enum class TextAttachmentDirectionType : std::int16_t
    {
        Horizontal = 0,
        Vertical = 1,
    };

    // TextAttachmentPoint.cs
    enum class TextAttachmentPointType : std::int16_t
    {
        Left = 1,
        Center = 2,
        Right = 3,
    };

    // TextAttachmentType.cs
    enum class TextAttachmentType : std::int16_t
    {
        TopOfTopLine = 0,
        MiddleOfTopLine = 1,
        MiddleOfText = 2,
        MiddleOfBottomLine = 3,
        BottomOfBottomLine = 4,
        BottomLine = 5,
        BottomOfTopLineUnderlineBottomLine = 6,
        BottomOfTopLineUnderlineTopLine = 7,
        BottomofTopLineUnderlineAll = 8,
        CenterOfText = 9,
        CenterOfTextOverline = 10,
    };

    // Tables/TextDirection.cs
    enum class TextDirection : std::uint8_t
    {
        LeftToRight = 0,
        RightToLeft = 1,
    };

    // Entities/TextHorizontalAlignment.cs
    enum class TextHorizontalAlignment : std::int16_t
    {
        Left = 0,
        Center = 1,
        Right = 2,
        Aligned = 3,
        Middle = 4,
        Fit = 5,
    };

    // Entities/TextMirrorFlag.cs
    enum class TextMirrorFlag : std::int16_t
    {
        None = 0,
        Backward = 2,
        UpsideDown = 4,
    };
    MINIDWG_ENUM_FLAGS(TextMirrorFlag)

    // Tables/TextMovement.cs
    enum class TextMovement : std::int16_t
    {
        MoveLineWithText,
        AddLeaderWhenTextMoved,
        FreeTextPosition,
    };

    // Entities/TextVerticalAlignmentType.cs
    enum class TextVerticalAlignmentType : std::int16_t
    {
        Baseline = 0,
        Bottom = 1,
        Middle = 2,
        Top = 3,
    };

    // Tables/ToleranceAlignment.cs
    enum class ToleranceAlignment : std::uint8_t
    {
        Bottom = 0,
        Middle = 1,
        Top = 2,
    };

    // Types/Units/UnitsType.cs
    enum class UnitsType : std::int16_t
    {
        Unitless = 0,
        Inches = 1,
        Feet = 2,
        Miles = 3,
        Millimeters = 4,
        Centimeters = 5,
        Meters = 6,
        Kilometers = 7,
        Microinches = 8,
        Mils = 9,
        Yards = 10,
        Angstroms = 11,
        Nanometers = 12,
        Microns = 13,
        Decimeters = 14,
        Decameters = 15,
        Hectometers = 16,
        Gigameters = 17,
        AstronomicalUnits = 18,
        LightYears = 19,
        Parsecs = 20,
        USSurveyFeet = 21,
        USSurveyInches = 22,
        USSurveyYards = 23,
        USSurveyMiles = 24,
    };

    // Tables/UscIconType.cs
    enum class UscIconType : std::int16_t
    {
        Off,
        OnLower,
        OffOrigin,
        OnOrigin,
    };

    // Entities/VertexFlags.cs
    enum class VertexFlags : std::int32_t
    {
        Default = 0,
        CurveFittingExtraVertex = 1,
        CurveFitTangent = 2,
        NotUsed = 4,
        SplineVertexFromSplineFitting = 8,
        SplineFrameControlPoint = 16,
        PolylineVertex3D = 32,
        PolygonMesh3D = 64,
        PolyFaceMeshVertex = 128,
    };
    MINIDWG_ENUM_FLAGS(VertexFlags)

    // Entities/VerticalAlignmentType.cs
    enum class VerticalAlignmentType : std::int16_t
    {
        Top = 0,
        Middle = 1,
        Bottom = 2,
    };

    // Tables/ViewModeType.cs
    enum class ViewModeType : std::int32_t
    {
        Off = 0,
        PerspectiveView = 1,
        FrontClipping = 2,
        BackClipping = 4,
        Follow = 8,
        FrontClippingZ = 16,
    };
    MINIDWG_ENUM_FLAGS(ViewModeType)

    // Entities/ViewportStatusFlags.cs
    enum class ViewportStatusFlags : std::int32_t
    {
        PerspectiveMode = 1,
        FrontClipping = 2,
        BackClipping = 4,
        UcsFollow = 8,
        FrontClipNotAtEye = 16,
        UcsIconVisibility = 32,
        UcsIconAtOrigin = 64,
        FastZoom = 128,
        SnapMode = 256,
        GridMode = 512,
        IsometricSnapStyle = 1024,
        HidePlotMode = 2048,
        IsoPairTop = 4096,
        IsoPairRight = 8192,
        ViewportZoomLocking = 16384,
        CurrentlyAlwaysEnabled = 32768,
        NonRectangularClipping = 65536,
        ViewportOff = 131072,
        DisplayGridBeyondDrawingLimits = 262144,
        AdaptiveGridDisplay = 524288,
        SubdivisionGridBelowSpacing = 1048576,
    };
    MINIDWG_ENUM_FLAGS(ViewportStatusFlags)

    // Header/XClipFrameType.cs
    enum class XClipFrameType : std::int32_t
    {
        None = 0,
        DisplayAndPlot = 1,
        DisplayNotPlot = 2,
    };

    // Tables/ZeroHandling.cs
    enum class ZeroHandling : std::uint8_t
    {
        SuppressZeroFeetAndInches = 0,
        ShowZeroFeetAndInches = 1,
        ShowZeroFeetSuppressZeroInches = 2,
        SuppressZeroFeetShowZeroInches = 3,
        SuppressDecimalLeadingZeroes = 4,
        SuppressDecimalTrailingZeroes = 8,
        SuppressDecimalLeadingAndTrailingZeroes = 12,
    };

}
