#pragma once
#include "Database/CadObject.h"
#include "Database/CadTable.h"
#include "Database/CadVersion.h"
#include "Database/DxfClass.h"
#include "Database/Handle.hpp"
#include "Database/Generated/CadHeader.g.h"
#include "Database/Generated/Model.g.h"
#include "Database/UnknownObjects.h"
#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace MiniDWG
{
    // 缩略图（DWG 的 AcDb:Preview 段、DXF 的 THUMBNAILIMAGE 段）
    struct CadPreview
    {
        enum class ImageType : std::uint8_t
        {
            None = 0,
            Bmp = 2,    // 不含文件头的 DIB（从 BITMAPINFOHEADER 开始），DXF 中也是这种格式
            Wmf = 3,
            Png = 6,    // R2013 起 AutoCAD 写 PNG
        };

        ImageType                 Type = ImageType::None;
        std::vector<std::uint8_t> Header;   // DWG 中图像前的头数据（AutoCAD 写 80 字节），原样保留
        std::vector<std::uint8_t> Image;
    };

    // 文件中原样保留的段（只能写回同一版本）：R2013 起的 AcDb:AcDsPrototype_1b（三维实体等的 ACIS 数据）
    struct RawSection
    {
        CadVersion                Version = CadVersion::Unknown;
        std::string               Name;
        std::vector<std::uint8_t> Data;
    };

    // 一张图纸的完整数据：头变量、符号表、块、实体、字典与对象。
    // DWG / DXF 的读写器都只和它打交道；宿主程序（MiniCAD）再把它映射到自己的场景。
    // 对应 ACadSharp 的 CadDocument，只保留数据，不带几何算法。
    //
    // 所有对象由数据库持有，按句柄索引；对象之间的引用（所有者、图层、块 ...）都存句柄。
    class CadDatabase
    {
    public:
        CadDatabase() = default;

        CadDatabase(const CadDatabase&) = delete;
        CadDatabase& operator=(const CadDatabase&) = delete;

        CadHeader Header;

        // 自定义类定义（CLASSES 段）
        std::vector<DxfClass> Classes;

        CadPreview Preview;

        // 原样保留的段
        std::vector<RawSection> RawSections;

        CadVersion GetVersion() const { return m_version; }
        void SetVersion(CadVersion version) { m_version = version; }

        // ── 句柄 ──

        // 分配一个新句柄，并推进 HANDSEED
        Handle AllocateHandle();

        // HANDSEED：下一个可用句柄。读取文件后设为文件里的值，保证新对象不与已有句柄冲突
        Handle GetHandleSeed() const { return m_handleSeed; }
        void SetHandleSeed(Handle seed);

        // ── 对象 ──

        // 加入对象：句柄为空时分配新句柄，否则沿用（读文件时）并推进 HANDSEED。
        // 句柄已被占用时返回 nullptr，对象被丢弃。
        CadObject* AddObject(std::unique_ptr<CadObject> object);

        template <class T>
        T* Add(std::unique_ptr<T> object)
        {
            return static_cast<T*>(AddObject(std::move(object)));
        }

        // 从数据库中取出对象（不维护引用它的集合与句柄，由调用方处理）；没有该对象时返回 nullptr
        std::unique_ptr<CadObject> RemoveObject(Handle handle);

        CadObject* Find(Handle handle) const;

        template <class T>
        T* FindAs(Handle handle) const
        {
            return dynamic_cast<T*>(Find(handle));
        }

        // 按句柄升序，写文件时顺序稳定
        const std::map<Handle, std::unique_ptr<CadObject>>& Objects() const { return m_objects; }

        // ── 符号表与根字典 ──

        CadTable* BlockRecords() const { return FindAs<CadTable>(m_blockRecords); }
        CadTable* Layers() const { return FindAs<CadTable>(m_layers); }
        CadTable* DimensionStyles() const { return FindAs<CadTable>(m_dimensionStyles); }
        CadTable* TextStyles() const { return FindAs<CadTable>(m_textStyles); }
        CadTable* LineTypes() const { return FindAs<CadTable>(m_lineTypes); }
        CadTable* Views() const { return FindAs<CadTable>(m_views); }
        CadTable* UCSs() const { return FindAs<CadTable>(m_ucss); }
        CadTable* VPorts() const { return FindAs<CadTable>(m_vports); }
        CadTable* AppIds() const { return FindAs<CadTable>(m_appIds); }

        CadDictionary* RootDictionary() const { return FindAs<CadDictionary>(m_rootDictionary); }

        // 读文件时登记已读入的表与根字典
        void SetTable(CadObjectType controlType, Handle table);
        void SetRootDictionary(Handle dictionary) { m_rootDictionary = dictionary; }

        // 按名称查表项（不区分大小写，与 AutoCAD 一致）
        TableEntry* FindTableEntry(const CadTable* table, std::string_view name) const;

        template <class T>
        T* FindTableEntry(const CadTable* table, std::string_view name) const
        {
            return dynamic_cast<T*>(FindTableEntry(table, name));
        }

        BlockRecord* ModelSpace() const;
        BlockRecord* PaperSpace() const;

        // 字典条目（按名称，区分大小写）
        CadObject* FindDictionaryEntry(const CadDictionary* dictionary, std::string_view name) const;
        CadDictionary* FindNamedDictionary(std::string_view name) const;

        // ── 编辑辅助：维护所有者与集合 ──

        // 加入表项：设置所有者并登记到表中
        template <class T>
        T* AddTableEntry(CadTable* table, std::unique_ptr<T> entry)
        {
            T* added = Add(std::move(entry));
            if (added != nullptr)
                LinkTableEntry(table, added);
            return added;
        }

        // 加入字典条目：设置所有者、名称并登记
        template <class T>
        T* AddDictionaryEntry(CadDictionary* dictionary, std::string_view name, std::unique_ptr<T> object)
        {
            T* added = Add(std::move(object));
            if (added != nullptr)
                LinkDictionaryEntry(dictionary, name, added);
            return added;
        }

        // 新建块记录及其 BLOCK / ENDBLK 实体
        BlockRecord* CreateBlockRecord(std::string_view name);

        // 把实体加入块（模型空间、图纸空间或普通块）；图层、线型为空时取 "0" 与 ByLayer
        template <class T>
        T* AddEntity(BlockRecord* block, std::unique_ptr<T> entity)
        {
            T* added = Add(std::move(entity));
            if (added != nullptr)
                LinkEntity(block, added);
            return added;
        }

        // 新建图纸的默认内容（对应 ACadSharp CadDocument.CreateDefaults）：
        // 符号表、根字典及其默认条目、默认表项（图层 0、线型 ByLayer/ByBlock/Continuous、
        // 文字样式与标注样式 Standard、APPID ACAD、视口 *Active）、模型空间与图纸空间及其布局
        void CreateDefaults();

    private:
        void LinkTableEntry(CadTable* table, TableEntry* entry);
        void LinkDictionaryEntry(CadDictionary* dictionary, std::string_view name, NonGraphicalObject* object);
        void LinkEntity(BlockRecord* block, Entity* entity);

        CadTable* CreateTable(Handle& slot, CadObjectType controlType, std::string_view entryDxfName);
        Layout* CreateLayout(BlockRecord* block, std::string_view name, int tabOrder);

        CadVersion m_version    = CadVersion::AC1032;
        Handle     m_handleSeed = 1;

        std::map<Handle, std::unique_ptr<CadObject>> m_objects;

        Handle m_blockRecords    = kNullHandle;
        Handle m_layers          = kNullHandle;
        Handle m_dimensionStyles = kNullHandle;
        Handle m_textStyles      = kNullHandle;
        Handle m_lineTypes       = kNullHandle;
        Handle m_views           = kNullHandle;
        Handle m_ucss            = kNullHandle;
        Handle m_vports          = kNullHandle;
        Handle m_appIds          = kNullHandle;
        Handle m_rootDictionary  = kNullHandle;
    };
}
