#pragma once
// DWG / DXF 文件交换：经 MiniDWG 的 CadDatabase 与 Scene 互相转换。
// 公开头文件不暴露 MiniDWG 类型。
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace MiniCAD
{
    class Scene;

    enum class CadFileKind
    {
        None,   // 不是 DWG / DXF（按扩展名判断）
        Dwg,
        Dxf,
    };

    // 写出 DWG / DXF 的版本。AutoCAD 2013～2017 共用 2013 格式（AC1027），2018 及以后为 2018 格式（AC1032）
    // 2007 格式（AC1021）只能写 DXF：DWG 写出时 MiniDWG 改写为 2010 格式（见 CadSaveVersionSupported）
    enum class CadSaveVersion
    {
        R2018,      // AC1032
        R2013,      // AC1027：AutoCAD 2013 / 2014 / 2015 / 2016 / 2017
        R2010,      // AC1024：AutoCAD 2010 / 2011 / 2012
        R2007,      // AC1021：AutoCAD 2007 / 2008 / 2009
        R2004,      // AC1018：AutoCAD 2004 / 2005 / 2006
        R2000,      // AC1015：AutoCAD 2000 / 2000i / 2002
    };

    // 新建文档、以及无法沿用原版本时默认的写出版本
    constexpr CadSaveVersion kDefaultCadSaveVersion = CadSaveVersion::R2004;

    // 界面上显示的版本名："2018"、"2013"、"2010"、"2007"、"2004"、"2000"
    const char* CadSaveVersionName(CadSaveVersion version);

    // 该格式能否按该版本写出（DWG 不支持 2007）
    bool CadSaveVersionSupported(CadFileKind kind, CadSaveVersion version);

    // 打开 DWG / DXF 时留下的原图：原始字节、格式与版本，以及场景实体与原图实体的对应关系和导入时的状态。
    // 另存为同一格式、同一版本时以原图为底，只改写变化过的实体与表项：没改过的实体（包括天正 TCH_* 等
    // 自定义对象）、MiniCAD 不支持而跳过的实体、图纸空间、字典等原样写回。定义在 CadExchange.cpp
    struct CadSource;

    // 按扩展名（不区分大小写）判断是否 DWG / DXF 文件
    CadFileKind CadKindFromPath(const std::string& path);

    // 读取 DWG / DXF 内存数据到空场景。格式按文件内容识别，失败返回 false 并在 error 中说明原因
    // version：文件版本对应的写出版本（2013 格式 → R2013，其余 → R2018），保存时据此沿用原版本
    // source：非空时返回原图，另存时传给 ExportCad
    // progress：读取进度（0～1），在调用线程上定期调用
    // sourcePath：图纸文件的路径（UTF-8），外部参照（XREF）按它所在目录解析相对路径并读入；为空时外部参照只剩空块
    using ImportProgress = std::function<void(float)>;
    bool ImportCad(const std::vector<std::uint8_t>& data, Scene& scene, std::string* error = nullptr,
                   CadSaveVersion* version = nullptr, std::shared_ptr<const CadSource>* source = nullptr,
                   const ImportProgress& progress = nullptr, const std::string& sourcePath = {});

    // 没读进来的外部参照（文件找不到或读取失败；卸载的不算）：块名与图纸里保存的路径。
    // 块参照在插入点显示这个路径（InsertEntity）
    struct UnresolvedXref
    {
        std::string Name;
        std::string Path;
    };
    std::vector<UnresolvedXref> FindUnresolvedXrefs(const Scene& scene);

    // 重新查找并读入没读进来的外部参照，返回读入的个数。
    // hostPath：宿主图纸路径（UTF-8），相对路径按它所在目录解析，可为空；
    // searchRoots：另外查找的目录（先按保存路径的后缀，再递归按文件名），如网页版用户选的本地文件夹。
    // 改的是块定义的内容，不进撤销栈；读入了就 Scene::MarkDirty
    int ReloadUnresolvedXrefs(Scene& scene, const std::string& hostPath, const std::vector<std::string>& searchRoots);

    // 把场景写成 DWG 或 DXF（ASCII）。失败返回空数组
    // source：场景打开时的原图。格式与版本都与原图相同时以原图为底写出（见 CadSource），否则重新生成整张图纸，
    // 天正等自定义对象只剩恢复出的普通几何
    std::vector<std::uint8_t> ExportCad(const Scene& scene, CadFileKind kind, CadSaveVersion version = CadSaveVersion::R2018,
                                        std::string* error = nullptr, const CadSource* source = nullptr);

    // 原图中模型空间的天正对象（TCH_*）个数；source 为空时为 0
    int CadSourceTchCount(const CadSource* source);
}
