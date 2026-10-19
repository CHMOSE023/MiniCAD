#pragma once
#include "Solid/ISolidKernel.h"
#include <memory>

// SMLib（3rd/SMLib）实现的 ISolidKernel。只有 SMLibKernel.cpp 包含 SMLib 的头文件。
// SMLib 静态库只能在一个线程里使用：整个进程只应创建并使用一个内核对象，且都在同一线程。
namespace MiniCAD::Solid
{
    std::unique_ptr<ISolidKernel> CreateSMLibKernel();
}
