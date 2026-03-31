#pragma once
#include "InputEvent.h"

namespace MiniCAD
{
    // 实现此接口的模块可以加入输入责任链 
    class IInputHandler
    {
    public:
        virtual ~IInputHandler() = default;
        /// <summary>
        /// 消息
        /// </summary>
        /// <param name="e">InputEvent</param>
        /// <returns> true  = 消费了这条消息，链条终止, false = 未消费，继续向下传递</returns>
        virtual bool OnInput(const InputEvent& e) = 0;
    };
}