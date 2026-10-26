// 按本平台编译进来的图形接口创建图形设备（宏由 cmake/Render.cmake 定义）
#include "IGraphicsDevice.h"
#include "Core/Log.h"
#include <algorithm>
#include <vector>
#ifdef MINICAD_RENDER_D3D12
#include "D3D12/D3D12GraphicsDevice.h"
#endif
#ifdef MINICAD_RENDER_D3D11
#include "D3D11/D3D11GraphicsDevice.h"
#endif
#ifdef MINICAD_RENDER_GLES3
#include "GLES3/GLES3GraphicsDevice.h"
#endif

namespace MiniCAD
{
    namespace
    {
        // 一种后端：小写名称（--gpu / MINICAD_GPU）、是否在默认顺序里、创建函数。
        // 默认顺序就是表里的顺序；同名的几项依次尝试
        struct Backend
        {
            const char* name;
            bool        byDefault;
            std::unique_ptr<IGraphicsDevice> (*create)();
        };

        const Backend kBackends[] =
        {
#if defined(MINICAD_RENDER_D3D12) && defined(MINICAD_RENDER_D3D11)
            // Windows：D3D12 → D3D11 → WARP（先 D3D12 的，再 D3D11 的）。验收见 docs/Render/PLAN.md 阶段 1
            { "d3d12", true, [] { return std::unique_ptr<IGraphicsDevice>(std::make_unique<D3D12GraphicsDevice>(false)); } },
            { "d3d11", true, [] { return std::unique_ptr<IGraphicsDevice>(std::make_unique<D3D11GraphicsDevice>(false)); } },
            { "warp",  true, [] { return std::unique_ptr<IGraphicsDevice>(std::make_unique<D3D12GraphicsDevice>(true)); } },
            { "warp",  true, [] { return std::unique_ptr<IGraphicsDevice>(std::make_unique<D3D11GraphicsDevice>(true)); } },
#endif
#if defined(MINICAD_RENDER_GLES3)
            { "webgl2", true, [] { return std::unique_ptr<IGraphicsDevice>(std::make_unique<GLES3GraphicsDevice>()); } },
#endif
        };
    }

    std::unique_ptr<IGraphicsDevice> CreateGraphicsDevice(const NativeWindow& window, int width, int height, const std::string& preference)
    {
        // 先试指定的，再按默认顺序（跳过已经试过的）
        std::vector<const Backend*> order;
        if (!preference.empty())
        {
            for (const Backend& b : kBackends)
                if (preference == b.name)
                    order.push_back(&b);
            if (order.empty())
                LOG_WARN("没有名为 %s 的图形接口，按默认顺序创建", preference.c_str());
        }
        const bool known = !order.empty();
        for (const Backend& b : kBackends)
            if (b.byDefault && std::find(order.begin(), order.end(), &b) == order.end())
                order.push_back(&b);

        for (const Backend* b : order)
        {
            std::unique_ptr<IGraphicsDevice> device = b->create();
            if (!device->Initialize(window, width, height))
                continue;
            if (known && preference != b->name)
                LOG_WARN("图形接口 %s 不可用，改用 %s", preference.c_str(), device->GetName());
            LOG_INFO("图形接口：%s", device->GetName());
            return device;
        }
        return nullptr;
    }
}
