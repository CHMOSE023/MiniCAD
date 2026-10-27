#pragma once
// WebGPU 后端共用的小工具：只用标准 webgpu.h（网页版经 Emscripten 的 emdawnwebgpu 绑定）
#include <webgpu/webgpu.h>
#include <cstdint>
#include <cstring>
#include <vector>

namespace WebGPUCommon
{
    inline WGPUStringView Str(const char* s)
    {
        return WGPUStringView{ s, s ? std::strlen(s) : 0 };
    }

    inline uint64_t Align(uint64_t v, uint64_t a)
    {
        return (v + a - 1) / a * a;
    }

    inline WGPUShaderModule CreateShader(WGPUDevice device, const char* wgsl, const char* label)
    {
        WGPUShaderSourceWGSL source = WGPU_SHADER_SOURCE_WGSL_INIT;
        source.code = Str(wgsl);
        WGPUShaderModuleDescriptor desc = WGPU_SHADER_MODULE_DESCRIPTOR_INIT;
        desc.nextInChain = &source.chain;
        desc.label       = Str(label);
        return wgpuDeviceCreateShaderModule(device, &desc);
    }

    inline WGPUBuffer CreateBuffer(WGPUDevice device, uint64_t size, WGPUBufferUsage usage)
    {
        WGPUBufferDescriptor desc = WGPU_BUFFER_DESCRIPTOR_INIT;
        desc.size  = Align(size < 4 ? 4 : size, 4);
        desc.usage = usage | WGPUBufferUsage_CopyDst;
        return wgpuDeviceCreateBuffer(device, &desc);
    }

    inline void DestroyBuffer(WGPUBuffer& buffer)
    {
        if (!buffer)
            return;
        wgpuBufferDestroy(buffer);      // 立即归还显存，不等垃圾回收
        wgpuBufferRelease(buffer);
        buffer = nullptr;
    }

    inline void DestroyTexture(WGPUTexture& texture)
    {
        if (!texture)
            return;
        wgpuTextureDestroy(texture);
        wgpuTextureRelease(texture);
        texture = nullptr;
    }

    // 帧内换下来的资源：已录进还没提交的命令里，提交之后才能销毁（销毁后再提交会报校验错误）
    struct RetireList
    {
        std::vector<WGPUBuffer>      buffers;
        std::vector<WGPUTexture>     textures;
        std::vector<WGPUTextureView> views;
        std::vector<WGPUBindGroup>   bindGroups;

        void Flush()
        {
            for (WGPUBuffer& b : buffers)        DestroyBuffer(b);
            for (WGPUTexture& t : textures)      DestroyTexture(t);
            for (WGPUTextureView v : views)      wgpuTextureViewRelease(v);
            for (WGPUBindGroup g : bindGroups)   wgpuBindGroupRelease(g);
            buffers.clear();
            textures.clear();
            views.clear();
            bindGroups.clear();
        }
    };
}
