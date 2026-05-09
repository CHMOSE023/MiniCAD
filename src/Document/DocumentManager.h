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

        Document* GetActive()const;
         
        void SetActive(Document* doc);

        std::vector<std::unique_ptr<Document>>& GetAll();

        void SetRenderer(Renderer* renderer); 

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

        Document* m_active = nullptr;

		Renderer* m_renderer      = nullptr; // 传递给文档创建用的渲染器指针，非拥有关系
		float     m_defaultWidth  = 600.f;
		float     m_defaultHeight = 400.f;
        
    private:
        int m_untitledCounter = 0;
    };
}