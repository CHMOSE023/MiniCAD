#pragma once
#include "../Math/Point3.hpp"
#include "Core/GeomKernel/Point.hpp"
#include "Entity.hpp"
#include "../Draw/IDrawSink.hpp"
#include <cmath>
#include <cctype>
#include <string>

namespace MiniCAD
{
	// ── 点样式（DXF HEADER 的 $PDMODE）──────────────────────────────────────
	//
	// POINT 实体**自身不带显示信息**：DXF/DWG 里只有坐标（10/20/30）、
	// 可选的挤出方向（210）和角度（50）。画成什么样完全由图纸级 $PDMODE 决定。
	// 这就是为什么"CAD 里是 ⊕、我们画成一个十字"—— 我们从来没读过它。
	//
	// $PDMODE 是位组合（与 AutoCAD / LibreCAD 一致）：
	//     低 4 位选"基本形"：0=实心小点 1=什么都不画 2=十字 3=叉 4=竖线
	//     bit5 (32) = 外加一个外接圆
	//     bit6 (64) = 外加一个外接方
	//     96 = 32|64 = 圆和方都加
	// 例：34 = 32|2 = 圆 + 十字 = ⊕。
	//
	// 低 4 位取 0~4 之外时按"十字"处理（与 LibreCAD 的 case _ 一致）。
	inline constexpr bool PdmodeHasCircle(int mode) { return (mode & 32) != 0; }
	inline constexpr bool PdmodeHasSquare(int mode) { return (mode & 64) != 0; }
	inline constexpr int  PdmodeShape(int mode)     { return mode & 0x0F; }

	// ── AutoCAD "点样式"（DDPTYPE）对话框的 20 个图标 ──────────────────────
	//
	// 20 = 4 种**外框** × 5 种**基本形**，网格是 4 行 × 5 列：
	//     第 row 行 = 外框 kPdmodeOuter[row]：无 / 圆(32) / 方(64) / 方+圆(96)
	//     第 col 列 = 基本形 col：点(0) / 无(1) / 十字(2) / 叉(3) / 竖线(4)
	// 所以第 row 行第 col 列的 $PDMODE = kPdmodeOuter[row] | col。
	// 例：第 1 行第 2 列 = 32|2 = 34 = ⊕。
	//
	// ⚠ 这个表是"图标顺序"的唯一来源：对话框按它排图标，测试按它验收，
	//   避免"对话框里排一套、图里画另一套"。
	inline constexpr int kPdmodeOuter[4] = { 0, 32, 64, 96 };

	// 第 row 行第 col 列的 $PDMODE（row ∈ [0,4)，col ∈ [0,5)）
	inline constexpr int PdmodeAt(int row, int col)
	{
		return kPdmodeOuter[(row >= 0 && row < 4) ? row : 0] | (col & 0x0F);
	}

	// 定义点图层（DEFPOINTS）上的 POINT 一律按"实心小点"画，忽略 $PDMODE。
	// AutoCAD 行为如此。
	inline bool IsDefpointsLayerName(const std::string& name)
	{
		const size_t bar = name.rfind('|');            // 块内图层形如 "块名|DEFPOINTS"
		const std::string tail = (bar == std::string::npos) ? name : name.substr(bar + 1);
		static const char* kDef = "DEFPOINTS";         // ⚠ 9 个字符（不是 10）
		if (tail.size() != 9) return false;
		for (size_t i = 0; i < 9; ++i)
			if (std::toupper(static_cast<unsigned char>(tail[i])) != kDef[i]) return false;
		return true;
	}

	class PointEntity : public Entity
	{
	public:
		PointEntity(ObjectID id, const  Math::Point3& position) : Entity(id), m_point(position) {};

		void         SetPoint(const Point& point) { m_point = point; }
		const Point& GetPoint() const             { return m_point; };

		virtual	AABB GetBoundingBox()  const override { return m_point.GetBounds(); };

		std::unique_ptr<Entity> Clone(ObjectID newId) const override
		{
			auto e = std::make_unique<PointEntity>(newId, m_point.Position);
			e->SetAttr(GetAttr());
			return e;
		}

		// ── 由 $PDMODE/$PDSIZE 算出图形尺寸 ────────────────────────────────
		// 返回外接圆/方的半径（= 图形尺寸的一半）。返回 <=0 表示"取不到有效尺寸"，
		// 调用方应退化成 1 像素的实心小点。
		//
		// $PDSIZE 语义：>0 = 世界单位；=0 = 视口高度的 5%；<0 = 视口高度的 |值|%。
		// 后两种是**屏幕相对**的，需要 worldPerPixel 与视口像素高度。
		// 与 LibreCAD / 用户 WkViewer 的 relativeWorldSize 完全一致：
		//     pct = (pdsize == 0) ? 5.0 : -pdsize;
		//     world = pct/100 × viewportHeightPx × worldPerPixel
		static double ResolveHalfSize(double pdSize, double worldPerPixel, double viewportHeightPx)
		{
			if (pdSize > 0.0)
				return pdSize * 0.5;                       // 世界单位
			if (!(worldPerPixel > 0.0) || !(viewportHeightPx > 0.0))
				return 0.0;                                // 相对尺寸但拿不到视口 → 退化成小点
			const double pct = (pdSize == 0.0) ? 5.0 : -pdSize;
			return (pct / 100.0) * viewportHeightPx * worldPerPixel * 0.5;
		}

		// 把 $PDMODE 描述的图形画到 sink（**不含**"实心小点"那条路径）。
		//
		// 视口与"点样式"对话框预览共用这一个实现 ⇒ 预览与图里必然一致。
		// mode == 0 或 mode 低 4 位 == 1 时什么都不画（调用方自行决定要不要点图元）。
		static void EmitGlyph(IDrawSink& sink, const Math::Point3& c,
		                      int mode, double sHalf, const Math::Color4& color)
		{
			if (!(sHalf > 0.0))
				return;
			auto L = [&](double x0, double y0, double x1, double y1)
			{
				sink.DrawLine({ c.x + x0, c.y + y0, c.z },
				              { c.x + x1, c.y + y1, c.z }, color, false);
			};

			// 基本形。十字/叉的臂长取**整个 PDSIZE**（= 2s），所以它会戳出外接圆/方，
			// 圆与方则正好落在半径 s 上 —— 这正是 AutoCAD 的观感
			//（⊕ 的十字四个端头露在圆外）。
			const double arm = 2.0 * sHalf;
			switch (PdmodeShape(mode))
			{
			case 0:  { const double d = sHalf * 0.05; L(-d, 0, d, 0); L(0, -d, 0, d); break; }
			case 1:  break;                                        // 只靠圆/方
			case 2:  L(-arm, 0, arm, 0); L(0, -arm, 0, arm); break;
			case 3:  L(-arm, -arm, arm, arm); L(-arm, arm, arm, -arm); break;
			case 4:  L(0, 0, 0, sHalf); break;
			default: L(-sHalf, 0, sHalf, 0); L(0, -sHalf, 0, sHalf); break;   // 未知值按十字
			}

			if (PdmodeHasCircle(mode))
			{
				// 用线段拟合圆。段数按半径自适应，下限保证小圆也圆得住，
				// 上限防止大圆爆顶点。
				int n = static_cast<int>(std::ceil(sHalf * 8.0));
				if (n < 16)  n = 16;
				if (n > 128) n = 128;
				const double step = 6.283185307179586476925286766559 / n;
				double px = c.x + sHalf, py = c.y;
				for (int i = 1; i <= n; ++i)
				{
					const double a = step * i;
					const double x = c.x + sHalf * std::cos(a);
					const double y = c.y + sHalf * std::sin(a);
					sink.DrawLine({ px, py, c.z }, { x, y, c.z }, color, false);
					px = x; py = y;
				}
			}
			if (PdmodeHasSquare(mode))
			{
				L(-sHalf, -sHalf, sHalf, -sHalf); L(sHalf, -sHalf, sHalf, sHalf);
				L(sHalf, sHalf, -sHalf, sHalf);   L(-sHalf, sHalf, -sHalf, -sHalf);
			}
		}

		virtual void Draw(IDrawSink& sink, bool isSelected, bool isHovered)   const override
		{
			const Math::Color4& color = isSelected ? IDrawSink::kSelectionColor : isHovered ? IDrawSink::kHoverColor : ResolveDrawColor(sink);
			const auto p = m_point.Position;

			// ── 取图纸级点样式 ──────────────────────────────────────────────
			int    mode = 0;
			double size = 0.0;
			if (!sink.GetPointStyle(mode, size))
			{
				// sink 不知道图纸设置（文字预览等）：按 AutoCAD 默认"实心小点"画。
				sink.DrawPoint(p, color, false);
				return;
			}
			// DEFPOINTS 层永远用小点（AutoCAD 行为）
			if (IsDefpointsLayerName(sink.GetLayerName(GetLayerID())))
				mode = 0;

			const double sHalf = ResolveHalfSize(size, sink.GetWorldPerPixel(),
			                                     sink.GetViewportHeightPx());
			// 相对尺寸按当前缩放换算成世界尺寸：缩放后要重新细分，屏幕上才保持视口高度的百分比
			if (size <= 0.0 && mode != 0)
				sink.MarkViewDependent();

			// mode == 0，或尺寸取不到（相对尺寸但没有视口信息）：
			// 一律用 **1 像素的点图元**。
			// ⚠ 不能退回"世界单位的小十字"：那在大坐标图纸里会小于一个像素而
			//    整片消失。
			if (mode == 0 || !(sHalf > 0.0))
			{
				sink.DrawPoint(p, color, false);
				return;
			}
			// $PDMODE 低 4 位 = 1 且没有圆/方：明确"什么都不画"（拾取仍能找到）
			if (PdmodeShape(mode) == 1 && !PdmodeHasCircle(mode) && !PdmodeHasSquare(mode))
				return;

			EmitGlyph(sink, p, mode, sHalf, color);
		}

		DECLARE_RUNTIME_TYPE(PointEntity, Entity)

	private:
		Point       m_point;
	};
}
