#include "LineEntity.hpp"
#include <memory>
#include <string>
#include <stdexcept>

namespace MiniCAD
{
    class ObjectFactory
    {
    public:
        // 根据序列化器创建对象
        static std::unique_ptr<Object> CreateFromSerializer(ISerializer& s)
        {
            // 读取类型名
            std::string type = s.ReadString();

            if (type == "LineEntity")
            {
                // 创建一个临时对象（ID 后续设置）
                auto obj = std::make_unique<LineEntity>(0, XMFLOAT3{}, XMFLOAT3{});
                obj->Deserialize(s);
                return obj;
            }

            // TODO: 其他类型可以在这里扩展
            throw std::runtime_error("Unknown object type: " + type);
        }
    };
}
