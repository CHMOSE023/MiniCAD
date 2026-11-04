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
只保留 GB2312 字符、常用符号和源码里出现的字符）；韩文后备字体 `assets/fonts/NotoSansKR-UI.otf` 由 Noto Sans CJK KR 生成
（只保留 KS X 1001 的韩文音节、兼容字母和源码里出现的韩文）。

- 来源：Google Noto Fonts（https://github.com/notofonts/noto-cjk）
- 许可证：SIL Open Font License 1.1（https://openfontlicense.org），字体文件的 name 表里附有版权与许可声明

## 单线 SHX 字体的笔画数据

SHX 文字缺字时回退用的单线字体 `assets/fonts/minicad_sc.shx`、`minicad_tc.shx`、`minicad_jp.shx`、`minicad_kr.shx`
由 `tools/make_shx_cjk.py` 生成：读入下面两份数据里每一笔的中线坐标，整理后写成 SHX 的画线指令。
韩文音节、标点、常用符号与钢筋符号是脚本里自绘的。

### Make Me a Hanzi（minicad_sc.shx、minicad_tc.shx）

- 来源：https://github.com/skishore/makemeahanzi 的 `graphics.txt`，它取自 Arphic PL KaitiM GB 与 Arphic PL UKai
- 许可证：Arphic Public License，原文随字体放在 `assets/fonts/ARPHICPL.TXT`；生成的两个字体同样按 Arphic Public License 发布

### KanjiVG（minicad_jp.shx）

- 来源：KanjiVG（https://kanjivg.tagaini.net），Copyright (C) 2009-2013 Ulrich Apel
- 许可证：Creative Commons Attribution-Share Alike 3.0（https://creativecommons.org/licenses/by-sa/3.0/）；
  生成的 `minicad_jp.shx` 同样按 CC BY-SA 3.0 发布，字体信息里注明了来源

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
