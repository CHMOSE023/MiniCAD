# dxfgen 生成报告

> 由 tools/dxfgen/dxfgen.py 从 ACadSharp 源码生成，不要手工修改。

- 类：105（其中 CadObject 派生 81）
- 枚举：105
- 成员：797
- 头变量：249

## 告警

- 类型名有歧义：DimensionAssociation（ACadSharp.Objects, ACadSharp.Header），取第一个
- 默认值无法翻译：CadHeader.FingerPrintGuid = `Guid.NewGuid().ToString()`（已置为值初始化）
- 默认值无法翻译：CadHeader.VersionGuid = `Guid.NewGuid().ToString()`（已置为值初始化）

## 未生成的公开可写属性

没有 Dxf 标注；需要时加入 `config.MANUAL_MEMBERS`。

- AttributeBase.IsLocked（`bool`）
- AttributeBase.MText（`MText`）
- BlockRecord.Flags（`BlockTypeFlags`）
- BlockRecord.IsAnonymous（`bool`）
- BlockRecord.IsUnloaded（`bool`）
- CadWipeoutBase.ShowImage（`bool`）
- Dimension.IsTextUserDefinedLocation（`bool`）
- DimensionAligned.Offset（`double`）
- DimensionAngular2Line.Offset（`double`）
- DimensionOrdinate.IsOrdinateTypeX（`bool`）
- DimensionStyle.Prefix（`string`）
- DimensionStyle.Suffix（`string`）
- Entity.ProxyGeometries（`List<IProxyGeometry>`）
- HatchPattern.Line.LineOffset（`double`）
- HatchPattern.Line.Shift（`double`）
- Insert.SpatialFilter（`SpatialFilter`）
- Layer.Flags（`LayerFlags`）
- LineType.Segment.IsShape（`bool`）
- LineType.Segment.IsText（`bool`）
- LineType.Segment.Owner（`LineType`）
- LwPolyline.IsClosed（`bool`）
- MLineStyle.Element.Owner（`MLineStyle`）
- MText.IsAnnotative（`bool`）
- MultiLeaderStyle.BlockContentScale（`XYZ`）
- ObjectContextData.HasFileToExtensionDictionary（`bool`）
- PlotSettings.PaperImageOrigin（`XY`）
- Polyline.IsClosed（`bool`）
- Polyline.MatchVerticesEntityProperties（`bool`）
- Spline.IsClosed（`bool`）
- Spline.IsPeriodic（`bool`）
- TextStyle.Flags（`StyleFlags`）
- Viewport.Scale（`Scale`）
