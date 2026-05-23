#pragma once
#include "Document.h"
#include "Editor/Editor.h"
#include "Viewport/Viewport.h"
#include "Render/IRenderer.h"
#include <vector>
#include <memory>
#include "Text/FontSystem.h"

namespace MiniCAD
{
    class DocumentManager
    {
    public:
        DocumentManager() = default;

        Document& Create();

        void Close(Document* doc);

        Document* GetActive() const;

        void SetActive(Document* doc);
        void SetFontSystem(FontSystem* fontSystem);

        std::vector<std::unique_ptr<Document>>& GetAll();

        void SetRenderer(IRenderer* renderer);

        // ── Viewport / Editor ────────────────────────────────
        void InitViewport(IRenderer& renderer, float w, float h);

        Viewport&       GetViewport()       { return *m_viewport; }
        const Viewport& GetViewport() const { return *m_viewport; }

        Editor&       GetEditor()       { return m_editor; }
        const Editor& GetEditor() const { return m_editor; }

        // --- 字体样式管理（代理 FontSystem）---

        FontStyle::FontStyleId RegisterFontStyle(FontStyle style);
        const FontStyle*       FindFontStyle(const std::string& name) const;
        const FontStyle*       FindFontStyle(FontStyle::FontStyleId id) const;

        void New();
        void Open();
        void Save();
        void SaveAs();
        void SaveAll();
        void Undo() const;
        void Redo() const;
        void Paste();

        void CopySelected();
    private:
        std::string GenerateUniqueName();

    private:
        std::vector<std::unique_ptr<Document>> m_docs;
        FontSystem*  m_fontSystem    = nullptr;
        Document*    m_active        = nullptr;
        IRenderer*   m_renderer      = nullptr;
        float        m_defaultWidth  = 600.f;
        float        m_defaultHeight = 400.f;

        std::unique_ptr<Viewport> m_viewport;
        Editor                    m_editor;

    private:
        int m_untitledCounter = 0;
    };
}
