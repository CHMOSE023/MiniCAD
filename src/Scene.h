 
#include"Grid.h"
#include"Renderer.h"
namespace MiniCAD
{ 
	class Scene
	{ 
	public:
		void SetGrid(const Grid& grid) { m_grid = grid; }
		void Draw(Renderer* renderer)
		{
			// 网格
			renderer->DrawGrad(m_grid);

			//renderer->DrawLine({ 0,0,0 }, { 0.1,0.0,0.0 }, { 1,0,0,1 }); // X
			//renderer->DrawLine({ 0,0,0 }, { 0.0,0.1,0.0 }, { 0,1,0,1 }); // Y
			//renderer->DrawLine({ 0,0,0 }, { 0,0,1 }, { 0,0,1,1 }); // Z

		}
	private:
		Grid m_grid; 
	};
}