#pragma once
#include "Document.h" 
#include "Render/D3D11/Renderer.h"
#include <vector>
#include <memory>
namespace MiniCAD
{
    class DocumentManager
    {  
    public:
        DocumentManager() = default;

        Document& Create(Renderer& r, float w, float h);

        void Close(Document* doc); 

        Document* GetActive()const { return m_active; } 
         
        void SetActive(Document* doc)
        {
            m_active = doc;
        } 

        std::vector<std::unique_ptr<Document>>& GetAll() { return m_docs; }

        void  SetRenderer(Renderer* renderer) { m_renderer = renderer; }
    public:
        void New()   
        {
            if (!m_renderer)
                return;

            Create(*m_renderer, m_defaultWidth, m_defaultHeight);
        }
        void Open()   { printf("Open\n"); }
        void Save()   { printf("Save\n"); }
		void SaveAs() { printf("Save As\n"); }
		void Undo() const  { GetActive()->Undo(); }
		void Redo() const  { GetActive()->Redo(); }
		void Paste()  { printf("Paste\n"); }

		void CopySelected()   { printf("Copy Selected\n"); }
    private:
        std::string GenerateUniqueName();

    private:
        std::vector<std::unique_ptr<Document>> m_docs; 

        Document* m_active = nullptr;

		Renderer* m_renderer      = nullptr; // 传递给文档创建用的渲染器指针，非拥有关系
		float     m_defaultWidth  = 600.f;
		float     m_defaultHeight = 400.f;
        
    private:
        int m_untitledCounter = 0;
    };
}