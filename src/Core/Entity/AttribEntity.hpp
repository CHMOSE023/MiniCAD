#pragma once
#include "Entity.hpp"
#include "Core/Math/Point3.hpp"
#include <string>
#include <cmath>

namespace MiniCAD
{
    // DXF 属性标志(ATTDEF/ATTRIB 组码 70)位掩码。
    enum class AttribFlags : uint16_t
    {
        None      = 0,
        Invisible = 1,   // 不可见(绘制时跳过)
        Constant  = 2,   // 常量(块内固定值,INSERT 不提示)
        Verify    = 4,   // 插入时校验
        Preset    = 8,   // 插入时使用默认值,不提示
    };

    inline uint16_t operator&(AttribFlags a, AttribFlags b) { return static_cast<uint16_t>(a) & static_cast<uint16_t>(b); }
    inline AttribFlags operator|(AttribFlags a, AttribFlags b) { return static_cast<AttribFlags>(static_cast<uint16_t>(a) | static_cast<uint16_t>(b)); }

    // 属性定义/实例公共数据与绘制。AttDefEntity(ATTDEF)位于块定义内,描述可填字段;
    // AttribEntity(ATTRIB)是 INSERT 实例化后随块引用一起出现的具体取值。
    // 二者结构高度一致,用同一基类承载 tag/文本/位置/标志,派生类区分语义字段。
    class AttribBase : public Entity
    {
    public:
        // ── 标识/文本 ────────────────────────────────────────────────────────
        const std::string& GetTag()  const { return m_tag; }    // 组码 2,标签(不含空格)
        void               SetTag(std::string t) { m_tag = std::move(t); }

        const std::string& GetText() const { return m_text; }   // 组码 1,显示文本
        void               SetText(std::string v) { m_text = std::move(v); }

        // ── 几何/排版(同 TEXT)────────────────────────────────────────────────
        const Math::Point3& GetPosition() const { return m_position; } // 组码 10
        void                SetPosition(const Math::Point3& p) { m_position = p; }

        double GetHeight() const   { return m_height; }         // 组码 40
        void   SetHeight(double h) { m_height = h; }

        double GetRotation() const   { return m_rotation; }     // 组码 50(弧度)
        void   SetRotation(double r) { m_rotation = r; }

        // ── 标志 ─────────────────────────────────────────────────────────────
        uint16_t GetFlags() const     { return m_flags; }       // 组码 70
        void     SetFlags(uint16_t f) { m_flags = f; }
        bool     IsInvisible() const  { return (m_flags & static_cast<uint16_t>(AttribFlags::Invisible)) != 0; }
        bool     IsConstant()  const  { return (m_flags & static_cast<uint16_t>(AttribFlags::Constant))  != 0; }

        AABB GetBoundingBox() const override
        {
            const double approxChars = static_cast<double>(m_text.size()) / 1.5;
            const double w = m_height * 0.6 * approxChars;
            return AABB(
                { m_position.x,     m_position.y - m_height * 0.2, m_position.z },
                { m_position.x + w, m_position.y + m_height,        m_position.z });
        }

        void Draw(IDrawSink& sink, bool isSelected, bool isHovered) const override
        {
            if (IsInvisible()) return;

            const auto& attr = GetAttr();
            const Math::Color4& col = isSelected ? IDrawSink::kSelectionColor
                                    : isHovered  ? IDrawSink::kHoverColor
                                                 : ResolveDrawColor(sink);
            // 属性绘制取值文本;空文本(纯定义未填值)显示标签作占位提示。
            const std::string& shown = m_text.empty() ? m_tag : m_text;
            sink.EmitMText(m_position, shown, 0, m_height, m_rotation, 0.0, col);
        }

    protected:
        AttribBase(ObjectID id, std::string tag, std::string text,
                   const Math::Point3& pos, double height, double rotation)
            : Entity(id)
            , m_tag(std::move(tag))
            , m_text(std::move(text))
            , m_position(pos)
            , m_height(height)
            , m_rotation(rotation)
        {}

        // 派生类 Clone 复用:把公共字段拷给已构造的目标对象。
        void CopyCommonTo(AttribBase& dst) const
        {
            dst.SetAttr(GetAttr());
            dst.m_flags = m_flags;
        }

        std::string  m_tag;
        std::string  m_text;
        Math::Point3 m_position{ 0, 0, 0 };
        double       m_height   = 2.5;
        double       m_rotation = 0.0;
        uint16_t     m_flags    = static_cast<uint16_t>(AttribFlags::None);

        DECLARE_RUNTIME_TYPE(AttribBase, Entity)
    };

    // ── 属性定义 ATTDEF(块定义内)─────────────────────────────────────────────
    class AttDefEntity : public AttribBase
    {
    public:
        AttDefEntity(ObjectID id, std::string tag, std::string defaultValue,
                     const Math::Point3& pos, double height = 2.5, double rotation = 0.0)
            : AttribBase(id, std::move(tag), std::move(defaultValue), pos, height, rotation)
        {}

        // 提示语(组码 3),仅 ATTDEF 拥有。
        const std::string& GetPrompt() const { return m_prompt; }
        void               SetPrompt(std::string p) { m_prompt = std::move(p); }

        // 默认值是基类 m_text(ATTDEF 的「文本」即默认值)。
        const std::string& GetDefaultValue() const { return m_text; }
        void               SetDefaultValue(std::string v) { m_text = std::move(v); }

        std::unique_ptr<Entity> Clone(ObjectID newId) const override
        {
            auto e = std::make_unique<AttDefEntity>(newId, m_tag, m_text, m_position, m_height, m_rotation);
            CopyCommonTo(*e);
            e->m_prompt = m_prompt;
            return e;
        }

        DECLARE_RUNTIME_TYPE(AttDefEntity, AttribBase)

    private:
        std::string m_prompt;   // 组码 3,插入时提示语
    };

    // ── 属性实例 ATTRIB(随 INSERT)─────────────────────────────────────────────
    class AttribEntity : public AttribBase
    {
    public:
        AttribEntity(ObjectID id, std::string tag, std::string value,
                     const Math::Point3& pos, double height = 2.5, double rotation = 0.0)
            : AttribBase(id, std::move(tag), std::move(value), pos, height, rotation)
        {}

        // 取值即基类 m_text。
        const std::string& GetValue() const { return m_text; }
        void               SetValue(std::string v) { m_text = std::move(v); }

        std::unique_ptr<Entity> Clone(ObjectID newId) const override
        {
            auto e = std::make_unique<AttribEntity>(newId, m_tag, m_text, m_position, m_height, m_rotation);
            CopyCommonTo(*e);
            return e;
        }

        DECLARE_RUNTIME_TYPE(AttribEntity, AttribBase)
    };
}
