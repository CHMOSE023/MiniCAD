"""界面文字的提取与检查（见 docs/I18n/PLAN.md 3.5）。

用法：
    python tools/i18n_extract.py              # 提取：更新 assets/i18n/*.json（新条目值为空，代码里没有了的移进 "obsolete"；
                                              #   新条目与某条过时条目相近时，把旧译文放进 "fuzzy" 待复核，同 gettext 的 msgmerge），输出完成度
    python tools/i18n_extract.py --check      # 检查（不改文件，CI 用）：翻译文件格式、占位符、命令行选项字母、菜单助记符、复数规则；
                                              #   翻译文件与代码不同步（有新条目或过时条目）、有待复核的译文只提示，不算失败
    python tools/i18n_extract.py --where TEXT # 反查：译文或原文里含 TEXT 的条目，列出原文与代码位置（英文日志、截图定位用）
    python tools/i18n_extract.py --lint       # 漏网检查（CI 用）：没有包 Tr / N_ 的中文字面量、格式串含中文的 LOG_*

提取的来源：
- 代码（src/、apps/ 的 .h / .hpp / .cpp）里 Tr / TrF / N_ / LOG_*_TR 的第一个字符串字面量，TrC / TrCF / NC_ 的上下文与原文；
  TrN / TrNF 的原文进 "plurals"（译文是数组，按文件的 "plural" 表达式选第几个，同 gettext 的 Plural-Forms）；
  节点的显示文字 NN_("…") / TrNode("…") 归上下文"节点"（显示时 Nodes::TrNode）；
  相邻的字面量（"甲" "乙"，可以跨行）按 C++ 的规则连成一条
- 界面描述文件 assets/ui/minicad_ui.json 的 title / label / tooltip / text / search（只取含非 ASCII 字符的）
- 节点示例索引 assets/nodes/examples/examples.json、示例图纸索引 assets/samples/samples.json 的 name / description
不提取：自测（apps/Win32/src/Host/SelfTest.cpp）、tests/、tools/。

漏网检查（--lint）跳过：
- 行里或紧挨着的上一行（只有注释的行）有 "i18n:" 注释的（// i18n: 数据、// i18n: 上下文……，写明为什么不翻）
- 节点的搜索关键字（Keywords = "…"，中英文都写，不翻）
"""
import argparse
import difflib
import json
import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent
I18N_DIR = ROOT / "assets" / "i18n"
UI_FILES = [ROOT / "assets" / "ui" / "minicad_ui.json"]
CODE_DIRS = [ROOT / "src", ROOT / "apps"]
CODE_EXCLUDE = {ROOT / "apps" / "Win32" / "src" / "Host" / "SelfTest.cpp"}
UI_TEXT_FIELDS = {"title", "label", "tooltip", "text", "search"}
# 其他要翻译的资源文件：文件 → 要取的字段（节点示例的名称与说明，节点面板“示例”菜单里显示）
TEXT_FILES = {ROOT / "assets" / "nodes" / "examples" / "examples.json": {"name", "description"},
              ROOT / "assets" / "samples" / "samples.json": {"name", "description"}}

LITERAL = r'"(?:[^"\\\n]|\\.)*"'
STRING = r'((?:' + LITERAL + r'\s*)+)'          # 一个或几个相邻的字面量（"甲" "乙"，可以跨行），按 C++ 的规则连成一条
CALL1 = re.compile(r'\b(?:Tr|TrF|N_|LOG_INFO_TR|LOG_WARN_TR|LOG_ERROR_TR)\s*\(\s*' + STRING)
CALL2 = re.compile(r'\b(?:TrC|TrCF|NC_)\s*\(\s*' + STRING + r'\s*,\s*' + STRING)
NODE_CALL = re.compile(r'\b(?:NN_|TrNode)\s*\(\s*' + STRING)     # 节点的显示文字（src/NodeGraph/Value.h），上下文固定
PLURAL_CALL = re.compile(r'\b(?:TrN|TrNF)\s*\(\s*' + STRING)      # 数量相关的句子，进 "plurals"
CONTEXT_ARG = re.compile(r'\b(?:TrC|TrCF|NC_)\s*\(\s*' + STRING)   # 上下文（原文是变量时只有上下文是字面量）
PLAIN_LOG = re.compile(r'\bLOG_(?:TRACE|DEBUG|INFO|WARN|ERROR)\s*\(\s*' + STRING)
CJK = re.compile(r'[　-〿㐀-鿿＀-￯]')     # 汉字与全角标点
KEYWORDS = re.compile(r'\bKeywords\s*=')
NODE_CONTEXT = "节点"
PLURAL = "#plural"          # Messages 里复数条目的"上下文"；obsolete / fuzzy 的键前缀
# 各语言默认的复数规则（gettext 的 Plural-Forms）；翻译文件里写了 "plural" 时用文件的
DEFAULT_PLURAL = {"en": "n != 1", "ja": "0", "ko": "0", "zh-TW": "0", "zh-CN": "0"}
FUZZY_RATIO = 0.75          # 新原文与过时原文的相似度达到它时，旧译文进 "fuzzy"
PLACEHOLDER = re.compile(r'(?<!\{)\{(\d+)\}')
OPTION_LETTER = re.compile(r'\(([A-Za-z0-9])\)')     # 命令行选项 [闭合(C)/放弃(U)]；助记符 (&S) 不算
MNEMONIC = re.compile(r'&([^&])')


def unescape(s):
    """STRING 匹配到的一串相邻字面量 → 连起来、去掉转义的文字"""
    return "".join(unescape_one(x) for x in re.findall(LITERAL, s))


def unescape_one(s):
    s = s[1:-1]
    out, i = [], 0
    table = {"n": "\n", "t": "\t", "r": "\r", '"': '"', "\\": "\\", "'": "'", "0": "\0"}
    while i < len(s):
        c = s[i]
        if c == "\\" and i + 1 < len(s):
            e = s[i + 1]
            if e in table:
                out.append(table[e])
                i += 2
                continue
            if e == "x":
                m = re.match(r"[0-9A-Fa-f]{1,2}", s[i + 2:])
                if m:
                    out.append(chr(int(m.group(0), 16)))
                    i += 2 + len(m.group(0))
                    continue
        out.append(c)
        i += 1
    return "".join(out)


def strip_json_comments(text):
    """去掉 // 与 /* */ 注释和末尾多余的逗号（MiniGUI 的 JSON 允许它们）；保留行号"""
    out, i, n = [], 0, len(text)
    in_str = False
    while i < n:
        c = text[i]
        if in_str:
            out.append(c)
            if c == "\\" and i + 1 < n:
                out.append(text[i + 1])
                i += 2
                continue
            if c == '"':
                in_str = False
            i += 1
        elif c == '"':
            in_str = True
            out.append(c)
            i += 1
        elif text.startswith("//", i):
            while i < n and text[i] != "\n":
                i += 1
        elif text.startswith("/*", i):
            j = text.find("*/", i + 2)
            j = n if j < 0 else j + 2
            out.append("\n" * text.count("\n", i, j))
            i = j
        else:
            out.append(c)
            i += 1
    return re.sub(r",(\s*[}\]])", r"\1", "".join(out))


def strip_cpp_comments(text):
    """去掉 C++ 注释（注释里举例的 Tr("…") 不算），保留字符串、字符字面量与行号"""
    out, i, n = [], 0, len(text)
    while i < n:
        c = text[i]
        if c in "\"'":
            # 原始字符串 R"( … )" 按普通字符串处理足够（里面没有要提取的 Tr）
            j = i + 1
            while j < n and text[j] != c and text[j] != "\n":
                j += 2 if text[j] == "\\" else 1
            out.append(text[i:j + 1])
            i = j + 1
        elif text.startswith("//", i):
            while i < n and text[i] != "\n":
                i += 1
        elif text.startswith("/*", i):
            j = text.find("*/", i + 2)
            j = n if j < 0 else j + 2
            out.append("\n" * text.count("\n", i, j))
            i = j
        else:
            out.append(c)
            i += 1
    return "".join(out)


def load_json(path):
    return json.loads(strip_json_comments(path.read_text(encoding="utf-8-sig")))


class Messages:
    """原文 → 出现位置；键为 (上下文, 原文)，没有上下文时上下文为 None"""

    def __init__(self):
        self.where = {}

    def add(self, ctx, msgid, loc):
        if msgid:
            self.where.setdefault((ctx, msgid), []).append(loc)


def rel(path):
    return path.relative_to(ROOT).as_posix()


def extract_code(msgs):
    for path in code_files():
        text = strip_cpp_comments(path.read_text(encoding="utf-8", errors="replace"))
        for m in CALL2.finditer(text):
            line = text.count("\n", 0, m.start()) + 1
            msgs.add(unescape(m.group(1)), unescape(m.group(2)), f"{rel(path)}:{line}")
        for m in CALL1.finditer(text):
            line = text.count("\n", 0, m.start()) + 1
            msgs.add(None, unescape(m.group(1)), f"{rel(path)}:{line}")
        for m in NODE_CALL.finditer(text):
            line = text.count("\n", 0, m.start()) + 1
            msgs.add(NODE_CONTEXT, unescape(m.group(1)), f"{rel(path)}:{line}")
        for m in PLURAL_CALL.finditer(text):
            line = text.count("\n", 0, m.start()) + 1
            msgs.add(PLURAL, unescape(m.group(1)), f"{rel(path)}:{line}")


def extract_ui(msgs):
    for path, fields in [(p, UI_TEXT_FIELDS) for p in UI_FILES] + list(TEXT_FILES.items()):
        text = path.read_text(encoding="utf-8-sig")
        doc = json.loads(strip_json_comments(text))

        def walk(v):
            if isinstance(v, dict):
                for k, x in v.items():
                    if k in fields and isinstance(x, str) and any(ord(ch) > 127 for ch in x):
                        # 行号：原文第一次出现的地方
                        pos = text.find(json.dumps(x, ensure_ascii=False))
                        line = text.count("\n", 0, pos) + 1 if pos >= 0 else 0
                        msgs.add(None, x, f"{rel(path)}:{line}")
                    else:
                        walk(x)
            elif isinstance(v, list):
                for x in v:
                    walk(x)

        walk(doc)


def collect():
    msgs = Messages()
    extract_code(msgs)
    extract_ui(msgs)
    return msgs


def catalogs():
    return sorted(I18N_DIR.glob("*.json")) if I18N_DIR.exists() else []


class PluralError(Exception):
    pass


def eval_plural(expr, n):
    """gettext 的 Plural-Forms 表达式（C 语法，变量 n）在 n 时的值；与 MiniI18n::EvalPlural 一致"""
    m = re.search(r"plural\s*=\s*([^;]*)", expr) if "nplurals" in expr else None
    expr = m.group(1) if m else expr
    tokens = re.findall(r"\s*(\d+|n|\|\||&&|==|!=|<=|>=|[-+*/%<>!?:()])", expr)
    if "".join(tokens).replace(" ", "") != re.sub(r"\s+", "", expr) or not tokens:
        raise PluralError(expr)
    tokens = [t.strip() for t in tokens]
    pos = [0]

    def peek():
        return tokens[pos[0]] if pos[0] < len(tokens) else None

    def eat(t):
        if peek() == t:
            pos[0] += 1
            return True
        return False

    def binary(sub, ops):
        def parse():
            v = sub()
            while peek() in ops:
                op = tokens[pos[0]]
                pos[0] += 1
                r = sub()
                v = {"||": lambda a, b: int(bool(a or b)), "&&": lambda a, b: int(bool(a and b)),
                     "==": lambda a, b: int(a == b), "!=": lambda a, b: int(a != b),
                     "<": lambda a, b: int(a < b), "<=": lambda a, b: int(a <= b),
                     ">": lambda a, b: int(a > b), ">=": lambda a, b: int(a >= b),
                     "+": lambda a, b: a + b, "-": lambda a, b: a - b, "*": lambda a, b: a * b,
                     "/": lambda a, b: a // b if b else 0, "%": lambda a, b: a % b if b else 0}[op](v, r)
            return v
        return parse

    def primary():
        if eat("("):
            v = ternary()
            if not eat(")"):
                raise PluralError(expr)
            return v
        if eat("n"):
            return n
        t = peek()
        if t is None or not t.isdigit():
            raise PluralError(expr)
        pos[0] += 1
        return int(t)

    def unary():
        if eat("!"):
            return int(not unary())
        return primary()

    mul = binary(unary, ("*", "/", "%"))
    add = binary(mul, ("+", "-"))
    relational = binary(add, ("<", "<=", ">", ">="))
    eq = binary(relational, ("==", "!="))
    land = binary(eq, ("&&",))
    lor = binary(land, ("||",))

    def ternary():
        c = lor()
        if eat("?"):
            a = ternary()
            if not eat(":"):
                raise PluralError(expr)
            b = ternary()
            return a if c else b
        return c

    v = ternary()
    if pos[0] != len(tokens):
        raise PluralError(expr)
    return v


def plural_count(rule):
    """复数规则一共有几种形式（看 n = 0～1000 时出现过的最大下标）"""
    return max(eval_plural(rule, n) for n in range(1001)) + 1


def nonempty(v):
    return any(v) if isinstance(v, list) else bool(v)


def fuzzy_match(msgid, pool):
    """pool 里与 msgid 最相近、相似度够的原文；没有时返回 None"""
    best, best_ratio = None, FUZZY_RATIO
    for old in pool:
        ratio = difflib.SequenceMatcher(None, msgid, old).ratio()
        if ratio >= best_ratio:
            best, best_ratio = old, ratio
    return best


def update_catalog(path, msgs):
    data = load_json(path)
    messages = data.get("messages", {}) or {}
    contexts = data.get("contexts", {}) or {}
    plurals = data.get("plurals", {}) or {}
    fuzzy = data.get("fuzzy", {}) or {}
    obsolete = data.get("obsolete", {}) or {}
    rule = data.get("plural") or DEFAULT_PLURAL.get(data.get("language", path.stem), "n != 1")
    forms = plural_count(rule)

    new_messages, new_contexts, new_plurals = {}, {}, {}
    for (ctx, msgid) in msgs.where:
        if ctx is None:
            new_messages[msgid] = messages.get(msgid, obsolete.get(msgid, ""))
        elif ctx == PLURAL:
            # 从 TrF 改成 TrNF 的：原来的译文先填进每种形式
            single = messages.get(msgid) or obsolete.get(msgid) or ""
            new_plurals[msgid] = plurals.get(msgid, obsolete.get(PLURAL + "|" + msgid, [single] * forms))
        else:
            new_contexts.setdefault(ctx, {})[msgid] = contexts.get(ctx, {}).get(msgid, "")
    for msgid, t in messages.items():
        if msgid not in new_messages and t:
            obsolete[msgid] = t
    for ctx, table in contexts.items():
        for msgid, t in table.items():
            if msgid not in new_contexts.get(ctx, {}) and t:
                obsolete[ctx + "|" + msgid] = t
    for msgid, t in plurals.items():
        if msgid not in new_plurals and nonempty(t):
            obsolete[PLURAL + "|" + msgid] = t
    for msgid in new_messages:
        obsolete.pop(msgid, None)
    for msgid in new_plurals:
        obsolete.pop(PLURAL + "|" + msgid, None)
        obsolete.pop(msgid, None)

    # 待复核：新条目还没译文、又有相近的过时条目时，旧译文放进 fuzzy（运行时不显示，确认后移进 messages / contexts）
    keys = {msgid: msgid for msgid, t in new_messages.items() if not t}
    keys.update({ctx + "|" + msgid: msgid for ctx, table in new_contexts.items() for msgid, t in table.items() if not t})
    fuzzy = {k: v for k, v in fuzzy.items() if k in keys}
    for key, msgid in keys.items():
        if key in fuzzy:
            continue
        prefix = key[:len(key) - len(msgid)]
        pool = [k[len(prefix):] for k, v in obsolete.items() if isinstance(v, str) and k.startswith(prefix)
                and (prefix or "|" not in k)]
        old = fuzzy_match(msgid, pool)
        if old is not None:
            fuzzy[key] = obsolete[prefix + old]

    out = {
        "language": data.get("language", path.stem),
        "name": data.get("name", path.stem),
        "plural": rule,
        "messages": new_messages,
        "contexts": new_contexts,
    }
    if new_plurals:
        out["plurals"] = new_plurals
    if fuzzy:
        out["fuzzy"] = fuzzy
    out["obsolete"] = obsolete
    path.write_text(json.dumps(out, ensure_ascii=False, indent=2) + "\n", encoding="utf-8", newline="\n")
    done = sum(1 for v in new_messages.values() if v) + sum(1 for t in new_contexts.values() for v in t.values() if v)
    done += sum(1 for v in new_plurals.values() if nonempty(v))
    total = len(new_messages) + sum(len(t) for t in new_contexts.values()) + len(new_plurals)
    return done, total, len(fuzzy)


def command_labels():
    """命令 ID → 名称原文：MainCommands.cpp 里 .id = "…" 之后、下一个 .id 之前的 .label = N_("…")；界面描述的 commands 覆盖"""
    labels = {}
    path = ROOT / "apps" / "Common" / "src" / "GUI" / "MainCommands.cpp"
    text = path.read_text(encoding="utf-8")
    ids = list(re.finditer(r'\.id\s*=\s*' + STRING, text))
    for i, m in enumerate(ids):
        end = ids[i + 1].start() if i + 1 < len(ids) else len(text)
        lm = re.search(r'\.label\s*=\s*(?:N_\(\s*)?' + STRING, text[m.end():end])
        if lm:
            labels[unescape(m.group(1))] = unescape(lm.group(1))
    for path in UI_FILES:
        for cid, fields in load_json(path).get("commands", {}).items():
            if isinstance(fields, dict) and isinstance(fields.get("label"), str):
                labels[cid] = fields["label"]
    return labels


def check(msgs):
    errors, warnings = [], []
    labels = command_labels()
    for path in catalogs():
        name = rel(path)
        try:
            data = load_json(path)
        except (json.JSONDecodeError, UnicodeDecodeError) as e:
            errors.append(f"{name}：格式错误，{e}")
            continue
        entries = [(None, k, v) for k, v in (data.get("messages") or {}).items()]
        entries += [(c, k, v) for c, t in (data.get("contexts") or {}).items() for k, v in t.items()]
        rule = data.get("plural") or DEFAULT_PLURAL.get(data.get("language", path.stem), "n != 1")
        try:
            forms = plural_count(rule)
        except PluralError:
            errors.append(f"{name}：复数规则 \"{rule}\" 无法识别")
            forms = 1
        for msgid, v in (data.get("plurals") or {}).items():
            loc = (msgs.where.get((PLURAL, msgid)) or ["?"])[0]
            values = v if isinstance(v, list) else [v]
            if not all(isinstance(x, str) for x in values):
                errors.append(f"{name}：\"{msgid}\" 的复数译文不是字符串数组")
                continue
            if isinstance(v, list) and any(values) and len(values) != forms:
                warnings.append(f"{name}：\"{msgid}\" 有 {len(values)} 个复数译文，复数规则要 {forms} 个（{loc}）")
            for x in values:
                if x and not set(PLACEHOLDER.findall(x)) <= set(PLACEHOLDER.findall(msgid)):
                    errors.append(f"{name}：占位符不一致（{loc}）\n    原文：{msgid}\n    译文：{x}")
        entries += [(PLURAL, k, "") for k in (data.get("plurals") or {})]
        if data.get("fuzzy"):
            warnings.append(f"{name}：{len(data['fuzzy'])} 条译文待复核（\"fuzzy\"：原文改过，旧译文核对后移进 messages / contexts）")
        known = set(msgs.where)
        missing = [k for k in msgs.where if not any(e[0] == k[0] and e[1] == k[1] for e in entries)]
        stale = [(c, k) for c, k, _ in entries if (c, k) not in known]
        if missing:
            warnings.append(f"{name}：{len(missing)} 条新原文还没进翻译文件（运行 python tools/i18n_extract.py 更新）")
        if stale:
            warnings.append(f"{name}：{len(stale)} 条原文在代码里已经没有了（运行 python tools/i18n_extract.py 移进 obsolete）")

        lookup = {}
        for ctx, msgid, t in entries:
            if not isinstance(t, str):
                errors.append(f"{name}：\"{msgid}\" 的译文不是字符串")
                continue
            lookup[(ctx, msgid)] = t
            if not t:
                continue
            loc = (msgs.where.get((ctx, msgid)) or ["?"])[0]
            if sorted(PLACEHOLDER.findall(msgid)) != sorted(PLACEHOLDER.findall(t)):
                errors.append(f"{name}：占位符不一致（{loc}）\n    原文：{msgid}\n    译文：{t}")
            src_opts = sorted(OPTION_LETTER.findall(msgid))
            if src_opts and sorted(OPTION_LETTER.findall(t)) != src_opts:
                errors.append(f"{name}：命令行选项字母不一致，应为 {''.join(src_opts)}（{loc}）\n    原文：{msgid}\n    译文：{t}")

        # 同一个菜单里的助记符不重复
        def tr(s):
            return lookup.get((None, s)) or s

        def menu_items(items, where):
            letters = {}
            for it in items:
                if isinstance(it, str):
                    if it in ("-", "|"):
                        continue
                    text = tr(labels.get(it, ""))
                elif isinstance(it, dict):
                    text = tr(it.get("title", ""))
                    if isinstance(it.get("items"), list):
                        menu_items(it["items"], where + " > " + text)
                else:
                    continue
                m = MNEMONIC.search(text)
                if m:
                    letters.setdefault(m.group(1).lower(), []).append(text)
            for letter, texts in letters.items():
                if len(texts) > 1:
                    warnings.append(f"{name}：菜单 {where} 里助记符 {letter.upper()} 重复：{' / '.join(texts)}")

        for ui in UI_FILES:
            for menu in load_json(ui).get("menus", []):
                menu_items(menu.get("items", []), tr(menu.get("title", "?")))
    return errors, warnings


def code_files():
    for d in CODE_DIRS:
        for path in sorted(d.rglob("*")):
            if path.suffix in (".h", ".hpp", ".cpp") and path not in CODE_EXCLUDE:
                yield path


def marked_above(lines, line):
    """同一条语句前面有只写注释的一行，并且写了 i18n:（字面量所在的行放不下注释、一整张表都是数据时）"""
    for i in range(line - 1, max(line - 40, -1), -1):
        above = lines[i].strip()
        if above.startswith("//"):
            if "i18n:" in above:
                return True
            continue
        if above.endswith((";", "}")):         # 到了上一条语句
            return False
    return False


def lint():
    """没有包 Tr / N_ 的中文字面量、格式串含中文的 LOG_*（要改成 LOG_*_TR，开发者看的改写成英文）"""
    problems = []
    for path in code_files():
        raw = path.read_text(encoding="utf-8", errors="replace")
        text = strip_cpp_comments(raw)
        lines = raw.split("\n")
        covered = []
        for rx in (CALL1, NODE_CALL, CONTEXT_ARG, PLURAL_CALL):
            covered += [m.span(1) for m in rx.finditer(text)]
        for m in CALL2.finditer(text):
            covered += [m.span(1), m.span(2)]
        for m in PLAIN_LOG.finditer(text):
            covered.append(m.span(1))
            line = text.count("\n", 0, m.start())
            if CJK.search(unescape(m.group(1))) and "i18n:" not in lines[line]:
                problems.append(f"{rel(path)}:{line + 1}：日志的格式串含中文（改 LOG_*_TR，开发者看的改写成英文）\n    {lines[line].strip()}")
        for m in re.finditer(LITERAL, text):
            if not CJK.search(m.group(0)) or any(a <= m.start() < b for a, b in covered):
                continue
            line = text.count("\n", 0, m.start())
            if "i18n:" in lines[line] or KEYWORDS.search(lines[line]) or marked_above(lines, line):
                continue
            problems.append(f"{rel(path)}:{line + 1}：中文字面量没有包 Tr / N_（数据加注释 // i18n: 数据）\n    {lines[line].strip()}")
    return list(dict.fromkeys(problems))     # 同一行的几个字面量只报一次


def where(msgs, text):
    def norm(s):
        return (s or "").replace("&", "").lower()      # 菜单里的助记符不影响查找

    want = norm(text)
    hits = []
    for path in catalogs():
        data = load_json(path)
        for msgid, t in (data.get("messages") or {}).items():
            if want in norm(t) or want in norm(msgid):
                hits.append((path.stem, None, msgid, t))
        for ctx, table in (data.get("contexts") or {}).items():
            for msgid, t in table.items():
                if want in norm(t) or want in norm(msgid):
                    hits.append((path.stem, ctx, msgid, t))
    if not catalogs():
        for (ctx, msgid) in msgs.where:
            if want in norm(msgid):
                hits.append(("zh-CN", ctx, msgid, ""))
    for path in catalogs():
        for msgid, t in (load_json(path).get("plurals") or {}).items():
            text_t = " / ".join(t) if isinstance(t, list) else t
            if want in norm(text_t) or want in norm(msgid):
                hits.append((path.stem, PLURAL, msgid, text_t))
    for lang, ctx, msgid, t in hits:
        print(f"[{lang}]{' (' + ('复数' if ctx == PLURAL else ctx) + ')' if ctx else ''} {msgid}")
        if t:
            print(f"    译文：{t}")
        for loc in msgs.where.get((ctx, msgid), ["（代码里已经没有）"]):
            print(f"    {loc}")
    return 0 if hits else 1


def main():
    for stream in (sys.stdout, sys.stderr):
        stream.reconfigure(encoding="utf-8")
    ap = argparse.ArgumentParser(description="界面文字的提取与检查")
    ap.add_argument("--check", action="store_true", help="只检查，不改文件")
    ap.add_argument("--where", metavar="TEXT", help="反查含 TEXT 的译文或原文")
    ap.add_argument("--lint", action="store_true", help="列出没有包 Tr / N_ 的中文字面量与格式串含中文的日志")
    args = ap.parse_args()

    if args.lint:
        problems = lint()
        for p in problems:
            print(p)
        print(f"{len(problems)} 处漏网的中文")
        return 1 if problems else 0

    msgs = collect()
    if args.where:
        return where(msgs, args.where)
    if args.check:
        errors, warnings = check(msgs)
        for w in warnings:
            print("提示：" + w)
        for e in errors:
            print("错误：" + e)
        print(f"{len(msgs.where)} 条原文，{len(catalogs())} 个翻译文件，{len(errors)} 个错误")
        return 1 if errors else 0
    I18N_DIR.mkdir(parents=True, exist_ok=True)
    print(f"{len(msgs.where)} 条原文")
    for path in catalogs():
        done, total, fuzzy = update_catalog(path, msgs)
        print(f"{rel(path)}：已翻译 {done} / {total}" + (f"，{fuzzy} 条待复核（fuzzy）" if fuzzy else ""))
    return 0


if __name__ == "__main__":
    sys.exit(main())
