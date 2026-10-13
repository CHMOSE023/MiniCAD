# 生成 TchGeometryRef.inc：天正 2014 分解结果 → MiniCADTchGeometryTests 的期望数据
#
# 用法：python tests/Core/tch_geomref.py [--report 报告文件]
#   分解日志由 AutoCAD 脚本生成（每个对象 "== 类型 包围盒" 后接逐个分解出的实体的 entget 与 TEXTBOX，GBK 编码），
#   见 docs/Tch-compatibility.md“几何回归测试”。日志不在仓库中，只提交生成结果。
#   先不带 --report 生成，编译后运行 `MiniCADTchGeometryTests --report > 报告`，再带 --report 重新生成：
#   报告中缺失的项标为 Known（分解与显示的差别或已知近似），多出的线段数记为 ExtraAllowed。
# 对象与原图句柄的配对：用 MiniCADDwgInspect（MINICAD_GEOM、MINICAD_GEOMBLOCKS）输出的生成几何，取覆盖得最好的同类型对象
import math, os, re, subprocess, sys

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
INSPECT = os.path.join(ROOT, 'out', 'release', 'tests', 'MiniCADDwgInspect.exe')
ACAD = os.path.join(ROOT, 'out', 'acad')

# (样例图（相对 tests/Data）, 分解日志（相对 out/acad）, {类型: 最多对象数}, 容差[, 'exact'])
# 'holes'：只收洞口（分解结果恰为两条线），C 详图块内的普通门窗不在这次核对范围
SOURCES = [
    ('Tch/p1.dwg', 'gen/r1.txt', {'*': 20}, 1.0),
    ('Tch/p2.dwg', 'gen/r2.txt', {'TCH_HANDRAIL': 5}, 1.0),
    ('Tch/p3.dwg', 'gen/r3.txt', {'TCH_TEXT': 5}, 1.0),
    ('Tch/v1.dwg', 'gen/rv1.txt', {'*': 20}, 1.0),
    ('Tch/v2.dwg', 'gen/rv2.txt', {'TCH_LINESTAIR': 4}, 1.0),
    ('Tch/q6.dwg', 'gen/r_q6.txt', {'*': 20}, 1.0),
    ('Tch/q7.dwg', 'gen/r_q7.txt', {'*': 2}, 1.0),
    ('TchArch/8#楼-B立剖面.dwg', 'x_axB.txt', {'TCH_AXIS_LABEL': 20, 'TCH_TEXT': 100}, 2.0),
    ('TchArch/8#楼-B立剖面.dwg', 'x_colB.txt', {'TCH_COLUMN': 8, 'TCH_DRAWINGNAME': 5, 'TCH_ARROW': 5}, 2.0),
    ('TchArch/8#楼-B立剖面.dwg', 'x_colBb.txt', {'TCH_COLUMN': 8}, 2.0),
    ('TchArch/8#楼-C详图.dwg', 'x_dnC.txt', {'TCH_DRAWINGNAME': 10}, 2.0),
    ('TchArch/8#楼-C详图.dwg', 'x_axC.txt', {'TCH_TEXT': 200}, 2.0),
    ('TchArch/8#楼-C详图.dwg', 'x_axCb.txt', {'TCH_AXIS_LABEL': 10}, 2.0),
    ('TchArch/8#楼-C详图.dwg', 'x_opC.txt', {'TCH_OPENING': 60}, 2.0, 'holes'),
    ('TchArch/8#楼-D墙身大样.dwg', 'x_axD.txt', {'TCH_AXIS_LABEL': 10, 'TCH_TEXT': 10, 'TCH_ARROW': 70}, 2.0),
]

# 显示为普通实体（不生成 *U_TCH_ 块）的类型：测试按几何在普通实体中查找
PLAIN = {'TCH_COLUMN'}

def bulge_arc(p, q, b):
    d = math.dist(p, q); th = 4 * math.atan(b)
    r = d / 2 / math.sin(abs(th) / 2)
    ux, uy = (q[0] - p[0]) / d, (q[1] - p[1]) / d
    h = r * math.cos(th / 2) * (1 if b > 0 else -1)
    cx, cy = (p[0] + q[0]) / 2 - uy * h, (p[1] + q[1]) / 2 + ux * h
    a0, a1 = math.atan2(p[1] - cy, p[0] - cx), math.atan2(q[1] - cy, q[0] - cx)
    if b < 0: a0, a1 = a1, a0
    return (cx, cy, abs(r), a0, a1)

def norm_text(s):
    return s.replace('%%%', '%').replace('%%d', '°').replace('%%D', '°').replace('%%p', '±').replace('%%P', '±')

def parse_log(path):
    objs, cur, last_text = [], None, None
    for line in open(path, encoding='mbcs', errors='replace'):
        line = line.rstrip('\n')
        if line.startswith('== '):
            cur = {'type': line.split()[1], 'L': [], 'C': [], 'A': [], 'T': []}; objs.append(cur); last_text = None; continue
        if cur is None or not line.startswith('  '): continue
        e = line.strip()
        grp = lambda c: [list(map(float, m.split())) for m in re.findall(r'\(%d ([-\d. e+]+?)\s*\)' % c, e)]
        def one(c, d=0.0):
            m = re.search(r'\(%d \. (-?[\d.e+]+)\)' % c, e)
            return float(m.group(1)) if m else d
        if e.startswith('LINE '):
            a, b = grp(10)[0], grp(11)[0]; cur['L'].append(((a[0], a[1]), (b[0], b[1])))
        elif e.startswith('LWPOLYLINE'):
            pts = [tuple(p[:2]) for p in grp(10)]
            bul = [float(x) for x in re.findall(r'\(42 \. (-?[\d.e+]+)\)', e)] + [0.0] * len(pts)
            n = len(pts)
            for i in range(n if int(one(70)) & 1 else n - 1):
                p, q = pts[i], pts[(i + 1) % n]
                if math.dist(p, q) < 1e-9: continue
                if abs(bul[i]) < 1e-9: cur['L'].append((p, q))
                else: cur['A'].append(bulge_arc(p, q, bul[i]))
        elif e.startswith('ARC '):
            c = grp(10)[0]; cur['A'].append((c[0], c[1], one(40), one(50), one(51)))
        elif e.startswith('CIRCLE '):
            c = grp(10)[0]; cur['C'].append((c[0], c[1], one(40)))
        elif e.startswith('TEXT '):
            c = grp(10)[0]; m = re.search(r'\(1 \. (.*?)\) \(50', e)
            last_text = {'v': norm_text(m.group(1)) if m else '', 'x': c[0], 'y': c[1], 'h': one(40), 'r': one(50)}
        elif e.startswith('TEXTBOX') and last_text is not None:
            nums = list(map(float, re.findall(r'-?\d+\.\d+', e)))
            x0, y0, x1, y1 = nums[0], nums[1], nums[3], nums[4]
            t = last_text; last_text = None
            cx, cy = (x0 + x1) / 2, (y0 + y1) / 2
            c, s = math.cos(t['r']), math.sin(t['r'])
            if t['v'].strip():
                cur['T'].append((t['v'], t['x'] + cx * c - cy * s, t['y'] + cx * s + cy * c, (x1 - x0) / 2, (y1 - y0) / 2, t['r'], t['h']))
    return objs

def merge(segs):
    groups = {}
    for (ax, ay), (bx, by) in segs:
        dx, dy = bx - ax, by - ay; l = math.hypot(dx, dy)
        if l < 1e-3: continue
        ux, uy = dx / l, dy / l
        if ux < -1e-4 or (abs(ux) <= 1e-4 and uy < 0): ux, uy = -ux, -uy
        off = -uy * ax + ux * ay
        t1, t2 = sorted((ux * ax + uy * ay, ux * bx + uy * by))
        groups.setdefault((round(ux, 3), round(uy, 3), round(off, 0)), []).append((t1, t2, ux, uy, off))
    out = []
    for iv in groups.values():
        iv.sort(); cur = list(iv[0])
        for a in iv[1:]:
            if a[0] <= cur[1] + 0.05: cur[1] = max(cur[1], a[1])
            else: out.append(tuple(cur)); cur = list(a)
        out.append(tuple(cur))
    res = []
    for t1, t2, ux, uy, off in out:
        res.append(((ux * t1 - uy * off, uy * t1 + ux * off), (ux * t2 - uy * off, uy * t2 + ux * off)))
    return res

def covered(s, segs, tol):
    (x1, y1), (x2, y2) = s
    L = math.hypot(x2 - x1, y2 - y1)
    if L < tol: return True
    ux, uy = (x2 - x1) / L, (y2 - y1) / L
    iv = []
    for (a, b) in segs:
        if abs(-uy * (a[0] - x1) + ux * (a[1] - y1)) > tol or abs(-uy * (b[0] - x1) + ux * (b[1] - y1)) > tol: continue
        p, q = sorted((ux * (a[0] - x1) + uy * (a[1] - y1), ux * (b[0] - x1) + uy * (b[1] - y1)))
        iv.append((p, q))
    iv.sort(); cur = 0.0
    for p, q in iv:
        if p > cur + tol: break
        cur = max(cur, q)
    return cur >= L - tol

def mine(dwg):
    env = dict(os.environ, MINICAD_GEOM='1', MINICAD_GEOMBLOCKS='1')
    out = subprocess.run([INSPECT, dwg], capture_output=True, env=env).stdout.decode('utf-8', 'replace')
    types, geo = {}, {}
    for line in out.splitlines():
        m = re.match(r'UNKNOWN ([0-9a-f]+) (\S+)', line)
        if m: types[int(m.group(1), 16)] = m.group(2)
        if not line.startswith('G '): continue
        p = line.split(' ', 3); h = int(p[1], 16); kind = p[2]; v = (p[3] if len(p) > 3 else '').split(' ')
        g = geo.setdefault(h, {'L': [], 'C': [], 'T': []})
        if kind == 'LINE': f = list(map(float, v)); g['L'].append(((f[0], f[1]), (f[2], f[3])))
        elif kind == 'CIRCLE': g['C'].append(tuple(map(float, v)))
        elif kind == 'MTEXT': g['T'].append((float(v[0]), float(v[1])))
        elif kind == 'PL':
            n = int(v[1]); f = list(map(float, v[2:2 + 3 * n]))
            for i in range(n - 1):
                if abs(f[3 * i + 2]) < 1e-9: g['L'].append(((f[3 * i], f[3 * i + 1]), (f[3 * i + 3], f[3 * i + 4])))
    return types, geo

def center(o):
    pts = [p for s in o['L'] for p in s] + [(c[0], c[1]) for c in o['C'] + o['A']] + [(t[1], t[2]) for t in o['T']]
    if not pts: return None
    return (sum(p[0] for p in pts) / len(pts), sum(p[1] for p in pts) / len(pts))

def cstr(s):
    # 非 ASCII 字符写成 \x 转义（UTF-8）；转义后紧跟十六进制字符时断开字符串，避免被并入转义
    out, escaped = '', False
    for c in s:
        if 32 <= ord(c) < 127 and c not in '"\\?':
            if escaped and c in '0123456789abcdefABCDEF': out += '" "'
            out += c; escaped = False
        else:
            out += ''.join('\\x%02x' % b for b in c.encode('utf-8')); escaped = True
    return '"' + out + '"'

def main():
    report = {}
    if '--report' in sys.argv:
        for line in open(sys.argv[sys.argv.index('--report') + 1], encoding='utf-8', errors='replace'):
            p = line.split()
            if len(p) >= 4 and p[0] == 'REPORT':
                report.setdefault(int(p[1]), {}).setdefault(p[2], set()).add(int(p[3]))
    out = ['// 由 tests/Core/tch_geomref.py 生成，不要手改', 'const RefObject kRefObjects[] = {']
    index = 0
    for dwg, log, caps, tol, *opts in SOURCES:
        logpath = os.path.join(ACAD, log)
        if not os.path.exists(logpath):
            print('缺少分解日志', logpath); continue
        types, geo = mine(os.path.join(ROOT, 'tests', 'Data', dwg))
        used, count = set(), {}
        for o in parse_log(logpath):
            cap = caps.get(o['type'], caps.get('*', 0))
            if count.get(o['type'], 0) >= cap: continue
            segs = merge(o['L'])
            if 'holes' in opts and (len(o['L']) != 2 or o['A'] or o['C'] or o['T']): continue
            if not segs and not o['C'] and not o['A'] and not o['T']: continue
            c = center(o)
            best = None
            if o['type'] in PLAIN:
                best = (0, 0, 0)
            else:
                best = None
            for h, t in types.items():
                if o['type'] in PLAIN: break
                if t != o['type'] or h in used or h not in geo: continue
                g = geo[h]
                score = sum(1 for s in segs if not covered(s, g['L'], tol)) + \
                        sum(1 for x in o['C'] if not any(abs(x[0] - y[0]) < tol and abs(x[1] - y[1]) < tol and abs(x[2] - y[2]) < tol for y in g['C']))
                pts = [p for s in g['L'] for p in s] + [(y[0], y[1]) for y in g['C']] + g['T']
                d = math.dist(c, (sum(p[0] for p in pts) / len(pts), sum(p[1] for p in pts) / len(pts))) if pts and c else 1e18
                if best is None or (score, d) < best[:2]: best = (score, d, h)
            if best is None or best[1] > 1e5: continue
            h = best[2]
            if h: used.add(h)
            count[o['type']] = count.get(o['type'], 0) + 1
            rep = report.get(index, {})
            known = lambda kind, i: 'true' if i in rep.get(kind, set()) else 'false'
            extra = max(rep.get('EXTRA', {0}))
            f = lambda v: '%.4f' % v
            body = []
            body.append('{' + ', '.join('{%s, %s, %s, %s, %s}' % (f(a[0]), f(a[1]), f(b[0]), f(b[1]), known('SEG', i)) for i, (a, b) in enumerate(segs)) + '}')
            body.append('{' + ', '.join('{%s, %s, %s, %s}' % (f(x[0]), f(x[1]), f(x[2]), known('CIRCLE', i)) for i, x in enumerate(o['C'])) + '}')
            body.append('{' + ', '.join('{%s, %s, %s, %s, %s, %s}' % (f(x[0]), f(x[1]), f(x[2]), f(x[3]), f(x[4]), known('ARC', i)) for i, x in enumerate(o['A'])) + '}')
            body.append('{' + ', '.join('{%s, %s, %s, %s, %s, %s, %s, %s}' % (cstr(t[0]), f(t[1]), f(t[2]), f(t[3]), f(t[4]), f(t[5]), f(t[6]), known('TEXT', i)) for i, t in enumerate(o['T'])) + '}')
            out.append('    {%s, "%s", 0x%x, %s,\n     %s,\n     %d},' % (cstr(dwg), o['type'], h, tol, ',\n     '.join(body), extra))
            index += 1
    out.append('};')
    dst = os.path.join(ROOT, 'tests', 'Core', 'TchGeometryRef.inc')
    open(dst, 'w', encoding='utf-8', newline='\n').write('\n'.join(out) + '\n')
    print('对象', index, '→', dst)

main()
