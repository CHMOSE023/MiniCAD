#pragma once
#define WIN32_LEAN_AND_MEAN // 减少 windows.h 包含范围
#define NOMINMAX            // 禁止 windows.h 定义 min/max 宏
#include <windows.h>
#include <windowsx.h> 
#include <commctrl.h>
#pragma comment(lib, "comctl32.lib")