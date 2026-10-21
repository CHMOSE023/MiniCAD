#pragma once
#include "Solid/ISolidKernel.h"
#include <memory>

// SMLib（3rd/SMLib）实现的 ISolidKernel。只有 SMLibKernel.cpp 包含 SMLib 的头文件。
// SMLib 静态库不能并发使用：内核方法与实体析构在内部持有同一把进程级锁，多个线程只能轮流使用（节点图的后台求值线程与界面线程）。
// 整个进程只应创建一个内核对象。
namespace MiniCAD::Solid
{
    std::unique_ptr<ISolidKernel> CreateSMLibKernel();
}
