"""界面文字的提取与检查（见 docs/I18n/PLAN.md 3.5）。

用法：
    python tools/i18n_extract.py              # 提取：更新 assets/i18n/*.json（新条目值为空，代码里没有了的移进 "obsolete"），输出完成度
    python tools/i18n_extract.py --check      # 检查（不改文件，CI 用）：翻译文件格式、占位符、命令行选项字母、菜单助记符；
                                              #   翻译文件与代码不同步（有新条目或过时条目）只提示，不算失败
    python tools/i18n_extract.py --where TEXT # 反查：译文或原文里含 TEXT 的条目，列出原文与代码位置（英文日志、截图定位用）

提取的来源：
- 代码（src/、apps/ 的 .h / .hpp / .cpp）里 Tr / TrF / N_ / LOG_*_TR 的第一个字符串字面量，TrC / TrCF / NC_ 的上下文与原文
- 界面描述文件 assets/ui/minicad_ui.json 的 title / label / tooltip / text / search（只取含非 ASCII 字符的）
不提取：自测（apps/Win32/src/Host/SelfTest.cpp）、tests/、tools/。
"""
import argparse
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

STRING = r'"((?:[^"\\\n]|\\.)*)"'
CALL1 = re.compile(r'\b(?:Tr|TrF|N_|LOG_INFO_TR|LOG_WARN_TR|LOG_ERROR_TR)\s*\(\s*' + STRING)
CALL2 = re.compile(r'\b(?:TrC|TrCF|NC_)\s*\(\s*' + STRING + r'\s*,\s*' + STRING)
PLACEHOLDER = re.compile(r'(?<!\{)\{(\d+)\}')
OPTION_LETTER = re.compile(r'\(([A-Za-z0-9])\)')     # 命令行选项 [闭合(C)/放弃(U)]；助记符 (&S) 不算
MNEMONIC = re.compile(r'&([^&])')


def unescape(s):
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
    for d in CODE_DIRS:
        for path in sorted(d.rglob("*")):
            if path.suffix not in (".h", ".hpp", ".cpp") or path in CODE_EXCLUDE:
                continue
            text = strip_cpp_comments(path.read_text(encoding="utf-8", errors="replace"))
            for m in CALL2.finditer(text):
                line = text.count("\n", 0, m.start()) + 1
                msgs.add(unescape(m.group(1)), unescape(m.group(2)), f"{rel(path)}:{line}")
            for m in CALL1.finditer(text):
                line = text.count("\n", 0, m.start()) + 1
                msgs.add(None, unescape(m.group(1)), f"{rel(path)}:{line}")


def extract_ui(msgs):
    for path in UI_FILES:
        text = path.read_text(encoding="utf-8-sig")
        doc = json.loads(strip_json_comments(text))

        def walk(v):
            if isinstance(v, dict):
                for k, x in v.items():
                    if k in UI_TEXT_FIELDS and isinstance(x, str) and any(ord(ch) > 127 for ch in x):
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


def update_catalog(path, msgs):
    data = load_json(path)
    messages = data.get("messages", {}) or {}
    contexts = data.get("contexts", {}) or {}
    obsolete = data.get("obsolete", {}) or {}

    new_messages, new_contexts = {}, {}
    for (ctx, msgid) in msgs.where:
        if ctx is None:
            new_messages[msgid] = messages.get(msgid, obsolete.get(msgid, ""))
        else:
            new_contexts.setdefault(ctx, {})[msgid] = contexts.get(ctx, {}).get(msgid, "")
    for msgid, t in messages.items():
        if msgid not in new_messages and t:
            obsolete[msgid] = t
    for ctx, table in contexts.items():
        for msgid, t in table.items():
            if msgid not in new_contexts.get(ctx, {}) and t:
                obsolete[ctx + "|" + msgid] = t
    for msgid in new_messages:
        obsolete.pop(msgid, None)

    out = {
        "language": data.get("language", path.stem),
        "name": data.get("name", path.stem),
        "messages": new_messages,
        "contexts": new_contexts,
        "obsolete": obsolete,
    }
    path.write_text(json.dumps(out, ensure_ascii=False, indent=2) + "\n", encoding="utf-8", newline="\n")
    done = sum(1 for v in new_messages.values() if v) + sum(1 for t in new_contexts.values() for v in t.values() if v)
    total = len(new_messages) + sum(len(t) for t in new_contexts.values())
    return done, total


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
    for lang, ctx, msgid, t in hits:
        print(f"[{lang}]{' (' + ctx + ')' if ctx else ''} {msgid}")
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
    args = ap.parse_args()

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
        done, total = update_catalog(path, msgs)
        print(f"{rel(path)}：已翻译 {done} / {total}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
