#pragma once
#include <DirectXMath.h>
#include <vector>

using namespace DirectX;

namespace MiniCAD 
{
    struct GridLine
    {
        XMFLOAT3 a;
        XMFLOAT3 b;
        XMFLOAT4 color;
    };

    class Grid
    {
    public:
        Grid(const XMFLOAT3& cameraPos)
        {
            Generate(cameraPos);
        }
        void Generate(const XMFLOAT3& cameraPos);

        const std::vector<GridLine>& GetLines() const { return m_lines; }

    private:
        std::vector<GridLine> m_lines;
    };
}
