#pragma once 
#include "Editor/Tools/ITool.h"
#include "Editor/Overlay/Overlay.h"
#include "Editor/Picking/Picking.h"
#include "Editor/Viewport/Viewport.h"
#include "Editor/Snap/SnapEngine.h"
#include "Editor/Snap/SnapResult.h" 
#include "Editor/Grip/GripEditor.h"
#include "Editor/Input/InputEvent.h"
#include "Scene/Scene.h"
#include "Document/CommandStack/CommandStack.h" 
#include "Core/GeomKernel/Line.hpp"
#include "Core/Object/Object.hpp"
#include <unordered_set> 
#include <memory>
#include <vector>
namespace MiniCAD
{
	class EditorContext
	{
	public:
		EditorContext(Scene&        scene, 
			          CommandStack& cmdStack, 
			          Viewport&     viewport,
			          Overlay&      overlay, 
			          Picking&      picking, 
			          SnapEngine&   snap, 
			          SnapResult&   currentSnap);

		bool OnInput(const InputEvent& e);

		// Picking 获取选择 
		const std::unordered_set<Object::ObjectID>& GetSelection();
		const std::unordered_set<Object::ObjectID>& GetHovered();

		// 获取选择
        Object*               GetPrimarySelectedObject();  // 获取选中单个
        std::vector<Object*>  GetSelectedObjects();        // 获取选中多个 
		 
		// ── 获取夹点 ───────────────────────────────────────
		GripEditor& GetGipEditor()        { return m_gripEditor; };
		const Line& GetAnchorLine() const { return m_anchorLine; };
		const bool  IsAcitveTool()  const { return m_tool != nullptr; };
		 
		// ── 工具 ───────────────────────────────────────
		void StartLineTool     ();                                 // 绘制线
		void StartPointTool    ();                                 // 绘制点
		void StartRectangleTool() { printf("绘制矩形\r\n"    ); };  // 绘制矩形
		void StartCircleTool   () { printf("绘制圆\r\n"      ); };  // 绘制圆
		void StartArcTool      () { printf("绘制圆弧\r\n"    ); };  // 绘制圆弧
		void StartEllipseTool  () { printf("绘制椭圆\r\n"    ); };  // 绘制椭圆
		void StartPolylineTool () { printf("绘制多段线\r\n"  ); };  // 绘制多段线
		void StartSplineTool   () { printf("绘制样条曲线\r\n"); };  // 绘制样条曲线
		 
		void StartCopyTool     () { printf("编辑 复制\r\n"); };     // 复制
		void StartMoveTool     () { printf("编辑 移动\r\n"); };     // 移动
		void StartMirrorTool   () { printf("编辑 镜像\r\n"); };     // 镜像
		void StartRotateTool   () { printf("编辑 旋转\r\n"); };     // 旋转 

		// ── 删除选中实体 ──────────────────────────────────
		void DeleteSelected();  
		    
		// ── 正交 ───────────────────────────────────────
		bool TryGetAnchor(DirectX::XMFLOAT3& out) const;
		bool IsOrthoEnabled() const;
		void SetOrthoEnabled(bool enabled);
		void ToggleOrtho(); 
		 
		// ── 捕捉 ───────────────────────────────────────
		bool IsSnapEnabled();
		void SetSnapEnabled(bool enabled);
		void ToggleSnap();
		 
		// ── Undo / Redo / Command  ─────────────────────
		void Undo();
		void Redo();
		void ExecuteCommand(std::unique_ptr<ICommand> cmd); 
	
	private:
		bool          HandleGlobal(const InputEvent& e);
		bool          HandleDefault(const InputEvent& e); 
		bool          ShouldSnap() const;                     // 允许捕获 
		void          UpdateSnap(const InputEvent& e);        // 更新捕获点currentSnap
		InputEvent    InjectSnap(const InputEvent& e);        // 注入捕获点 
		InputEvent    ApplyConstraints(const InputEvent& e);  // 约束事件

	private:
		std::unique_ptr<ITool>   m_tool;
		Scene&                   m_scene;            
		CommandStack&            m_cmdStack;		   
		Viewport&                m_viewport;		   
		Overlay&                 m_overlay; 
		Picking&                 m_picking;  
		SnapEngine&              m_snap;
		SnapResult&              m_currentSnap;  
		GripEditor               m_gripEditor;           // 新增 
					             
		Line                     m_anchorLine;           // 约束辅助线
		bool                     m_snapEnabled  = true;  
		bool                     m_orthoEnabled = false; // 正交 

	};
}