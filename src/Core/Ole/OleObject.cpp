#include "Core/Ole/OleObject.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace MiniCAD::Ole
{
    namespace
    {
        uint16_t U16(const uint8_t* p) { return static_cast<uint16_t>(p[0] | (p[1] << 8)); }
        uint32_t U32(const uint8_t* p) { return static_cast<uint32_t>(p[0] | (p[1] << 8) | (p[2] << 16) | (static_cast<uint32_t>(p[3]) << 24)); }
        int32_t  S32(const uint8_t* p) { return static_cast<int32_t>(U32(p)); }
        double   F64(const uint8_t* p) { double v; std::memcpy(&v, p, sizeof v); return v; }

        constexpr uint8_t kCfbSignature[8] = { 0xD0, 0xCF, 0x11, 0xE0, 0xA1, 0xB1, 0x1A, 0xE1 };
        constexpr uint32_t kEndOfChain = 0xFFFFFFFE;
        constexpr uint32_t kMaxRegularSector = 0xFFFFFFFA;

        // ── 复合文档（MS-CFB）────────────────────────────────────────────────
        class Compound
        {
        public:
            Compound(const uint8_t* file, size_t size) : m_file(file), m_size(size) {}

            bool Open()
            {
                if (m_size < 512 || std::memcmp(m_file, kCfbSignature, 8) != 0) return false;
                const uint16_t sectorShift = U16(m_file + 0x1E);
                const uint16_t miniShift   = U16(m_file + 0x20);
                if (sectorShift < 7 || sectorShift > 12 || miniShift > sectorShift) return false;
                m_sector     = size_t(1) << sectorShift;
                m_miniSector = size_t(1) << miniShift;
                const uint32_t fatSectors  = U32(m_file + 0x2C);
                m_dirStart                 = U32(m_file + 0x30);
                m_miniCutoff               = U32(m_file + 0x38);
                const uint32_t miniFatStart = U32(m_file + 0x3C);
                uint32_t difatSector       = U32(m_file + 0x44);
                const uint32_t difatCount  = U32(m_file + 0x48);
                m_sectorCount = (m_size - 512) / m_sector;

                // FAT：DIFAT 的前 109 项在头里，其余在 DIFAT 扇区链里（每个扇区最后一项指向下一个 DIFAT 扇区）
                std::vector<uint32_t> fatList;
                for (uint32_t i = 0; i < 109 && fatList.size() < fatSectors; ++i)
                {
                    const uint32_t s = U32(m_file + 0x4C + i * 4);
                    if (s >= kMaxRegularSector) break;
                    fatList.push_back(s);
                }
                for (uint32_t n = 0; n < difatCount && difatSector < kMaxRegularSector; ++n)
                {
                    const uint8_t* sec = Sector(difatSector);
                    if (!sec) break;
                    const size_t entries = m_sector / 4 - 1;
                    for (size_t i = 0; i < entries && fatList.size() < fatSectors; ++i)
                    {
                        const uint32_t s = U32(sec + i * 4);
                        if (s >= kMaxRegularSector) break;
                        fatList.push_back(s);
                    }
                    difatSector = U32(sec + entries * 4);
                }
                for (uint32_t s : fatList)
                {
                    const uint8_t* sec = Sector(s);
                    if (!sec) return false;
                    for (size_t i = 0; i < m_sector / 4; ++i) m_fat.push_back(U32(sec + i * 4));
                }
                if (m_fat.empty()) return false;

                // 目录
                m_dir = ReadChain(m_dirStart, SIZE_MAX);
                if (m_dir.size() < 128) return false;

                // 小流（根目录项的数据）与小 FAT
                const uint32_t rootStart = U32(m_dir.data() + 0x74);
                const uint64_t rootSize  = StreamSize(m_dir.data());
                m_mini = ReadChain(rootStart, static_cast<size_t>(std::min<uint64_t>(rootSize, m_size)));
                if (miniFatStart < kMaxRegularSector)
                {
                    const std::vector<uint8_t> raw = ReadChain(miniFatStart, SIZE_MAX);
                    for (size_t i = 0; i + 4 <= raw.size(); i += 4) m_miniFat.push_back(U32(raw.data() + i));
                }
                return true;
            }

            bool ReadStream(const std::u16string& name, std::vector<uint8_t>& out) const
            {
                for (size_t off = 0; off + 128 <= m_dir.size(); off += 128)
                {
                    const uint8_t* e = m_dir.data() + off;
                    const uint16_t nameBytes = U16(e + 0x40);
                    if (e[0x42] != 2 || nameBytes < 2 || nameBytes > 64) continue;   // 2 = 流
                    std::u16string entryName;
                    for (size_t i = 0; i + 1 < nameBytes - 1u; i += 2) entryName.push_back(static_cast<char16_t>(U16(e + i)));
                    if (entryName != name) continue;
                    const uint32_t start = U32(e + 0x74);
                    const uint64_t size  = StreamSize(e);
                    if (size > m_size) return false;
                    out = size < m_miniCutoff ? ReadMiniChain(start, static_cast<size_t>(size)) : ReadChain(start, static_cast<size_t>(size));
                    return out.size() == size;
                }
                return false;
            }

        private:
            const uint8_t* Sector(uint32_t s) const
            {
                if (s >= m_sectorCount) return nullptr;
                return m_file + 512 + static_cast<size_t>(s) * m_sector;
            }
            // 版本 3 的流长度只有低 32 位有效
            uint64_t StreamSize(const uint8_t* entry) const
            {
                return U16(m_file + 0x1A) == 3 ? U32(entry + 0x78) : (static_cast<uint64_t>(U32(entry + 0x7C)) << 32 | U32(entry + 0x78));
            }
            std::vector<uint8_t> ReadChain(uint32_t start, size_t size) const
            {
                std::vector<uint8_t> out;
                size_t guard = 0;
                for (uint32_t s = start; s < kMaxRegularSector && out.size() < size && guard++ <= m_sectorCount; s = s < m_fat.size() ? m_fat[s] : kEndOfChain)
                {
                    const uint8_t* sec = Sector(s);
                    if (!sec) break;
                    out.insert(out.end(), sec, sec + m_sector);
                }
                if (out.size() > size) out.resize(size);
                return out;
            }
            std::vector<uint8_t> ReadMiniChain(uint32_t start, size_t size) const
            {
                std::vector<uint8_t> out;
                size_t guard = 0;
                for (uint32_t s = start; s < kMaxRegularSector && out.size() < size && guard++ <= m_miniFat.size(); s = s < m_miniFat.size() ? m_miniFat[s] : kEndOfChain)
                {
                    const size_t off = static_cast<size_t>(s) * m_miniSector;
                    if (off + m_miniSector > m_mini.size()) break;
                    out.insert(out.end(), m_mini.begin() + off, m_mini.begin() + off + m_miniSector);
                }
                if (out.size() > size) out.resize(size);
                return out;
            }

            const uint8_t*        m_file;
            size_t                m_size;
            size_t                m_sector = 512, m_miniSector = 64, m_sectorCount = 0;
            uint32_t              m_dirStart = 0, m_miniCutoff = 4096;
            std::vector<uint32_t> m_fat, m_miniFat;
            std::vector<uint8_t>  m_dir, m_mini;
        };

        // 按内容判断图元文件 / 位图的格式；WMF 的可放置头（22 字节）与 METAFILEPICT 头（8 字节）去掉
        PictureFormat Classify(std::vector<uint8_t>& data)
        {
            auto isWmf = [](const uint8_t* p, size_t n)
            {
                return n >= 18 && (U16(p) == 1 || U16(p) == 2) && U16(p + 2) == 9 && (U16(p + 4) == 0x0100 || U16(p + 4) == 0x0300);
            };
            if (data.size() >= 88 && U32(data.data()) == 1 && std::memcmp(data.data() + 40, " EMF", 4) == 0)
                return PictureFormat::Emf;
            if (data.size() >= 22 + 18 && U32(data.data()) == 0x9AC6CDD7)         // 可放置 WMF
            {
                data.erase(data.begin(), data.begin() + 22);
                return PictureFormat::Wmf;
            }
            if (isWmf(data.data(), data.size()))
                return PictureFormat::Wmf;
            if (data.size() >= 8 + 18 && isWmf(data.data() + 8, data.size() - 8))  // METAFILEPICT 头 + WMF
            {
                data.erase(data.begin(), data.begin() + 8);
                return PictureFormat::Wmf;
            }
            if (data.size() >= 40)
            {
                const uint32_t hdr = U32(data.data());
                if (hdr == 40 || hdr == 52 || hdr == 56 || hdr == 108 || hdr == 124)
                    return PictureFormat::Dib;
            }
            return PictureFormat::None;
        }

        // MS-OLEDS 2.3.4 OLEPresentationStream
        bool ParsePresentationStream(const std::vector<uint8_t>& s, Presentation& out)
        {
            if (s.size() < 8) return false;
            size_t off = 0;
            const uint32_t marker = U32(s.data());
            off += 4;
            if (marker == 0xFFFFFFFF || marker == 0xFFFFFFFE)
                off += 4;                               // 标准剪贴板格式号（内容按字节判断，不用它）
            else if (marker != 0)
                off += marker;                          // 注册格式名（ANSI 字符串）
            if (off + 4 > s.size()) return false;
            const uint32_t targetDeviceSize = U32(s.data() + off);
            off += 4;

            // 之后：Aspect、Lindex、Advf、Reserved1、Width、Height、Size、Data。目标设备块的长度字段是否包含自身
            // 规范说得不清楚，两种解释都试：Size 不超过剩余字节的那种是对的
            for (size_t skip : { targetDeviceSize > 4 ? targetDeviceSize - 4 : 0u, targetDeviceSize > 4 ? targetDeviceSize : 0u })
            {
                const size_t p = off + skip;
                if (p + 28 > s.size()) continue;
                const uint32_t width  = U32(s.data() + p + 16);
                const uint32_t height = U32(s.data() + p + 20);
                const uint32_t size   = U32(s.data() + p + 24);
                if (size == 0 || p + 28 + size > s.size()) continue;
                out.WidthHimetric  = width;
                out.HeightHimetric = height;
                out.Data.assign(s.begin() + static_cast<std::ptrdiff_t>(p + 28), s.begin() + static_cast<std::ptrdiff_t>(p + 28 + size));
                out.Format = Classify(out.Data);
                return out.Format != PictureFormat::None;
            }
            return false;
        }
    }

    bool ParseFrame(const std::vector<uint8_t>& data, Frame& out)
    {
        out = Frame{};
        const size_t kHeader = 2 + 12 * 8 + 30;     // 版本 + 四个角点 + 30 字节（末尾 4 字节是复合文档长度）
        if (data.size() >= kHeader && U16(data.data()) == 0x5581)
        {
            for (size_t i = 0; i < 4; ++i)
                out.Corners[i] = { F64(data.data() + 2 + i * 24), F64(data.data() + 2 + i * 24 + 8), F64(data.data() + 2 + i * 24 + 16) };
            out.CompoundOffset = kHeader;
            out.CompoundSize   = std::min<size_t>(U32(data.data() + kHeader - 4), data.size() - kHeader);
            if (out.CompoundSize == 0) out.CompoundSize = data.size() - kHeader;
            bool finite = true;
            for (const auto& c : out.Corners)
                finite = finite && std::isfinite(c.x) && std::isfinite(c.y) && std::isfinite(c.z);
            if (finite) return true;
        }
        // 头不认识：按复合文档签名找
        const auto it = std::search(data.begin(), data.end(), std::begin(kCfbSignature), std::end(kCfbSignature));
        if (it == data.end()) return false;
        out = Frame{};
        out.CompoundOffset = static_cast<size_t>(it - data.begin());
        out.CompoundSize   = data.size() - out.CompoundOffset;
        return true;
    }

    bool ReadCompoundStream(const uint8_t* file, size_t size, const std::u16string& name, std::vector<uint8_t>& out)
    {
        Compound doc(file, size);
        return doc.Open() && doc.ReadStream(name, out);
    }

    bool ExtractPresentation(const std::vector<uint8_t>& data, Presentation& out)
    {
        out = Presentation{};
        Frame frame;
        if (!ParseFrame(data, frame) || frame.CompoundSize == 0) return false;
        Compound doc(data.data() + frame.CompoundOffset, frame.CompoundSize);
        if (!doc.Open()) return false;
        // 通常只有 \2OlePres000；多个展示流时取第一个能解析的
        for (char16_t n = u'0'; n <= u'2'; ++n)
        {
            std::vector<uint8_t> stream;
            if (doc.ReadStream(std::u16string(u"\x0002OlePres00") + n, stream) && ParsePresentationStream(stream, out))
                return true;
        }
        return false;
    }

    std::shared_ptr<ImageData> DecodeDib(const std::vector<uint8_t>& dib, uint64_t key)
    {
        if (dib.size() < 40) return nullptr;
        const uint8_t* h = dib.data();
        const uint32_t headerSize = U32(h);
        if (headerSize < 40 || headerSize > dib.size()) return nullptr;
        const int32_t  width    = S32(h + 4);
        const int32_t  heightS  = S32(h + 8);
        const uint16_t bpp      = U16(h + 14);
        const uint32_t compress = U32(h + 16);
        uint32_t       clrUsed  = U32(h + 32);
        const bool topDown = heightS < 0;
        const int32_t height = topDown ? -heightS : heightS;
        if (width <= 0 || height <= 0 || width > 16384 || height > 16384) return nullptr;
        if (!(compress == 0 || (compress == 3 && (bpp == 32 || bpp == 16)))) return nullptr;
        if (bpp != 1 && bpp != 4 && bpp != 8 && bpp != 16 && bpp != 24 && bpp != 32) return nullptr;

        size_t off = headerSize;
        if (compress == 3 && headerSize == 40) off += 12;              // 位域掩码（按标准 BGR 顺序处理）
        std::vector<uint32_t> palette;
        if (bpp <= 8)
        {
            if (clrUsed == 0 || clrUsed > (1u << bpp)) clrUsed = 1u << bpp;
            if (off + clrUsed * 4 > dib.size()) return nullptr;
            for (uint32_t i = 0; i < clrUsed; ++i) palette.push_back(U32(h + off + i * 4));
            off += clrUsed * 4;
        }
        const size_t stride = ((static_cast<size_t>(width) * bpp + 31) / 32) * 4;
        if (off + stride * static_cast<size_t>(height) > dib.size()) return nullptr;

        auto img = std::make_shared<ImageData>();
        img->Key = key;
        img->Width = static_cast<uint32_t>(width);
        img->Height = static_cast<uint32_t>(height);
        img->Rgba.resize(static_cast<size_t>(width) * height * 4);
        for (int32_t y = 0; y < height; ++y)
        {
            const uint8_t* row = h + off + stride * static_cast<size_t>(topDown ? y : height - 1 - y);
            uint8_t* dst = img->Rgba.data() + static_cast<size_t>(y) * width * 4;
            for (int32_t x = 0; x < width; ++x, dst += 4)
            {
                uint8_t r = 0, g = 0, b = 0;
                switch (bpp)
                {
                case 32: b = row[x * 4]; g = row[x * 4 + 1]; r = row[x * 4 + 2]; break;
                case 24: b = row[x * 3]; g = row[x * 3 + 1]; r = row[x * 3 + 2]; break;
                case 16:
                {
                    const uint16_t v = U16(row + x * 2);               // 5-5-5
                    r = static_cast<uint8_t>(((v >> 10) & 31) * 255 / 31);
                    g = static_cast<uint8_t>(((v >> 5) & 31) * 255 / 31);
                    b = static_cast<uint8_t>((v & 31) * 255 / 31);
                    break;
                }
                default:
                {
                    uint32_t idx = 0;
                    if (bpp == 8)      idx = row[x];
                    else if (bpp == 4) idx = (row[x / 2] >> ((x & 1) ? 0 : 4)) & 15;
                    else               idx = (row[x / 8] >> (7 - (x & 7))) & 1;
                    const uint32_t c = idx < palette.size() ? palette[idx] : 0;
                    b = static_cast<uint8_t>(c); g = static_cast<uint8_t>(c >> 8); r = static_cast<uint8_t>(c >> 16);
                    break;
                }
                }
                dst[0] = r; dst[1] = g; dst[2] = b; dst[3] = 255;
            }
        }
        return img;
    }

    uint64_t HashData(const std::vector<uint8_t>& data)
    {
        uint64_t h = 1469598103934665603ull;
        for (uint8_t b : data) { h ^= b; h *= 1099511628211ull; }
        h ^= data.size();
        return h ? h : 1;
    }

    DataStore& DataStore::Instance()
    {
        static DataStore store;
        return store;
    }

    void DataStore::Put(uint64_t key, const std::shared_ptr<const std::vector<uint8_t>>& data)
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_data[key] = data;
        if (m_data.size() > 4096)       // 顺便清掉已经没人持有的
            for (auto it = m_data.begin(); it != m_data.end();)
                it = it->second.expired() ? m_data.erase(it) : std::next(it);
    }

    std::shared_ptr<const std::vector<uint8_t>> DataStore::Get(uint64_t key)
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        const auto it = m_data.find(key);
        return it == m_data.end() ? nullptr : it->second.lock();
    }

    std::string ImagePath(uint64_t key)
    {
        char buf[32];
        std::snprintf(buf, sizeof buf, "ole:%016llx", static_cast<unsigned long long>(key));
        return buf;
    }

    bool ParseImagePath(const std::string& path, uint64_t& key)
    {
        if (path.size() != 20 || path.compare(0, 4, "ole:") != 0) return false;
        key = 0;
        for (size_t i = 4; i < path.size(); ++i)
        {
            const char c = path[i];
            const int v = c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1;
            if (v < 0) return false;
            key = key << 4 | static_cast<uint64_t>(v);
        }
        return true;
    }
}
