#include "Grid.h"
#include <cmath>

namespace MiniCAD
{
    void Grid::Generate(const XMFLOAT3& cameraPos)
    {
        m_lines.clear();

        // 根据距离动态调整网格大小
        float dist  = sqrtf(cameraPos.x * cameraPos.x + cameraPos.z * cameraPos.z);
        float scale = powf(10.0f, floorf(log10f(dist + 1)));

        float step = scale / 10.0f;
        int halfCount = 20;

        float size = step * halfCount;

        for (int i = -halfCount; i <= halfCount; ++i)
        {
            float s = i * step;

            // Z方向线
            m_lines.push_back({ {s,  -size,0.0}, {s, size,0.0 },(i == 0) ? XMFLOAT4(1,0,0,1) : XMFLOAT4(0.5f,0.5f,0.5f,1) });

            // X方向线
            m_lines.push_back({ {-size, s, 0.0},{size, s, 0.0},(i == 0) ? XMFLOAT4(0,0,1,1) : XMFLOAT4(0.5f,0.5f,0.5f,1) });
        }
    }
}

