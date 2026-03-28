#include"Render/Viewport/Grid.h"
#include"Render/D3D11/Renderer.h"
#include"Core/Object/Object.hpp"
#include"Core/Entity/LineEntity.hpp"
#include <vector>
namespace MiniCAD
{ 
	class Scene
	{ 
	public:
		Scene(float width, float height)
		{
			m_camera = std::make_unique<Camera>(width, height);
			 
			m_grid   = std::make_unique<Grid>(); 

			m_grid->Generate(XMFLOAT3());
		} 

		void AddEntity(std::shared_ptr<Object> e)
		{
			if (e) m_entities.push_back(e);
		}

		void Draw(Renderer* renderer)
		{ 
			renderer->DrawGrad(*m_grid); // *m_grid 返回 Grid&
			 
			for (auto& obj :m_entities)
			{  
				if (obj->IsKindOf<LineEntity>())
				{
					auto lineEntity = static_cast<LineEntity*>(obj.get());
					const auto& line = lineEntity->GetLine();
					renderer->DrawLine(line.Start, line.End, lineEntity->GetAttr().Color);
				} 

			}
		}

		Camera* GetCamera() const { return m_camera.get(); }
		Grid*   GetGrid()   const { return m_grid.get(); }
	private:  
		std::unique_ptr<Grid>    m_grid   = nullptr;
		std::unique_ptr<Camera>  m_camera = nullptr; 

		std::vector<std::shared_ptr<Object>> m_entities;
	};
}