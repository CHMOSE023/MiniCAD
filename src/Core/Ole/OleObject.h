#pragma once
// OLE 对象（DWG / DXF 的 OLE2FRAME：图纸里嵌入的 Word / Excel 文档等）的显示数据。
//
// OLE2FRAME 的二进制数据 = AutoCAD 自己的 128 字节头（版本、四个角点 …）+ 一份 OLE 复合文档（MS-CFB）。
// 复合文档里除了对象本身（Word 的 WordDocument、Excel 的 Workbook …）还带一份"展示数据"流 \2OlePres000
//（MS-OLEDS 2.3.4 OLEPresentationStream）：对象上次在源程序里的样子，存成 WMF / EMF 图元文件或 DIB 位图，
// 容器程序不启动 Word 也靠它把对象画出来。MiniCAD 同样只画这张图：这里解析出图元文件 / 位图的字节与尺寸，
// 位图直接解码（DecodeDib，与平台无关），图元文件交给平台（AppPlatform::RasterizeOle，桌面版用 GDI 回放）光栅化。
#include "Core/Math/Point3.hpp"
#include "Core/Image/ImageData.hpp"
#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace MiniCAD::Ole
{
    enum class PictureFormat { None, Wmf, Emf, Dib };

    // 展示数据
    struct Presentation
    {
        PictureFormat        Format = PictureFormat::None;
        std::vector<uint8_t> Data;              // Wmf：WMF 字节（已去掉可放置头）；Emf：EMF 字节；Dib：BITMAPINFOHEADER 起的 DIB
        uint32_t             WidthHimetric  = 0; // 展示流里的尺寸（0.01 mm），图元文件自己没有尺寸时用
        uint32_t             HeightHimetric = 0;
    };

    // AutoCAD 头里的信息
    struct Frame
    {
        std::array<Math::Point3, 4> Corners{};   // 左上、右上、右下、左下（WCS）
        size_t CompoundOffset = 0;               // 复合文档在数据里的起点与长度
        size_t CompoundSize   = 0;
    };

    // 解析 OLE2FRAME 数据的头：版本 0x5581 的 128 字节头 + 复合文档。头不对时按复合文档签名找起点，连签名都没有返回 false
    bool ParseFrame(const std::vector<uint8_t>& data, Frame& out);

    // 从 OLE2FRAME 数据里取展示图：没有复合文档 / 没有展示流 / 格式不认识时返回 false
    bool ExtractPresentation(const std::vector<uint8_t>& data, Presentation& out);

    // 复合文档（MS-CFB）里按名称读出一个流（名称区分大小写，控制字符前缀照写，如 u"\x0002OlePres000"）
    bool ReadCompoundStream(const uint8_t* file, size_t size, const std::u16string& name, std::vector<uint8_t>& out);

    // DIB（BITMAPINFOHEADER + 调色板 + 像素；1 / 4 / 8 / 24 / 32 位，不压缩）→ RGBA。不支持的返回 nullptr
    std::shared_ptr<ImageData> DecodeDib(const std::vector<uint8_t>& dib, uint64_t key);

    // 数据的 FNV-1a 哈希：同一份数据的实体（复制、块里的多个参照）共用一份光栅图
    uint64_t HashData(const std::vector<uint8_t>& data);

    // OLE 数据登记处：实体把自己的数据按哈希登记进来，Editor 的图像回调按 "ole:<哈希>" 取回去光栅化。
    // 只存弱引用，实体没了数据就能释放
    class DataStore
    {
    public:
        static DataStore& Instance();
        void Put(uint64_t key, const std::shared_ptr<const std::vector<uint8_t>>& data);
        std::shared_ptr<const std::vector<uint8_t>> Get(uint64_t key);

    private:
        std::mutex m_mutex;
        std::unordered_map<uint64_t, std::weak_ptr<const std::vector<uint8_t>>> m_data;
    };

    // 实体给 IDrawSink::EmitImage 的"路径"："ole:" + 十六进制哈希；解析失败返回 false
    std::string ImagePath(uint64_t key);
    bool        ParseImagePath(const std::string& path, uint64_t& key);
}
