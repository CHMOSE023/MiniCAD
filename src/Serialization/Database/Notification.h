#pragma once
#include <functional>
#include <string_view>

namespace MiniDWG
{
    enum class NotificationType
    {
        Info,
        Warning,    // 可以继续：跳过了不支持的对象、数据有小问题等
        Error,      // 当前对象或段读写失败
    };

    // 读写过程中的消息回调（对应 ACadSharp 的 NotificationEventHandler）。
    // 库本身不打印日志，由宿主接到自己的日志系统。
    using NotificationHandler = std::function<void(NotificationType type, std::string_view message)>;
}
