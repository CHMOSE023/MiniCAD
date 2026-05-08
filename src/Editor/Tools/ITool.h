#pragma once 
#include "Editor/Input/InputEvent.h"
#include <functional>
namespace MiniCAD
{ 
    class ITool
    {
    public:
        virtual ~ITool() = default;

        virtual bool OnInput(const InputEvent& e) = 0;

        virtual void Cancel() {}

        std::function<void()> OnFinished;

    public:
        virtual bool HasAnchor() const { return false; }            // 是否有“锚点”
        virtual DirectX::XMFLOAT3 GetAnchor() const { return {}; }  // 获取锚点（仅在 HasAnchor() == true 时有效）
    };
}