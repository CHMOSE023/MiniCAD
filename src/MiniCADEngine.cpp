#include "MiniCADEngine.h"

// 以下头文件随模块迁入后逐步解注：
// #include "Document/DocumentManager.h"
// #include "Viewport/ViewState.h"
// #include "Editor/Input/InputEvent.h"

namespace MiniCADLib
{
    struct MiniCADEngine::Impl
    {
        float viewportW = 800.f;
        float viewportH = 600.f;

        // 模块迁入后在此添加：
        // DocumentManager docManager;
        // Editor          editor;
        // Viewport        viewport;
    };

    MiniCADEngine::MiniCADEngine()
        : m_impl(std::make_unique<Impl>())
    {
    }

    MiniCADEngine::~MiniCADEngine() = default;

    void MiniCADEngine::Init(float viewportW, float viewportH)
    {
        m_impl->viewportW = viewportW;
        m_impl->viewportH = viewportH;
    }

    void MiniCADEngine::Resize(float w, float h)
    {
        m_impl->viewportW = w;
        m_impl->viewportH = h;
    }
}
