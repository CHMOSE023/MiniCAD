"""生成网页版界面字体：Noto Sans SC 可变字体 → Regular 字重 + 常用字子集。

浏览器里读不到系统字体，WASM 版的 MiniGUI 界面使用这里生成的字体文件。
覆盖范围：ASCII、拉丁补充、常用标点与符号、GB2312 全部字符，以及源码和界面描述文件里出现的所有字符。

用法（需要 pip install fonttools）：
    python tools/make_web_font.py [源字体路径]
默认源字体为 Windows 自带的 C:/Windows/Fonts/NotoSansSC-VF.ttf（SIL OFL 1.1）。
输出：assets/fonts/NotoSansSC-UI.ttf
"""
import pathlib
import sys

from fontTools import subset
from fontTools.ttLib import TTFont
from fontTools.varLib import instancer

ROOT = pathlib.Path(__file__).resolve().parent.parent
SOURCE = pathlib.Path(sys.argv[1] if len(sys.argv) > 1 else "C:/Windows/Fonts/NotoSansSC-VF.ttf")
OUTPUT = ROOT / "assets" / "fonts" / "NotoSansSC-UI.ttf"


def gb2312_chars():
    chars = set()
    for hi in range(0xA1, 0xF8):
        for lo in range(0xA1, 0xFF):
            try:
                chars.add(bytes([hi, lo]).decode("gb2312"))
            except UnicodeDecodeError:
                pass
    return chars


def source_chars():
    chars = set()
    patterns = ["src/**/*.cpp", "src/**/*.h", "src/**/*.hpp", "apps/**/*.cpp", "apps/**/*.h",
                "wasm/**/*.cpp", "wasm/**/*.h", "assets/ui/*.json"]
    for pattern in patterns:
        for path in ROOT.glob(pattern):
            try:
                chars.update(path.read_text(encoding="utf-8"))
            except UnicodeDecodeError:
                pass
    return {c for c in chars if ord(c) >= 0x20}


def main():
    chars = gb2312_chars() | source_chars()
    ranges = [(0x20, 0x7E), (0xA0, 0x17F), (0x2000, 0x206F), (0x2100, 0x214F), (0x2190, 0x21FF),
              (0x2200, 0x22FF), (0x2460, 0x24FF), (0x2500, 0x25FF), (0x3000, 0x303F), (0xFF00, 0xFFEF)]
    for lo, hi in ranges:
        chars.update(chr(c) for c in range(lo, hi + 1))

    font = TTFont(SOURCE)
    # 可变字体默认是 Thin（100），固定到 Regular（400）并去掉变体表（stb_truetype 只读默认轮廓）
    font = instancer.instantiateVariableFont(font, {"wght": 400})

    options = subset.Options()
    options.layout_features = ["*"]
    options.name_IDs = ["*"]
    options.name_languages = ["*"]
    options.notdef_outline = True
    options.hinting = False
    subsetter = subset.Subsetter(options)
    subsetter.populate(text="".join(sorted(chars)))
    subsetter.subset(font)

    OUTPUT.parent.mkdir(parents=True, exist_ok=True)
    font.save(OUTPUT)
    print(f"{OUTPUT}  {OUTPUT.stat().st_size / 1024 / 1024:.2f} MB  {len(font.getBestCmap())} 个字符")


if __name__ == "__main__":
    main()
