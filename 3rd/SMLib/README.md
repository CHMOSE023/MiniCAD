# SMLib（第三方：NURBS BRep 实体建模内核）

来源：NVIDIA USD BRep（usd-brep）1.0.1，提交 `3fd5b95`（2026-10-23 由 1.0.0 `c9c5979` 的差异补丁同步）。
许可证：Apache License 2.0，见同目录 `LICENSE`；各源文件头部保留原版权声明（上游仓库没有 NOTICE 文件）。

只复制了内核与高层接口两部分，USD、Python、OCCT、Hydra 等其余部分不带：

| 目录 | 上游路径 | 内容 |
|---|---|---|
| `SMLib/inc`、`SMLib/src` | `source/SMLib/` | 核心内核（几何、拓扑、算法） |
| `SM_API/inc`、`SM_API/src` | `source/SM_API/` | C 风格高层接口（`SmApi*`） |

上游的 `premake5.lua` 没有复制，构建改由 MiniCAD 的 CMake 负责。

## 本地修改

对上游源码的改动都记在这里，便于跟进上游更新时重新应用。

**静态库**：用宏 `SM_STATIC` 控制（`cmake/SMLib.cmake` 里定义），不定义时与上游一致。

- `SMLib/inc/SmTypes.h`、`SMLib/inc/nurbs.h`、`SM_API/inc/SmApiTypes.h`：`SM_STATIC` 时 `SM_EXPORT`、`GW_EXPORT`、`SMAPI_EXPORT` 为空（静态库不导入 / 导出符号）
- `SMLib/src/SmThreadLocalStorage.cpp`：`SM_STATIC` 时 Windows 不走 `TlsAlloc` 路径，改用与 Linux / macOS 构建相同的进程全局路径（静态库没有 `DllMain` 来分配 TLS 槽）。因此内核不能并发使用：`src/Solid/SMLib/SMLibKernel.cpp` 的每个内核方法与实体析构都持有同一把进程级递归锁，多个线程只能轮流调用

**C++20**：上游按 C++17 编译，C++20 会为 `a == b` 额外合成参数对调的候选（`b == a`），下面两类写法因此产生歧义（C2666）。改动不影响 C++17 下的行为。

- 26 个成员 `operator==` 原本不是 const（`SmTree.h`、`SmLocalSolve1d.h`、`SmPointSet.h`/`.cpp`、`SmHealProbArray.h` 等 11 个头文件和 5 个源文件），全部补上 `const`
- 派生类声明的是 `operator==(const 基类&)`，比较两个派生类对象时正反两种顺序转换相同：`SmEllipse.cpp`、`SmSurfOfExtrusion.cpp`、`SmSurfOfRevolution.cpp`、`SmVolume.cpp` 各一处改为显式调用 `p->operator==(*q)`（虚函数分派不变）
- `SmHealProbArray.h`：类模板的析构函数原写作 `~SmProbArray<TYPE>()`，C++20 不允许这种写法（MSVC 放过，GCC 报错），改为 `~SmProbArray()`

构建时不编译 `SMLib/src/SmDllMain.cpp`（只用于 DLL）和两个 `StdAfx.cpp`（改用 `target_precompile_headers`）。

## 使用注意

- SMLib 的头文件带有 `ULONG`、`TRUE` 等宏，只应在适配层的 `.cpp` 里包含，不要进入 MiniCAD 的公开头文件
- 使用前调用一次 `SmApiCreateContext()`
- 冒烟测试：`tests/Solid/SMLibSmokeTests.cpp`
