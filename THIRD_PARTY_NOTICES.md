# 第三方声明

## ACadSharp

MiniDWG（`src/Serialization/` 下的 `Codec/`、`Database/`、`Dwg/`、`Dxf/`）的 DWG/DXF 读写实现移植自 [ACadSharp](https://github.com/DomCR/ACadSharp)（C#），
`tests/Data/Dwg/` 下的样例图纸（`sample_AC10xx.*`）也取自该项目的 `samples/` 目录。

- 移植基准提交：`78a562a66a5e43820a00f13be6645adc626071c9`（2026-10-06）
- 许可证：MIT，原文如下

```
MIT License

Copyright (c) 2021 Albert Domenech

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
```

## Noto Sans SC

网页版界面字体 `assets/fonts/NotoSansSC-UI.ttf` 由 Noto Sans SC 生成（`tools/make_web_font.py`：固定为 Regular 字重，
只保留 GB2312 字符、常用符号和源码里出现的字符）。

- 来源：Google Noto Fonts（https://github.com/notofonts/noto-cjk）
- 许可证：SIL Open Font License 1.1（https://openfontlicense.org），字体文件的 name 表里附有版权与许可声明


## SDL3

Linux 版（`MiniCADLinux`）的窗口、输入、剪贴板与文件对话框使用 SDL3，发布包里以静态库链入。

- 来源：https://github.com/libsdl-org/SDL（版本见 `.github/workflows/build.yml` 的 `SDL3_VERSION`）
- 许可证：zlib，原文如下

```
Copyright (C) 1997-2026 Sam Lantinga <slouken@libsdl.org>

This software is provided 'as-is', without any express or implied
warranty.  In no event will the authors be held liable for any damages
arising from the use of this software.

Permission is granted to anyone to use this software for any purpose,
including commercial applications, and to alter it and redistribute it
freely, subject to the following restrictions:

1. The origin of this software must not be misrepresented; you must not
   claim that you wrote the original software. If you use this software
   in a product, an acknowledgment in the product documentation would be
   appreciated but is not required.
2. Altered source versions must be plainly marked as such, and must not be
   misrepresented as being the original software.
3. This notice may not be removed or altered from any source distribution.
```
