#include "D3D12/D3D12Context.h"
#include <d3dcompiler.h>
#include <algorithm>
#include <iterator>
#include <cstdio>
#include <cstring>
#include <stdexcept>

using Microsoft::WRL::ComPtr;

namespace MiniGUI
{
    namespace
    {
        constexpr uint64_t kPageSize      = 1u << 20;     // 上传页的默认大小
        constexpr uint64_t kMaxPooledPage = 4u << 20;     // 更大的上传单独建页，用完就释放（场景静态流可达几百 MB）
        constexpr uint64_t kIdleFrames    = 60;           // 空闲页超过这么多帧没用就归还系统
        constexpr UINT     kCpuHeapBlock  = 16;           // RTV / DSV 堆每块的描述符个数
        constexpr uint32_t kSrvInitial    = 64;           // SRV 堆的初始容量，不够时翻倍

        uint64_t AlignUp(uint64_t v, uint64_t a) { return (v + a - 1) & ~(a - 1); }

        uint64_t NextPow2(uint64_t v)
        {
            uint64_t p = 1;
            while (p < v)
                p <<= 1;
            return p;
        }

        std::string ToUtf8(const wchar_t* w)
        {
            const int n = WideCharToMultiByte(CP_UTF8, 0, w, -1, nullptr, 0, nullptr, nullptr);
            std::string s(static_cast<size_t>(n > 0 ? n - 1 : 0), '\0');
            if (n > 1)
                WideCharToMultiByte(CP_UTF8, 0, w, -1, s.data(), n, nullptr, nullptr);
            return s;
        }

        // 调试层的消息打到 stderr（控制台版能看到）
        void CALLBACK OnDebugMessage(D3D12_MESSAGE_CATEGORY, D3D12_MESSAGE_SEVERITY severity, D3D12_MESSAGE_ID, LPCSTR description, void*)
        {
            if (severity <= D3D12_MESSAGE_SEVERITY_WARNING)
                std::fprintf(stderr, "[D3D12] %s\n", description);
        }
    }

    void D3D12ThrowIfFailed(HRESULT hr, const char* what)
    {
        if (FAILED(hr))
        {
            char buffer[160] = {};
            std::snprintf(buffer, sizeof(buffer), "D3D12: %s 失败 (HRESULT 0x%08X)", what, static_cast<unsigned int>(hr));
            throw std::runtime_error(buffer);
        }
    }

    // =========================================================
    // 资源状态
    // =========================================================
    void D3D12Texture::Transition(D3D12Context& ctx, D3D12_RESOURCE_STATES to)
    {
        if (!resource || state == to)
            return;
        ctx.Barrier(resource.Get(), state, to);
        state = to;
    }

    void D3D12Buffer::Transition(D3D12Context& ctx, D3D12_RESOURCE_STATES to)
    {
        if (!resource)
            return;
        // 上一条命令列表执行完后缓冲已退回 COMMON；从 COMMON 出发由驱动隐式提升，不用屏障
        if (epoch != ctx.GetEpoch())
        {
            state = D3D12_RESOURCE_STATE_COMMON;
            epoch = ctx.GetEpoch();
        }
        if (state == to)
            return;
        if (state != D3D12_RESOURCE_STATE_COMMON)
            ctx.Barrier(resource.Get(), state, to);
        state = to;
    }

    // =========================================================
    // 创建
    // =========================================================
    D3D12Context::D3D12Context() = default;

    D3D12Context::~D3D12Context()
    {
        if (m_queue && m_fence && m_list)
        {
            // 丢弃还没提交的命令，等 GPU 执行完已提交的，之后成员才能安全释放
            m_list->Close();
            const uint64_t value = ++m_lastSignaled;
            if (SUCCEEDED(m_queue->Signal(m_fence.Get(), value)))
                WaitForFence(value);
        }
        if (m_fenceEvent)
            CloseHandle(m_fenceEvent);
    }

    void D3D12Context::Initialize(const Options& options)
    {
        CreateDevice(options);

        D3D12_COMMAND_QUEUE_DESC qd = {};
        qd.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
        D3D12ThrowIfFailed(m_device->CreateCommandQueue(&qd, IID_PPV_ARGS(m_queue.GetAddressOf())), "CreateCommandQueue");
        D3D12ThrowIfFailed(m_device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(m_fence.GetAddressOf())), "CreateFence");
        m_fenceEvent = CreateEventW(nullptr, FALSE, FALSE, nullptr);
        if (!m_fenceEvent)
            D3D12ThrowIfFailed(HRESULT_FROM_WIN32(GetLastError()), "CreateEvent");

        m_srvIncrement = m_device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
        m_rtvHeap.increment = m_device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
        m_dsvHeap.increment = m_device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_DSV);
        GrowSrvHeap(kSrvInitial);

        OpenList();
    }

    void D3D12Context::CreateDevice(const Options& options)
    {
        UINT factoryFlags = 0;
        if (options.debug)
        {
            ComPtr<ID3D12Debug> debug;
            if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(debug.GetAddressOf()))))
            {
                debug->EnableDebugLayer();
                factoryFlags |= DXGI_CREATE_FACTORY_DEBUG;
            }
        }
        D3D12ThrowIfFailed(CreateDXGIFactory2(factoryFlags, IID_PPV_ARGS(m_factory.GetAddressOf())), "CreateDXGIFactory2");

        ComPtr<IDXGIAdapter1> adapter;
        if (options.warp)
        {
            D3D12ThrowIfFailed(m_factory->EnumWarpAdapter(IID_PPV_ARGS(adapter.GetAddressOf())), "EnumWarpAdapter");
        }
        else
        {
            // 与 D3D11 相同：高性能显卡优先，跳过软件适配器；第一块能建 11_0 设备的就用它
            auto usable = [](IDXGIAdapter1* a)
            {
                DXGI_ADAPTER_DESC1 desc;
                a->GetDesc1(&desc);
                return !(desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE)
                    && SUCCEEDED(D3D12CreateDevice(a, D3D_FEATURE_LEVEL_11_0, __uuidof(ID3D12Device), nullptr));
            };
            ComPtr<IDXGIFactory6> factory6;
            if (SUCCEEDED(m_factory.As(&factory6)))
            {
                for (UINT i = 0; SUCCEEDED(factory6->EnumAdapterByGpuPreference(i, DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE, IID_PPV_ARGS(adapter.ReleaseAndGetAddressOf()))); ++i)
                    if (usable(adapter.Get()))
                        break;
                    else
                        adapter.Reset();
            }
            if (!adapter)
            {
                for (UINT i = 0; SUCCEEDED(m_factory->EnumAdapters1(i, adapter.ReleaseAndGetAddressOf())); ++i)
                    if (usable(adapter.Get()))
                        break;
                    else
                        adapter.Reset();
            }
            if (!adapter)
                throw std::runtime_error("D3D12: 没有支持 D3D12 的显卡");
        }

        D3D12ThrowIfFailed(D3D12CreateDevice(adapter.Get(), D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(m_device.GetAddressOf())), "D3D12CreateDevice");
        m_warp = options.warp;

        DXGI_ADAPTER_DESC1 desc;
        if (SUCCEEDED(adapter->GetDesc1(&desc)))
            m_adapterName = ToUtf8(desc.Description);

        if (options.debug)
        {
            ComPtr<ID3D12InfoQueue1> info;
            DWORD cookie = 0;
            if (SUCCEEDED(m_device.As(&info)) && SUCCEEDED(info->RegisterMessageCallback(OnDebugMessage, D3D12_MESSAGE_CALLBACK_FLAG_NONE, nullptr, &cookie)))
                std::fprintf(stderr, "D3D12 调试层已打开：警告与错误输出到 stderr（以 [D3D12] 开头）\n");
        }
    }

    // =========================================================
    // 提交
    // =========================================================
    void D3D12Context::OpenList()
    {
        // 复用 GPU 已执行完的分配器，没有就新建（稳定后一般只有两三个）
        const uint64_t completed = m_fence->GetCompletedValue();
        m_currentAllocator = {};
        for (size_t i = 0; i < m_allocators.size(); ++i)
        {
            if (m_allocators[i].fence <= completed)
            {
                m_currentAllocator = std::move(m_allocators[i]);
                m_allocators.erase(m_allocators.begin() + static_cast<ptrdiff_t>(i));
                break;
            }
        }
        if (m_currentAllocator.allocator)
            D3D12ThrowIfFailed(m_currentAllocator.allocator->Reset(), "CommandAllocator::Reset");
        else
            D3D12ThrowIfFailed(m_device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(m_currentAllocator.allocator.GetAddressOf())), "CreateCommandAllocator");

        if (m_list)
            D3D12ThrowIfFailed(m_list->Reset(m_currentAllocator.allocator.Get(), nullptr), "CommandList::Reset");
        else
            D3D12ThrowIfFailed(m_device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, m_currentAllocator.allocator.Get(), nullptr,
                                                           IID_PPV_ARGS(m_list.GetAddressOf())), "CreateCommandList");
        ++m_epoch;
    }

    uint64_t D3D12Context::Submit()
    {
        const HRESULT hr = m_list->Close();
        if (SUCCEEDED(hr))
        {
            ID3D12CommandList* lists[] = { m_list.Get() };
            m_queue->ExecuteCommandLists(1, lists);
        }
        else
        {
            std::fprintf(stderr, "D3D12: 命令列表关闭失败 (HRESULT 0x%08X)，本次命令丢弃\n", static_cast<unsigned int>(hr));
        }
        const uint64_t value = ++m_lastSignaled;
        m_queue->Signal(m_fence.Get(), value);
        m_currentAllocator.fence = value;
        m_allocators.push_back(std::move(m_currentAllocator));
        if (m_page.resource)
            m_page.fence = value;   // 当前页还没写满，继续给下一条列表用；回收时按最后一次使用的提交算
        OpenList();
        Retire();
        return value;
    }

    void D3D12Context::WaitForFence(uint64_t value)
    {
        if (m_fence->GetCompletedValue() >= value)
            return;
        if (SUCCEEDED(m_fence->SetEventOnCompletion(value, m_fenceEvent)))
            WaitForSingleObject(m_fenceEvent, INFINITE);
    }

    void D3D12Context::WaitIdle()
    {
        WaitForFence(Submit());
        m_transientBytes = 0;
        Retire();
    }

    void D3D12Context::Retire()
    {
        const uint64_t completed = m_fence->GetCompletedValue();
        for (size_t i = 0; i < m_retiredPages.size();)
        {
            if (m_retiredPages[i].fence > completed)
            {
                ++i;
                continue;
            }
            UploadPage page = std::move(m_retiredPages[i]);
            m_retiredPages.erase(m_retiredPages.begin() + static_cast<ptrdiff_t>(i));
            if (page.dedicated)
                continue;           // 一次性的大页、暂存区：直接释放
            page.offset = 0;
            m_freePages.push_back(std::move(page));
        }
        while (!m_deferred.empty() && m_deferred.front().fence <= completed)
            m_deferred.pop_front();
        while (!m_srvPending.empty() && m_srvPending.front().first <= completed)
        {
            m_srvFree.push_back(m_srvPending.front().second);
            m_srvPending.pop_front();
        }
    }

    void D3D12Context::EndFrame()
    {
        ++m_frame;
        for (UploadPage& p : m_staging)
        {
            if (p.resource)
            {
                m_transientBytes += p.size;
                m_retiredPages.push_back(std::move(p));
            }
            p = {};
        }
        if (m_transientBytes > 0)
        {
            WaitForFence(m_lastSignaled);
            m_transientBytes = 0;
        }
        Retire();
        // 空闲太久的上传页归还系统（大图纸、大界面过后不长期占着几 MB 的上传内存），留一页默认大小的备用
        bool keptOne = false;
        for (size_t i = m_freePages.size(); i-- > 0;)
        {
            UploadPage& p = m_freePages[i];
            const bool idle = m_frame - p.lastUsed > kIdleFrames;
            if (!keptOne && p.size == kPageSize)
            {
                keptOne = true;
                continue;
            }
            if (idle)
                m_freePages.erase(m_freePages.begin() + static_cast<ptrdiff_t>(i));
        }
        // 当前页是一次大分配撑大的、又很久没用：换下来，下次分配时用默认大小的页
        if (m_page.resource && m_page.size > kPageSize && m_frame - m_page.lastUsed > kIdleFrames)
        {
            m_retiredPages.push_back(std::move(m_page));
            m_page = {};
        }
    }

    void D3D12Context::ReleaseFreeMemory()
    {
        WaitIdle();
        if (m_page.resource && m_page.size > kPageSize)
            m_page = {};
        m_freePages.clear();
        m_deferred.clear();
        // 分配器的内存只增不减：空闲的全部丢掉，下次需要时再建
        const uint64_t completed = m_fence->GetCompletedValue();
        std::erase_if(m_allocators, [completed](const Allocator& a) { return a.fence <= completed; });
    }

    void D3D12Context::Defer(ComPtr<IUnknown> object)
    {
        if (!object)
            return;
        ComPtr<ID3D12Resource> resource;
        if (SUCCEEDED(object.As(&resource)))
        {
            const D3D12_RESOURCE_DESC desc = resource->GetDesc();
            const uint64_t bytes = desc.Dimension == D3D12_RESOURCE_DIMENSION_BUFFER ? desc.Width : desc.Width * desc.Height * 4;
            if (bytes > kMaxPooledPage)
                m_transientBytes += bytes;
        }
        m_deferred.push_back({ m_lastSignaled + 1, std::move(object) });
    }

    // =========================================================
    // 上传缓冲
    // =========================================================
    D3D12Context::UploadPage D3D12Context::CreateUploadPage(uint64_t size, bool dedicated)
    {
        UploadPage page;
        D3D12_HEAP_PROPERTIES heap = { D3D12_HEAP_TYPE_UPLOAD };
        D3D12_RESOURCE_DESC   desc = {};
        desc.Dimension        = D3D12_RESOURCE_DIMENSION_BUFFER;
        desc.Width            = size;
        desc.Height           = 1;
        desc.DepthOrArraySize = 1;
        desc.MipLevels        = 1;
        desc.SampleDesc.Count = 1;
        desc.Layout           = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        D3D12ThrowIfFailed(m_device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_GENERIC_READ,
                                                             nullptr, IID_PPV_ARGS(page.resource.GetAddressOf())), "CreateCommittedResource(上传页)");
        void* cpu = nullptr;
        const D3D12_RANGE noRead = { 0, 0 };
        D3D12ThrowIfFailed(page.resource->Map(0, &noRead, &cpu), "Map(上传页)");
        page.cpu       = static_cast<uint8_t*>(cpu);
        page.gpu       = page.resource->GetGPUVirtualAddress();
        page.size      = size;
        page.dedicated = dedicated;
        return page;
    }

    D3D12Context::Upload D3D12Context::AllocateUpload(uint64_t size, uint64_t alignment)
    {
        size = std::max<uint64_t>(size, 1);
        if (size > kMaxPooledPage)
        {
            UploadPage page = CreateUploadPage(AlignUp(size, 64 * 1024), true);
            page.offset = size;
            page.fence  = m_lastSignaled + 1;
            const Upload up{ page.cpu, page.gpu, page.resource.Get(), 0 };
            m_transientBytes += page.size;
            m_retiredPages.push_back(std::move(page));
            return up;
        }

        uint64_t offset = AlignUp(m_page.offset, alignment);
        if (!m_page.resource || offset + size > m_page.size)
        {
            if (m_page.resource)
            {
                m_page.fence = m_lastSignaled + 1;
                m_retiredPages.push_back(std::move(m_page));
                m_page = {};
            }
            // 空闲池里够大的最小一页；没有就新建（默认 1 MB，大的取 2 的幂，便于以后复用）
            size_t best = m_freePages.size();
            for (size_t i = 0; i < m_freePages.size(); ++i)
                if (m_freePages[i].size >= size && (best == m_freePages.size() || m_freePages[i].size < m_freePages[best].size))
                    best = i;
            if (best < m_freePages.size())
            {
                m_page = std::move(m_freePages[best]);
                m_freePages.erase(m_freePages.begin() + static_cast<ptrdiff_t>(best));
            }
            else
            {
                m_page = CreateUploadPage(std::max(kPageSize, NextPow2(size)), false);
            }
            offset = 0;
        }
        m_page.lastUsed = m_frame;
        m_page.offset   = offset + size;
        return { m_page.cpu + offset, m_page.gpu + offset, m_page.resource.Get(), offset };
    }

    D3D12Context::UploadPage& D3D12Context::NextStagingPage()
    {
        // 轮流用：写这一页时其他页的复制在 GPU 上进行（每次提交有零点几毫秒的延迟，页数少了盖不住）
        UploadPage& page = m_staging[m_stagingNext];
        m_stagingNext = (m_stagingNext + 1) % static_cast<int>(std::size(m_staging));
        if (!page.resource)
            page = CreateUploadPage(kMaxPooledPage, true);
        WaitForFence(page.fence);
        Retire();               // 复制开始前换下的旧缓冲此时已可释放
        return page;
    }

    void D3D12Context::UploadBuffer(D3D12Buffer& dst, uint64_t dstOffset, const void* data, uint64_t size)
    {
        if (size == 0)
            return;
        if (size > kMaxPooledPage)
        {
            const uint8_t* src = static_cast<const uint8_t*>(data);
            for (uint64_t done = 0; done < size;)
            {
                UploadPage& page = NextStagingPage();
                const uint64_t n = std::min(page.size, size - done);
                std::memcpy(page.cpu, src + done, n);
                dst.Transition(*this, D3D12_RESOURCE_STATE_COPY_DEST);
                m_list->CopyBufferRegion(dst.resource.Get(), dstOffset + done, page.resource.Get(), 0, n);
                page.fence = Submit();
                done += n;
            }
            return;
        }
        const Upload up = AllocateUpload(size, 16);
        std::memcpy(up.cpu, data, size);
        dst.Transition(*this, D3D12_RESOURCE_STATE_COPY_DEST);
        m_list->CopyBufferRegion(dst.resource.Get(), dstOffset, up.resource, up.offset, size);
    }

    void D3D12Context::UploadTexture(D3D12Texture& dst, int x, int y, int width, int height, const void* pixels, int pitch)
    {
        if (width <= 0 || height <= 0)
            return;
        const uint64_t rowBytes = static_cast<uint64_t>(width) * 4;
        const uint64_t rowPitch = AlignUp(rowBytes, D3D12_TEXTURE_DATA_PITCH_ALIGNMENT);
        // rows 行像素写到 cpu，再录一条从 resource + offset 复制到纹理第 firstRow 行起的命令
        auto copyRows = [&](uint8_t* cpu, ID3D12Resource* resource, uint64_t offset, int firstRow, int rows)
        {
            for (int r = 0; r < rows; ++r)
            {
                uint8_t* out = cpu + rowPitch * static_cast<uint64_t>(r);
                if (pixels)
                    std::memcpy(out, static_cast<const uint8_t*>(pixels) + static_cast<size_t>(pitch) * static_cast<size_t>(firstRow + r), rowBytes);
                else
                    std::memset(out, 0, rowBytes);
            }
            D3D12_TEXTURE_COPY_LOCATION src = {};
            src.pResource                          = resource;
            src.Type                               = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
            src.PlacedFootprint.Offset             = offset;
            src.PlacedFootprint.Footprint.Format   = DXGI_FORMAT_R8G8B8A8_UNORM;
            src.PlacedFootprint.Footprint.Width    = static_cast<UINT>(width);
            src.PlacedFootprint.Footprint.Height   = static_cast<UINT>(rows);
            src.PlacedFootprint.Footprint.Depth    = 1;
            src.PlacedFootprint.Footprint.RowPitch = static_cast<UINT>(rowPitch);
            D3D12_TEXTURE_COPY_LOCATION dstLoc = {};
            dstLoc.pResource        = dst.resource.Get();
            dstLoc.Type             = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
            dstLoc.SubresourceIndex = 0;
            dst.Transition(*this, D3D12_RESOURCE_STATE_COPY_DEST);
            m_list->CopyTextureRegion(&dstLoc, static_cast<UINT>(x), static_cast<UINT>(y + firstRow), 0, &src, nullptr);
        };

        if (rowPitch * static_cast<uint64_t>(height) <= kMaxPooledPage)
        {
            const Upload up = AllocateUpload(rowPitch * static_cast<uint64_t>(height), D3D12_TEXTURE_DATA_PLACEMENT_ALIGNMENT);
            copyRows(up.cpu, up.resource, up.offset, 0, height);
        }
        else
        {
            // 大图像：按行分块经暂存区复制（同 UploadBuffer）
            const int rowsPerChunk = static_cast<int>(std::max<uint64_t>(1, kMaxPooledPage / rowPitch));
            for (int row = 0; row < height; row += rowsPerChunk)
            {
                UploadPage& page = NextStagingPage();
                copyRows(page.cpu, page.resource.Get(), 0, row, std::min(rowsPerChunk, height - row));
                page.fence = Submit();
            }
        }
        dst.Transition(*this, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    }

    // =========================================================
    // 资源
    // =========================================================
    D3D12Buffer D3D12Context::CreateBuffer(uint64_t size)
    {
        D3D12Buffer buffer;
        D3D12_HEAP_PROPERTIES heap = { D3D12_HEAP_TYPE_DEFAULT };
        D3D12_RESOURCE_DESC   desc = {};
        desc.Dimension        = D3D12_RESOURCE_DIMENSION_BUFFER;
        desc.Width            = std::max<uint64_t>(size, 1);
        desc.Height           = 1;
        desc.DepthOrArraySize = 1;
        desc.MipLevels        = 1;
        desc.SampleDesc.Count = 1;
        desc.Layout           = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        if (SUCCEEDED(m_device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_COMMON,
                                                        nullptr, IID_PPV_ARGS(buffer.resource.GetAddressOf()))))
            buffer.size = size;
        return buffer;
    }

    std::unique_ptr<D3D12Texture> D3D12Context::CreateTexture(int width, int height, DXGI_FORMAT format,
                                                              D3D12_RESOURCE_FLAGS flags, D3D12_RESOURCE_STATES initial,
                                                              const D3D12_CLEAR_VALUE* clear)
    {
        D3D12_HEAP_PROPERTIES heap = { D3D12_HEAP_TYPE_DEFAULT };
        D3D12_RESOURCE_DESC   desc = {};
        desc.Dimension        = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        desc.Width            = static_cast<UINT64>(width);
        desc.Height           = static_cast<UINT>(height);
        desc.DepthOrArraySize = 1;
        desc.MipLevels        = 1;
        desc.Format           = format;
        desc.SampleDesc.Count = 1;
        desc.Flags            = flags;

        auto texture = std::make_unique<D3D12Texture>();
        if (FAILED(m_device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc, initial, clear,
                                                     IID_PPV_ARGS(texture->resource.GetAddressOf()))))
            return nullptr;
        texture->state  = initial;
        texture->width  = width;
        texture->height = height;
        if (!(flags & D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL))
            texture->srv = AllocateSrv(texture->resource.Get(), nullptr);
        return texture;
    }

    void D3D12Context::ReleaseTexture(std::unique_ptr<D3D12Texture>& texture)
    {
        if (!texture)
            return;
        Defer(texture->resource);
        if (texture->srv != D3D12Texture::kNoSrv)
            FreeSrv(texture->srv);
        texture.reset();
    }

    void D3D12Context::Barrier(ID3D12Resource* resource, D3D12_RESOURCE_STATES before, D3D12_RESOURCE_STATES after)
    {
        D3D12_RESOURCE_BARRIER b = {};
        b.Type                   = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        b.Transition.pResource   = resource;
        b.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        b.Transition.StateBefore = before;
        b.Transition.StateAfter  = after;
        m_list->ResourceBarrier(1, &b);
    }

    // =========================================================
    // 描述符
    // =========================================================
    void D3D12Context::GrowSrvHeap(uint32_t minCapacity)
    {
        uint32_t capacity = std::max(m_srvCapacity, kSrvInitial);
        while (capacity < minCapacity)
            capacity *= 2;

        D3D12_DESCRIPTOR_HEAP_DESC desc = {};
        desc.Type           = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
        desc.NumDescriptors = capacity;
        ComPtr<ID3D12DescriptorHeap> cpu, gpu;
        D3D12ThrowIfFailed(m_device->CreateDescriptorHeap(&desc, IID_PPV_ARGS(cpu.GetAddressOf())), "CreateDescriptorHeap(SRV)");
        desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
        D3D12ThrowIfFailed(m_device->CreateDescriptorHeap(&desc, IID_PPV_ARGS(gpu.GetAddressOf())), "CreateDescriptorHeap(SRV, 着色器可见)");

        if (m_srvCpu && m_srvNext > 0)
        {
            m_device->CopyDescriptorsSimple(m_srvNext, cpu->GetCPUDescriptorHandleForHeapStart(), m_srvCpu->GetCPUDescriptorHandleForHeapStart(), desc.Type);
            m_device->CopyDescriptorsSimple(m_srvNext, gpu->GetCPUDescriptorHandleForHeapStart(), cpu->GetCPUDescriptorHandleForHeapStart(), desc.Type);
        }
        Defer(m_srvGpu);            // 已录制的命令可能还引用旧堆
        m_srvCpu      = cpu;
        m_srvGpu      = gpu;
        m_srvCapacity = capacity;
    }

    uint32_t D3D12Context::AllocateSrv(ID3D12Resource* resource, const D3D12_SHADER_RESOURCE_VIEW_DESC* desc)
    {
        uint32_t slot;
        if (!m_srvFree.empty())
        {
            slot = m_srvFree.back();
            m_srvFree.pop_back();
        }
        else
        {
            if (m_srvNext >= m_srvCapacity)
                GrowSrvHeap(m_srvNext + 1);
            slot = m_srvNext++;
        }
        D3D12_CPU_DESCRIPTOR_HANDLE cpu = m_srvCpu->GetCPUDescriptorHandleForHeapStart();
        D3D12_CPU_DESCRIPTOR_HANDLE gpu = m_srvGpu->GetCPUDescriptorHandleForHeapStart();
        cpu.ptr += static_cast<SIZE_T>(slot) * m_srvIncrement;
        gpu.ptr += static_cast<SIZE_T>(slot) * m_srvIncrement;
        m_device->CreateShaderResourceView(resource, desc, cpu);
        m_device->CopyDescriptorsSimple(1, gpu, cpu, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
        return slot;
    }

    void D3D12Context::FreeSrv(uint32_t slot)
    {
        m_srvPending.emplace_back(m_lastSignaled + 1, slot);
    }

    D3D12_GPU_DESCRIPTOR_HANDLE D3D12Context::GetSrvGpu(uint32_t slot) const
    {
        D3D12_GPU_DESCRIPTOR_HANDLE h = m_srvGpu->GetGPUDescriptorHandleForHeapStart();
        h.ptr += static_cast<UINT64>(slot) * m_srvIncrement;
        return h;
    }

    void D3D12Context::BindSrvHeap()
    {
        if (m_srvBoundEpoch == m_epoch && m_srvBoundHeap == m_srvGpu.Get())
            return;
        ID3D12DescriptorHeap* heaps[] = { m_srvGpu.Get() };
        m_list->SetDescriptorHeaps(1, heaps);
        m_srvBoundEpoch = m_epoch;
        m_srvBoundHeap  = m_srvGpu.Get();
    }

    D3D12_CPU_DESCRIPTOR_HANDLE D3D12Context::AllocateCpu(CpuHeap& heap)
    {
        if (heap.free.empty())
        {
            D3D12_DESCRIPTOR_HEAP_DESC desc = {};
            desc.Type           = heap.type;
            desc.NumDescriptors = kCpuHeapBlock;
            ComPtr<ID3D12DescriptorHeap> block;
            D3D12ThrowIfFailed(m_device->CreateDescriptorHeap(&desc, IID_PPV_ARGS(block.GetAddressOf())), "CreateDescriptorHeap(RTV/DSV)");
            D3D12_CPU_DESCRIPTOR_HANDLE h = block->GetCPUDescriptorHandleForHeapStart();
            for (UINT i = kCpuHeapBlock; i-- > 0;)
                heap.free.push_back({ h.ptr + static_cast<SIZE_T>(i) * heap.increment });
            heap.blocks.push_back(std::move(block));
        }
        const D3D12_CPU_DESCRIPTOR_HANDLE h = heap.free.back();
        heap.free.pop_back();
        return h;
    }

    D3D12_CPU_DESCRIPTOR_HANDLE D3D12Context::AllocateRtv()                      { return AllocateCpu(m_rtvHeap); }
    void D3D12Context::FreeRtv(D3D12_CPU_DESCRIPTOR_HANDLE handle)               { if (handle.ptr) m_rtvHeap.free.push_back(handle); }
    D3D12_CPU_DESCRIPTOR_HANDLE D3D12Context::AllocateDsv()                      { return AllocateCpu(m_dsvHeap); }
    void D3D12Context::FreeDsv(D3D12_CPU_DESCRIPTOR_HANDLE handle)               { if (handle.ptr) m_dsvHeap.free.push_back(handle); }

    // =========================================================
    // 着色器
    // =========================================================
    ComPtr<ID3DBlob> D3D12Context::CompileShader(const char* source, const char* entry, const char* target, unsigned flags)
    {
        ComPtr<ID3DBlob> blob, errors;
        const HRESULT hr = D3DCompile(source, std::strlen(source), nullptr, nullptr, nullptr, entry, target, flags, 0,
                                      blob.GetAddressOf(), errors.GetAddressOf());
        if (FAILED(hr))
        {
            std::string msg = std::string("D3D12: 着色器编译失败 ") + entry + " (" + target + ")";
            if (errors)
                msg += std::string("\n") + static_cast<const char*>(errors->GetBufferPointer());
            throw std::runtime_error(msg);
        }
        return blob;
    }
}
