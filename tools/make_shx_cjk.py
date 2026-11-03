"""生成单线 SHX 字体 minicad_sc / minicad_tc / minicad_jp / minicad_kr（Unicode 编码的 unifont）。

SHX 文字缺字时只在单线字体之间回退（FontEngine::ShxFallbackChain），不混用 TrueType 填充字体；
这几个字体补上常用 SHX 大字体没有的字：繁体字配简体大字体、日文假名、韩文等。

笔画来源（都是笔画中线的坐标，换成 SHX 的画线指令）：
- 简体、繁体、韩文汉字：Make Me a Hanzi 的 graphics.txt（https://github.com/skishore/makemeahanzi），
  取自 Arphic PL KaitiM GB / UKai，按 Arphic Public License 发布，生成的字体同样按 Arphic Public License
- 日文汉字与假名：KanjiVG（https://kanjivg.tagaini.net），CC BY-SA 3.0，生成的 minicad_jp.shx 同样按 CC BY-SA 3.0
- 韩文音节：由本脚本里的字母笔画按初声、中声、终声的位置拼成（全部 11172 个音节与兼容字母）
- 中日文标点、常用符号、钢筋符号（%%130～%%133，形号 0x82～0x85，与天正 tssdeng.shx 的形号一致）：本脚本自绘

字形规格与 TSSDCHN.SHX 相当：全角方格，字高单位 254（above=254、below=0），基线在格子底边。

用法：
    python -X utf8 tools/make_shx_cjk.py --mmh <graphics.txt> --kanjivg <kanjivg-*.xml.gz> [--out assets/fonts] [--fonts sc,tc,jp,kr]
"""
import argparse
import gzip
import json
import math
import pathlib
import re
import struct

ROOT = pathlib.Path(__file__).resolve().parent.parent
G = 254   # 每个全角格子的单位数（字高）

# ─────────────────────────────── 字符集 ───────────────────────────────


def dbcs_chars(encoding, leads, trails):
    out = []
    for a in leads:
        for b in trails:
            try:
                c = bytes([a, b]).decode(encoding)
            except UnicodeDecodeError:
                continue
            if len(c) == 1:
                out.append(c)
    return out


def gb2312_hanzi():
    return dbcs_chars("gb2312", range(0xB0, 0xF8), range(0xA1, 0xFF))


def big5_hanzi():
    trails = list(range(0x40, 0x7F)) + list(range(0xA1, 0xFF))
    return dbcs_chars("big5", list(range(0xA4, 0xC7)) + list(range(0xC9, 0xFA)), trails)


# ─────────────────────────────── 读笔画 ───────────────────────────────
# 统一换成"格子坐标"：0～1 为一个全角格子，y 向上，基线在 0


def load_mmh(path):
    # graphics.txt 的坐标：1024 一个字身，y 向上，字身底边 -124；按全部字的中线范围居中放进格子
    k, cx, cy = 1.0 / 900.0, 515.0, 395.0
    out = {}
    with open(path, encoding="utf-8") as f:
        for line in f:
            d = json.loads(line)
            out[d["character"]] = [[((x - cx) * k + 0.5, (y - cy) * k + 0.5) for x, y in m] for m in d["medians"]]
    return out


_NUM = re.compile(r"[-+]?(?:\d+\.?\d*|\.\d+)(?:[eE][-+]?\d+)?")
_TOK = re.compile(r"[MmCcSsLlHhVvZzQqTt]|[-+]?(?:\d+\.?\d*|\.\d+)(?:[eE][-+]?\d+)?")


def svg_path_points(d, steps=10):
    """SVG 路径 → 折线（三次、二次贝塞尔按段细分）"""
    toks = _TOK.findall(d)
    i, cmd = 0, None
    x = y = 0.0
    sx = sy = 0.0
    lcx = lcy = None
    pts = []

    def num():
        nonlocal i
        v = float(toks[i])
        i += 1
        return v

    def cubic(x0, y0, x1, y1, x2, y2, x3, y3):
        for s in range(1, steps + 1):
            t = s / steps
            u = 1 - t
            pts.append((u * u * u * x0 + 3 * u * u * t * x1 + 3 * u * t * t * x2 + t * t * t * x3,
                        u * u * u * y0 + 3 * u * u * t * y1 + 3 * u * t * t * y2 + t * t * t * y3))

    while i < len(toks):
        if re.fullmatch(r"[A-Za-z]", toks[i]):
            cmd = toks[i]
            i += 1
            if cmd in "Zz":
                pts.append((sx, sy))
                x, y = sx, sy
                continue
        rel = cmd.islower()
        c = cmd.upper()
        if c == "M":
            x, y = (x + num(), y + num()) if rel else (num(), num())
            sx, sy = x, y
            pts.append((x, y))
            cmd = "l" if rel else "L"
            lcx = None
        elif c == "L":
            x, y = (x + num(), y + num()) if rel else (num(), num())
            pts.append((x, y))
            lcx = None
        elif c == "H":
            x = x + num() if rel else num()
            pts.append((x, y))
            lcx = None
        elif c == "V":
            y = y + num() if rel else num()
            pts.append((x, y))
            lcx = None
        elif c == "C":
            a = [num() for _ in range(6)]
            if rel:
                a = [a[0] + x, a[1] + y, a[2] + x, a[3] + y, a[4] + x, a[5] + y]
            cubic(x, y, *a)
            lcx, lcy = a[2], a[3]
            x, y = a[4], a[5]
        elif c == "S":
            a = [num() for _ in range(4)]
            if rel:
                a = [a[0] + x, a[1] + y, a[2] + x, a[3] + y]
            c1 = (2 * x - lcx, 2 * y - lcy) if lcx is not None else (x, y)
            cubic(x, y, c1[0], c1[1], *a)
            lcx, lcy = a[0], a[1]
            x, y = a[2], a[3]
        elif c == "Q":
            a = [num() for _ in range(4)]
            if rel:
                a = [a[0] + x, a[1] + y, a[2] + x, a[3] + y]
            cubic(x, y, x + 2 / 3 * (a[0] - x), y + 2 / 3 * (a[1] - y),
                  a[2] + 2 / 3 * (a[0] - a[2]), a[3] + 2 / 3 * (a[1] - a[3]), a[2], a[3])
            lcx = None
            x, y = a[2], a[3]
        else:
            raise ValueError(f"不支持的路径指令 {cmd}")
    return pts


def load_kanjivg(path):
    # KanjiVG：109 一格，y 向下；笔画按 path 的出现顺序
    k, cx, cy = 1.0 / 92.5, 52.5, 53.0
    opener = gzip.open if str(path).endswith(".gz") else open
    with opener(path, "rt", encoding="utf-8") as f:
        text = f.read()
    out = {}
    for m in re.finditer(r'<kanji id="kvg:kanji_([0-9a-f]+)">(.*?)</kanji>', text, re.S):
        ch = chr(int(m.group(1), 16))
        strokes = []
        for d in re.findall(r'<path [^>]*\bd="([^"]+)"', m.group(2)):
            strokes.append([((px - cx) * k + 0.5, (cy - py) * k + 0.5) for px, py in svg_path_points(d)])
        if strokes:
            out[ch] = strokes
    return out


# ─────────────────────────────── 笔画整理 ───────────────────────────────


def _dist(a, b):
    return math.hypot(b[0] - a[0], b[1] - a[1])


def _turn(a, b, c):
    """a→b 与 b→c 的转角（度）"""
    v1 = (b[0] - a[0], b[1] - a[1])
    v2 = (c[0] - b[0], c[1] - b[1])
    n1, n2 = math.hypot(*v1), math.hypot(*v2)
    if n1 == 0 or n2 == 0:
        return 0.0
    cosv = max(-1.0, min(1.0, (v1[0] * v2[0] + v1[1] * v2[1]) / (n1 * n2)))
    return math.degrees(math.acos(cosv))


def rdp(pts, eps):
    if len(pts) < 3:
        return list(pts)
    a, b = pts[0], pts[-1]
    L = _dist(a, b)
    best, idx = -1.0, 0
    for i in range(1, len(pts) - 1):
        p = pts[i]
        if L == 0:
            d = _dist(a, p)
        else:
            d = abs((b[0] - a[0]) * (a[1] - p[1]) - (a[0] - p[0]) * (b[1] - a[1])) / L
        if d > best:
            best, idx = d, i
    if best <= eps:
        return [a, b]
    return rdp(pts[:idx + 1], eps)[:-1] + rdp(pts[idx:], eps)


def straighten(pts, h_tol=12.0, v_tol=10.0, min_len=0.05):
    """接近水平 / 竖直的笔段拉平拉直：每个点取所在横段的平均 y、竖段的平均 x"""
    if len(pts) < 2:
        return pts
    ys = [[] for _ in pts]
    xs = [[] for _ in pts]
    for i in range(len(pts) - 1):
        a, b = pts[i], pts[i + 1]
        dx, dy = b[0] - a[0], b[1] - a[1]
        if math.hypot(dx, dy) < min_len:
            continue
        ang = math.degrees(math.atan2(abs(dy), abs(dx)))
        if ang < h_tol:
            y = (a[1] + b[1]) / 2
            ys[i].append(y)
            ys[i + 1].append(y)
        elif ang > 90 - v_tol:
            x = (a[0] + b[0]) / 2
            xs[i].append(x)
            xs[i + 1].append(x)
    return [(sum(xs[i]) / len(xs[i]) if xs[i] else p[0], sum(ys[i]) / len(ys[i]) if ys[i] else p[1])
            for i, p in enumerate(pts)]


def clean_stroke(pts, snap=True):
    q = [pts[0]]
    for p in pts[1:]:
        if _dist(q[-1], p) > 1e-6:
            q.append(p)
    pts = rdp(q, 0.006)
    # 楷体中线起笔处的顿笔、收笔的回锋：很短又急转的首末段去掉
    if len(pts) >= 3 and _dist(pts[0], pts[1]) < 0.045 and _turn(pts[0], pts[1], pts[2]) > 40:
        pts = pts[1:]
    if len(pts) >= 3 and _dist(pts[-2], pts[-1]) < 0.03 and _turn(pts[-3], pts[-2], pts[-1]) > 50:
        pts = pts[:-1]
    pts = rdp(pts, 0.008)
    return straighten(pts) if snap else pts


def _ray_hit(p, d, segs, max_len):
    """从 p 沿单位向量 d 走，最先碰到的线段距离（max_len 以内），没有返回 None"""
    best = None
    for a, b in segs:
        ex, ey = b[0] - a[0], b[1] - a[1]
        den = d[0] * ey - d[1] * ex
        if abs(den) < 1e-9:
            continue
        wx, wy = a[0] - p[0], a[1] - p[1]
        t = (wx * ey - wy * ex) / den
        s = (wx * d[1] - wy * d[0]) / den
        if 1e-4 < t <= max_len and -0.02 <= s <= 1.02 and (best is None or t < best):
            best = t
    return best


def join_strokes(strokes, max_ext=0.09):
    """笔画中线在交接处往往差一点没碰上（口字的左上角等）：首末段沿原方向延长到碰上别的笔画"""
    out = [list(s) for s in strokes]
    for i, s in enumerate(out):
        if len(s) < 2:
            continue
        others = [(o[j], o[j + 1]) for k, o in enumerate(out) if k != i for j in range(len(o) - 1)]
        for end in (0, -1):
            p, q = (s[0], s[1]) if end == 0 else (s[-1], s[-2])
            L = _dist(q, p)
            if L < 0.06:
                continue
            d = ((p[0] - q[0]) / L, (p[1] - q[1]) / L)
            t = _ray_hit(p, d, others, min(max_ext, 0.6 * L))
            if t is not None:
                s[end] = (p[0] + d[0] * t, p[1] + d[1] * t)
    return out


def prepare(strokes, snap=True, join=True):
    s = [clean_stroke(st, snap) for st in strokes if len(st) >= 2]
    s = [st for st in s if len(st) >= 2]
    return join_strokes(s) if join else s


# ─────────────────────────────── SHX 编码 ───────────────────────────────

_DIRS = [(1, 0), (2, 1), (1, 1), (1, 2), (0, 1), (-1, 2), (-1, 1), (-2, 1),
         (-1, 0), (-2, -1), (-1, -1), (-1, -2), (0, -1), (1, -2), (1, -1), (2, -1)]
# 长度 / 方向字节：方向 n 的单位向量（半格方向分量 0.5 时整体按 2 倍表示）
_VEC = {}
for n, (ux, uy) in enumerate(_DIRS):
    half = 2 if (abs(ux) == 2 or abs(uy) == 2) else 1
    for ln in range(1, 16):
        if half == 2 and ln % 2:
            continue
        v = (ux * ln // half, uy * ln // half)
        _VEC.setdefault(v, (ln << 4) | n)


def _sb(v):
    return v & 0xFF


def _split(dx, dy):
    """拆成每步都在 -127～127 以内"""
    n = max(1, math.ceil(max(abs(dx), abs(dy)) / 127))
    out = []
    px = py = 0
    for k in range(1, n + 1):
        x, y = round(dx * k / n), round(dy * k / n)
        out.append((x - px, y - py))
        px, py = x, y
    return out


class ShapeWriter:
    def __init__(self):
        self.b = bytearray(b"\0")   # 空形名
        self.x = self.y = 0
        self.pen = True

    def _pen(self, down):
        if self.pen != down:
            self.b.append(1 if down else 2)
            self.pen = down

    def _moves(self, deltas):
        run = []

        def flush():
            if not run:
                return
            if len(run) == 1:
                self.b += bytes([8, _sb(run[0][0]), _sb(run[0][1])])
            else:
                self.b.append(9)
                for dx, dy in run:
                    self.b += bytes([_sb(dx), _sb(dy)])
                self.b += b"\0\0"
            run.clear()

        for dx, dy in deltas:
            if dx == 0 and dy == 0:
                continue
            code = _VEC.get((dx, dy))
            if code is not None:
                flush()
                self.b.append(code)
            else:
                run.append((dx, dy))
        flush()

    def move_to(self, x, y):
        if (x, y) == (self.x, self.y):
            return
        self._pen(False)
        self._moves(_split(x - self.x, y - self.y))
        self.x, self.y = x, y

    def polyline(self, pts):
        pts = [(round(px * G), round(py * G)) for px, py in pts]
        q = [pts[0]]
        for p in pts[1:]:
            if p != q[-1]:
                q.append(p)
        if len(q) < 2:
            # 太短的点画至少画一个单位
            q.append((q[0][0] + 1, q[0][1] - 1))
        self.move_to(*q[0])
        self._pen(True)
        deltas = []
        for a, b in zip(q, q[1:]):
            deltas += _split(b[0] - a[0], b[1] - a[1])
        self._moves(deltas)
        self.x, self.y = q[-1]

    def circle(self, cx, cy, r):
        """整圆：SHX 八分圆弧指令（0A 半径 起点八分位 / 段数 0 表示整圆），从最下点起画"""
        cx, cy, r = round(cx * G), round(cy * G), max(1, min(255, round(r * G)))
        self.move_to(cx, cy - r)
        self._pen(True)
        self.b += bytes([0x0A, r, 0x60])

    def finish(self, advance):
        self.move_to(round(advance * G), 0)
        self.b.append(0)
        return bytes(self.b)


def encode(strokes, advance=1.0, circles=()):
    w = ShapeWriter()
    for s in strokes:
        if len(s) >= 2:
            w.polyline(s)
    for c in circles:
        w.circle(*c)
    return w.finish(advance)


def write_unifont(path, name, shapes):
    """unifont：头 + u32 形数 + u16 字体信息长度 + 字体信息 + 逐个 [u16 码, u16 长度, 数据]"""
    info = name.encode("ascii") + b"\0" + bytes([G, 0, 0, 0, 0, 0])
    out = bytearray(b"AutoCAD-86 unifont 1.0\r\n\x1a")
    out += struct.pack("<IH", len(shapes) + 1, len(info)) + info
    for code in sorted(shapes):
        data = shapes[code]
        if len(data) > 0xFFFF:
            raise ValueError(f"U+{code:04X} 形数据过长")
        out += struct.pack("<HH", code, len(data)) + data
    path.write_bytes(bytes(out))
    return len(out)


# ─────────────────────────────── 自绘：标点与符号 ───────────────────────────────


def arc(cx, cy, rx, ry, a0, a1, n=12):
    return [(cx + rx * math.cos(math.radians(a0 + (a1 - a0) * i / n)),
             cy + ry * math.sin(math.radians(a0 + (a1 - a0) * i / n))) for i in range(n + 1)]


def dot(x, y, r=0.025):
    return ([(x, y + r), (x + r, y), (x, y - r), (x - r, y), (x, y + r)],)


def comma(x, y):
    return [(x, y + 0.03), (x + 0.025, y), (x + 0.01, y - 0.05), (x - 0.03, y - 0.09)]


def punctuation(script):
    """中日韩全角标点。台湾的句号逗号放在格子正中，大陆、日本、韩国放在左下"""
    center = script == "tc"
    px, py = (0.5, 0.5) if center else (0.2, 0.12)
    g = {}
    g["、"] = [[(px - 0.05, py + 0.06), (px + 0.06, py - 0.05)]]                        # 、
    g["。"] = [arc(px, py, 0.065, 0.065, 0, 360, 16)]                                   # 。
    g["，"] = [comma(px, py)]                                                            # ，
    g["．"] = list(dot(px, py))                                                          # ．
    cx = 0.5 if center else 0.3
    g["："] = [*dot(cx, 0.62), *dot(cx, 0.22)]                                           # ：
    g["；"] = [*dot(cx, 0.62), comma(cx, 0.22)]                                          # ；
    g["！"] = [[(0.5, 0.9), (0.5, 0.3)], *dot(0.5, 0.12)]                               # ！
    g["？"] = [arc(0.5, 0.7, 0.2, 0.18, 160, -60, 10) + [(0.5, 0.45), (0.5, 0.3)], *dot(0.5, 0.12)]   # ？
    g["“"] = [[(0.74, 0.88), (0.68, 0.72)], [(0.86, 0.88), (0.80, 0.72)]]              # “
    g["”"] = [[(0.20, 0.88), (0.14, 0.72)], [(0.32, 0.88), (0.26, 0.72)]]              # ”
    g["‘"] = [[(0.80, 0.88), (0.74, 0.72)]]                                             # ‘
    g["’"] = [[(0.26, 0.88), (0.20, 0.72)]]                                             # ’
    g["（"] = [arc(1.0, 0.5, 0.38, 0.5, 125, 235, 12)]                                   # （
    g["）"] = [arc(0.0, 0.5, 0.38, 0.5, 55, -55, 12)]                                    # ）
    g["【"] = [[(0.85, 0.95), (0.6, 0.95), (0.6, 0.05), (0.85, 0.05)], [(0.85, 0.95), (0.7, 0.8), (0.7, 0.2), (0.85, 0.05)]]   # 【
    g["】"] = [[(0.15, 0.95), (0.4, 0.95), (0.4, 0.05), (0.15, 0.05)], [(0.15, 0.95), (0.3, 0.8), (0.3, 0.2), (0.15, 0.05)]]   # 】
    g["《"] = [[(0.75, 0.95), (0.45, 0.5), (0.75, 0.05)], [(0.92, 0.95), (0.62, 0.5), (0.92, 0.05)]]   # 《
    g["》"] = [[(0.25, 0.95), (0.55, 0.5), (0.25, 0.05)], [(0.08, 0.95), (0.38, 0.5), (0.08, 0.05)]]   # 》
    g["〈"] = [[(0.8, 0.95), (0.5, 0.5), (0.8, 0.05)]]                                   # 〈
    g["〉"] = [[(0.2, 0.95), (0.5, 0.5), (0.2, 0.05)]]                                   # 〉
    g["「"] = [[(0.9, 0.95), (0.55, 0.95), (0.55, 0.45)]]                                # 「
    g["」"] = [[(0.45, 0.55), (0.45, 0.05), (0.1, 0.05)]]                                # 」
    g["『"] = [[(0.92, 0.95), (0.5, 0.95), (0.5, 0.4), (0.62, 0.4), (0.62, 0.83), (0.92, 0.83), (0.92, 0.95)]]   # 『
    g["』"] = [[(0.08, 0.05), (0.5, 0.05), (0.5, 0.6), (0.38, 0.6), (0.38, 0.17), (0.08, 0.17), (0.08, 0.05)]]   # 』
    g["〔"] = [[(0.85, 0.95), (0.62, 0.8), (0.62, 0.2), (0.85, 0.05)]]                  # 〔
    g["〕"] = [[(0.15, 0.95), (0.38, 0.8), (0.38, 0.2), (0.15, 0.05)]]                  # 〕
    g["［"] = [[(0.8, 0.95), (0.6, 0.95), (0.6, 0.05), (0.8, 0.05)]]                    # ［
    g["］"] = [[(0.2, 0.95), (0.4, 0.95), (0.4, 0.05), (0.2, 0.05)]]                    # ］
    g["｛"] = [arc(0.78, 0.74, 0.12, 0.21, 90, 180, 6) + [(0.66, 0.6), (0.56, 0.5), (0.66, 0.4)] + arc(0.78, 0.26, 0.12, 0.21, 180, 270, 6)]   # ｛
    g["｝"] = [arc(0.22, 0.74, 0.12, 0.21, 90, 0, 6) + [(0.34, 0.6), (0.44, 0.5), (0.34, 0.4)] + arc(0.22, 0.26, 0.12, 0.21, 0, -90, 6)]      # ｝
    g["…"] = [*dot(0.17, 0.12 if not center else 0.5), *dot(0.5, 0.12 if not center else 0.5), *dot(0.83, 0.12 if not center else 0.5)]   # …
    g["⋯"] = [*dot(0.17, 0.5), *dot(0.5, 0.5), *dot(0.83, 0.5)]                        # ⋯
    g["—"] = [[(0.0, 0.5), (1.0, 0.5)]]                                                 # —
    g["―"] = [[(0.0, 0.5), (1.0, 0.5)]]                                                 # ―
    g["～"] = [arc(0.3, 0.45, 0.2, 0.1, 180, 0, 8)[:-1] + arc(0.7, 0.55, 0.2, 0.1, 180, 360, 8)]   # ～
    g["·"] = list(dot(0.5, 0.5))                                                        # ·
    g["・"] = list(dot(0.5, 0.5))                                                        # ・
    g["　"] = []                                                                         # 全角空格
    return g


def symbols():
    """常用工程符号（全角）"""
    g = {}
    c = {}
    g["°"] = [arc(0.5, 0.8, 0.09, 0.09, 0, 360, 12)]                                   # °
    g["℃"] = [arc(0.2, 0.8, 0.08, 0.08, 0, 360, 12), arc(0.65, 0.48, 0.28, 0.38, 50, 310, 16)]   # ℃
    g["±"] = [[(0.2, 0.62), (0.8, 0.62)], [(0.5, 0.92), (0.5, 0.32)], [(0.2, 0.12), (0.8, 0.12)]]   # ±
    g["×"] = [[(0.25, 0.75), (0.75, 0.25)], [(0.25, 0.25), (0.75, 0.75)]]               # ×
    g["÷"] = [[(0.15, 0.5), (0.85, 0.5)], *dot(0.5, 0.78, 0.03), *dot(0.5, 0.22, 0.03)]   # ÷
    g["≤"] = [[(0.8, 0.85), (0.2, 0.6), (0.8, 0.35)], [(0.2, 0.18), (0.8, 0.18)]]      # ≤
    g["≥"] = [[(0.2, 0.85), (0.8, 0.6), (0.2, 0.35)], [(0.2, 0.18), (0.8, 0.18)]]      # ≥
    g["≠"] = [[(0.15, 0.62), (0.85, 0.62)], [(0.15, 0.38), (0.85, 0.38)], [(0.65, 0.88), (0.35, 0.12)]]   # ≠
    g["≈"] = [arc(0.32, 0.6, 0.17, 0.07, 180, 0, 6)[:-1] + arc(0.68, 0.6, 0.17, 0.07, 180, 360, 6),
                   arc(0.32, 0.36, 0.17, 0.07, 180, 0, 6)[:-1] + arc(0.68, 0.36, 0.17, 0.07, 180, 360, 6)]   # ≈
    g["∞"] = [arc(0.32, 0.5, 0.18, 0.15, 0, 360, 16), arc(0.68, 0.5, 0.18, 0.15, 180, 540, 16)]   # ∞
    g["√"] = [[(0.1, 0.45), (0.22, 0.52), (0.4, 0.08), (0.62, 0.95), (0.95, 0.95)]]   # √
    g["∠"] = [[(0.85, 0.85), (0.15, 0.15), (0.9, 0.15)]]                                # ∠
    g["⊥"] = [[(0.5, 0.88), (0.5, 0.15)], [(0.12, 0.15), (0.88, 0.15)]]                # ⊥
    g["∥"] = [[(0.42, 0.9), (0.42, 0.1)], [(0.58, 0.9), (0.58, 0.1)]]                  # ∥
    g["‖"] = g["∥"]                                                                # ‖
    g["△"] = [[(0.5, 0.88), (0.1, 0.12), (0.9, 0.12), (0.5, 0.88)]]                    # △
    g["□"] = [[(0.12, 0.1), (0.12, 0.9), (0.88, 0.9), (0.88, 0.1), (0.12, 0.1)]]       # □
    g["○"] = [arc(0.5, 0.5, 0.4, 0.4, 0, 360, 24)]                                     # ○
    g["◎"] = [arc(0.5, 0.5, 0.4, 0.4, 0, 360, 24), arc(0.5, 0.5, 0.22, 0.22, 0, 360, 16)]   # ◎
    g["◇"] = [[(0.5, 0.92), (0.1, 0.5), (0.5, 0.08), (0.9, 0.5), (0.5, 0.92)]]         # ◇
    g["※"] = [[(0.2, 0.8), (0.8, 0.2)], [(0.2, 0.2), (0.8, 0.8)], *dot(0.5, 0.85), *dot(0.5, 0.15), *dot(0.15, 0.5), *dot(0.85, 0.5)]   # ※
    g["→"] = [[(0.08, 0.5), (0.92, 0.5)], [(0.7, 0.68), (0.92, 0.5), (0.7, 0.32)]]     # →
    g["←"] = [[(0.92, 0.5), (0.08, 0.5)], [(0.3, 0.68), (0.08, 0.5), (0.3, 0.32)]]     # ←
    g["↑"] = [[(0.5, 0.08), (0.5, 0.92)], [(0.32, 0.7), (0.5, 0.92), (0.68, 0.7)]]     # ↑
    g["↓"] = [[(0.5, 0.92), (0.5, 0.08)], [(0.32, 0.3), (0.5, 0.08), (0.68, 0.3)]]     # ↓
    g["‰"] = [arc(0.2, 0.75, 0.08, 0.12, 0, 360, 12), [(0.62, 0.9), (0.18, 0.1)],
                   arc(0.5, 0.22, 0.08, 0.12, 0, 360, 12), arc(0.8, 0.22, 0.08, 0.12, 0, 360, 12)]   # ‰
    g["∅"] = [arc(0.5, 0.5, 0.3, 0.3, 0, 360, 20), [(0.2, 0.1), (0.8, 0.9)]]           # ∅（%%c）
    # 罗马数字 Ⅰ～Ⅻ（钢筋等级常用）
    strokes = {
        "I": lambda x0, w: [[(x0 + w / 2, 0.9), (x0 + w / 2, 0.1)]],
        "V": lambda x0, w: [[(x0, 0.9), (x0 + w / 2, 0.1), (x0 + w, 0.9)]],
        "X": lambda x0, w: [[(x0, 0.9), (x0 + w, 0.1)], [(x0, 0.1), (x0 + w, 0.9)]],
    }
    romans = ["I", "II", "III", "IV", "V", "VI", "VII", "VIII", "IX", "X", "XI", "XII"]
    for i, r in enumerate(romans):
        widths = [0.08 if ch == "I" else 0.26 for ch in r]
        total = sum(widths) + 0.06 * (len(r) - 1)
        x = 0.5 - total / 2
        st = [[(0.5 - total / 2 - 0.04, 0.9), (0.5 + total / 2 + 0.04, 0.9)], [(0.5 - total / 2 - 0.04, 0.1), (0.5 + total / 2 + 0.04, 0.1)]]
        for ch, w in zip(r, widths):
            st += strokes[ch](x, w)
            x += w + 0.06
        g[chr(0x2160 + i)] = st
    return g, c


def rebar_shapes():
    """钢筋符号，形号与天正 tssdeng.shx 相同，%%130～%%133：
    0x82 HPB300（Ⅰ级）Φ；0x83 HRB335（Ⅱ级）Φ 下加底横；0x84 HRB400（Ⅲ级）双竖加底横；0x85 RRB400（Ⅳ级）双竖加上下横。
    尺寸按西文字高：竖高 0.64、圆直径 0.42、字宽 0.61（字高 1 = 主字体的 above + below）"""
    H, R, W = 0.64, 0.21, 0.61
    cx = 0.21
    cy = H / 2
    base = [(0.03, 0.0), (0.39, 0.0)]
    top = [(0.03, H), (0.39, H)]
    one = [[(cx, 0.0), (cx, H)]]
    two = [[(cx - 0.06, 0.0), (cx - 0.06, H)], [(cx + 0.06, 0.0), (cx + 0.06, H)]]
    circle = [(cx, cy, R)]
    return {
        0x82: encode(one, W, circle),
        0x83: encode([base] + one, W, circle),
        0x84: encode([base] + two, W, circle),
        0x85: encode([base, top] + two, W, circle),
    }


# ─────────────────────────────── 韩文：字母拼音节 ───────────────────────────────

def _circle_pts(cx, cy, rx, ry, n=16):
    return arc(cx, cy, rx, ry, 90, 450, n)


# 子音字母，单位框 0～1（y 向上）
_CONS = {
    "ㄱ": [[(0.1, 0.88), (0.85, 0.88), (0.85, 0.45), (0.7, 0.08)]],
    "ㄴ": [[(0.12, 0.92), (0.12, 0.12), (0.92, 0.12)]],
    "ㄷ": [[(0.88, 0.88), (0.12, 0.88), (0.12, 0.12), (0.92, 0.12)]],
    "ㄹ": [[(0.12, 0.9), (0.85, 0.9), (0.85, 0.52), (0.12, 0.52), (0.12, 0.1), (0.92, 0.1)]],
    "ㅁ": [[(0.12, 0.1), (0.12, 0.9), (0.88, 0.9), (0.88, 0.1), (0.12, 0.1)]],
    "ㅂ": [[(0.15, 0.92), (0.15, 0.1), (0.85, 0.1), (0.85, 0.92)], [(0.15, 0.52), (0.85, 0.52)]],
    "ㅅ": [[(0.5, 0.92), (0.08, 0.08)], [(0.36, 0.64), (0.92, 0.08)]],
    "ㅇ": [_circle_pts(0.5, 0.5, 0.4, 0.4, 16)],
    "ㅈ": [[(0.1, 0.88), (0.9, 0.88)], [(0.5, 0.88), (0.08, 0.08)], [(0.37, 0.62), (0.92, 0.08)]],
    "ㅊ": [[(0.5, 1.0), (0.5, 0.86)], [(0.1, 0.8), (0.9, 0.8)], [(0.5, 0.8), (0.08, 0.08)], [(0.37, 0.56), (0.92, 0.08)]],
    "ㅋ": [[(0.1, 0.88), (0.85, 0.88), (0.85, 0.45), (0.7, 0.08)], [(0.1, 0.5), (0.84, 0.5)]],
    "ㅌ": [[(0.88, 0.88), (0.12, 0.88), (0.12, 0.12), (0.92, 0.12)], [(0.12, 0.5), (0.85, 0.5)]],
    "ㅍ": [[(0.08, 0.88), (0.92, 0.88)], [(0.32, 0.88), (0.32, 0.12)], [(0.68, 0.88), (0.68, 0.12)], [(0.05, 0.12), (0.95, 0.12)]],
    "ㅎ": [[(0.5, 1.0), (0.5, 0.86)], [(0.12, 0.78), (0.88, 0.78)], _circle_pts(0.5, 0.34, 0.3, 0.28, 16)],
}


def _side_by_side(a, b):
    return [[(x * 0.48, y) for x, y in s] for s in _CONS[a]] + [[(0.52 + x * 0.48, y) for x, y in s] for s in _CONS[b]]


for _d, _s in {"ㄲ": "ㄱ", "ㄸ": "ㄷ", "ㅃ": "ㅂ", "ㅆ": "ㅅ", "ㅉ": "ㅈ"}.items():
    _CONS[_d] = _side_by_side(_s, _s)
for _d, (_a, _b) in {"ㄳ": "ㄱㅅ", "ㄵ": "ㄴㅈ", "ㄶ": "ㄴㅎ", "ㄺ": "ㄹㄱ", "ㄻ": "ㄹㅁ", "ㄼ": "ㄹㅂ", "ㄽ": "ㄹㅅ",
                     "ㄾ": "ㄹㅌ", "ㄿ": "ㄹㅍ", "ㅀ": "ㄹㅎ", "ㅄ": "ㅂㅅ"}.items():
    _CONS[_d] = _side_by_side(_a, _b)

# 竖向母音：单位框里竖画在 x=stem；横向母音：横画在 y=bar
_VERT = {   # (竖画 x 列表, 短横 [(x0, x1, y)])
    "ㅏ": ([0.35], [(0.35, 0.85, 0.5)]),
    "ㅐ": ([0.3, 0.8], [(0.3, 0.55, 0.5)]),
    "ㅑ": ([0.35], [(0.35, 0.85, 0.62), (0.35, 0.85, 0.38)]),
    "ㅒ": ([0.3, 0.8], [(0.3, 0.55, 0.62), (0.3, 0.55, 0.38)]),
    "ㅓ": ([0.65], [(0.15, 0.65, 0.5)]),
    "ㅔ": ([0.45, 0.8], [(0.1, 0.45, 0.5)]),
    "ㅕ": ([0.65], [(0.15, 0.65, 0.62), (0.15, 0.65, 0.38)]),
    "ㅖ": ([0.45, 0.8], [(0.1, 0.45, 0.62), (0.1, 0.45, 0.38)]),
    "ㅣ": ([0.5], []),
}
_HORZ = {   # (横画 y, 短竖 [(x, y0, y1)])
    "ㅗ": (0.25, [(0.5, 0.25, 0.75)]),
    "ㅛ": (0.25, [(0.35, 0.25, 0.75), (0.65, 0.25, 0.75)]),
    "ㅜ": (0.75, [(0.5, 0.75, 0.1)]),
    "ㅠ": (0.75, [(0.35, 0.75, 0.1), (0.65, 0.75, 0.1)]),
    "ㅡ": (0.5, []),
}
_MIXED = {"ㅘ": "ㅗㅏ", "ㅙ": "ㅗㅐ", "ㅚ": "ㅗㅣ", "ㅝ": "ㅜㅓ", "ㅞ": "ㅜㅔ", "ㅟ": "ㅜㅣ", "ㅢ": "ㅡㅣ"}

_L = "ㄱㄲㄴㄷㄸㄹㅁㅂㅃㅅㅆㅇㅈㅉㅊㅋㅌㅍㅎ"
_V = "ㅏㅐㅑㅒㅓㅔㅕㅖㅗㅘㅙㅚㅛㅜㅝㅞㅟㅠㅡㅢㅣ"
_T = ["", *"ㄱㄲㄳㄴㄵㄶㄷㄹㄺㄻㄼㄽㄾㄿㅀㅁㅂㅄㅅㅆㅇㅈㅊㅋㅌㅍㅎ"]


def _place(strokes, box):
    x0, y0, x1, y1 = box
    return [[(x0 + x * (x1 - x0), y0 + y * (y1 - y0)) for x, y in s] for s in strokes]


def _place_cons(c, box):
    """子音放进框里；ㅇ、ㅎ 的圆不压得太扁"""
    x0, y0, x1, y1 = box
    if c in "ㅇㅎ":
        w, h = x1 - x0, y1 - y0
        if w > 1.25 * h:
            d = (w - 1.25 * h) / 2
            x0, x1 = x0 + d, x1 - d
        elif h > 1.25 * w:
            d = (h - 1.25 * w) / 2
            y0, y1 = y0 + d, y1 - d
    return _place(_CONS[c], (x0, y0, x1, y1))


def _vert_strokes(v, ytop=1.0, ybot=0.0):
    stems, ticks = _VERT[v]
    st = [[(x, ytop), (x, ybot)] for x in stems]
    st += [[(a, y), (b, y)] for a, b, y in ticks]
    return st


def _horz_strokes(v, x0=0.0, x1=1.0):
    bar, ticks = _HORZ[v]
    return [[(x0, bar), (x1, bar)]] + [[(x, a), (x, b)] for x, a, b in ticks]


def hangul_parts(L, V, T):
    """一个音节的初声、中声、终声各自的笔画（格子坐标）"""
    has_t = bool(T)
    top = 0.38 if has_t else 0.04   # 有终声时初声、中声压到上部
    parts = []
    if V in _VERT:
        left = 0.46 if V in "ㅓㅔㅕㅖㅐㅒ" else 0.56
        parts.append(_place_cons(L, (0.08, top + (0.18 if not has_t else 0.06), left, 0.9)))
        parts.append(_place(_vert_strokes(V), (0.5 if left > 0.5 else 0.42, top, 0.96, 0.96)))
    elif V in _HORZ:
        parts.append(_place_cons(L, (0.22, top + (0.46 if not has_t else 0.3), 0.78, 0.94)))
        parts.append(_place(_horz_strokes(V), (0.06, top + 0.02, 0.94, top + (0.44 if not has_t else 0.28))))
    else:
        h, v = _MIXED[V]
        right = 0.62 if v in "ㅏㅣ" else 0.56
        parts.append(_place_cons(L, (0.08, top + (0.46 if not has_t else 0.3), right - 0.04, 0.94)))
        parts.append(_place(_horz_strokes(h), (0.04, top + 0.02, right + 0.08, top + (0.44 if not has_t else 0.28))) +
                     _place(_vert_strokes(v), (right - (0.12 if v in "ㅓㅔ" else 0.02), top, 0.96, 0.96)))
    if has_t:
        parts.append(_place_cons(T, (0.16, 0.04, 0.84, 0.32)))
    return parts


HANGUL_PART_BASE = 0xF000   # 韩文字母部件子形放在私用区，音节只调用子形


def encode_part(strokes):
    """子形：抬笔从原点出发，画完抬笔回到原点，前后不影响调用它的形"""
    w = ShapeWriter()
    w.pen = None
    for st in strokes:
        w.polyline(st)
    return w.finish(0.0)


def hangul_shapes():
    """返回 {码: 形数据}：11172 个音节（每个由 2～3 个部件子形组成）、部件子形、兼容字母"""
    shapes = {}
    part_codes = {}
    for li, L in enumerate(_L):
        for vi, V in enumerate(_V):
            for ti, T in enumerate(_T):
                body = bytearray(b"\0")
                for strokes in hangul_parts(L, V, T):
                    key = tuple(tuple((round(x * G), round(y * G)) for x, y in st) for st in strokes)
                    code = part_codes.get(key)
                    if code is None:
                        code = part_codes[key] = HANGUL_PART_BASE + len(part_codes)
                        shapes[code] = encode_part(strokes)
                    body += bytes([7, code >> 8, code & 0xFF])
                body += bytes([2, 8, 127, 0, 8, 127, 0, 0])   # 抬笔移到下一字（254 单位）
                shapes[0xAC00 + (li * 21 + vi) * 28 + ti] = bytes(body)
    # 兼容字母 U+3131～U+3163
    for cp in range(0x3131, 0x3164):
        ch = chr(cp)
        if ch in _CONS:
            g = _place_cons(ch, (0.15, 0.1, 0.85, 0.85))
        elif ch in _VERT:
            g = _place(_vert_strokes(ch), (0.2, 0.08, 0.85, 0.92))
        elif ch in _HORZ:
            g = _place(_horz_strokes(ch), (0.1, 0.15, 0.9, 0.75))
        elif ch in _MIXED:
            h, v = _MIXED[ch]
            g = (_place(_horz_strokes(h), (0.06, 0.12, 0.66, 0.55)) +
                 _place(_vert_strokes(v), (0.5, 0.08, 0.94, 0.92)))
        else:
            continue
        shapes[cp] = encode(g)
    return shapes


# ─────────────────────────────── 组装字体 ───────────────────────────────


def build(chars, *sources, extra=None, snap=True):
    """chars：要的字；sources：按顺序找笔画；extra：自绘字形（不做清理）"""
    shapes = {}
    missing = 0
    for ch in chars:
        for src in sources:
            if ch in src:
                shapes[ord(ch)] = encode(prepare(src[ch], snap=snap))
                break
        else:
            missing += 1
    for ch, strokes in (extra or {}).items():
        shapes.setdefault(ord(ch), encode(strokes))
    return shapes, missing


def common_extra(script):
    g = punctuation(script)
    sym, _ = symbols()
    g.update(sym)
    return g


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--mmh", required=True, help="Make Me a Hanzi 的 graphics.txt")
    ap.add_argument("--kanjivg", required=True, help="KanjiVG 的 kanjivg-*.xml(.gz)")
    ap.add_argument("--out", default=str(ROOT / "assets" / "fonts"))
    ap.add_argument("--fonts", default="sc,tc,jp,kr")
    args = ap.parse_args()
    out = pathlib.Path(args.out)
    want = set(args.fonts.split(","))

    mmh = load_mmh(args.mmh)
    kvg = load_kanjivg(args.kanjivg) if "jp" in want else {}
    rebar = rebar_shapes()

    gb = gb2312_hanzi()
    b5 = big5_hanzi()
    b5set = set(b5)

    def emit(key, name, shapes, missing, total):
        shapes.update(rebar)
        size = write_unifont(out / f"minicad_{key}.shx", name, shapes)
        print(f"minicad_{key}.shx：{len(shapes)} 个形，缺 {missing}/{total} 字，{size / 1024:.0f} KB")

    # 简体：GB2312 加上 Big5 以外的字；繁体：Big5 里简体字体没有的字。
    # 两个字体的笔画同出一源，简繁共用的字只存在简体字体里，缺字回退时先繁后简（FontEngine 的单线回退顺序）
    gbset = set(gb)
    sc_chars = gb + sorted(c for c in mmh if c not in b5set and c not in gbset)
    sc_set = set(sc_chars)
    if "sc" in want:
        shapes, miss = build(sc_chars, mmh, extra=common_extra("sc"))
        emit("sc", "MiniCAD SC single-line, strokes from Make Me a Hanzi (Arphic PL)", shapes, miss, len(sc_chars))
    if "tc" in want:
        tc_chars = [c for c in b5 if c not in sc_set]
        shapes, miss = build(tc_chars, mmh, extra=common_extra("tc"))
        emit("tc", "MiniCAD TC single-line, strokes from Make Me a Hanzi (Arphic PL)", shapes, miss, len(tc_chars))
    if "jp" in want:
        chars = [c for c in sorted(kvg) if 0x80 <= ord(c) <= 0xFFFF]
        extra = common_extra("jp")
        # KanjiVG 自己的 、。・ー 等优先
        shapes, miss = build(chars, kvg, extra=extra)
        emit("jp", "MiniCAD JP single-line, strokes from KanjiVG (CC BY-SA 3.0)", shapes, miss, len(chars))
    if "kr" in want:
        # 汉字（韩文汉字）不重复存放：回退时到繁体、简体字体里找
        shapes, _ = build([], extra=common_extra("kr"))
        shapes.update(hangul_shapes())
        emit("kr", "MiniCAD KR single-line Hangul", shapes, 0, 0)


if __name__ == "__main__":
    main()
