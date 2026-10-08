#pragma once
#include <string>

namespace MiniCAD
{
	class Scene;
	class ICommand
	{
	public:
		virtual ~ICommand() = default;
		virtual bool        Execute(Scene& scene) = 0;   // false = 执行失败，不入 Undo 栈
		virtual void        Undo(Scene& scene) = 0;
		virtual std::string GetName() const = 0;
		// false = 只改视图（缩放等），不让文档变成"已修改"
		virtual bool        AffectsDocument() const { return true; }
	};
}
