#include "doc/Archive/JsonSerializer.h"
#include <cstdio>
#include <cstdlib>
#include <sstream>

namespace MiniCAD {

    // ── 写入 ─────────────────────────────
    void JsonSerializer::Write(std::string_view key, int value) {
        m_data[std::string(key)] = std::to_string(value);
    }

    void JsonSerializer::Write(std::string_view key, Real value) {
        m_data[std::string(key)] = RealToStr(value);
    }

    void JsonSerializer::Write(std::string_view key, std::string_view value) {
        m_data[std::string(key)] = std::string("\"") + std::string(value) + "\"";
    }

    void JsonSerializer::Write(std::string_view key, const Vec3& value) {
        WriteVec3(std::string(key).c_str(), value.x, value.y, value.z);
    }

    void JsonSerializer::WriteVec3(const char* key, Real x, Real y, Real z) {
        std::ostringstream oss;
        oss << "[" << RealToStr(x) << "," << RealToStr(y) << "," << RealToStr(z) << "]";
        m_data[key] = oss.str();
    }

    // ── 读取 ─────────────────────────────
    int JsonSerializer::ReadInt(std::string_view key) const {
        auto it = m_data.find(std::string(key));
        return it != m_data.end() ? std::stoi(it->second) : 0;
    }

    float JsonSerializer::ReadReal(std::string_view key) const {
        auto it = m_data.find(std::string(key));
        return it != m_data.end() ? JsonSerializer::StrToReal(it->second) : 0.0f;
    }

    std::string JsonSerializer::ReadString(std::string_view key) const {
        auto it = m_data.find(std::string(key));
        if (it == m_data.end()) return "";
        const std::string& s = it->second;
        if (s.size() >= 2 && s.front() == '"' && s.back() == '"')
            return s.substr(1, s.size() - 2);
        return s;
    }

    Vec3 JsonSerializer::ReadVec3(std::string_view key) const {
        Real x = 0.0f, y = 0.0f, z = 0.0f;
        ReadVec3Impl(std::string(key).c_str(), x, y, z);
        return Vec3(x, y, z);
    }

    void JsonSerializer::ReadVec3Impl(const char* key, Real& x, Real& y, Real& z) const {
        x = y = z = 0.0f;
        auto it = m_data.find(key);
        if (it == m_data.end()) return;

        const std::string& s = it->second;
        if (s.size() < 5 || s.front() != '[') return;

        std::string inner = s.substr(1, s.size() - 2);
        auto comma1 = inner.find(',');
        auto comma2 = inner.rfind(',');
        if (comma1 == std::string::npos || comma1 == comma2) return;

        x = JsonSerializer::StrToReal(inner.substr(0, comma1));
        y = JsonSerializer::StrToReal(inner.substr(comma1 + 1, comma2 - comma1 - 1));
        z = JsonSerializer::StrToReal(inner.substr(comma2 + 1));
    }

    // ── Flush / Load ─────────────────────
    std::string JsonSerializer::Flush() const {
        std::ostringstream oss;
        oss << "{\n";
        bool first = true;
        for (const auto& kv : m_data) {
            if (!first) oss << ",\n";
            oss << "  \"" << kv.first << "\": " << kv.second;
            first = false;
        }
        oss << "\n}";
        return oss.str();
    }

    bool JsonSerializer::Load(const std::string& json) {
        m_data.clear();
        std::istringstream iss(json);
        std::string line;
        while (std::getline(iss, line)) {
            auto colon = line.find(':');
            if (colon == std::string::npos) continue;

            std::string kpart = line.substr(0, colon);
            auto ks = kpart.find('"');
            auto ke = kpart.rfind('"');
            if (ks == ke) continue;
            std::string key = kpart.substr(ks + 1, ke - ks - 1);

            std::string vpart = line.substr(colon + 1);
            size_t vs = 0;
            while (vs < vpart.size() && (vpart[vs] == ' ' || vpart[vs] == '\t')) ++vs;
            size_t ve = vpart.size();
            while (ve > vs && (vpart[ve - 1] == ',' || vpart[ve - 1] == ' ' || vpart[ve - 1] == '\t' || vpart[ve - 1] == '\r')) --ve;

            m_data[key] = vpart.substr(vs, ve - vs);
        }
        return !m_data.empty();
    }

    // ── 私有辅助 ─────────────────────────
    std::string JsonSerializer::RealToStr(Real v) {
        char buf[32];
        std::snprintf(buf, sizeof(buf), "%.6g", (double)v);
        return buf;
    }

    float JsonSerializer::StrToReal(const std::string& s) {
        return s.empty() ? 0.0f : static_cast<float>(std::strtod(s.c_str(), nullptr));
    }

} // namespace MiniCAD