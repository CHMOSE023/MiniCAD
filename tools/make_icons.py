"""生成功能区命令图标（assets/icons/*.png，64×64 RGBA）。

风格与原有图标（Line.png、Save.png 等）一致：灰色线条 (179,179,179) + 青色强调 (0,192,232)，不透明度 224。
4 倍尺寸绘制后缩小抗锯齿。图标与命令的对应写在 assets/ui/minicad_ui.json 的 "commands" 里。
用法：python tools/make_icons.py            （覆盖本脚本生成的图标，不动其他文件）
注意：根目录 .gitignore 的 tools 规则会命中本目录，新增文件要 git add -f。
"""
import math
import os
from PIL import Image, ImageDraw

S = 4
G = (179, 179, 179, 224)      # 线条
A = (0, 192, 232, 224)        # 强调
AF = (0, 192, 232, 80)        # 强调色填充
GF = (179, 179, 179, 60)      # 灰色填充
W = 4                          # 线宽（64 网格）
OUT = os.path.join(os.path.dirname(__file__), "..", "assets", "icons")


class Icon:
    def __init__(self):
        self.im = Image.new("RGBA", (64 * S, 64 * S), (0, 0, 0, 0))
        self.d = ImageDraw.Draw(self.im)

    @staticmethod
    def P(pts):
        return [(x * S, y * S) for x, y in pts]

    def line(self, pts, c=G, w=W):
        self.d.line(self.P(pts), fill=c, width=int(w * S), joint="curve")
        for x, y in (pts[0], pts[-1]):     # 圆头
            r = w * S / 2
            self.d.ellipse([x * S - r, y * S - r, x * S + r, y * S + r], fill=c)

    def poly(self, pts, c=G, fill=None, w=W):
        if fill:
            self.d.polygon(self.P(pts), fill=fill)
        self.line(list(pts) + [pts[0]], c, w)

    def rect(self, x0, y0, x1, y1, c=G, fill=None, w=W, r=0):
        if r:
            self.d.rounded_rectangle([x0 * S, y0 * S, x1 * S, y1 * S], radius=r * S, outline=c, fill=fill, width=int(w * S))
        else:
            self.poly([(x0, y0), (x1, y0), (x1, y1), (x0, y1)], c, fill, w)

    def circle(self, cx, cy, r, c=G, fill=None, w=W):
        self.d.ellipse([(cx - r) * S, (cy - r) * S, (cx + r) * S, (cy + r) * S], outline=c, fill=fill, width=int(w * S))

    def ellipse(self, cx, cy, rx, ry, c=G, fill=None, w=W):
        self.d.ellipse([(cx - rx) * S, (cy - ry) * S, (cx + rx) * S, (cy + ry) * S], outline=c, fill=fill, width=int(w * S))

    def arc(self, cx, cy, r, a0, a1, c=G, w=W, ry=None):
        ry = ry or r
        self.d.arc([(cx - r) * S, (cy - ry) * S, (cx + r) * S, (cy + ry) * S], a0, a1, fill=c, width=int(w * S))

    def dot(self, x, y, r=3.5, c=A):
        self.circle(x, y, r, c, fill=c, w=0.5)

    def arrow(self, x0, y0, x1, y1, c=A, w=W, head=8, both=False):
        self.line([(x0, y0), (x1, y1)], c, w)
        self.head(x0, y0, x1, y1, c, head)
        if both:
            self.head(x1, y1, x0, y0, c, head)

    def head(self, x0, y0, x1, y1, c=A, head=8):
        a = math.atan2(y1 - y0, x1 - x0)
        p1 = (x1 - head * math.cos(a - 0.45), y1 - head * math.sin(a - 0.45))
        p2 = (x1 - head * math.cos(a + 0.45), y1 - head * math.sin(a + 0.45))
        self.d.polygon(self.P([(x1, y1), p1, p2]), fill=c)

    def dashed(self, pts, c=G, w=3, dash=5, gap=4):
        for (x0, y0), (x1, y1) in zip(pts, pts[1:]):
            L = math.hypot(x1 - x0, y1 - y0)
            t = 0.0
            while t < L:
                t1 = min(L, t + dash)
                self.line([(x0 + (x1 - x0) * t / L, y0 + (y1 - y0) * t / L), (x0 + (x1 - x0) * t1 / L, y0 + (y1 - y0) * t1 / L)], c, w)
                t += dash + gap

    def save(self, name):
        self.im.resize((64, 64), Image.LANCZOS).save(os.path.join(OUT, name + ".png"))


ICONS = {}


def icon(name):
    def deco(f):
        ICONS[name] = f
        return f
    return deco


# ── 立方体（三维视图、实体）────────────────────────────────────
def cube(i, face=None, fill_all=False, edges=G):
    top = [(32, 8), (54, 19), (32, 30), (10, 19)]
    left = [(10, 19), (32, 30), (32, 56), (10, 45)]
    right = [(32, 30), (54, 19), (54, 45), (32, 56)]
    faces = {"top": top, "left": left, "right": right}
    if fill_all:
        i.d.polygon(i.P(top), fill=(179, 179, 179, 110))
        i.d.polygon(i.P(left), fill=(179, 179, 179, 70))
        i.d.polygon(i.P(right), fill=(179, 179, 179, 40))
    if face:
        i.d.polygon(i.P(faces[face]), fill=AF)
    i.line([(10, 19), (32, 8), (54, 19), (54, 45), (32, 56), (10, 45), (10, 19)], edges)
    i.line([(10, 19), (32, 30), (54, 19)], edges)
    i.line([(32, 30), (32, 56)], edges)
    if face:
        i.line(list(faces[face]) + [faces[face][0]], A)


@icon("Point")
def _(i):
    i.line([(32, 14), (32, 50)], G, 3); i.line([(14, 32), (50, 32)], G, 3); i.dot(32, 32, 6)


@icon("EllipseArc")
def _(i):
    i.ellipse(32, 32, 24, 15, (179, 179, 179, 90), w=2.5); i.arc(32, 32, 24, 180, 360, A, ry=15)
    i.dot(8, 32); i.dot(56, 32)


@icon("Solid2D")
def _(i):
    i.poly([(10, 50), (22, 12), (54, 18), (48, 54)], G, AF)


@icon("Image")
def _(i):
    i.rect(8, 12, 56, 52, G)
    i.d.polygon(i.P([(12, 48), (26, 28), (36, 40), (42, 34), (52, 48)]), fill=AF)
    i.line([(12, 48), (26, 28), (36, 40), (42, 34), (52, 48)], A, 3)
    i.dot(44, 22, 4)


@icon("XLine")
def _(i):
    i.arrow(32, 32, 6, 50, G); i.arrow(32, 32, 58, 14, G); i.dot(32, 32, 5)


@icon("Ray")
def _(i):
    i.arrow(12, 50, 56, 12, G); i.dot(12, 50, 5)


@icon("Table")
def _(i):
    i.rect(8, 12, 56, 52, G); i.d.rectangle(i.P([(8, 12), (56, 23)]), fill=AF)
    i.line([(8, 23), (56, 23)]); i.line([(8, 37), (56, 37)], G, 3)
    i.line([(26, 12), (26, 52)], G, 3); i.line([(42, 12), (42, 52)], G, 3)


@icon("Region")
def _(i):
    pts = [(12, 40), (14, 18), (32, 10), (52, 18), (54, 38), (40, 54), (22, 52)]
    i.poly(pts, A, AF)


@icon("Union")
def _(i):
    i.circle(24, 32, 16, A, AF); i.circle(40, 32, 16, A, AF)


@icon("Subtract")
def _(i):
    i.rect(8, 14, 44, 50, A, AF); i.circle(44, 32, 14, G, (30, 30, 30, 0)); i.circle(44, 32, 14, G)


@icon("Intersect")
def _(i):
    i.circle(24, 32, 16, G, w=3); i.circle(40, 32, 16, G, w=3)
    i.d.chord(i.P([(8, 16), (40, 48)]), -48, 48, fill=AF)  # 近似透镜
    i.arc(24, 32, 16, -48, 48, A); i.arc(40, 32, 16, 132, 228, A)


@icon("Box")
def _(i):
    cube(i, "top")


@icon("Cylinder")
def _(i):
    i.ellipse(32, 16, 20, 8, A, AF); i.line([(12, 16), (12, 48)]); i.line([(52, 16), (52, 48)])
    i.arc(32, 48, 20, 0, 180, G, ry=8)


@icon("Sphere")
def _(i):
    i.circle(32, 32, 22, G, GF); i.arc(32, 32, 22, 0, 180, A, ry=8); i.arc(32, 32, 22, 180, 360, (0, 192, 232, 110), w=2.5, ry=8)


@icon("Extrude")
def _(i):
    i.poly([(8, 46), (34, 46), (46, 38), (20, 38)], G, GF)
    i.line([(8, 46), (8, 22)], G, 3); i.line([(34, 46), (34, 22)], G, 3); i.line([(46, 38), (46, 14)], G, 3)
    i.poly([(8, 22), (34, 22), (46, 14), (20, 14)], A, AF, 3)
    i.arrow(56, 50, 56, 12, A, 3)


@icon("Revolve")
def _(i):
    i.line([(12, 8), (12, 56)], G, 2.5); i.poly([(18, 14), (30, 14), (30, 26), (24, 32), (30, 50), (18, 50)], G, GF, 3)
    i.arc(32, 32, 22, 200, 330, A, ry=10); i.head(48, 26, 52, 27, A, 9)


@icon("Sweep")
def _(i):
    i.arc(36, 54, 28, 200, 290, G, 3); i.ellipse(12, 46, 5, 9, A, AF, 3)
    i.ellipse(40, 26, 5, 9, A, AF, 3); i.line([(12, 37), (40, 17)], A, 2.5); i.line([(12, 55), (40, 35)], A, 2.5)


@icon("FilletEdge")
def _(i):
    i.line([(10, 54), (10, 30)]); i.arc(30, 30, 20, 180, 270, A); i.line([(30, 10), (54, 10)])
    i.line([(10, 54), (34, 54)], G, 3); i.line([(54, 10), (54, 34)], G, 3)


@icon("ChamferEdge")
def _(i):
    i.line([(10, 54), (10, 26)]); i.line([(10, 26), (26, 10)], A); i.line([(26, 10), (54, 10)])
    i.line([(10, 54), (34, 54)], G, 3); i.line([(54, 10), (54, 34)], G, 3)


@icon("Section")
def _(i):
    cube(i)
    i.poly([(4, 36), (36, 20), (60, 30), (28, 46)], A, AF, 3)


@icon("Flatshot")
def _(i):
    i.line([(18, 6), (34, 14), (34, 30), (18, 22), (18, 6)], G, 3); i.line([(18, 6), (6, 12), (6, 28), (18, 22)], G, 3)
    i.arrow(30, 34, 30, 44, A, 3, 6)
    i.rect(10, 46, 54, 58, A, AF, 3)


@icon("Area")
def _(i):
    i.poly([(8, 50), (14, 14), (40, 10), (56, 34), (34, 54)], A, AF)
    for k in range(4):
        i.line([(16 + k * 9, 46), (24 + k * 9, 18)], (0, 192, 232, 140), 2)


@icon("Wipeout")
def _(i):
    i.line([(6, 20), (58, 44)], G, 3); i.line([(6, 44), (58, 20)], G, 3)
    i.rect(16, 18, 48, 46, (179, 179, 179, 0), (30, 31, 34, 255), 0.1)
    i.dashed([(16, 18), (48, 18), (48, 46), (16, 46), (16, 18)], A, 3)


@icon("Hatch")
def _(i):
    x0, y0, x1, y1 = 8, 10, 56, 54
    for c in range(-40, 50, 10):          # 斜线 y = -x + k，裁到矩形内
        k = x0 + y1 + c
        pts = []
        for x in (x0, x1):
            y = k - x
            if y0 <= y <= y1:
                pts.append((x, y))
        for y in (y0, y1):
            x = k - y
            if x0 < x < x1:
                pts.append((x, y))
        if len(pts) >= 2:
            i.line(pts[:2], A, 2.5)
    i.rect(x0, y0, x1, y1, G)


def letter_a(i, x, y, h, c=G, w=W):
    i.line([(x, y + h), (x + h * 0.4, y), (x + h * 0.8, y + h)], c, w)
    i.line([(x + h * 0.18, y + h * 0.6), (x + h * 0.62, y + h * 0.6)], c, w)


def gear(i, cx, cy, r=9):
    for k in range(8):
        a = k * math.pi / 4
        i.line([(cx + r * 0.7 * math.cos(a), cy + r * 0.7 * math.sin(a)), (cx + r * 1.15 * math.cos(a), cy + r * 1.15 * math.sin(a))], A, 3.5)
    i.circle(cx, cy, r * 0.7, A, w=3.5); i.dot(cx, cy, 2.2)


@icon("TextStyle")
def _(i):
    letter_a(i, 6, 10, 38); gear(i, 47, 46)


@icon("MLineStyle")
def _(i):
    for k, y in enumerate((14, 24, 34)):
        i.line([(6, y), (40, y)], G if k != 1 else A, 3)
    gear(i, 47, 46)


@icon("PointStyle")
def _(i):
    for (x, y) in ((14, 14), (32, 14), (50, 14), (14, 32), (32, 32), (50, 32)):
        i.line([(x - 5, y - 5), (x + 5, y + 5)], G, 3); i.line([(x - 5, y + 5), (x + 5, y - 5)], G, 3)
    i.circle(14, 50, 5, A, w=3); i.dot(32, 50); i.rect(45, 45, 55, 55, A, w=3)


@icon("LineTypeScale")
def _(i):
    i.dashed([(6, 20), (58, 20)], G, 4, 8, 5); i.dashed([(6, 34), (58, 34)], A, 4, 14, 7)
    i.arrow(20, 50, 6, 50, A, 3, 6); i.arrow(44, 50, 58, 50, A, 3, 6)


@icon("DimLinear")
def _(i):
    i.line([(10, 54), (10, 18)], G, 3); i.line([(54, 54), (54, 18)], G, 3)
    i.arrow(32, 26, 11, 26, A, 3); i.arrow(32, 26, 53, 26, A, 3)
    i.line([(22, 10), (42, 10)], A, 3)


@icon("DimAngular")
def _(i):
    i.line([(10, 54), (56, 54)]); i.line([(10, 54), (46, 14)])
    i.arc(10, 54, 34, -48, 0, A, 3); i.head(30, 31, 32, 30, A, 7); i.head(44, 44, 44, 53, A, 7)


@icon("DimRadius")
def _(i):
    i.circle(30, 34, 22); i.dot(30, 34, 3, G); i.arrow(30, 34, 45, 18, A, 3); i.line([(45, 18), (58, 8)], A, 3)


@icon("DimDiameter")
def _(i):
    i.circle(32, 32, 22); i.arrow(32, 32, 16, 48, A, 3); i.arrow(32, 32, 48, 16, A, 3)


@icon("DimJogged")
def _(i):
    i.circle(26, 36, 20); i.line([(56, 8), (46, 22), (40, 18), (34, 30)], A, 3); i.head(40, 22, 33, 31, A, 7)


@icon("DimArc")
def _(i):
    i.arc(32, 46, 26, 200, 340, G); i.arc(32, 46, 34, 205, 335, A, 3)
    i.head(20, 20, 4, 30, A, 7); i.head(44, 20, 60, 30, A, 7)


@icon("DimOrdinate")
def _(i):
    i.line([(10, 56), (10, 8)], G, 3); i.line([(10, 56), (58, 56)], G, 3)
    for x in (24, 38, 52):
        i.line([(x, 52), (x, 22)], A, 2.5); i.line([(x, 22), (x + 4, 14)], A, 2.5)


@icon("Leader")
def _(i):
    i.arrow(36, 26, 8, 52, A, 3); i.line([(36, 26), (58, 26)], A, 3)
    i.line([(38, 16), (58, 16)], G, 3)


@icon("MLeader")
def _(i):
    i.arrow(30, 28, 8, 52, A, 3); i.rect(30, 10, 58, 36, G, GF, 3, r=3)
    i.line([(36, 19), (52, 19)], G, 2.5); i.line([(36, 27), (48, 27)], G, 2.5)


@icon("BlockDefine")
def _(i):
    i.dashed([(6, 6), (58, 6), (58, 58), (6, 58), (6, 6)], G, 3)
    i.circle(24, 26, 9, A, AF, 3); i.rect(30, 32, 50, 50, A, AF, 3)


@icon("BlockInsert")
def _(i):
    i.rect(24, 6, 58, 40, G, GF, 3); i.circle(41, 23, 8, G, w=3)
    i.arrow(6, 58, 26, 38, A, 4)


@icon("MLineEdit")
def _(i):
    i.line([(6, 26), (58, 26)], G, 3); i.line([(6, 38), (58, 38)], G, 3)
    i.line([(26, 6), (26, 58)], A, 3); i.line([(38, 6), (38, 58)], A, 3)


@icon("Offset")
def _(i):
    i.arc(40, 56, 34, 200, 300, G); i.arc(40, 56, 22, 200, 300, A); i.arrow(20, 34, 28, 42, A, 3, 6)


@icon("Scale")
def _(i):
    i.rect(8, 34, 26, 52, G, w=3); i.rect(8, 10, 54, 52, A, w=3); i.arrow(24, 36, 44, 18, A, 3)


@icon("Stretch")
def _(i):
    i.poly([(8, 22), (34, 22), (34, 46), (8, 46)], G, None, 3)
    i.dashed([(26, 14), (44, 14), (44, 54), (26, 54), (26, 14)], A, 2.5)
    i.arrow(36, 34, 58, 34, A, 3)


@icon("Trim")
def _(i):
    i.line([(32, 6), (32, 58)], G, 3); i.line([(6, 32), (32, 32)], G); i.dashed([(34, 32), (58, 32)], A, 3)
    i.line([(44, 40), (54, 24)], A, 3); i.line([(44, 24), (54, 40)], A, 3)


@icon("Extend")
def _(i):
    i.line([(54, 6), (54, 58)], G, 3); i.line([(8, 32), (28, 32)]); i.arrow(28, 32, 52, 32, A, 4)


@icon("Break")
def _(i):
    i.line([(6, 40), (24, 26)]); i.line([(40, 38), (58, 24)]); i.dot(24, 26, 4); i.dot(40, 38, 4)


@icon("Fillet")
def _(i):
    i.line([(10, 56), (10, 34)]); i.arc(34, 34, 24, 180, 270, A); i.line([(34, 10), (56, 10)])
    i.dashed([(10, 30), (10, 10), (30, 10)], G, 2.5)


@icon("Chamfer")
def _(i):
    i.line([(10, 56), (10, 28)]); i.line([(10, 28), (28, 10)], A); i.line([(28, 10), (56, 10)])
    i.dashed([(10, 24), (10, 10), (24, 10)], G, 2.5)


@icon("Explode")
def _(i):
    i.line([(14, 26), (14, 12), (28, 12)]); i.line([(36, 12), (50, 12), (50, 26)])
    i.line([(14, 38), (14, 52), (28, 52)]); i.line([(36, 52), (50, 52), (50, 38)])
    i.dot(32, 32, 4)
    for a in range(4):
        t = a * math.pi / 2 + math.pi / 4
        i.line([(32 + 7 * math.cos(t), 32 + 7 * math.sin(t)), (32 + 12 * math.cos(t), 32 + 12 * math.sin(t))], A, 3)


@icon("Array")
def _(i):
    for r in range(3):
        for c in range(3):
            x, y = 8 + c * 18, 8 + r * 18
            i.rect(x, y, x + 12, y + 12, A if (r, c) == (0, 0) else G, AF if (r, c) == (0, 0) else None, 3)


@icon("Delete")
def _(i):
    i.rect(14, 18, 50, 56, G, w=3, r=3); i.line([(8, 14), (56, 14)], G, 3); i.line([(26, 14), (26, 8), (38, 8), (38, 14)], G, 3)
    i.line([(24, 28), (40, 46)], A); i.line([(40, 28), (24, 46)], A)


@icon("Cut")
def _(i):
    i.circle(18, 48, 8, A, w=3.5); i.circle(46, 48, 8, A, w=3.5)
    i.line([(23, 42), (44, 8)]); i.line([(41, 42), (20, 8)])


def page(i, x, y, w, h, c=G, fill=None):
    i.rect(x, y, x + w, y + h, c, fill, 3, r=3)


@icon("ClipCopy")
def _(i):
    page(i, 8, 6, 30, 38); page(i, 24, 20, 30, 38, A, (30, 31, 34, 255))


@icon("CopyBase")
def _(i):
    page(i, 8, 6, 30, 38); page(i, 24, 20, 30, 38, G, (30, 31, 34, 255)); i.dot(24, 58, 5)


@icon("Paste")
def _(i):
    i.rect(10, 10, 46, 58, G, w=3, r=3); i.rect(20, 6, 36, 14, G, GF, 3, r=2)
    page(i, 28, 26, 28, 32, A, (30, 31, 34, 255))


@icon("SelectAll")
def _(i):
    i.dashed([(6, 6), (58, 6), (58, 58), (6, 58), (6, 6)], A, 3)
    i.d.polygon(i.P([(22, 18), (22, 48), (30, 40), (36, 52), (41, 50), (35, 38), (46, 38)]), fill=G)


def layers(i, top_accent=True):
    for k, y in enumerate((40, 30, 20)):
        pts = [(8, y), (32, y - 10), (56, y), (32, y + 10)]
        last = k == 2
        i.poly(pts, A if (last and top_accent) else G, AF if (last and top_accent) else (30, 31, 34, 255), 3)


@icon("LayerNew")
def _(i):
    layers(i); i.line([(50, 46), (50, 62)], A, 4); i.line([(42, 54), (58, 54)], A, 4)


@icon("LayersPanel")
def _(i):
    layers(i)


@icon("LayoutNew")
def _(i):
    page(i, 8, 8, 36, 46); i.rect(14, 14, 38, 34, G, w=2.5)
    i.line([(48, 40), (48, 60)], A, 4); i.line([(38, 50), (58, 50)], A, 4)


@icon("LayoutDelete")
def _(i):
    page(i, 8, 8, 36, 46); i.rect(14, 14, 38, 34, G, w=2.5)
    i.line([(38, 50), (58, 50)], A, 4)


for name, face in (("ViewTop", "top"), ("ViewFront", "left"), ("ViewRight", "right")):
    ICONS[name] = (lambda f: (lambda i: cube(i, f)))(face)


@icon("ViewSWIso")
def _(i):
    cube(i); i.arrow(4, 60, 14, 50, A, 3, 6)


@icon("ViewSEIso")
def _(i):
    cube(i); i.arrow(60, 60, 50, 50, A, 3, 6)


@icon("Wireframe")
def _(i):
    cube(i); i.dashed([(10, 45), (32, 34), (54, 45)], G, 2); i.dashed([(32, 34), (32, 8)], G, 2)


@icon("Shaded")
def _(i):
    cube(i, fill_all=True)


@icon("Grid")
def _(i):
    for x in range(10, 60, 11):
        for y in range(10, 60, 11):
            i.dot(x, y, 2.2, G)
    i.dot(32, 32, 3.5)


@icon("Axis")
def _(i):
    i.arrow(14, 50, 56, 50, A, 3.5); i.arrow(14, 50, 14, 8, (120, 200, 120, 224), 3.5); i.dot(14, 50, 4, G)


@icon("Gizmo")
def _(i):
    i.arrow(32, 32, 58, 32, (230, 90, 90, 224), 3.5); i.arrow(32, 32, 32, 6, (120, 200, 120, 224), 3.5)
    i.arrow(32, 32, 12, 52, A, 3.5); i.circle(32, 32, 5, G, GF, 2.5)


@icon("ViewCube")
def _(i):
    i.circle(32, 34, 26, G, w=3)
    i.d.polygon(i.P([(32, 18), (44, 24), (32, 30), (20, 24)]), fill=AF)
    i.line([(20, 24), (32, 18), (44, 24), (44, 40), (32, 46), (20, 40), (20, 24)], A, 3); i.line([(20, 24), (32, 30), (44, 24)], A, 3)
    i.line([(32, 30), (32, 46)], A, 3)


@icon("ThinLines")
def _(i):
    i.line([(8, 16), (56, 16)], G, 8); i.line([(8, 34), (56, 34)], G, 4); i.line([(8, 50), (56, 50)], A, 2)


@icon("ZoomExtents")
def _(i):
    i.circle(28, 28, 16); i.line([(40, 40), (56, 56)], G, 6)
    for (x, y, dx, dy) in ((6, 6, 1, 1), (58, 6, -1, 1), (6, 58, 1, -1)):
        i.line([(x + dx * 10, y), (x, y), (x, y + dy * 10)], A, 3)


@icon("ZoomIn")
def _(i):
    i.circle(27, 27, 17); i.line([(40, 40), (56, 56)], G, 6); i.line([(19, 27), (35, 27)], A, 4); i.line([(27, 19), (27, 35)], A, 4)


@icon("ZoomOut")
def _(i):
    i.circle(27, 27, 17); i.line([(40, 40), (56, 56)], G, 6); i.line([(19, 27), (35, 27)], A, 4)


@icon("ZoomWindow")
def _(i):
    i.dashed([(6, 6), (40, 6), (40, 34), (6, 34), (6, 6)], A, 3)
    i.circle(36, 36, 13); i.line([(46, 46), (58, 58)], G, 6)


@icon("FileMenu")
def _(i):
    # 标题栏的“文件”菜单：折角的纸 + 几行字
    i.poly([(14, 6), (40, 6), (52, 18), (52, 58), (14, 58), (14, 6)], G, w=4)
    i.line([(40, 6), (40, 18), (52, 18)], G, 3)
    for y in (30, 39, 48):
        i.line([(22, y), (44, y)], A, 4)


@icon("Pan")
def _(i):
    # 张开的手（实时平移）：与其他图标一样只有灰色线条、不填充。
    # 先画放大一圈的灰色剪影，再用透明色挖空，剩下的就是轮廓线；手指逐个画，相邻手指之间留出分隔线
    fingers = [(18, 14, 26, 38), (27, 8, 35, 34), (36, 10, 44, 34), (45, 17, 53, 38)]
    palm = (18, 28, 53, 58)
    thumb = [(7, 34), (12, 29), (24, 40), (24, 52), (18, 52)]
    clear = (0, 0, 0, 0)
    p = W - 0.5                         # 线宽

    def rr(box, r, pad, c):
        x0, y0, x1, y1 = box
        i.d.rounded_rectangle([(x0 - pad) * S, (y0 - pad) * S, (x1 + pad) * S, (y1 + pad) * S], radius=(r + pad) * S, fill=c)

    def thumb_shape(pad, c):
        i.d.polygon(i.P(thumb), fill=c)
        if pad:
            i.d.line(i.P(thumb + [thumb[0]]), fill=c, width=int(pad * 2 * S), joint="curve")

    rr(palm, 10, p, G)
    thumb_shape(p, G)
    thumb_shape(0, clear)
    for f in fingers:
        rr(f, 4, p, G)
        rr(f, 4, 0, clear)
    rr(palm, 10, 0, clear)              # 手掌里不留手指根部的线
    thumb_shape(0, clear)


@icon("PropertiesPanel")
def _(i):
    i.rect(8, 8, 56, 56, G, w=3, r=4); i.d.rectangle(i.P([(8, 8), (56, 18)]), fill=AF)
    for y in (28, 38, 48):
        i.line([(14, y), (26, y)], G, 3); i.line([(32, y), (50, y)], A, 3)


@icon("CommandLine")
def _(i):
    i.rect(6, 12, 58, 52, G, w=3, r=4); i.line([(14, 24), (24, 32), (14, 40)], A, 4); i.line([(28, 42), (44, 42)], G, 4)


@icon("ResetLayout")
def _(i):
    i.rect(6, 8, 42, 44, G, w=3, r=3); i.line([(6, 18), (42, 18)], G, 3); i.line([(22, 18), (22, 44)], G, 3)
    i.arc(46, 46, 12, 200, 470, A, 3.5); i.head(36, 40, 34, 45, A, 7)


@icon("Bake")
def _(i):
    for (x, y) in ((6, 8), (6, 24)):
        i.rect(x, y, x + 14, y + 10, G, w=3, r=2)
    i.line([(20, 13), (28, 13), (28, 29), (20, 29)], A, 3); i.arrow(28, 21, 40, 21, A, 3, 6)
    cube_pts = [(44, 30), (58, 38), (58, 56), (44, 62), (30, 56), (30, 38)]
    i.poly([(44, 30), (58, 37), (44, 44), (30, 37)], A, AF, 3); i.line([(30, 37), (30, 54), (44, 61), (58, 54), (58, 37)], G, 3); i.line([(44, 44), (44, 61)], G, 3)


@icon("Snap")
def _(i):
    i.rect(16, 16, 48, 48, A, w=3.5); i.line([(4, 32), (60, 32)], G, 2.5); i.line([(32, 4), (32, 60)], G, 2.5)


@icon("Ortho")
def _(i):
    i.line([(12, 8), (12, 52), (56, 52)]); i.rect(12, 40, 24, 52, A, w=3)


@icon("Polar")
def _(i):
    i.line([(10, 54), (58, 54)]); i.line([(10, 54), (52, 30)], A, 3); i.line([(10, 54), (34, 12)], A, 3)
    i.arc(10, 54, 22, -60, 0, G, 2.5)


@icon("Hover")
def _(i):
    i.d.polygon(i.P([(14, 10), (14, 44), (22, 36), (28, 50), (33, 48), (27, 34), (38, 34)]), fill=G)
    i.dashed([(36, 18), (58, 18)], A, 3); i.dashed([(46, 8), (46, 58)], A, 3)


@icon("Theme")
def _(i):
    i.circle(32, 32, 22, G, w=3.5); i.d.pieslice(i.P([(10, 10), (54, 54)]), 90, 270, fill=A)


@icon("Reload")
def _(i):
    i.arc(32, 32, 20, 30, 330, A, 4.5); i.head(48, 18, 52, 22, A, 10)


@icon("Help")
def _(i):
    i.circle(32, 32, 24, G, w=3.5)
    i.arc(32, 25, 9, 180, 400, A, 4); i.line([(32, 34), (32, 40)], A, 4); i.dot(32, 48, 3)


@icon("Settings")
def _(i):
    gear(i, 32, 32, 18)


@icon("Search")
def _(i):
    i.circle(27, 27, 15, G, w=4); i.line([(38, 38), (54, 54)], G, 5)



@icon("AI")
def _(i):
    # 对话气泡 + 四角星（AI 助手）
    i.rect(6, 10, 50, 42, G, w=3.5, r=9)
    i.line([(16, 42), (12, 54), (26, 42)], G, 3.5)
    star = [(36, 14), (40, 24), (50, 28), (40, 32), (36, 42), (32, 32), (22, 28), (32, 24)]
    i.poly([(x + 6, y - 4) for x, y in star], A, fill=AF, w=3)

if __name__ == "__main__":
    for name, f in ICONS.items():
        i = Icon()
        f(i)
        i.save(name)
    print(f"{len(ICONS)} 个图标 → {os.path.abspath(OUT)}")
