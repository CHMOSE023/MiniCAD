#pragma once
#include "Document.h" 
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

        Document& Create(IRenderer& r, float w, float h);

        void Close(Document* doc); 

        Document* GetActive()const;
         
        void SetActive(Document* doc);
        void SetFontSystem(FontSystem* fontSystem);

        std::vector<std::unique_ptr<Document>>& GetAll();

        void SetRenderer(IRenderer* renderer);

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
		IRenderer*   m_renderer      = nullptr; // 传递给文档创建用的渲染器指针，非拥有关系
		float        m_defaultWidth  = 600.f;
		float        m_defaultHeight = 400.f;
        
    private:
        int m_untitledCounter = 0;
    };
}