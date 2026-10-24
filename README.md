<p align="center">
  <img src="assets/icons/app.ico" width="96" height="96" alt="MiniCAD" />
</p>

<h1 align="center">MiniCAD</h1>

<p align="center">
  <strong>开源 2D/3D CAD 应用，使用C++20开发。</strong><br />
  在windows和浏览器内运行CAD，内核使用SMLib, 支持三维Node节点建模，<strong>兼容dwg、天正</strong>。
</p>

<p align="center">
  <a href="https://minicad.bimhub.top">网页版</a> ·
  <a href="https://github.com/CHMOSE023/MiniCAD/releases">下载桌面版</a>     
</p>
 
<p align="center">
  <a href="https://github.com/CHMOSE023/MiniCAD/actions/workflows/build.yml">
  <img src="https://github.com/CHMOSE023/MiniCAD/actions/workflows/build.yml/badge.svg" alt="CI" />
  </a>
  <a href="https://github.com/CHMOSE023/MiniCAD/releases/latest"><img src="https://img.shields.io/github/v/release/CHMOSE023/MiniCAD?label=release" alt="Latest release" /></a>
  <a href="https://github.com/CHMOSE023/MiniCAD/releases"><img src="https://img.shields.io/github/downloads/CHMOSE023/MiniCAD/total?label=downloads" alt="Downloads" /></a>
  <a href="LICENSE"><img src="https://img.shields.io/github/license/CHMOSE023/MiniCAD" alt="Licence" /></a>
  <a href="https://qm.qq.com/q/O08yWfRvmE"><img src="https://img.shields.io/badge/QQ群-加入-12B7F5?logo=tencentqq&logoColor=white" alt="QQ群" /></a>
  <a href="https://github.com/CHMOSE023/MiniCAD/stargazers"><img src="https://img.shields.io/github/stars/CHMOSE023/MiniCAD?style=flat" alt="Stars" /></a>
</p>

![MiniCAD](docs/images/screenshot.jpg)
 

## Windows版编译

需要 CMake 3.20+、Ninja、Visual Studio（MSVC，C++20）。

```bat
cmake --preset debug
cmake --build --preset debug
out\debug\MiniCADWin\MiniCADWin.exe
```
**选择以下任意环境编译** 

-  "Developer Command Prompt for VS"中执行上述命令

-  **PowerShell** VS开发环境： cmd /k """C:\Program Files\Microsoft Visual Studio\18\Community\Common7\Tools\VsDevCmd.bat"" -arch=x64 -host_arch=x64"

-  **PowerShell** x64编译环境： cmd /k """C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat"""


**发布版用 `release`编译** 

## Web版编译

需要 [Emscripten SDK](https://emscripten.org)（路径 `D:\dev\emsdk`）, Web版编译脚本 `build_wasm.bat` 。

```bat
build_wasm.bat serve
```

输出目录 `out/build/wasm/MiniCADWeb/`，serve 参数，自动启动本地服务器， http://localhost:8080/MiniCADWeb。

## 测试

```bat
run_tests.bat
```
    
## 第三方组件

- SMLib实体建模 [usd-brep](https://github.com/NVIDIA-Omniverse/usd-brep)
- DWG解析移植自 [ACadSharp](https://github.com/DomCR/ACadSharp)
- 图像加载使用 [stb_image](https://github.com/nothings/stb/blob/master/stb_image.h)
- 字体处理使用 [stb_truetype](https://github.com/nothings/stb/blob/master/stb_truetype.h)
- 网页界面字体 [Noto Sans SC](https://github.com/notofonts/noto-cjk)  
- 测试字体  `GB2312.ttf` `simplex.shx`  `TSSDCHN.SHX` `tssdeng.shx`

## 入门教程

[LearnMiniCAD](https://github.com/CHMOSE023/LearnMiniCAD) 学习开发CAD。

## 致谢
在积极3D开发功能，版本更新快，期待能尽快稳定下拉，也会尝试加入图像渲染和动画功能，欢迎为仓库贡献。

感谢为MiniCAD做出贡献的人。

[@七只蜗牛](https://github.com/qizhiwoniu)   [@wkslgjx](https://github.com/wkslgjx)  

  
## 许可证

[MIT](LICENSE) © 2026 CHMOSE023
