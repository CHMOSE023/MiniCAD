#include "KeyCodeUtils.h"

namespace MiniCAD
{
    KeyCode FromWin32Key(WPARAM vk)
    { 
        // A-Z 
        if (vk >= 'A' && vk <= 'Z')
        {
            return static_cast<KeyCode>(  (int)KeyCode::A + (vk - 'A'));
        }
         
        // 0-9 
        if (vk >= '0' && vk <= '9')
        {
            return static_cast<KeyCode>(  (int)KeyCode::Num0 + (vk - '0'));
        }

        switch (vk)
        {
            // 基础控制
        case VK_ESCAPE:      return KeyCode::Escape;
        case VK_RETURN:      return KeyCode::Enter;
        case VK_TAB:         return KeyCode::Tab;
        case VK_BACK:        return KeyCode::Backspace;
        case VK_SPACE:       return KeyCode::Space;
        case VK_DELETE:      return KeyCode::Delete;
        case VK_INSERT:      return KeyCode::Insert;
                             
            // 方向键         
        case VK_LEFT:        return KeyCode::Left;
        case VK_RIGHT:       return KeyCode::Right;
        case VK_UP:          return KeyCode::Up;
        case VK_DOWN:        return KeyCode::Down;

            // 功能键 F1-F12
        case VK_F1:          return KeyCode::F1;
        case VK_F2:          return KeyCode::F2;
        case VK_F3:          return KeyCode::F3;
        case VK_F4:          return KeyCode::F4;
        case VK_F5:          return KeyCode::F5;
        case VK_F6:          return KeyCode::F6;
        case VK_F7:          return KeyCode::F7;
        case VK_F8:          return KeyCode::F8;
        case VK_F9:          return KeyCode::F9;
        case VK_F10:         return KeyCode::F10;
        case VK_F11:         return KeyCode::F11;
        case VK_F12:         return KeyCode::F12;
             
            // 修饰键（建议只记录 Left/Right 或合并）
        case VK_SHIFT:       return KeyCode::Shift;
        case VK_CONTROL:     return KeyCode::Ctrl;
        case VK_MENU:        return KeyCode::Alt;
                             
        case VK_LSHIFT:      return KeyCode::LShift;
        case VK_RSHIFT:      return KeyCode::RShift;
                             
        case VK_LCONTROL:    return KeyCode::LCtrl;
        case VK_RCONTROL:    return KeyCode::RCtrl;
                             
        case VK_LMENU:       return KeyCode::LAlt;
        case VK_RMENU:       return KeyCode::RAlt;

            // 编辑键区
        case VK_HOME:        return KeyCode::Home;
        case VK_END:         return KeyCode::End;
        case VK_PRIOR:       return KeyCode::PageUp;
        case VK_NEXT:        return KeyCode::PageDown;

            // 符号键（OEM）
        case VK_OEM_MINUS:   return KeyCode::OemMinus;
        case VK_OEM_PLUS:    return KeyCode::OemPlus;
        case VK_OEM_COMMA:   return KeyCode::OemComma;
        case VK_OEM_PERIOD:  return KeyCode::OemPeriod;
        case VK_OEM_2:       return KeyCode::OemSlash;
        case VK_OEM_5:       return KeyCode::OemBackslash;
        case VK_OEM_1:       return KeyCode::OemSemicolon;
        case VK_OEM_7:       return KeyCode::OemQuotes;
        case VK_OEM_4:       return KeyCode::OemOpenBracket;
        case VK_OEM_6:       return KeyCode::OemCloseBracket;
        case VK_OEM_3:       return KeyCode::OemGrave;

        default:
            return KeyCode::Unknown;
        }
    }
}