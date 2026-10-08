#pragma once
#include "Render/DrawData.hpp"
#include "Style/ColorRef.hpp"
#include <span>
#include <vector>

namespace MiniGUI
{
    // 多个 DrawList 共享的参数，由 UIContext（目前是宿主）持有
    struct DrawListSharedData
    {
        TextureId whiteTexture = InvalidTextureId;  // 含白色像素的纹理（以后就是字形图集）
        Vec2      whitePixelUV;                     // 白色像素中心的纹理坐标，纯色填充都采样这里
        float     pixelScale   = 1.0f;              // 物理像素 / 逻辑像素，决定羽化宽度和曲线细分
        float     curveMaxError = 0.30f;            // 曲线细分允许的最大误差（物理像素）
        const Color32* palette = nullptr;           // 当前主题的颜色表（ThemeColors::Data），解析 ColorRef 用；为空时用深色主题
    };

    // 绘制指令列表：把矩形、线条、圆等图元转换为三角形 + 纹理 + 裁剪矩形。
    // 抗锯齿用"边缘羽化几何"实现：边缘外侧多生成一圈 alpha 渐变为 0 的三角形，
    // 宽度为 1 个物理像素，所以不依赖 MSAA，软件光栅也能得到相同结果。
    // 所有坐标都是逻辑像素，y 轴向下。
    class DrawList
    {
    public:
        explicit DrawList(const DrawListSharedData* shared);

        // 每帧开始时调用：清空缓冲，恢复默认裁剪矩形（整个显示区域）和默认纹理
        void Reset(const Rect& displayRect);

        // ── 状态栈 ──────────────────────────────────────────────
        void PushClipRect(const Rect& rect, bool intersectWithCurrent = true);
        void PopClipRect();
        void PushTexture(TextureId texture);
        void PopTexture();

        const Rect& GetClipRect() const { return m_clipStack.back(); }

        // 按当前主题把颜色引用解析为具体颜色
        Color32 Resolve(ColorRef color) const;

        // ── 图元 ────────────────────────────────────────────────
        void AddLine            (Vec2 a, Vec2 b, ColorRef color, float thickness = 1.0f);
        void AddRect            (const Rect& rect, ColorRef color, float rounding = 0.0f, float thickness = 1.0f);  // 描边画在矩形内侧
        void AddRectFilled      (const Rect& rect, ColorRef color, float rounding = 0.0f);
        void AddCircle          (Vec2 center, float radius, ColorRef color, float thickness = 1.0f, int segments = 0);
        void AddCircleFilled    (Vec2 center, float radius, ColorRef color, int segments = 0);
        void AddTriangleFilled  (Vec2 a, Vec2 b, Vec2 c, ColorRef color);
        void AddPolyline        (std::span<const Vec2> points, ColorRef color, bool closed, float thickness);
        void AddConvexPolyFilled(std::span<const Vec2> points, ColorRef color);
        void AddCross           (Vec2 center, float halfSize, ColorRef color, float thickness = 1.0f);  // ×：逐物理像素绘制，严格对称
        void AddImage           (TextureId texture, const Rect& rect, Vec2 uv0 = { 0, 0 }, Vec2 uv1 = { 1, 1 }, ColorRef tint = Colors::White);

        // ── 路径 ────────────────────────────────────────────────
        void PathClear() { m_path.clear(); }
        void PathLineTo(Vec2 p);
        void PathArcTo (Vec2 center, float radius, float angleMin, float angleMax, int segments = 0);  // 角度为弧度，y 轴向下时顺时针递增
        void PathRect  (const Rect& rect, float rounding = 0.0f);
        void PathFillConvex(ColorRef color) { AddConvexPolyFilled(m_path, color); m_path.clear(); }
        void PathStroke    (ColorRef color, bool closed, float thickness = 1.0f) { AddPolyline(m_path, color, closed, thickness); m_path.clear(); }

        // ── 底层接口：直接写顶点和索引 ──────────────────────────
        // PrimReserve 之后必须恰好写入预留数量的顶点和索引
        void PrimReserve(uint32_t indexCount, uint32_t vertexCount);
        void PrimRect   (const Rect& rect, ColorRef color);                                   // 不带抗锯齿的实心矩形
        void PrimRectUV (const Rect& rect, Vec2 uv0, Vec2 uv1, ColorRef color);

        // ── 输出 ────────────────────────────────────────────────
        const std::vector<DrawVert>&  GetVertices() const { return m_vertices; }
        const std::vector<DrawIndex>& GetIndices()  const { return m_indices;  }
        const std::vector<DrawCmd>&   GetCommands() const { return m_commands; }

    private:
        void  UpdateCurrentCommand();                   // 裁剪矩形或纹理变化后，按需切分新命令
        int   CalcCircleSegments(float radius) const;   // 根据半径和允许误差计算整圆细分段数
        float FringeWidth() const;                      // 1 个物理像素对应的逻辑像素宽度

        void  WriteVertex(Vec2 pos, Vec2 uv, Color32 color);
        void  WriteIndex (uint32_t index) { m_indices.push_back(index); }

    private:
        const DrawListSharedData* m_shared = nullptr;

        std::vector<DrawVert>  m_vertices;
        std::vector<DrawIndex> m_indices;
        std::vector<DrawCmd>   m_commands;

        std::vector<Rect>      m_clipStack;
        std::vector<TextureId> m_textureStack;
        std::vector<Vec2>      m_path;

        std::vector<Vec2>      m_tempNormals;           // 生成羽化几何时的临时缓冲，避免每次分配
    };
}
