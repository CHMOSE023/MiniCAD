#pragma once
#include <string>
#include <vector>

namespace MiniCAD
{
    struct FileDialogFilter
    {
        std::string name;
        std::vector<std::string> extensions;     // 不带点；"*" = 所有文件
    };

    struct FileDialogRequest
    {
        enum class Mode { Open, OpenMultiple, Folder, Save };
        Mode mode = Mode::Open;
        std::string title;
        std::string parent;                      // xdg-desktop-portal 的父窗口标识（"x11:<十六进制>"）
        std::string currentName;                 // 保存时的默认文件名
        std::vector<FileDialogFilter> filters;
    };

    struct FileDialogResult
    {
        std::vector<std::string> paths;          // 空 = 取消
        int filter = -1;                         // 所选筛选器的下标；对话框不报告时为 -1
        std::string error;                       // 不为空 = 对话框打不开
    };

    // 阻塞到用户关闭对话框，在后台线程上调用：先 xdg-desktop-portal（D-Bus），没有时 zenity
    FileDialogResult RunFileDialog(const FileDialogRequest& request);

    bool LaunchDetached(const std::vector<std::string>& argv);   // xdg-open 等
}
