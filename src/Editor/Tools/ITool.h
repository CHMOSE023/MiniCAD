#pragma once 
#include "Editor/Input/InputEvent.h"
#include "Core/Math/Point3.hpp"
#include <functional>
using namespace DirectX;
namespace MiniCAD
{ 
    class ITool
    {
    public:
        virtual ~ITool() = default; 

        virtual bool     OnInput(const InputEvent& e) = 0;
        virtual void     Cancel()          {}
        virtual void     OnSceneChanged()  {}                 // Undo / Redo / Delete 后
        virtual void     OnFocusLost()     {}                 // 中键平移开始
        virtual void     OnFocusRestored() {}                 // 中键平移结束         
        virtual bool     HasAnchor() const { return false; }  // 是否有“锚点”

        virtual Math::Point3 GetAnchor() const { return {}; }     // 获取锚点（仅在 HasAnchor() == true 时有效）

        std::function<void()> OnFinished;  
    };
}
