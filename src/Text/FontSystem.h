#pragma once
#include <memory>
#include <string>
#include <unordered_map>
#include <stdexcept>
#include "Text/Font/SHXCompositeFont.h"
#include "Text/Font/FontEngine.h"

namespace MiniCAD
{
    class FontSystem
    {
    public:
        using FontStyleId = FontStyle::FontStyleId;

        // Built-in style IDs
        static constexpr FontStyleId kStandardStyleId = 1;

        enum class State
        {
            Uninitialized,
            Initializing,
            Ready,
            Shutdown
        };

    public:
        FontSystem() = default;
        ~FontSystem() { Shutdown(); }

        // --- Lifecycle ---
        void Initialize()
        {
            if (m_state != State::Uninitialized)
                return;

            m_state = State::Initializing;
            InitFontEngine();
            InitDefaultStyles();
            m_state = State::Ready;  
        }

        void Shutdown()
        {
            if (m_state == State::Shutdown)
                return;

            m_fontEngine.reset();
            m_state = State::Shutdown;
        }

        bool IsReady() const { return m_state == State::Ready; }

        // --- Subsystem access ---

        FontEngine& GetFontEngine()
        {
            EnsureReady();
            return *m_fontEngine;
        }

        // --- Style registry ---

        // Register a new style; assigns an ID if style.id == 0. Returns the assigned ID.
        FontStyleId RegisterStyle(FontStyle style)
        {
            EnsureReady();  
            style.id = m_nextStyleId++;
            m_stylesByName[style.name] = style.id; 
            m_styles[style.id]         = std::move(style);  
            return style.id;
        }

        const FontStyle* FindStyle(const std::string& name) const
        {
            auto it = m_stylesByName.find(name);
            if (it == m_stylesByName.end()) return nullptr;
            return FindStyle(it->second);
        }

        const FontStyle* FindStyle(FontStyleId id) const
        {
            auto it = m_styles.find(id);
            return it != m_styles.end() ? &it->second : nullptr;
        }

        // Resolve IFont for a registered style (lazy-loads via FontEngine).
        IFont& ResolveFont(FontStyleId id)
        {
            EnsureReady();
            const FontStyle* style = FindStyle(id);
            if (!style)
                throw std::runtime_error("FontStyle ID not registered");
            return ResolveFont(*style);
        }

        IFont& ResolveFont(const FontStyle& style)
        {
            EnsureReady();  
            return m_fontEngine->Resolve(style);
        }

        /// <summary>
        /// 获取探索者字体
        /// </summary> 
        IFont* GetTssdSHXCompositeFont()
        {
            return m_shxCompositeFont.get();           
        }

        // --- Global parameters ---
        double GetDefaultTextHeight() const { return m_defaultTextHeight; }
        void   SetDefaultTextHeight(double h) { m_defaultTextHeight = h; }

        // --- Lazy preload --- 
        // 加载探索者字体
        void PreloadDefaultFonts()
        {
            EnsureReady();
            auto mainFont = std::make_shared<SHXFont>("tssdeng", "fonts/tssdeng.shx", /*fontId*/ 100); // 英文
            auto bigFont  = std::make_shared<SHXFont>("tssdchn", "fonts/TSSDCHN.SHX", /*fontId*/ 101); // 中文
            m_shxCompositeFont = std::make_unique<SHXCompositeFont>("tssdeng+tssdchn", mainFont, bigFont, /*fontId*/ 200); // 英文 + 中文
        }

    private:

        void InitFontEngine()
        {
            m_fontEngine = std::make_unique<FontEngine>();
        }

        void InitDefaultStyles()
        {
            m_defaultTextHeight = 1.0;
            m_nextStyleId       = kStandardStyleId + 1;
             
            FontStyle standard;
            standard.id       = kStandardStyleId;
            standard.name     = "Standard";
            standard.fontFile = "simplex.shx";
            standard.isShx    = true;

            m_styles[standard.id]         = standard;
            m_stylesByName[standard.name] = standard.id;
        }

        void EnsureReady() const
        {
            if (m_state != State::Ready)
            {
                throw std::runtime_error("FontSystem not initialized");
            }
        }

    private:
        State m_state = State::Uninitialized;

        FontStyleId                                   m_nextStyleId = kStandardStyleId + 1;  
        std::unique_ptr<FontEngine>                   m_fontEngine;          
        std::unordered_map<FontStyleId, FontStyle>    m_styles;
        std::unordered_map<std::string, FontStyleId>  m_stylesByName;
        std::unique_ptr<SHXCompositeFont>             m_shxCompositeFont;

        double m_defaultTextHeight = 1.0;
    };
}
