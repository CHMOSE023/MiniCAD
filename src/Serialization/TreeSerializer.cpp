#include "TreeSerializer.h"
#include "JsonValue.hpp"

namespace MiniCAD
{
using JsonDetail::JsonValue;

TreeSerializer::TreeSerializer()
    : m_root(std::make_unique<JsonValue>())
{
    m_root->type = JsonValue::Type::Object;
    m_stack.push_back(m_root.get());
}

TreeSerializer::~TreeSerializer() = default;

void TreeSerializer::ResetForLoad(std::unique_ptr<JsonValue> root)
{
    m_root = std::move(root);
    m_stack.clear();
    m_stack.push_back(m_root.get());
    m_loading = true;
}

JsonValue* TreeSerializer::Cur() const
{
    return m_stack.empty() ? m_root.get() : m_stack.back();
}

JsonValue* TreeSerializer::WriteSlot(const char* key)
{
    return &Cur()->GetOrAdd(key);
}

const JsonValue* TreeSerializer::ReadSlot(const char* key) const
{
    return Cur()->Find(key);
}

bool TreeSerializer::BeginObject(const char* key)
{
    if (m_loading)
    {
        JsonValue* v = Cur()->Find(key);
        if (!v || v->type != JsonValue::Type::Object) return false;
        m_stack.push_back(v);
        return true;
    }
    JsonValue* v = WriteSlot(key);
    v->type = JsonValue::Type::Object;
    m_stack.push_back(v);
    return true;
}

void TreeSerializer::EndObject()
{
    if (m_stack.size() > 1) m_stack.pop_back();
}

bool TreeSerializer::BeginArray(const char* key, size_t& count)
{
    if (m_loading)
    {
        JsonValue* v = Cur()->Find(key);
        if (!v || v->type != JsonValue::Type::Array) { count = 0; return false; }
        count = v->arr.size();
        m_stack.push_back(v);
        return true;
    }
    JsonValue* v = WriteSlot(key);
    v->type = JsonValue::Type::Array;
    m_stack.push_back(v);
    return true;
}

void TreeSerializer::EndArray()
{
    if (m_stack.size() > 1) m_stack.pop_back();
}

bool TreeSerializer::BeginElement(size_t index)
{
    JsonValue* a = Cur();
    if (a->type != JsonValue::Type::Array) return false;
    if (m_loading)
    {
        if (index >= a->arr.size()) return false;
        m_stack.push_back(&a->arr[index]);
        return true;
    }
    a->arr.emplace_back();
    a->arr.back().type = JsonValue::Type::Object;
    m_stack.push_back(&a->arr.back());
    return true;
}

void TreeSerializer::EndElement()
{
    if (m_stack.size() > 1) m_stack.pop_back();
}

void TreeSerializer::Value(const char* key, bool& v)
{
    if (m_loading)
    {
        const JsonValue* j = ReadSlot(key);
        if (j && j->type == JsonValue::Type::Bool) v = j->b;
        return;
    }
    JsonValue* j = WriteSlot(key);
    j->type = JsonValue::Type::Bool;
    j->b = v;
}

void TreeSerializer::Value(const char* key, int32_t& v)
{
    int64_t t = v;
    Value(key, t);
    v = static_cast<int32_t>(t);
}

void TreeSerializer::Value(const char* key, uint32_t& v)
{
    uint64_t t = v;
    Value(key, t);
    v = static_cast<uint32_t>(t);
}

void TreeSerializer::Value(const char* key, int64_t& v)
{
    if (m_loading)
    {
        const JsonValue* j = ReadSlot(key);
        if (j && j->type == JsonValue::Type::Number)
            v = j->isInt ? j->inum : static_cast<int64_t>(j->num);
        return;
    }
    WriteSlot(key)->SetInt(v);
}

void TreeSerializer::Value(const char* key, uint64_t& v)
{
    if (m_loading)
    {
        const JsonValue* j = ReadSlot(key);
        if (j && j->type == JsonValue::Type::Number)
            v = j->isInt ? j->unum : static_cast<uint64_t>(j->num);
        return;
    }
    WriteSlot(key)->SetUInt(v);
}

void TreeSerializer::Value(const char* key, double& v)
{
    if (m_loading)
    {
        const JsonValue* j = ReadSlot(key);
        if (j && j->type == JsonValue::Type::Number) v = j->num;
        return;
    }
    WriteSlot(key)->SetNum(v);
}

void TreeSerializer::Value(const char* key, float& v)
{
    double t = v;
    Value(key, t);
    v = static_cast<float>(t);
}

void TreeSerializer::Value(const char* key, std::string& v)
{
    if (m_loading)
    {
        const JsonValue* j = ReadSlot(key);
        if (j && j->type == JsonValue::Type::String) v = j->str;
        return;
    }
    JsonValue* j = WriteSlot(key);
    j->type = JsonValue::Type::String;
    j->str = v;
}

void TreeSerializer::Value(const char* key, std::vector<double>& v)
{
    if (m_loading)
    {
        const JsonValue* j = ReadSlot(key);
        if (!j || j->type != JsonValue::Type::Array) return;
        v.clear();
        v.reserve(j->arr.size());
        for (const auto& e : j->arr)
            v.push_back(e.type == JsonValue::Type::Number ? e.num : 0.0);
        return;
    }
    JsonValue* j = WriteSlot(key);
    j->type = JsonValue::Type::Array;
    j->arr.clear();
    j->arr.reserve(v.size());
    for (double d : v)
    {
        j->arr.emplace_back();
        j->arr.back().SetNum(d);
    }
}

bool TreeSerializer::Has(const char* key) const
{
    if (!m_loading) return false;
    return Cur()->Find(key) != nullptr;
}
}
