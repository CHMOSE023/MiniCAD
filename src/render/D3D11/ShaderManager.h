// ============================================================
// MiniCAD — render/D3D11/ShaderManager.h
// 职责：VS/PS 编译缓存 / 绑定 / 热重载（Debug 模式）；顶点缓冲池
// 依赖：（无 math 依赖，仅 Win32 / D3D11 前向声明）
// 约束：D3D11 具体类型仅限 D3D11/ 目录内使用
// ============================================================
#pragma once

#include <string>
#include <cstdint>

// 前向声明
struct ID3D11Device;
struct ID3D11DeviceContext;
struct ID3D11VertexShader;
struct ID3D11PixelShader;
struct ID3D11InputLayout;
struct ID3D11Buffer;

namespace MiniCAD {

    // ============================================================
    // ShaderManager — 单例，管理所有 Shader 生命周期
    // ============================================================
    class ShaderManager {
    public:
        static ShaderManager& Instance();

        // 不可拷贝
        ShaderManager(const ShaderManager&)            = delete;
        ShaderManager& operator=(const ShaderManager&) = delete;

        void Initialize(ID3D11Device* device, ID3D11DeviceContext* context);
        void Shutdown();

        // --- Shader 绑定 ---
        void BindLineShader(ID3D11DeviceContext* context);
        void BindFillShader(ID3D11DeviceContext* context);
        void BindHighlightShader(ID3D11DeviceContext* context);

        // --- 顶点缓冲上传并绘制 ---
        // topology 为 D3D11_PRIMITIVE_TOPOLOGY 值（unsigned int）
        void UploadAndDraw( ID3D11Device*         device,
                            ID3D11DeviceContext*  context,
                            const void*           vertexData,
                            uint32_t              vertexCount,
                            uint32_t              vertexStride,
                            unsigned int          topology);

        // --- Debug 热重载 ---
#ifdef _DEBUG
        void HotReload(ID3D11Device* device, ID3D11DeviceContext* context);
#endif

    private:
        ShaderManager() = default;
        ~ShaderManager() = default;

        bool CompileShader(ID3D11Device*        device,
                           const std::string&   srcPath,
                           const std::string&   entryVS,
                           const std::string&   entryPS,
                           ID3D11VertexShader** outVS,
                           ID3D11PixelShader**  outPS,
                           ID3D11InputLayout**  outLayout);

        void EnsureDynamicBuffer(ID3D11Device* device, uint32_t requiredBytes);

        // Line shader
        ID3D11VertexShader* m_lineVS     = nullptr;
        ID3D11PixelShader*  m_linePS     = nullptr;
        ID3D11InputLayout*  m_lineLayout = nullptr;

        // Fill shader
        ID3D11VertexShader* m_fillVS     = nullptr;
        ID3D11PixelShader*  m_fillPS     = nullptr;
        ID3D11InputLayout*  m_fillLayout = nullptr;

        // Highlight shader
        ID3D11VertexShader* m_hlVS     = nullptr;
        ID3D11PixelShader*  m_hlPS     = nullptr;
        ID3D11InputLayout*  m_hlLayout = nullptr;

        // 共享动态顶点缓冲（环形复用）
        ID3D11Buffer*       m_dynamicVB     = nullptr;
        uint32_t            m_dynamicVBSize = 0;

        bool                m_initialized   = false;
    };

} // namespace MiniCAD 
