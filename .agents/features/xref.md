---
covers: src/Core/Entity/BlockEntity.hpp, src/Core/Entity/InsertEntity.hpp, tests/Core/CadXrefTests.cpp, apps/Web/src/Host/WebApp.*, tests/Web/FolderPickerTests.cjs
---

# 外部参照（XREF）

## 概述

宿主图纸里的外部参照是一个带 XRef 标志（组码 70 位 4，叠加型位 8）与文件路径（组码 1）的空块记录，内容在另一个 DWG / DXF 里。MiniCAD 打开宿主时读入那个文件的模型空间，放进宿主里的那个块；块参照照常引用它。

## 读入（`src/Import/CadExchange.cpp`）

- `ImportCad` 带图纸路径时建立解析环境 `XrefContext`（宿主目录、另外查找的目录 `SearchRoots`、嵌套深度、正在读取的文件）；没有路径（内存数据）时不读外部文件
- `Importer::LoadXrefs` 逐个处理外部参照块：已卸载（组码 71）的不读；叠加型不随宿主再被参照时显示
- `Importer::LoadXref`（静态）读文件、建一个新的 Importer 调 `ImportAsXref`：表项名一律加「参照名|」前缀，宿主已有的依赖图层 / 样式直接复用（颜色、开关以宿主为准），外部图纸的图层 0 并入宿主的图层 0；不读布局、图纸空间与图纸变量。嵌套参照递归（最多 8 层），循环引用跳过
- 保存：`BlockEntity` 的 XRef 标志、路径、卸载标志随 `.mcad` / JSON 保存；重新生成 DWG / DXF 时外部参照只写标志与路径（不绑定），名字带 `|` 的依赖块不写

## 查找文件（`ResolveXrefPath`）

依次：

1. 保存的路径：绝对路径直接用，相对路径按宿主所在目录解析
2. 按路径后缀在宿主目录及其各级上级目录里找，后缀越长越优先（项目整体搬家、换盘符时 `D:\项目\共用\A.dwg` 能找到 `E:\备份\项目\共用\A.dwg`）；只剩文件名时只在宿主目录里找
3. 在 `SearchRoots` 里先按后缀、再递归按文件名找，多个同名文件时取与保存路径末尾相同段数最多的

没有扩展名的路径补上 `.dwg` 再试。

## 没读进来的参照

- `BlockEntity::IsUnresolvedXRef()`：是外部参照、没卸载、内容为空
- `InsertEntity` 对这样的块在插入点写出保存的路径（字高按屏幕 14 像素，`MarkViewDependent`），范围取插入点
- `FindUnresolvedXrefs(scene)` 列出它们；`ReloadUnresolvedXrefs(scene, 宿主路径, 搜索目录)` 再找一遍并读入（改块定义的内容，不进撤销栈，读入后 `Scene::MarkDirty`）

## 界面（`apps/Common/src/GUI`）

- 打开一批图纸后 `MainFrame::PromptMissingXrefs` 提示找不到的参照，可"选择文件夹…"
- `MainFrame::LocateXrefs` → `AppPlatform::PickFolder(需要的文件名, 回调)` → `ReloadUnresolvedXrefs`；仍有找不到的再提示。命令 `xref.locate`（"文件 > 查找外部参照"），命令行 `XREF` / `XR`
- 桌面版 `PickFolder`：系统的文件夹对话框（`IFileOpenDialog`）
- 网页版 `PickFolder`：上传的图纸在 `/upload/<请求号>/<序号>/`，旁边没有参照文件。有 `showDirectoryPicker`（Chromium 系）时经目录授权逐级遍历，没有时用 `<input webkitdirectory>`；都只读取需要的文件名，按原子目录写进 `/xref/<请求号>/`，再作为搜索目录。参照文件里的嵌套参照要再执行一次查找

## 测试与排查

- `MiniCADTests` 的 `CadXrefTests.cpp`：不依赖本机图纸的部分在临时目录里现做宿主与参照；本机有 `tests/Data/Xref.dwg`（相对路径参照 `Xref-A.dwg` / `Xref-B.dwg`）时再核对依赖图层复用、天正符号等
- `tests/Web/FolderPickerTests.cjs`：网页版选文件夹的 JS
- 日志：找不到时 `LOG_WARN`「外部参照 X：找不到文件 …（已在 … 下查找）」，读入时 `LOG_INFO`「从 … 读入 N 个实体」
