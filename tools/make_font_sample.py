"""生成起始页的示例图纸"字体与符号"：随程序带的字体、钢筋符号、控制码与常用工程符号；
右侧每种语言一张 A2 字表，列出单线字体 minicad_sc / tc / jp / kr.shx 收录的全部字（从 assets/fonts 里的字体读出）。

先写 DXF（R12，非 ASCII 字符写成 \\U+XXXX），再由 MiniCADDwgInspect 按 MiniCAD 打开、存成 DWG 2004，复制到 assets/samples/：
    python -X utf8 tools/make_font_sample.py out/tmp/fonts-and-symbols.dxf
    out/debug/tests/MiniCADDwgInspect.exe --convert out/tmp/fonts-and-symbols.dxf out/tmp/fonts-and-symbols.dwg
    copy out\\tmp\\fonts-and-symbols.dwg assets\\samples\\
"""
import os
import struct
import sys

# (样式名, 主字体, 大字体)；minicad_*.shx 是 Unicode 单线字体，MiniCAD 里可直接当大字体用，SHX 缺字时也自动从它们补
STYLES = [
    ("Standard", "tssdeng.shx", "TSSDCHN.SHX"),
    ("SIMPLEX", "simplex.shx", "TSSDCHN.SHX"),
    ("MC_SC", "tssdeng.shx", "minicad_sc.shx"),
    ("MC_TC", "tssdeng.shx", "minicad_tc.shx"),
    ("MC_JP", "tssdeng.shx", "minicad_jp.shx"),
    ("MC_KR", "tssdeng.shx", "minicad_kr.shx"),
    ("GB2312", "GB2312.ttf", ""),
]
LAYERS = [("FRAME", 7), ("GRID", 8), ("TITLE", 2), ("TEXT", 7), ("SYMBOL", 3), ("NOTE", 9)]

out = []


def g(code, val):
    out.append(str(code))
    out.append(val if isinstance(val, str) else repr(float(val)) if isinstance(val, float) else str(val))


def esc(s):
    return "".join(c if ord(c) < 0x80 else "\\U+%04X" % ord(c) for c in s)


def line(x0, y0, x1, y1, layer="GRID"):
    g(0, "LINE"); g(8, layer); g(10, float(x0)); g(20, float(y0)); g(30, 0.0); g(11, float(x1)); g(21, float(y1)); g(31, 0.0)


def rect(x0, y0, x1, y1, layer="FRAME"):
    line(x0, y0, x1, y0, layer); line(x1, y0, x1, y1, layer); line(x1, y1, x0, y1, layer); line(x0, y1, x0, y0, layer)


def text(x, y, s, h=3.5, style="Standard", layer="TEXT", center=False):
    g(0, "TEXT"); g(8, layer); g(10, float(x)); g(20, float(y)); g(30, 0.0); g(40, float(h)); g(1, esc(s)); g(7, style)
    if center:
        g(72, 1); g(11, float(x)); g(21, float(y)); g(31, 0.0)


def table(x, y, widths, rows, row_h=8.0, h=3.5, styles=None, header_style="Standard"):
    """rows[0] 是表头；styles[i] 是第 i 行（不含表头）各格的样式，None 时用 Standard。返回表格底边 y"""
    total = sum(widths)
    n = len(rows)
    for i in range(n + 1):
        line(x, y - i * row_h, x + total, y - i * row_h)
    cx = x
    for w in [0] + widths:
        cx += w
        line(cx, y, cx, y - n * row_h)
    line(x, y, x, y - n * row_h)
    for r, row in enumerate(rows):
        cx = x
        for c, cell in enumerate(row):
            st = header_style if r == 0 else (styles[r - 1][c] if styles and styles[r - 1][c] else "Standard")
            text(cx + 2.0, y - r * row_h - row_h / 2 - h / 2, cell, h, st, "TITLE" if r == 0 else "TEXT")
            cx += widths[c]
    return y - n * row_h


def section(x, y, title):
    text(x, y, title, 5.0, "Standard", "TITLE")


def build():
    W, H = 420.0, 297.0
    rect(0, 0, W, H)
    rect(10, 10, W - 10, H - 10)
    text(W / 2, H - 24, "MiniCAD 字体与符号示例", 8.0, "Standard", "TITLE", center=True)

    # 一、字体
    y = H - 36
    section(16, y, "一、随程序提供的字体（文字样式）")
    rows = [["样式名", "主字体", "大字体", "示例"]]
    samples = {
        "Standard": "探索者字体：钢筋混凝土结构设计说明 ABC abc 0123",
        "SIMPLEX": "Simplex 西文 + 探索者中文：建筑施工图 ABC 0123",
        "MC_SC": "简体单线：结构平面布置图 梁 板 柱 基础 楼梯",
        "MC_TC": "繁體單線：鋼筋混凝土結構設計說明 樓層 圖紙",
        "MC_JP": "日本語：鉄筋コンクリート構造 ひらがな カタカナ",
        "MC_KR": "한국어：철근콘크리트 구조 설계 도면",
        "GB2312": "TrueType 填充字体：宋体 GB2312（与单线字分开使用）",
    }
    styles = []
    for name, font, big in STYLES:
        rows.append([name, font, big or "—", samples[name]])
        styles.append([None, None, None, name])
    y = table(16, y - 6, [36, 40, 44, 268], rows, styles=styles)

    # 二、钢筋符号
    y0 = y - 12
    section(16, y0, "二、钢筋符号（控制码 %%%%%%130～%%%%%%133）")
    rows = [["控制码", "符号", "钢筋牌号", "标注示例"],
            ["%%%%%%130", "%%130", "HPB300（Ⅰ级，光圆）", "%%1308@200    2%%13012"],
            ["%%%%%%131", "%%131", "HRB335（Ⅱ级）", "4%%13120    %%13110@150"],
            ["%%%%%%132", "%%132", "HRB400（Ⅲ级）", "%%13210@100/200(2)    3%%13225"],
            ["%%%%%%133", "%%133", "RRB400（Ⅳ级，余热处理）", "2%%13322    %%13312@200"]]
    yb = table(16, y0 - 6, [28, 18, 66, 82], rows, row_h=9.0, h=4.0)
    text(16, yb - 7, "说明：钢筋符号是形号，主字体（如 tssdeng.shx）里没有时 MiniCAD 从单线字体 minicad_*.shx 补上", 3.0, layer="NOTE")

    # 三、控制码
    section(222, y0, "三、控制码")
    rows = [["写法", "显示", "说明"],
            ["%%%%%%c", "%%c20", "直径"],
            ["%%%%%%d", "45%%d", "度"],
            ["%%%%%%p", "%%p0.000", "正负公差"],
            ["%%%%%%u", "%%u下划线%%u", "下划线开关"],
            ["%%%%%%o", "%%o上划线%%o", "上划线开关"],
            ["%%%%%%%%%", "50%%%", "百分号"]]
    table(222, y0 - 6, [30, 52, 100], rows, row_h=9.0, h=4.0)

    # 四、工程符号
    y = yb - 20
    section(16, y, "四、常用工程符号与标点（Standard 样式）")
    lines = [
        ("数学", "° ± × ÷ ≤ ≥ ≠ ≈ ∞ √ ∠ ⊥ ∥ ‰ ℃ ∅ Φ φ"),
        ("图形", "△ □ ○ ◎ ◇ ※ → ← ↑ ↓"),
        ("罗马数字", "Ⅰ Ⅱ Ⅲ Ⅳ Ⅴ Ⅵ Ⅶ Ⅷ Ⅸ Ⅹ Ⅺ Ⅻ"),
        ("希腊字母", "α β γ δ ε θ λ μ π σ τ φ ω Δ Σ Ω"),
        ("标点", "，。、：；？！“”‘’（）【】《》〈〉「」『』〔〕…—～·"),
        ("标注示例", "C30 混凝土  保护层 25mm  坡度 i=2%%%  标高 %%p0.000  抗震等级 Ⅱ 级  %%c600 圆柱"),
    ]
    for k, (label, s) in enumerate(lines):
        yy = y - 10 - k * 8
        text(20, yy, label, 3.5, layer="TITLE")
        text(52, yy, s, 4.0, layer="SYMBOL")

    # 五、混排
    y = y - 10 - len(lines) * 8 - 6
    section(16, y, "五、简繁日韩混排（Standard 样式：TSSDCHN 缺的字自动从单线字体补，不混用 TrueType）")
    text(20, y - 10, "简体：建筑结构设计  繁體：建築結構設計  日本語：建築構造設計  한국어：건축 구조 설계", 4.5)
    text(20, y - 19, "字库中都没有的字显示为“?”，与 AutoCAD 一致", 3.0, layer="NOTE")

    text(16, 16, "单线字体 minicad_sc / tc / kr 的汉字笔画取自 Make Me a Hanzi（Arphic PL），minicad_jp 取自 KanjiVG（CC BY-SA 3.0），"
                 "韩文、标点、符号与钢筋符号为 MiniCAD 自绘", 2.5, layer="NOTE")


FONT_DIR = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "assets", "fonts")


def font_codes(name):
    with open(os.path.join(FONT_DIR, name), "rb") as f:
        d = f.read()
    p = d.index(b"\x1a") + 1
    n, = struct.unpack_from("<I", d, p)
    ln, = struct.unpack_from("<H", d, p + 4)
    p += 6 + ln
    codes = set()
    for _ in range(n - 1):
        c, l = struct.unpack_from("<HH", d, p)
        p += 4 + l
        codes.add(c)
    return codes


def is_han(c):
    return 0x3400 <= c <= 0x9FFF or 0xF900 <= c <= 0xFAFF


def dbcs_order(enc, lead, trail):
    """按编码顺序列出某个双字节字符集里的汉字：[(编码, 码位)]，重码只取第一次"""
    seen, out = set(), []
    for a in lead:
        for b in trail:
            try:
                c = ord(bytes([a, b]).decode(enc))
            except UnicodeDecodeError:
                continue
            if is_han(c) and c not in seen:
                seen.add(c)
                out.append(("%02X%02X" % (a, b), c))
    return out


def char_groups():
    sc, tc, jp, kr = (font_codes("minicad_%s.shx" % k) for k in ("sc", "tc", "jp", "kr"))
    gb = dbcs_order("gb2312", range(0xB0, 0xF8), range(0xA1, 0xFF))
    gb_set = {c for _, c in gb}
    big5 = dbcs_order("big5", range(0xA4, 0xFA), list(range(0x40, 0x7F)) + list(range(0xA1, 0xFF)))
    tc_all = sc | tc
    big5_in = [(k, c) for k, c in big5 if c in tc_all]
    common = [(k, c) for k, c in big5_in if k < "C6A1"]
    u = lambda cs: [("U+%04X" % c, c) for c in sorted(cs)]
    return [
        ("简体中文", "MC_SC", "minicad_sc.shx", None, [
            ("GB2312 一级汉字（按拼音）", [(k, c) for k, c in gb if k < "D8A0"]),
            ("GB2312 二级汉字（按部首）", [(k, c) for k, c in gb if k >= "D8A0"]),
            ("GB2312 以外的简体字", u(c for c in sc if is_han(c) and c not in gb_set)),
        ]),
        ("繁體中文", "MC_TC", "minicad_tc.shx", "Big5 共 %d 个汉字，目前收录其中 %d 个，其余用到时显示为“?”；与简体同形的字存在 minicad_sc.shx 里，显示时自动补上"
         % (len(big5), len(big5_in)), [
            ("Big5 常用字（按笔画）", common),
            ("Big5 次常用字（按笔画）", [(k, c) for k, c in big5_in if k >= "C6A1"]),
        ]),
        ("日本語", "MC_JP", "minicad_jp.shx", None, [
            ("平假名、片假名", u(c for c in jp if 0x3040 <= c <= 0x30FF)),
            ("汉字", u(c for c in jp if is_han(c))),
        ]),
        ("한국어", "MC_KR", "minicad_kr.shx", None, [
            ("兼容字母", u(c for c in kr if 0x3131 <= c <= 0x318E)),
            ("音节", u(c for c in kr if 0xAC00 <= c <= 0xD7A3)),
        ]),
    ]


def char_sheet(x0, title, style, font, note, groups):
    """一种语言一张 A2（420×594）：分组列出字体里的全部字，每行开头标第一个字的编码"""
    W, H = 420.0, 594.0
    rect(x0, 0, x0 + W, H)
    rect(x0 + 10, 10, x0 + W - 10, H - 10)
    total = sum(len(cs) for _, cs in groups)
    text(x0 + W / 2, H - 28, "%s：%s（样式 %s），共 %d 字" % (title, font, style, total), 8.0, "Standard", "TITLE", center=True)
    if note:
        text(x0 + 20, H - 42, note, 3.0, layer="NOTE")
    top, bottom, span = H - 52, 20.0, 360.0
    h = 5.0
    while h > 2.0:
        cols = int(span / h) // 10 * 10
        rows = sum(-(-len(cs) // cols) + 2 for _, cs in groups)
        if rows * h * 1.5 <= top - bottom:
            break
        h -= 0.25
    pitch = h * 1.5
    y = top
    for name, cs in groups:
        y -= pitch
        section(x0 + 20, y, "%s（%d 字）" % (name, len(cs)))
        y -= pitch * 0.5
        for i in range(0, len(cs), cols):
            y -= pitch
            row = cs[i:i + cols]
            text(x0 + 20, y, row[0][0], min(h * 0.6, 2.5), layer="NOTE")
            text(x0 + W - 20 - span, y, "".join(chr(c) for _, c in row), h, style)
        y -= pitch * 0.5
    return total


def main():
    build()
    for i, (title, style, font, note, groups) in enumerate(char_groups()):
        n = char_sheet(430.0 * (i + 1), title, style, font, note, groups)
        print("%s: %d" % (font, n))
    head = []
    out_body = out[:]
    out.clear()
    g(0, "SECTION"); g(2, "HEADER"); g(9, "$ACADVER"); g(1, "AC1009"); g(9, "$DWGCODEPAGE"); g(3, "ANSI_936")
    g(9, "$INSUNITS"); g(70, 4); g(0, "ENDSEC")
    g(0, "SECTION"); g(2, "TABLES")
    g(0, "TABLE"); g(2, "LAYER"); g(70, len(LAYERS))
    for name, color in LAYERS:
        g(0, "LAYER"); g(2, name); g(70, 0); g(62, color); g(6, "CONTINUOUS")
    g(0, "ENDTAB")
    g(0, "TABLE"); g(2, "STYLE"); g(70, len(STYLES))
    for name, font, big in STYLES:
        g(0, "STYLE"); g(2, name); g(70, 0); g(40, 0.0); g(41, 1.0); g(50, 0.0); g(71, 0); g(42, 3.5); g(3, font); g(4, big)
    g(0, "ENDTAB")
    g(0, "ENDSEC")
    g(0, "SECTION"); g(2, "ENTITIES")
    head = out[:]
    out.clear()
    g(0, "ENDSEC"); g(0, "EOF")
    with open(sys.argv[1], "w", encoding="ascii", newline="\r\n") as f:
        f.write("\n".join(head + out_body + out) + "\n")


if __name__ == "__main__":
    main()
