# stb（第三方：单文件库）

来源：[nothings/stb](https://github.com/nothings/stb)，公有领域（或 MIT，二选一，见各文件末尾）。

| 文件 | 版本 | 用途 |
|---|---|---|
| `stb_image.h` | 见文件开头 | 光栅图像解码（`src/Core/Image/ImageLibrary.cpp`） |
| `stb_truetype.h` | v1.26 | TrueType 字形轮廓：图纸里的 TTF 文字（`src/Text/Font/TTFFont.cpp`）与 MiniGUI 的界面字体（`src/UI/Text/Font.cpp`），两处各编一份私有副本 |

## 本地修改

对上游源码的改动都记在这里，便于更新时重新应用；改动处标有"MiniCAD 修改"。

- `stb_truetype.h` 的 `stbtt__GetGlyphShapeTT`：轮廓以曲线外的点开头时，上游无条件读取下一个点 `vertices[off+i+1]` 来定起点。轮廓只有这一个点时（如宋体 simsun.ttc 的空格），读到的是下一个轮廓的点，最后一个轮廓则越过数组末尾读到未初始化的堆内存，起点随机，字形画成一串长斜线，而且每次运行都不同。改为：下一个点不在本轮廓里时从这个点自身开始（字形只剩一条零长度线段）。测试：`tests/Core/TextStyleTests.cpp` 第 7 节（现做一个只有单点轮廓的最小字体）
