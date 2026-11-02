"""生成网页版界面字体：Noto Sans SC 可变字体 → Regular 字重 + 常用字子集；另从 Noto Sans CJK KR 取韩文子集。

浏览器里读不到系统字体，WASM 版的 MiniGUI 界面使用这里生成的字体文件。
主字体覆盖：ASCII、拉丁补充、常用标点与符号、假名、GB2312 全部字符，以及源码、界面描述文件和翻译文件（assets/i18n）里出现的所有字符。
韩文字体覆盖：KS X 1001 的 2350 个韩文音节、兼容字母，以及上述文件里出现的韩文（Noto Sans SC 没有韩文）。

用法（需要 pip install fonttools）：
    python tools/make_web_font.py [源字体路径] [韩文源字体路径]
默认源字体为 Windows 自带的 C:/Windows/Fonts/NotoSansSC-VF.ttf；韩文源字体为 NotoSansCJK-Regular.ttc
（Ubuntu 的 fonts-noto-cjk，/usr/share/fonts/opentype/noto/），找不到时不生成韩文字体。两者均为 SIL OFL 1.1。
输出：assets/fonts/NotoSansSC-UI.ttf、assets/fonts/NotoSansKR-UI.otf
"""
import pathlib
import sys

from fontTools import subset
from fontTools.ttLib import TTCollection, TTFont
from fontTools.varLib import instancer

ROOT = pathlib.Path(__file__).resolve().parent.parent
SOURCE = pathlib.Path(sys.argv[1] if len(sys.argv) > 1 else "C:/Windows/Fonts/NotoSansSC-VF.ttf")
OUTPUT = ROOT / "assets" / "fonts" / "NotoSansSC-UI.ttf"
HANGUL_SOURCE = pathlib.Path(sys.argv[2] if len(sys.argv) > 2 else "/usr/share/fonts/opentype/noto/NotoSansCJK-Regular.ttc")
HANGUL_OUTPUT = ROOT / "assets" / "fonts" / "NotoSansKR-UI.otf"


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
                "wasm/**/*.cpp", "wasm/**/*.h", "assets/ui/*.json", "assets/i18n/*.json"]
    for pattern in patterns:
        for path in ROOT.glob(pattern):
            try:
                chars.update(path.read_text(encoding="utf-8"))
            except UnicodeDecodeError:
                pass
    return {c for c in chars if ord(c) >= 0x20}


def is_hangul(c):
    o = ord(c)
    return 0x1100 <= o <= 0x11FF or 0x3130 <= o <= 0x318F or 0xAC00 <= o <= 0xD7A3


def hangul_chars():
    chars = {c for c in source_chars() if is_hangul(c)}
    for hi in range(0xB0, 0xC9):
        for lo in range(0xA1, 0xFF):
            chars.add(bytes([hi, lo]).decode("euc-kr"))
    chars.update(chr(c) for c in range(0x3131, 0x318F))
    return chars


def subset_options():
    options = subset.Options()
    options.layout_features = ["*"]
    options.name_IDs = ["*"]
    options.name_languages = ["*"]
    options.notdef_outline = True
    options.hinting = False
    return options


def make_hangul_font():
    if not HANGUL_SOURCE.exists():
        print(f"没有 {HANGUL_SOURCE}，跳过韩文字体")
        return
    font = None
    if HANGUL_SOURCE.suffix.lower() == ".ttc":
        collection = TTCollection(HANGUL_SOURCE)
        font = next((f for f in collection.fonts if f["name"].getDebugName(1) == "Noto Sans CJK KR"), None)
        if font is None:
            sys.exit(f"{HANGUL_SOURCE} 里没有 Noto Sans CJK KR")
    else:
        font = TTFont(HANGUL_SOURCE)

    subsetter = subset.Subsetter(subset_options())
    subsetter.populate(text="".join(sorted(hangul_chars())))
    subsetter.subset(font)
    font.save(HANGUL_OUTPUT)
    print(f"{HANGUL_OUTPUT}  {HANGUL_OUTPUT.stat().st_size / 1024 / 1024:.2f} MB  {len(font.getBestCmap())} 个字符")


def main():
    chars = gb2312_chars() | source_chars()
    ranges = [(0x20, 0x7E), (0xA0, 0x17F), (0x2000, 0x206F), (0x2100, 0x214F), (0x2190, 0x21FF),
              (0x2200, 0x22FF), (0x2460, 0x24FF), (0x2500, 0x25FF), (0x3000, 0x30FF), (0xFF00, 0xFFEF)]
    for lo, hi in ranges:
        chars.update(chr(c) for c in range(lo, hi + 1))

    font = TTFont(SOURCE)
    # 可变字体默认是 Thin（100），固定到 Regular（400）并去掉变体表（stb_truetype 只读默认轮廓）
    font = instancer.instantiateVariableFont(font, {"wght": 400})

    subsetter = subset.Subsetter(subset_options())
    subsetter.populate(text="".join(sorted(chars)))
    subsetter.subset(font)

    OUTPUT.parent.mkdir(parents=True, exist_ok=True)
    font.save(OUTPUT)
    print(f"{OUTPUT}  {OUTPUT.stat().st_size / 1024 / 1024:.2f} MB  {len(font.getBestCmap())} 个字符")

    make_hangul_font()


if __name__ == "__main__":
    main()
