# MiniCAD

一个轻量级跨平台二维 CAD 编辑器（C++20）。Windows 桌面版（D3D11）和网页版（WebAssembly + WebGL2）使用同一套界面代码，界面完全相同。

![MiniCAD](docs/images/MiniCAD.png)

## 目录结构

```
MiniCAD
├── src/                    # MiniCADLib 核心库（命名空间 MiniCAD）：实体、场景、文档、编辑器、视口、文字
│   ├── UI/                 # MiniGUI 保留模式界面库（命名空间 MiniGUI）
│   └── Serialization/      # 原生格式（JSON / 二进制）+ MiniDWG DWG/DXF 读写库（Codec/ Database/ Dwg/ Dxf/，命名空间 MiniDWG）
├── apps/Common/            # 主窗口界面与逻辑（桌面版、网页版共用），平台服务经 AppPlatform 接口提供
├── apps/Win32/             # 桌面版 MiniCADWin：窗口、D3D11、消息循环（含 D3D11 / Software 渲染后端）
├── apps/Web/               # 网页版 MiniCADWeb：画布、WebGL2、浏览器文件选择与下载
├── wasm/                   # 旧版 C 接口 WASM（需 -DMINICAD_BUILD_LEGACY_WASM=ON）
├── tests/                  # Core/ UI/ Serialization/ 测试与 Data/ 测试数据
├── assets/                 # 字体、图标、填充图案、界面描述文件
├── cmake/                  # MiniGUI、MiniDWG 的目标定义
├── tools/                  # make_web_font.py：生成网页版界面字体
├── 3rd/                    # stb_image、stb_truetype
└── docs/                   # 教程与各库的开发计划
```

## 构建

需要 CMake 3.20+、Ninja、MSVC（C++20）。在普通命令行中需先调用 `vcvars64.bat`。

```bat
cmake --preset debug
cmake --build --preset debug
out\debug\MiniCADWin\MiniCADWin.exe
```

运行全部测试：

```bat
run_tests.bat
```

网页版（需要 Emscripten SDK，脚本顶部配置 emsdk 路径）：

```bat
build_wasm.bat serve
```

构建后在 http://localhost:8080/MiniCADWeb/ 打开（必须经 HTTP 服务器，不能直接双击 html）。输出在 `out/build/wasm/MiniCADWeb/`，部署时把该目录下的 `index.html`、`index.js`、`index.wasm`、`index.data` 放到任意静态网站即可。

## 许可

MiniDWG 部分移植自 [ACadSharp](https://github.com/DomCR/ACadSharp)（MIT），`tests/Data/Dwg/` 下的样例图纸也来自该项目，网页版界面字体由 Noto Sans SC（SIL OFL 1.1）生成，详见 [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md)。
