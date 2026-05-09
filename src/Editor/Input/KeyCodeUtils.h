#pragma once
#include <Windows.h>
#include "KeyCode.h"

namespace MiniCAD
{
    KeyCode FromWin32Key(WPARAM vk);
}