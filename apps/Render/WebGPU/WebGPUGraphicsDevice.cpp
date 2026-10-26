#include "WebGPU/WebGPUGraphicsDevice.h"
#include "WebGPU/WebGPUBackend.h"
#include "WebGPU/WebGPURenderer.h"
#include "WebGPU/WebGPURenderTarget.h"
#include "WebGPU/WebGPUCommon.h"
#include "Core/Log.h"
#include <emscripten.h>
#include <cstring>

namespace MiniCAD
{
    using namespace WebGPUCommon;

    namespace
    {
        EM_JS(int, MiniCADWebGPU_HasNavigatorGpu, (), {
            return (typeof navigator !== "undefined" && navigator.gpu) ? 1 : 0;
        });

        std::string ToString(WGPUStringView s)
        {
            if (!s.data)
                return {};
            return std::string(s.data, s.length == WGPU_STRLEN ? std::strlen(s.data) : s.length);
        }

        // 异步创建过程中的状态：每个回调接手所有权，失败时释放已取到的对象
        struct PendingCreate
        {
            std::string                             selector;
            int                                     width  = 0;
            int                                     height = 0;
            WebGPUGraphicsDevice::CreatedFunc       done;
            WGPUInstance                            instance = nullptr;
            WGPUAdapter                             adapter  = nullptr;

            void Fail(const char* what, WGPUStringView message)
            {
                LOG_WARN("WebGPU：%s（%s）", what, ToString(message).c_str());
                if (adapter)  wgpuAdapterRelease(adapter);
                if (instance) wgpuInstanceRelease(instance);
                adapter  = nullptr;
                instance = nullptr;
                done(nullptr);
            }
        };

        void OnUncapturedError(WGPUDevice const*, WGPUErrorType type, WGPUStringView message, void*, void*)
        {
            LOG_ERROR("WebGPU 错误（类型 %d）：%s", static_cast<int>(type), ToString(message).c_str());
        }

        void OnDeviceLost(WGPUDevice const*, WGPUDeviceLostReason reason, WGPUStringView message, void*, void*)
        {
            // 设备丢失后不恢复（与原型相同），只记日志；正常销毁不算
            if (reason != WGPUDeviceLostReason_Destroyed && reason != WGPUDeviceLostReason_CallbackCancelled)
                LOG_ERROR("WebGPU 设备丢失（原因 %d）：%s", static_cast<int>(reason), ToString(message).c_str());
        }

        void OnDevice(WGPURequestDeviceStatus status, WGPUDevice device, WGPUStringView message, void* userdata, void*)
        {
            std::unique_ptr<PendingCreate> p(static_cast<PendingCreate*>(userdata));
            if (status != WGPURequestDeviceStatus_Success || !device)
            {
                p->Fail("取不到设备", message);
                return;
            }
            auto graphics = std::make_unique<WebGPUGraphicsDevice>(p->instance, p->adapter, device);   // 接手三者的引用
            p->instance = nullptr;
            p->adapter  = nullptr;
            if (!graphics->Initialize(NativeWindow{ const_cast<char*>(p->selector.c_str()) }, p->width, p->height))
            {
                LOG_WARN("WebGPU：画布表面创建失败");
                graphics.reset();
            }
            p->done(std::move(graphics));
        }

        void OnAdapter(WGPURequestAdapterStatus status, WGPUAdapter adapter, WGPUStringView message, void* userdata, void*)
        {
            std::unique_ptr<PendingCreate> p(static_cast<PendingCreate*>(userdata));
            if (status != WGPURequestAdapterStatus_Success || !adapter)
            {
                p->Fail("取不到适配器", message);
                return;
            }
            p->adapter = adapter;

            // 按适配器的上限申请缓冲与纹理尺寸：几百万顶点的图纸要很大的顶点缓冲（默认上限 256 MB）
            WGPULimits supported = WGPU_LIMITS_INIT;
            WGPULimits required  = WGPU_LIMITS_INIT;
            if (wgpuAdapterGetLimits(adapter, &supported) == WGPUStatus_Success)
            {
                required.maxBufferSize         = supported.maxBufferSize;
                required.maxTextureDimension2D = supported.maxTextureDimension2D;
            }

            WGPUDeviceDescriptor desc = WGPU_DEVICE_DESCRIPTOR_INIT;
            desc.label          = Str("MiniCAD");
            desc.requiredLimits = &required;
            desc.uncapturedErrorCallbackInfo.callback = OnUncapturedError;
            desc.deviceLostCallbackInfo.mode     = WGPUCallbackMode_AllowSpontaneous;
            desc.deviceLostCallbackInfo.callback = OnDeviceLost;

            WGPURequestDeviceCallbackInfo cb = WGPU_REQUEST_DEVICE_CALLBACK_INFO_INIT;
            cb.mode      = WGPUCallbackMode_AllowSpontaneous;     // 浏览器的 Promise 完成时直接回调，不用轮询
            cb.callback  = OnDevice;
            cb.userdata1 = p.release();
            wgpuAdapterRequestDevice(adapter, &desc, cb);
        }
    }

    bool WebGPUGraphicsDevice::IsAvailable()
    {
        return MiniCADWebGPU_HasNavigatorGpu() != 0;
    }

    void WebGPUGraphicsDevice::CreateAsync(const NativeWindow& window, int width, int height, CreatedFunc done)
    {
        if (!IsAvailable())
        {
            LOG_INFO("WebGPU：浏览器没有 navigator.gpu");
            done(nullptr);
            return;
        }
        auto p = std::make_unique<PendingCreate>();
        p->selector = window.handle ? static_cast<const char*>(window.handle) : "#canvas";
        p->width    = width;
        p->height   = height;
        p->done     = std::move(done);
        p->instance = wgpuCreateInstance(nullptr);
        if (!p->instance)
        {
            p->Fail("创建实例失败", Str(""));
            return;
        }

        WGPURequestAdapterOptions options = WGPU_REQUEST_ADAPTER_OPTIONS_INIT;
        options.powerPreference = WGPUPowerPreference_HighPerformance;
        WGPURequestAdapterCallbackInfo cb = WGPU_REQUEST_ADAPTER_CALLBACK_INFO_INIT;
        cb.mode     = WGPUCallbackMode_AllowSpontaneous;
        cb.callback = OnAdapter;
        WGPUInstance instance = p->instance;
        cb.userdata1 = p.release();
        wgpuInstanceRequestAdapter(instance, &options, cb);
    }

    WebGPUGraphicsDevice::WebGPUGraphicsDevice(WGPUInstance instance, WGPUAdapter adapter, WGPUDevice device)
        : m_instance(instance), m_adapter(adapter), m_device(device)
    {
    }

    WebGPUGraphicsDevice::~WebGPUGraphicsDevice()
    {
        if (m_pass)
        {
            wgpuRenderPassEncoderEnd(m_pass);
            wgpuRenderPassEncoderRelease(m_pass);
        }
        if (m_encoder)      wgpuCommandEncoderRelease(m_encoder);
        if (m_frameView)    wgpuTextureViewRelease(m_frameView);
        if (m_frameTexture) wgpuTextureRelease(m_frameTexture);
        m_renderer.reset();
        m_backend.reset();
        if (m_surface)
        {
            wgpuSurfaceUnconfigure(m_surface);
            wgpuSurfaceRelease(m_surface);
        }
        if (m_queue)    wgpuQueueRelease(m_queue);
        if (m_device)
        {
            wgpuDeviceDestroy(m_device);
            wgpuDeviceRelease(m_device);
        }
        if (m_adapter)  wgpuAdapterRelease(m_adapter);
        if (m_instance) wgpuInstanceRelease(m_instance);
    }

    bool WebGPUGraphicsDevice::Initialize(const NativeWindow& window, int width, int height)
    {
        const char* selector = window.handle ? static_cast<const char*>(window.handle) : "#canvas";
        WGPUEmscriptenSurfaceSourceCanvasHTMLSelector source = WGPU_EMSCRIPTEN_SURFACE_SOURCE_CANVAS_HTML_SELECTOR_INIT;
        source.selector = Str(selector);
        WGPUSurfaceDescriptor sd = WGPU_SURFACE_DESCRIPTOR_INIT;
        sd.nextInChain = &source.chain;
        m_surface = wgpuInstanceCreateSurface(m_instance, &sd);
        if (!m_surface)
            return false;

        // 画布的首选格式（Chrome 一般是 BGRA8Unorm）排在第一个
        WGPUSurfaceCapabilities caps = WGPU_SURFACE_CAPABILITIES_INIT;
        if (wgpuSurfaceGetCapabilities(m_surface, m_adapter, &caps) == WGPUStatus_Success && caps.formatCount > 0)
            m_format = caps.formats[0];
        wgpuSurfaceCapabilitiesFreeMembers(caps);

        m_queue  = wgpuDeviceGetQueue(m_device);
        m_width  = width;
        m_height = height;
        Configure();

        m_backend  = std::make_unique<MiniGUI::WebGPUBackend>(m_device, m_queue, m_format);
        m_renderer = std::make_unique<WebGPURenderer>(m_device, m_queue);

        // 关于对话框显示的名称：浏览器给的厂商与架构（可能为空，取决于浏览器的隐私策略）
        WGPUAdapterInfo info = WGPU_ADAPTER_INFO_INIT;
        if (wgpuAdapterGetInfo(m_adapter, &info) == WGPUStatus_Success)
        {
            std::string detail = ToString(info.vendor);
            const std::string arch = ToString(info.architecture);
            if (!arch.empty())
                detail += (detail.empty() ? "" : " ") + arch;
            if (!detail.empty())
                m_name = "WebGPU (" + detail + ")";
            wgpuAdapterInfoFreeMembers(info);
        }
        return true;
    }

    void WebGPUGraphicsDevice::Configure()
    {
        WGPUSurfaceConfiguration config = WGPU_SURFACE_CONFIGURATION_INIT;
        config.device      = m_device;
        config.format      = m_format;
        config.usage       = WGPUTextureUsage_RenderAttachment;
        config.width       = static_cast<uint32_t>(m_width);
        config.height      = static_cast<uint32_t>(m_height);
        config.alphaMode   = WGPUCompositeAlphaMode_Opaque;      // 不与页面背景混合
        config.presentMode = WGPUPresentMode_Fifo;
        wgpuSurfaceConfigure(m_surface, &config);
    }

    void WebGPUGraphicsDevice::Resize(int width, int height)
    {
        if (width == m_width && height == m_height)
            return;
        m_width  = width;
        m_height = height;
        Configure();
    }

    void WebGPUGraphicsDevice::BeginUiPass()
    {
        WGPUSurfaceTexture surfaceTexture = WGPU_SURFACE_TEXTURE_INIT;
        wgpuSurfaceGetCurrentTexture(m_surface, &surfaceTexture);
        if (surfaceTexture.status != WGPUSurfaceGetCurrentTextureStatus_SuccessOptimal &&
            surfaceTexture.status != WGPUSurfaceGetCurrentTextureStatus_SuccessSuboptimal)
        {
            // 这一帧不画（界面后端没有通道时 Render 什么也不做）；下次按当前尺寸重新配置
            if (surfaceTexture.texture)
                wgpuTextureRelease(surfaceTexture.texture);
            Configure();
            return;
        }
        m_frameTexture = surfaceTexture.texture;
        m_frameView    = wgpuTextureCreateView(m_frameTexture, nullptr);
        m_encoder      = wgpuDeviceCreateCommandEncoder(m_device, nullptr);

        WGPURenderPassColorAttachment color = WGPU_RENDER_PASS_COLOR_ATTACHMENT_INIT;
        color.view       = m_frameView;
        color.loadOp     = WGPULoadOp_Clear;
        color.storeOp    = WGPUStoreOp_Store;
        color.clearValue = WGPUColor{ 0.1, 0.1, 0.1, 1.0 };
        WGPURenderPassDescriptor desc = WGPU_RENDER_PASS_DESCRIPTOR_INIT;
        desc.colorAttachmentCount = 1;
        desc.colorAttachments     = &color;
        m_pass = wgpuCommandEncoderBeginRenderPass(m_encoder, &desc);

        m_backend->BeginFrame(m_pass, static_cast<int>(wgpuTextureGetWidth(m_frameTexture)),
                              static_cast<int>(wgpuTextureGetHeight(m_frameTexture)));
    }

    void WebGPUGraphicsDevice::Present()
    {
        if (!m_pass)
            return;
        wgpuRenderPassEncoderEnd(m_pass);
        wgpuRenderPassEncoderRelease(m_pass);
        m_pass = nullptr;
        WGPUCommandBuffer commands = wgpuCommandEncoderFinish(m_encoder, nullptr);
        wgpuQueueSubmit(m_queue, 1, &commands);
        wgpuCommandBufferRelease(commands);
        wgpuCommandEncoderRelease(m_encoder);
        m_encoder = nullptr;
        m_backend->EndFrame();

        // 呈现由浏览器在本次 requestAnimationFrame 回调返回后完成
        wgpuTextureViewRelease(m_frameView);
        wgpuTextureRelease(m_frameTexture);
        m_frameView    = nullptr;
        m_frameTexture = nullptr;
    }

    IRenderer& WebGPUGraphicsDevice::GetRenderer()
    {
        return *m_renderer;
    }

    MiniGUI::IRenderBackend& WebGPUGraphicsDevice::GetUiBackend()
    {
        return *m_backend;
    }

    std::unique_ptr<IRenderTarget> WebGPUGraphicsDevice::CreateRenderTarget()
    {
        return std::make_unique<WebGPURenderTarget>(m_device);
    }

    MiniGUI::TextureId WebGPUGraphicsDevice::RegisterViewportTexture(void* nativeShaderResource)
    {
        return m_backend->RegisterExternalTexture(static_cast<WGPUTextureView>(nativeShaderResource));
    }
}
