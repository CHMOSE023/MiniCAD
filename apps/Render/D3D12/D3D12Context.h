#pragma once
// D3D12 渲染代码共用的设备上下文：设备、命令队列、一条始终打开的命令列表、围栏、
// 上传缓冲（帧内流数据）、着色器可见的 SRV 描述符堆、RTV / DSV 描述符、延迟释放。
// CAD 视口渲染器（D3D12Renderer）与界面后端（D3D12Backend）共用一个上下文，命令录进同一条列表，
// 呈现（或 WaitIdle / 读回像素）时一次提交。见 docs/Render/PLAN.md 阶段 1。

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <Windows.h>
#include <wrl/client.h>
#include <d3d12.h>
#include <dxgi1_6.h>
#include <cstdint>
#include <deque>
#include <memory>
#include <string>
#include <vector>

namespace MiniGUI
{
    void D3D12ThrowIfFailed(HRESULT hr, const char* what);

    class D3D12Context;

    // 带状态跟踪的纹理（界面纹理、光栅图像、渲染目标的颜色缓冲）：资源 + 当前状态 + SRV 描述符槽。
    // IRenderTarget::GetNativeShaderResource 返回的就是它（D3D12Texture*），界面后端按它登记外部纹理
    struct D3D12Texture
    {
        static constexpr uint32_t kNoSrv = 0xFFFFFFFFu;

        Microsoft::WRL::ComPtr<ID3D12Resource> resource;
        D3D12_RESOURCE_STATES state  = D3D12_RESOURCE_STATE_COMMON;
        uint32_t              srv    = kNoSrv;
        int                   width  = 0;
        int                   height = 0;

        // 录一条状态转换（已在该状态时什么都不做）
        void Transition(D3D12Context& ctx, D3D12_RESOURCE_STATES to);
    };

    // 默认堆里的缓冲：每次 ExecuteCommandLists 结束后缓冲的状态自动退回 COMMON，按命令列表的轮次跟踪
    struct D3D12Buffer
    {
        Microsoft::WRL::ComPtr<ID3D12Resource> resource;
        D3D12_RESOURCE_STATES state = D3D12_RESOURCE_STATE_COMMON;
        uint64_t              epoch = 0;
        uint64_t              size  = 0;

        void Transition(D3D12Context& ctx, D3D12_RESOURCE_STATES to);
    };

    class D3D12Context
    {
    public:
        struct Options
        {
            bool warp  = false;     // 用 WARP（微软的软件实现），没有显卡的环境
            bool debug = false;     // 打开调试层（需要系统装有 Graphics Tools）
        };

        D3D12Context();
        ~D3D12Context();

        D3D12Context(const D3D12Context&) = delete;
        D3D12Context& operator=(const D3D12Context&) = delete;

        // 失败时抛 std::runtime_error
        void Initialize(const Options& options);

        ID3D12Device*              GetDevice()  const { return m_device.Get(); }
        IDXGIFactory4*             GetFactory() const { return m_factory.Get(); }
        ID3D12CommandQueue*        GetQueue()   const { return m_queue.Get(); }
        ID3D12GraphicsCommandList* GetList()    const { return m_list.Get(); }
        const std::string&         GetAdapterName() const { return m_adapterName; }
        bool                       IsWarp() const { return m_warp; }

        // 命令列表的轮次：每次提交后加一。录制方据此判断之前设置的管线状态是否已随旧列表失效
        uint64_t GetEpoch() const { return m_epoch; }

        // ── 提交 ────────────────────────────────────────────────
        // 关闭并执行当前命令列表、发出围栏信号，再用另一个分配器重新打开；返回这次的围栏值
        uint64_t Submit();
        void     WaitForFence(uint64_t value);
        void     WaitIdle();                        // 提交并等 GPU 执行完
        // 每帧呈现后调用：回收 GPU 已用完的上传页、分配器与延迟释放的对象；长时间没用的上传页归还系统。
        // 这一帧有超过 4 MB 的一次性上传页或换下了大资源时，先等 GPU 执行完再回收，不让它们多留一帧
        void     EndFrame();
        // 回收一切能回收的（关闭文档后）：等 GPU 空闲，释放延迟对象与全部空闲上传页
        void     ReleaseFreeMemory();

        // 当前命令列表执行完之后才释放（资源可能还被已录制的命令引用）；超过 4 MB 的资源在本帧 EndFrame 时就释放
        void Defer(Microsoft::WRL::ComPtr<IUnknown> object);

        // ── 上传缓冲：帧内的顶点 / 索引 / 纹理数据，GPU 用完（围栏）后整页复用 ──
        struct Upload
        {
            uint8_t*                  cpu      = nullptr;
            D3D12_GPU_VIRTUAL_ADDRESS gpu      = 0;
            ID3D12Resource*           resource = nullptr;
            uint64_t                  offset   = 0;
        };
        // 超过 4 MB 的分配单独建一页，本帧 EndFrame 时释放（界面特别大的一帧；缓存提交走 UploadBuffer 的分块复制）
        Upload AllocateUpload(uint64_t size, uint64_t alignment = 16);

        // 经上传缓冲复制到默认堆的缓冲（录制命令；缓冲转到 COPY_DEST）。
        // 超过 4 MB 的数据（场景静态流可达几百 MB）经 4 页 4 MB 的暂存区轮流分块复制，每块录完就提交、写一页前等它上次的复制执行完：
        // 暂存内存最多 16 MB（帧末释放），换下的旧缓冲在复制途中就能释放。若像 D3D11 那样整块建一个同样大的上传缓冲，
        // 新旧缓冲加暂存区会同时占着，私有内存比 D3D11 多几十 MB，建上传缓冲本身也要十几毫秒
        void UploadBuffer(D3D12Buffer& dst, uint64_t dstOffset, const void* data, uint64_t size);
        // 纹理区域：pitch 为源数据的行字节数，pixels 为空时填零；纹理先转到 COPY_DEST，复制完转到 PIXEL_SHADER_RESOURCE。
        // 大图像同 UploadBuffer 按行分块
        void UploadTexture(D3D12Texture& dst, int x, int y, int width, int height, const void* pixels, int pitch);

        // ── 资源创建 ───────────────────────────────────────────
        D3D12Buffer CreateBuffer(uint64_t size);    // 默认堆，COMMON 状态
        // 2D 纹理（RGBA8 等），带一个 SRV；flags 可含 ALLOW_RENDER_TARGET
        std::unique_ptr<D3D12Texture> CreateTexture(int width, int height, DXGI_FORMAT format,
                                                    D3D12_RESOURCE_FLAGS flags, D3D12_RESOURCE_STATES initial,
                                                    const D3D12_CLEAR_VALUE* clear = nullptr);
        void ReleaseTexture(std::unique_ptr<D3D12Texture>& texture);     // 延迟释放资源与 SRV 槽

        // ── 描述符 ─────────────────────────────────────────────
        // SRV：CPU 端一份 + 着色器可见堆一份（同一槽号）；容量不够时整体换成两倍大的堆
        uint32_t AllocateSrv(ID3D12Resource* resource, const D3D12_SHADER_RESOURCE_VIEW_DESC* desc);
        void     FreeSrv(uint32_t slot);            // 延迟到当前命令列表执行完
        D3D12_GPU_DESCRIPTOR_HANDLE GetSrvGpu(uint32_t slot) const;
        // 绑定着色器可见的 SRV 堆（本轮列表还没绑定、或堆已换过时才真正调用 SetDescriptorHeaps）
        void     BindSrvHeap();

        // RTV / DSV 只在 CPU 端用（录制时就复制进命令列表），释放后马上可以复用
        D3D12_CPU_DESCRIPTOR_HANDLE AllocateRtv();
        void                        FreeRtv(D3D12_CPU_DESCRIPTOR_HANDLE handle);
        D3D12_CPU_DESCRIPTOR_HANDLE AllocateDsv();
        void                        FreeDsv(D3D12_CPU_DESCRIPTOR_HANDLE handle);

        void Barrier(ID3D12Resource* resource, D3D12_RESOURCE_STATES before, D3D12_RESOURCE_STATES after);

        // ── 界面后端画到哪里（宿主在 BeginUiPass 里设置：交换链的后备缓冲区，或截图测试的离屏纹理）──
        struct UiTarget
        {
            D3D12_CPU_DESCRIPTOR_HANDLE rtv    = {};
            int                         width  = 0;
            int                         height = 0;
            DXGI_FORMAT                 format = DXGI_FORMAT_UNKNOWN;
        };
        void            SetUiTarget(const UiTarget& target) { m_uiTarget = target; }
        const UiTarget& GetUiTarget() const { return m_uiTarget; }

        // 编译 HLSL（SM 5.1）；失败抛 std::runtime_error
        static Microsoft::WRL::ComPtr<ID3DBlob> CompileShader(const char* source, const char* entry, const char* target, unsigned flags);

    private:
        struct UploadPage
        {
            Microsoft::WRL::ComPtr<ID3D12Resource> resource;
            uint8_t*                  cpu      = nullptr;
            D3D12_GPU_VIRTUAL_ADDRESS gpu      = 0;
            uint64_t                  size     = 0;
            uint64_t                  offset   = 0;
            uint64_t                  fence    = 0;     // 最后一次使用它的提交
            uint64_t                  lastUsed = 0;     // 最后一次使用时的帧号（空闲太久的页归还系统）
            bool                      dedicated = false; // 超大的一次性上传：用完直接释放，不进空闲池
        };
        struct Deferred
        {
            uint64_t                         fence = 0;
            Microsoft::WRL::ComPtr<IUnknown> object;
        };
        struct Allocator
        {
            Microsoft::WRL::ComPtr<ID3D12CommandAllocator> allocator;
            uint64_t                                       fence = 0;
        };
        struct CpuHeap      // RTV / DSV：按块增长的 CPU 描述符堆
        {
            D3D12_DESCRIPTOR_HEAP_TYPE                              type;
            UINT                                                    increment = 0;
            std::vector<Microsoft::WRL::ComPtr<ID3D12DescriptorHeap>> blocks;
            std::vector<D3D12_CPU_DESCRIPTOR_HANDLE>                free;
        };

        void CreateDevice(const Options& options);
        void OpenList();
        void Retire();                              // 回收已完成的上传页、分配器、延迟对象
        UploadPage& NextStagingPage();              // 大块上传的暂存区轮流用：等这一页上次的复制执行完
        UploadPage CreateUploadPage(uint64_t size, bool dedicated);
        void GrowSrvHeap(uint32_t minCapacity);
        D3D12_CPU_DESCRIPTOR_HANDLE AllocateCpu(CpuHeap& heap);

    private:
        Microsoft::WRL::ComPtr<IDXGIFactory4>             m_factory;
        Microsoft::WRL::ComPtr<ID3D12Device>              m_device;
        Microsoft::WRL::ComPtr<ID3D12CommandQueue>        m_queue;
        Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList> m_list;
        Microsoft::WRL::ComPtr<ID3D12Fence>               m_fence;
        HANDLE                                            m_fenceEvent = nullptr;
        uint64_t m_lastSignaled = 0;    // 最后一次 Signal 的值；当前打开的列表提交时发出 m_lastSignaled + 1
        uint64_t m_epoch        = 1;
        uint64_t m_frame        = 0;
        uint64_t m_transientBytes = 0;  // 本帧超过 4 MB 的一次性上传页与换下的大资源：EndFrame 时等 GPU 执行完再释放
        std::string m_adapterName;
        bool        m_warp = false;

        std::vector<Allocator> m_allocators;        // 空闲的与 GPU 还在用的
        Allocator              m_currentAllocator;

        UploadPage              m_page;             // 正在分配的上传页
        std::deque<UploadPage>  m_retiredPages;     // 已写满、GPU 可能还在读
        std::vector<UploadPage> m_freePages;        // GPU 已用完，可复用
        std::deque<Deferred>    m_deferred;
        UploadPage              m_staging[4];       // 大块上传的暂存区（用到时才建，帧末交给回收）
        int                     m_stagingNext = 0;

        // SRV：CPU 端一份 + 着色器可见堆一份（复制描述符的源必须是 CPU 端的堆）
        Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> m_srvCpu;
        Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> m_srvGpu;
        UINT                                         m_srvIncrement = 0;
        uint32_t                                     m_srvCapacity  = 0;
        uint32_t                                     m_srvNext      = 0;     // 从未用过的槽从这里开始
        std::vector<uint32_t>                        m_srvFree;
        std::deque<std::pair<uint64_t, uint32_t>>    m_srvPending;          // (围栏, 槽)：执行完才能复用
        uint64_t                                     m_srvBoundEpoch = 0;
        ID3D12DescriptorHeap*                        m_srvBoundHeap  = nullptr;

        CpuHeap  m_rtvHeap{ D3D12_DESCRIPTOR_HEAP_TYPE_RTV };
        CpuHeap  m_dsvHeap{ D3D12_DESCRIPTOR_HEAP_TYPE_DSV };
        UiTarget m_uiTarget;
    };
}
