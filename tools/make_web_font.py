"""生成网页版界面字体：Noto Sans SC 可变字体 → Regular 字重 + 常用字子集；另从 Noto Sans CJK 取韩文、繁体、日文子集。

浏览器里读不到系统字体，WASM 版的 MiniGUI 界面与图纸文字的缺字回退使用这里生成的字体文件。
主字体覆盖：ASCII、拉丁补充、常用标点与符号、假名、GB2312 全部字符，以及源码、界面描述文件和翻译文件（assets/i18n）里出现的所有字符。
韩文字体覆盖：KS X 1001 的 2350 个韩文音节、兼容字母，以及上述文件里出现的韩文（Noto Sans SC 没有韩文）。
繁体字体覆盖 Big5 的常用字，日文字体覆盖 JIS X 0208 的第一水准，都加上 ASCII 与上述文件里的字符；
这两个只给网页版用（apps/Web/fonts，启动后才下载），桌面版、Linux、安卓用系统字体。

用法（需要 pip install fonttools）：
    python tools/make_web_font.py [源字体路径] [Noto Sans CJK 路径]
默认源字体为 Windows 自带的 C:/Windows/Fonts/NotoSansSC-VF.ttf；Noto Sans CJK 为 NotoSansCJK-Regular.ttc
（Ubuntu 的 fonts-noto-cjk，/usr/share/fonts/opentype/noto/），找不到时不生成韩文、繁体、日文字体。均为 SIL OFL 1.1。
输出：assets/fonts/NotoSansSC-UI.ttf、assets/fonts/NotoSansKR-UI.otf、apps/Web/fonts/NotoSansTC-UI.otf、apps/Web/fonts/NotoSansJP-UI.otf
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
WEB_FONT_DIR = ROOT / "apps" / "Web" / "fonts"
ASCII = {chr(c) for c in range(0x20, 0x7F)}


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
    return chars | ASCII


def double_byte_chars(encoding, leads, trails):
    chars = set()
    for hi in leads:
        for lo in trails:
            try:
                chars.add(bytes([hi, lo]).decode(encoding))
            except UnicodeDecodeError:
                pass
    return chars


def big5_chars():
    """Big5 的符号与常用字（A140～C67E，5401 个汉字）"""
    trails = list(range(0x40, 0x7F)) + list(range(0xA1, 0xFF))
    return double_byte_chars("big5", range(0xA1, 0xC6), trails) | double_byte_chars("big5", [0xC6], range(0x40, 0x7F))


def jis_chars():
    """JIS X 0208 的非汉字与第一水准汉字（1～47 区，2965 个汉字）"""
    return double_byte_chars("euc_jp", range(0xA1, 0xD0), range(0xA1, 0xFF))


def subset_options():
    options = subset.Options()
    options.layout_features = ["*"]
    options.name_IDs = ["*"]
    options.name_languages = ["*"]
    options.notdef_outline = True
    options.hinting = False
    return options


def cjk_face(name):
    if HANGUL_SOURCE.suffix.lower() == ".ttc":
        collection = TTCollection(HANGUL_SOURCE)
        font = next((f for f in collection.fonts if f["name"].getDebugName(1) == name), None)
        if font is None:
            sys.exit(f"{HANGUL_SOURCE} 里没有 {name}")
        return font
    return TTFont(HANGUL_SOURCE)


def make_cjk_font(face, chars, output):
    font = cjk_face(face)
    subsetter = subset.Subsetter(subset_options())
    subsetter.populate(text="".join(sorted(chars)))
    subsetter.subset(font)
    output.parent.mkdir(parents=True, exist_ok=True)
    font.save(output)
    print(f"{output}  {output.stat().st_size / 1024 / 1024:.2f} MB  {len(font.getBestCmap())} 个字符")


def make_cjk_fonts():
    if not HANGUL_SOURCE.exists():
        print(f"没有 {HANGUL_SOURCE}，跳过韩文、繁体、日文字体")
        return
    make_cjk_font("Noto Sans CJK KR", hangul_chars(), HANGUL_OUTPUT)
    extra = ASCII | source_chars()
    make_cjk_font("Noto Sans CJK TC", big5_chars() | extra, WEB_FONT_DIR / "NotoSansTC-UI.otf")
    make_cjk_font("Noto Sans CJK JP", jis_chars() | extra, WEB_FONT_DIR / "NotoSansJP-UI.otf")


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

    make_cjk_fonts()


if __name__ == "__main__":
    main()
