#pragma once
#include <memory>
#include <string>

#include "Text/Font/FontEngine.h"

namespace MiniCAD
{
    class FontSystem
    {
    public:
        enum class State
        {
            Uninitialized,
            Initializing,
            Ready,
            Shutdown
        };

    public:
        FontSystem() = default;
        ~FontSystem()
        {
            Shutdown();
        }

        // =========================
        // 生命周期
        // =========================

        void Initialize()
        {
            if (m_state != State::Uninitialized)
                return;

            m_state = State::Initializing;

            InitFontEngine();
            InitGlobalStyles();
            InitRenderState();

            m_state = State::Ready;
        }

        void Shutdown()
        {
            if (m_state == State::Shutdown)
                return;

            m_fontEngine.reset();

            m_state = State::Shutdown;
        }

        bool IsReady() const
        {
            return m_state == State::Ready;
        }

        // =========================
        // Subsystems Access
        // =========================

        FontEngine& GetFontEngine()
        {
            EnsureReady();
            return *m_fontEngine;
        }

        const FontEngine& GetFontEngine() const
        {
            EnsureReady();
            return *m_fontEngine;
        }

        // =========================
        // Lazy Loading API
        // =========================

        void PreloadDefaultFonts()
        {
            EnsureReady();

            // SHX fallback
            // m_fontEngine->LoadFont("simplex", "simplex.shx");

            // TTF fallback
            // m_fontEngine->LoadFont("arial", "arial.ttf");
        }

        // =========================
        // Global Settings
        // =========================

        double GetDefaultTextHeight() const { return m_defaultTextHeight; }
        void   SetDefaultTextHeight(double h) { m_defaultTextHeight = h; }

    private:
        // =========================
        // Init steps
        // =========================

        void InitFontEngine()
        {
            m_fontEngine = std::make_unique<FontEngine>();

            // 不在这里加载字体（避免启动卡顿）
            // 改为 lazy / Preload
        }

        void InitGlobalStyles()
        {
            m_defaultTextHeight = 1.0;
        }

        IFont& Resolve(const FontStyle& style);

        void InitRenderState()
        {
            // reserved: DPI, lineweight scale, etc.
        }

        void EnsureReady() const
        {
            if (m_state != State::Ready)
                throw std::runtime_error("CoreContext not initialized");
        }

    private:
        State m_state = State::Uninitialized;

        // =========================
        // Subsystems
        // =========================

        std::unique_ptr<FontEngine> m_fontEngine;

        // =========================
        // Global parameters
        // =========================

        double m_defaultTextHeight = 1.0;
    };
}
