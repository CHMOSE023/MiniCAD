#pragma once
// 桌面版宿主强制包含的头文件（/FIpch.h）。图形接口的头文件在 apps/Render 里各自包含，这里不放

#include <vector>
#include <memory>
#include <stdexcept>
#include <cstdio>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <Windows.h>
#include <windowsx.h>
