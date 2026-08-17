#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""
tools/refman.py - hyperlinked reference manual generator for the Vyb stdlib.

Scans stdlib/**/*.vyb and emits a Markdown refman under docs/refman/ whose core
value is the *inter-relationship graph*: modules, files, exported symbols, and
the import / implement / uses-type / runtime / call / prose-reference edges
between them. Prose is reused from existing '#'/'//' doc comments.

Deterministic: a clean tree regenerates byte-identical output.

Usage:
  tools/refman.py                          # write docs/refman
  tools/refman.py --check                  # validate resolution + drift
  tools/refman.py --emit-dir X --check     # regenerate to X and validate
"""
from __future__ import annotations

import argparse
import json
import re
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
DEFAULT_STDLIB = ROOT / "stdlib"
DEFAULT_RUNTIME = ROOT / "runtime" / "vyb_runtime.c"
DEFAULT_OUT = ROOT / "docs" / "refman"

# --------------------------------------------------------------------------
# small lexer helpers
# --------------------------------------------------------------------------


def strip_inline_comment(line: str) -> str:
    """Drop a trailing '// ...' (not inside a string literal) from a line."""
    out = []
    i, n = 0, len(line)
    in_str = False
    while i < n:
        c = line[i]
        if c == '"':
            in_str = not in_str
        elif c == "/" and i + 1 < n and line[i + 1] == "/" and not in_str:
            break
        out.append(c)
        i += 1
    return "".join(out)


_BR_PAIRS = {">": "<", ")": "(", "]": "["}


def _depth(line: str) -> int:
    """Angle-bracket nesting depth after scanning `line` (for split_top)."""
    stack = []
    dep = 0
    for ch in line:
        if ch in "<([":
            stack.append(ch)
            dep += 1
        elif ch in ">)]" and stack and stack[-1] == _BR_PAIRS[ch]:
            stack.pop()
            dep -= 1
    return dep


def split_top(s: str, sep: str = ",") -> list[str]:
    parts, cur, stack = [], [], []
    for ch in s:
        if ch in "<([":
            stack.append(ch)
        elif ch in ">)]":
            if stack and stack[-1] == _BR_PAIRS[ch]:
                stack.pop()
        if ch == sep and not stack:
            parts.append("".join(cur).strip())
            cur = []
        else:
            cur.append(ch)
    if cur:
        parts.append("".join(cur).strip())
    return [p for p in parts if p]


def match_paren(s: str, start: int, o: str, c: str) -> int:
    d = 0
    for i in range(start, len(s)):
        if s[i] == o:
            d += 1
        elif s[i] == c:
            d -= 1
            if d == 0:
                return i
    return -1


def bracket_span(s: str, start: int) -> tuple[int, int]:
    """Given s[start]=='<', return (open idx, close idx) honoring nesting."""
    d = 0
    for i in range(start, len(s)):
        if s[i] == "<":
            d += 1
        elif s[i] == ">":
            d -= 1
            if d == 0:
                return start, i
    return start, len(s) - 1


_DIRECTIONS = "<(["


def find_arrow(s: str) -> int:
    """Find the FIRST '->' that is NOT inside <...>(...)[...] (body arrow)."""
    stack = []
    i = 0
    while i < len(s) - 1:
        ch = s[i]
        if ch in "<([":
            stack.append(ch)
        elif ch in ">)]":
            if stack and stack[-1] == _BR_PAIRS[ch]:
                stack.pop()
        elif ch == "-" and s[i + 1] == ">" and not stack:
            return i
        i += 1
    return -1


def extract_call(sig: str):
    """Parse `name(a<T>, b<U>)<Ret>` -> (name, [(p, t)], ret). Handles nested
    <...> and fn(T)->T params. No arrow needed (call - not lambda body)."""
    op = sig.find("(")
    if op == -1:
        return None
    cp = match_paren(sig, op, "(", ")")
    if cp == -1:
        return None
    name = (sig[:op].strip().split() or [""])[-1]
    if not name or name.startswith("("):
        return None
    params = []
    for part in split_top(sig[op + 1:cp]):
        m = re.match(r"^([A-Za-z_]\w*)\s*<(.+)>$", part)
        if m:
            params.append((m.group(1), m.group(2)))
        else:
            params.append((part, ""))
    ret = ""
    tail = sig[cp + 1:].strip()
    if tail.startswith("<"):
        a, b = bracket_span(tail, 0)
        ret = tail[a + 1:b]
    return name, params, ret


def strip_comment(ln: str) -> str:
    """Peel a leading `#`/`//` comment marker but keep the relative indentation
    that follows it, so nested lists and continuation lines in doc comments render
    as valid Markdown instead of collapsing to column zero."""
    s = ln.strip()
    return re.sub(r"^[#/]+ ?", "", s, count=1)


def comment_text(lines: list[str]) -> str:
    return "\n".join(strip_comment(ln) for ln in lines)


def doc_summary(doc: str) -> str:
    """First meaningful doc line for a one-line summary. Skips leading
    horizontal-rule separators (e.g. a `# ----…` opener)."""
    for ln in doc.splitlines():
        s = ln.strip()
        if s and not re.match(r"^-{3,}$", s):
            return s
    return ""


# --------------------------------------------------------------------------
# per-file parsing
# --------------------------------------------------------------------------


def parse_import(text: str, reexport: bool) -> dict:
    m = re.match(r"^([^\s{}:]+(?:::[^\s{}:]+)*)(?:\s*::?\s*\{([^}]*)\})?$", text.strip())
    target = m.group(1) if m else text.strip()
    names = [x.strip() for x in split_top(m.group(2))] if (m and m.group(2)) else []
    return {"target": target, "names": names, "reexport": reexport}


def parse_bind(header: str) -> dict:
    body = header.replace("share(all)", "").strip()
    rest = body[len("bind"):].strip() if body.startswith("bind") else body
    tvar = ""
    if rest.startswith("<"):
        a, b = bracket_span(rest, 0)
        tvar = rest[a:b + 1]
        rest = rest[b + 1:].strip()
    mt = re.match(r"([A-Za-z_]\w*)\s*->\s*([A-Za-z_]\w*)", rest)
    if not mt:
        return {"name": header.strip().split("->", 1)[-1].strip() or header.strip(),
                "kind": "bind", "signature": header.strip(), "tvar": tvar,
                "aspect": "", "target": "", "exported": True}
    aspect, target = mt.group(1), mt.group(2)
    return {"name": "%s->%s" % (aspect, target), "kind": "bind",
            "signature": header.strip(), "tvar": tvar,
            "aspect": aspect, "target": target, "exported": True}


def parse_simple_header(header: str, kind: str) -> dict:
    # `struct BTreeMap<K, V> {` etc. The generic span may contain spaces and
    # commas, and the declaration ends with `{`, so a whitespace split truncates
    # the name (e.g. `BTreeMap<K,`). Strip the brace first, then walk the generic
    # span bracket-balanced to capture the full `Name<...>`.
    body = header.replace("share(all)", "").strip()
    decl = body.split("{", 1)[0].strip()
    name = tvar = ""
    if decl.startswith(kind):
        rest = decl[len(kind):].strip()
        m = re.match(r"([A-Za-z_]\w*)", rest)
        if m:
            name = m.group(1)
            tail = rest[len(name):].strip()
            if tail.startswith("<"):
                a, b = bracket_span(tail, 0)
                if b > a:
                    tvar = tail[a:b + 1]
    if not name:
        toks = body.split()
        name = toks[1] if len(toks) > 1 else body.strip()
    return {"name": (name + tvar) if tvar else name, "kind": kind,
            "signature": header.strip(), "tvar": tvar, "aspect": "", "target": "",
            "exported": True}


def parse_members(kind: str, block: str) -> list:
    members = []
    lines = [strip_inline_comment(l).strip() for l in block.splitlines()]
    for l in lines:
        if not l or l == "}":
            continue
        if kind in ("struct",):
            for part in split_top(l.rstrip(",")):
                m = re.match(r"^([A-Za-z_]\w*)\s*<(.+)>$", part)
                if m:
                    members.append(("field", m.group(1), m.group(2)))
        elif kind == "enum":
            m = re.match(r"^([A-Za-z_]\w*)\s*=\s*(.+)$", l.rstrip(","))
            members.append(("member", m.group(1), m.group(2).strip()) if m else ("member", l.rstrip(","), ""))
        else:  # aspect / bind -> methods
            if re.match(r"^[A-Za-z_]\w*\s*\(", l) and not re.match(r"^(return|if|else|while|for|let|var)\b", l):
                head = l.split("{", 1)[0]
                # Aspects declare plain `name(…) <Ret>` signatures (no arrow); binds
                # carry a `->` body separator on the header line. extract_call is
                # tolerant of both, but for binds we still require the arrow so that
                # inner calls in method bodies are not mistaken for methods.
                sig = head
                if kind != "aspect":
                    ar = head.rfind("->")
                    sig = head[:ar].strip() if ar != -1 else ""
                parsed = extract_call(sig) if sig else None
                if parsed:
                    members.append(parsed)
    return members


def take_bodies(path: Path) -> str:
    """Return raw text with doc comments kept but stripping nothing; used only
    for call-mining (we parse function bodies inline instead for correctness)."""
    return path.read_text()


def parse_file(text: str) -> dict:
    symbols = []
    imports = []
    header = []
    header_done = False
    pending = []  # raw comment lines awaiting the next decl
    share_pending = False

    lines = text.splitlines()
    i, n = 0, len(lines)

    def attach():
        nonlocal pending
        doc = "\n".join(strip_comment(ln) for ln in pending) if pending else ""
        pending = []
        return doc

    while i < n:
        raw = lines[i]
        stripped = raw.strip()

        if not stripped:
            if not header_done:
                pass
            pending = []
            header_done = True
            i += 1
            continue

        if stripped.startswith("#") or stripped.startswith("//"):
            if not header_done:
                header.append(stripped)
            else:
                pending.append(stripped)
            i += 1
            continue

        header_done = True

        if stripped == "share(all)":
            share_pending = True
            i += 1
            continue

        m_imp = re.match(r"^(share\(all\)\s+)?import\s+(.*)$", stripped)
        if m_imp:
            def _bal(s): return s.count("{") - s.count("}")
            acc = [strip_inline_comment(m_imp.group(2)).strip()]
            bal = _bal(acc[0])
            while bal > 0 and i + 1 < n:
                i += 1
                ln = strip_inline_comment(lines[i]).strip()
                acc.append(ln)
                bal += _bal(ln)
            imp = parse_import(" ".join(acc), bool(share_pending))
            imports.append({"target": imp["target"], "names": imp["names"],
                            "reexport": bool(share_pending) or bool(m_imp.group(1)),
                            "doc": attach()})
            share_pending = False
            i += 1
            continue

        kind = None
        for k in ("struct", "enum", "aspect", "bind"):
            if re.match(r"^(?:share\(all\)\s+)?%s\b" % k, stripped):
                kind = k
                break
        if kind:
            doc = attach()
            headerline = re.sub(r"^(share\(all\))\s+", "", stripped)
            parsed = parse_bind(headerline) if kind == "bind" else parse_simple_header(headerline, kind)
            # consume member block to matching close brace
            depth = stripped.count("{") - stripped.count("}")
            block_lines = []
            j = i + 1
            while j < n and depth > 0:
                block_lines.append(lines[j])
                depth += lines[j].count("{") - lines[j].count("}")
                j += 1
            block = "\n".join(block_lines)
            members = parse_members(kind, block)
            parsed["params"] = members
            parsed["doc"] = doc
            symbols.append(parsed)
            i = j
            share_pending = False
            continue

        # function declaration
        op = stripped.find("(")
        if op > 0 and not stripped.endswith(","):
            cp = match_paren(stripped, op, "(", ")")
            if cp > op and stripped[op + 1:cp].strip() or True:
                # it must look like a call signature followed by <Ret>? -> {
                sig = extract_call(stripped)
                if sig:
                    name, params, ret = sig
                    # find body
                    openb = find_arrow(stripped)
                    if openb == -1:
                        body = ""
                        end = i + 1
                    else:
                        # locate '{' after arrow, then consume balanced body
                        bo = stripped.find("{", openb)
                        if bo == -1:
                            body, end = "", i + 1
                        else:
                            depth = 1
                            seg = stripped[bo + 1:]
                            body_parts = [seg]
                            while depth > 0:
                                depth += seg.count("{") - seg.count("}")
                                if depth > 0:
                                    i += 1
                                    seg = lines[i]
                                    body_parts.append(seg)
                                    depth += seg.count("{") - seg.count("}")
                            body = "\n".join(body_parts)
                            end = i + 1
                    symbols.append({
                        "name": name, "kind": "fn", "signature": stripped.split("{", 1)[0].strip(),
                        "tvar": "", "aspect": "", "target": "",
                        "params": params, "ret": ret, "doc": attach(),
                        "exported": bool(share_pending), "body": body,
                    })
                    share_pending = False
                    i = end
                    continue

        i += 1

    return {"symbols": symbols, "imports": imports,
            "header": "\n".join(strip_comment(ln) for ln in header),
            "share": None}


def module_of(rel: str) -> str:
    if not rel.startswith("stdlib/"):
        return rel.split("/")[0]
    seg = rel[len("stdlib/"):].split("/")
    return "prelude" if len(seg) == 1 else seg[0]


# --------------------------------------------------------------------------
# relationship graph
# --------------------------------------------------------------------------


def runtime_map(runtime_path: Path) -> dict:
    if not runtime_path.exists():
        return {}
    out = {}
    for idx, ln in enumerate(runtime_path.read_text().splitlines(), start=1):
        m = re.search(r"\b__vyb_([A-Za-z_0-9]+)\s*\(", ln)
        if m and "VYB_WEAK" in ln:
            out["vyb_" + m.group(1)] = idx
    return out


def build_graph(files: dict, rt: dict) -> dict:
    module_order = []
    for rel in files:
        m = module_of(rel)
        if m not in module_order:
            module_order.append(m)

    export_index = {}
    omnisearch = {}
    for rel, fm in files.items():
        m = module_of(rel)
        for s in fm["symbols"]:
            if s.get("exported"):
                export_index[(m, s["name"])] = s
                omnisearch.setdefault(s["name"], []).append((m, s["name"]))

    edges = []
    for rel, fm in files.items():
        m = module_of(rel)
        for imp in fm["imports"]:
            tm = imp["target"].split("::", 1)[0]
            names = imp["names"]
            if not names:
                names = [""]
            for nm in names:
                edges.append({"type": "import", "from_module": m, "from_symbol": "",
                              "to_module": tm, "to_symbol": nm, "reexport": imp["reexport"],
                              "resolved": (tm, nm) in export_index if nm else tm in module_order})
        for s in fm["symbols"]:
            if not s.get("exported"):
                continue
            sm, sn = m, s["name"]
            if s["kind"] == "bind" and s.get("aspect") and s.get("target"):
                edges.append({"type": "implement", "from_module": sm, "from_symbol": sn,
                              "to_module": sm, "to_symbol": s["aspect"]})
                edges.append({"type": "implementOn", "from_module": sm, "from_symbol": sn,
                              "to_module": sm, "to_symbol": s["target"]})
            if s["kind"] == "fn":
                for _, ty in s.get("params", []) + [("", s.get("ret", ""))]:
                    for tid in re.findall(r"\b([A-Za-z_]\w*)\b", ty):
                        if (sm, tid) in export_index and tid != sn:
                            edges.append({"type": "usesType", "from_module": sm, "from_symbol": sn,
                                          "to_module": sm, "to_symbol": tid})
                        elif omnisearch.get(tid):
                            for (tm, tn) in omnisearch[tid]:
                                if (tm, tn) != (sm, sn):
                                    edges.append({"type": "usesType", "from_module": sm, "from_symbol": sn,
                                                  "to_module": tm, "to_symbol": tn})
                body = s.get("body", "")
                for rn in set(re.findall(r"\b(vyb_[a-z0-9_]+)\b", body)):
                    edges.append({"type": "runtimeRef", "from_module": sm, "from_symbol": sn,
                                  "to_symbol": rn})
                for rn in set(re.findall(r"\b([A-Za-z_]\w*)\s*\(", body)):
                    if rn == sn:
                        continue
                    if (sm, rn) in export_index:
                        edges.append({"type": "call", "from_module": sm, "from_symbol": sn,
                                      "to_module": sm, "to_symbol": rn})
                    elif omnisearch.get(rn):
                        for (tm, tn) in omnisearch[rn]:
                            if (tm, tn) != (sm, sn):
                                edges.append({"type": "call", "from_module": sm, "from_symbol": sn,
                                              "to_module": tm, "to_symbol": tn})
            for ref in set(re.findall(r"`([A-Za-z_]\w*)(?:\(\))?`", s.get("doc", ""))):
                if (sm, ref) in export_index and ref != sn:
                    edges.append({"type": "proseRef", "from_module": sm, "from_symbol": sn,
                                  "to_module": sm, "to_symbol": ref})
                elif omnisearch.get(ref):
                    for (tm, tn) in omnisearch[ref]:
                        if (tm, tn) != (sm, sn):
                            edges.append({"type": "proseRef", "from_module": sm, "from_symbol": sn,
                                          "to_module": tm, "to_symbol": tn})

    edges.sort(key=lambda e: (e["type"], e.get("from_module", ""), e.get("from_symbol", ""),
                              e.get("to_module", ""), e.get("to_symbol", "")))
    return {"module_order": module_order, "files": files, "export_index": export_index,
            "omnisearch": omnisearch, "runtime_map": rt, "edges": edges}


# --------------------------------------------------------------------------
# rendering
# --------------------------------------------------------------------------


def slug(name):
    return "sym-" + re.sub(r"[^A-Za-z0-9_-]", "-", name).lower()


def prov(rev):
    return ("<!-- generated by tools/refman.py; do not edit -->\n"
            "<!-- source: stdlib/ ; runtime: runtime/vyb_runtime.c ; commit: %s -->\n" % rev)


def page(mod):
    return "%s.md" % mod


def symlink(g, mod, name, text=None, _from=None):
    # A same-module reference is an in-page anchor (`#sym-x`), not a jump to the
    # page again; only cross-module refs fan out to `other.md#sym-x`.
    if _from and _from == mod:
        return "[`%s`](#%s)" % (text or name, slug(name))
    return "[`%s`](%s#%s)" % (text or name, page(mod), slug(name))


def module_link(mod):
    return "[`%s`](%s)" % (mod, page(mod))


def file_slug(rel):
    return "file-" + re.sub(r"[^A-Za-z0-9_-]", "-", rel).lower()


REF_TYPES = {"import", "call", "usesType", "proseRef"}


def module_fans(g, mod):
    """Distinct external modules that reference `mod` (fan-in) and that `mod`
    references (fan-out), across all meaningfully-crossing edge types."""
    inm, outm = set(), set()
    for e in g["edges"]:
        if e["type"] not in REF_TYPES:
            continue
        fm, tm = e.get("from_module"), e.get("to_module")
        if not tm:
            continue
        if fm == mod and tm != mod:
            outm.add(tm)
        if tm == mod and fm and fm != mod:
            inm.add(fm)
    return inm, outm


def symbol_fan_in(g, mod, name):
    """Distinct external modules that reference exported symbol `name`."""
    mods = set()
    for e in g["edges"]:
        if e["type"] in REF_TYPES and e.get("to_module") == mod \
           and e.get("to_symbol") == name and e.get("from_module") not in (None, mod):
            mods.add(e["from_module"])
    return mods


def render_index(g):
    L = [prov(g["git_rev"]), "# Vyb Standard Library Reference", "",
         "Auto-generated. Each module page links every exported symbol and the "
         "edges between modules: imports, imports-by, implements/binds, uses-type, "
         "runtime calls, and prose references.", "",
         "Cross-indexes: [functions](functions.md) · [types](types.md) · "
         "[aspects & binds](aspects.md) · [shared types & consumers](interfaces.md) "
         "· [runtime intrinsics](runtime.md). Start with the "
         "[Programmer's Guide](PROGRAMMERS_GUIDE.md).", "",
         "## Modules", "",
         "| Module | Files | Exported | Imports | Fan-in | Fan-out | Edges |",
         "|---|---|---|---|---|---|---|"]
    bymod = {}
    for rel in g["files"]:
        bymod.setdefault(module_of(rel), []).append(rel)
    for m in g["module_order"]:
        files = bymod[m]
        exported = sum(1 for rel in files for s in g["files"][rel]["symbols"] if s.get("exported"))
        n_imp = sum(1 for e in g["edges"]
                    if e["type"] == "import" and e.get("from_module") == m)
        fin, fout = module_fans(g, m)
        n_edge = sum(1 for e in g["edges"] if e.get("from_module") == m or e.get("to_module") == m)
        L.append("| %s | %d | %d | %d | %d | %d | %d |" % (
            module_link(m), len(files), exported, n_imp, len(fin), len(fout), n_edge))
    L += ["", "## Exports by kind", ""]
    kinds = {}
    for rel in g["files"]:
        for s in g["files"][rel]["symbols"]:
            if s.get("exported"):
                kinds[s["kind"]] = kinds.get(s["kind"], 0) + 1
    for k in sorted(kinds):
        L.append("- `%s`: %d" % (k, kinds[k]))
    L += ["", "## Relationships", ""]
    counts = {}
    for e in g["edges"]:
        counts[e["type"]] = counts.get(e["type"], 0) + 1
    for t in sorted(counts):
        L.append("- `%s`: %d" % (t, counts[t]))
    L += ["", "See [`PLAN.md`](PLAN.md) for the generator design.", ""]
    return "\n".join(L).rstrip() + "\n"


def render_module(m, g):
    rels = sorted(r for r in g["files"] if module_of(r) == m)
    prim = g["files"][rels[0]]
    L = [prov(g["git_rev"]), "# Module `%s`" % m, ""]
    if prim["header"]:
        L += [prim["header"], ""]
    all_imports = []
    for rel in rels:
        for imp in g["files"][rel]["imports"]:
            names = imp["names"] or [""]
            for nm in names:
                all_imports.append((imp["target"], nm, imp["reexport"]))
    all_imports = sorted(set(all_imports))
    if all_imports:
        L += ["## Imports", ""]
        for tgt, nm, reex in all_imports:
            tm = tgt.split("::", 1)[0]
            if nm:
                link = symlink(g, tm, nm, _from=m) if (tm, nm) in g["export_index"] else "`%s`" % nm
                L.append("- %s `%s::%s`" % ("re-exports" if reex else "imports", tgt, link))
            else:
                L.append("- %s %s" % ("re-exports module" if reex else "imports module",
                                      module_link(tm)))
        L.append("")
    importers = {}
    for e in g["edges"]:
        if e["type"] == "import" and e["to_module"] == m and e["from_module"] != m:
            importers.setdefault(e["from_module"], set()).add(e["to_symbol"])
    if importers:
        L += ["## Imported by", ""]
        for fm in sorted(importers):
            L.append("- %s imports %s" % (module_link(fm),
                     ", ".join(symlink(g, m, s, _from=m) if s else "module"
                               for s in sorted(importers[fm]))))
        L.append("")
    fin, fout = module_fans(g, m)
    L += ["## Fan-in / Fan-out", ""]
    L.append("- **Referenced by** (%d): %s" % (
        len(fin), ", ".join(module_link(x) for x in sorted(fin)) if fin else "—"))
    L.append("- **References** (%d): %s" % (
        len(fout), ", ".join(module_link(x) for x in sorted(fout)) if fout else "—"))
    L.append("")

    # exports, grouped by kind
    export_syms = []
    for rel in rels:
        for s in g["files"][rel]["symbols"]:
            if s.get("exported"):
                export_syms.append((rel, s))
    multi = len(rels) > 1
    export_syms.sort(key=lambda t: (t[0], t[1]["kind"], t[1]["name"]))
    if multi:
        L += ["## Exported symbols", "", "| Symbol | File | Kind | Fan-in | Summary |",
              "|---|---|---|---|---|"]
    else:
        L += ["## Exported symbols", "", "| Symbol | Kind | Fan-in | Summary |",
              "|---|---|---|---|"]
    for rel, s in export_syms:
        one = doc_summary(s.get("doc", ""))
        fin_sym = symbol_fan_in(g, m, s["name"])
        fincell = "%d" % len(fin_sym) if fin_sym else ""
        fcell = "[`%s`](#%s)" % (rel, file_slug(rel)) if multi else ""
        if multi:
            L.append("| [`%s`](#%s) | %s | %s | %s | %s |" % (
                s["name"], slug(s["name"]), fcell, s["kind"], fincell, one))
        else:
            L.append("| [`%s`](#%s) | %s | %s | %s |" % (
                s["name"], slug(s["name"]), s["kind"], fincell, one))
    L.append("")

    # per-symbol detail
    prev_rel = None
    for rel, s in export_syms:
        if multi and rel != prev_rel:
            L.append('<a id="%s"></a>' % file_slug(rel))
            L.append("**File: `%s`**" % rel)
            hdr = g["files"][rel].get("header")
            if hdr:
                L.append("")
                L.append(hdr)
            L.append("")
            prev_rel = rel
        L.append('<a id="%s"></a>' % slug(s["name"]))
        L.append("### `%s` · %s" % (s["name"], s["kind"]))
        L.append("")
        if s["kind"] == "aspect":
            # Render the aspect as a complete, balanced declaration. The raw header
            # line carries an open brace with no closing one, so reconstruct the
            # full body from the parsed method signatures.
            L.append("```")
            L.append("aspect %s {" % s.get("name", ""))
            for name, params, ret in s.get("params") or []:
                args = ", ".join(p if not t else "%s<%s>" % (p, t) for p, t in params)
                sig = "%s(%s)" % (name, args)
                if ret:
                    sig += "<%s>" % ret
                L.append("    " + sig)
            L.append("}")
            L.append("```")
            L.append("")
        elif s["kind"] == "bind" and (s.get("aspect") or s.get("target")):
            L.append("`%s` implements aspect `%s` on type `%s`%s" % (
                s["name"], s.get("aspect"), s.get("target"),
                " (%s)" % s.get("tvar") if s.get("tvar") else ""))
            L.append("")
        elif s.get("signature"):
            L.append("```")
            L.append(s["signature"])
            L.append("```")
            L.append("")
        plain = []
        label = ""
        if s["kind"] in ("struct", "enum") and s.get("params"):
            label = "Fields:" if s["kind"] == "struct" else "Variants:"
            for item in s["params"]:
                plain.append("`%s<%s>`" % (item[1], item[2]) if item[0] == "field"
                             else "`%s = %s`" % (item[1], item[2]))
        elif s["kind"] == "bind" and s.get("params"):
            label = "Methods:"
            for item in s["params"]:
                name, params, ret = item
                args = ", ".join(p if not t else "%s<%s>" % (p, t) for p, t in params)
                plain.append("`%s(%s)%s`" % (name, args, "<%s>" % ret if ret else ""))
        if plain:
            L.append(label)
            L.append("")
            for item in plain:
                L.append("- " + item)
            L.append("")
        if s.get("doc"):
            L += [s["doc"], ""]
        rel_out = []
        for e in g["edges"]:
            if e["type"] in ("implement", "implementOn", "usesType", "call", "proseRef") \
               and e["from_module"] == m and e["from_symbol"] == s["name"]:
                if e["type"] == "runtimeRef":
                    ln = g["runtime_map"].get(e["to_symbol"])
                    rel_out.append("- calls runtime `%s`%s" % (
                        e["to_symbol"], " ([C:%d](runtime.md))" % ln if ln else ""))
                elif e["type"] == "usesType":
                    rel_out.append("- uses type %s" % symlink(g, e["to_module"], e["to_symbol"], _from=m))
                elif e["type"] == "call":
                    rel_out.append("- calls %s" % symlink(g, e["to_module"], e["to_symbol"], _from=m))
                elif e["type"] == "proseRef":
                    rel_out.append("- mentions %s" % symlink(g, e["to_module"], e["to_symbol"], _from=m))
                elif e["type"] == "implement":
                    rel_out.append("- implements aspect %s" % symlink(g, e["to_module"], e["to_symbol"], _from=m))
        fin_sym = symbol_fan_in(g, m, s["name"])
        if fin_sym:
            rel_out.append("- referenced by %d: %s" % (
                len(fin_sym), ", ".join(module_link(x) for x in sorted(fin_sym))))
        if rel_out:
            seen = set()
            uniq = []
            for o in rel_out:
                if o not in seen:
                    seen.add(o); uniq.append(o)
            L += ["**Relationships**", ""]
            L += uniq
            L.append("")
    return "\n".join(L).rstrip() + "\n"


def render_cross(g):
    docs = {}
    for category, title, kinds in (
            ("functions.md", "Functions", {"fn"}),
            ("types.md", "Types (struct / enum)", {"struct", "enum"}),
            ("aspects.md", "Aspects / binds", {"aspect", "bind"})):
        L = [prov(g["git_rev"]), "# %s" % title, ""]
        for m in g["module_order"]:
            names = []
            for rel in g["files"]:
                if module_of(rel) != m:
                    continue
                for s in g["files"][rel]["symbols"]:
                    if s.get("exported") and s["kind"] in kinds:
                        names.append((s["name"], symlink(g, m, s["name"], _from=m)))
            if names:
                names.sort()
                L.append("## `%s`" % m)
                L.append("")
                L.append(", ".join(nm for _, nm in names))
                L.append("")
        docs[category] = "\n".join(L).rstrip() + "\n"
    R = [prov(g["git_rev"]), "# Runtime intrinsics used by the stdlib", ""]
    used = sorted({e["to_symbol"] for e in g["edges"] if e["type"] == "runtimeRef"})
    R.append(", ".join("`%s`" % u for u in used))
    R.append("")
    docs["runtime.md"] = "\n".join(R).rstrip() + "\n"
    return docs


def shared_types(g):
    """Cross-module structs/enums (and how each other module consumes them)."""
    structs = {}
    for rel, fm in g["files"].items():
        for s in fm["symbols"]:
            if s.get("exported") and s["kind"] in ("struct", "enum"):
                structs[(module_of(rel), s["name"])] = s
    consumers = {}
    for e in g["edges"]:
        if e["type"] not in ("usesType", "import"):
            continue
        key = (e.get("to_module"), e.get("to_symbol"))
        fm, tm = e.get("from_module"), e.get("to_module")
        if not fm or not tm or fm == tm or key not in structs:
            continue
        rec = (fm, e.get("from_symbol") or "module", e["type"], e.get("reexport", False))
        consumers.setdefault(key, set()).add(rec)
    return structs, consumers


def render_interfaces(g):
    structs, consumers = shared_types(g)
    shared = {k: v for k, v in consumers.items()}
    L = [prov(g["git_rev"]), "# Shared types & consumers", "",
         "Cross-module structs and enums — types defined in one module but "
         "imported, built, or handled by another. For each shared type: who "
         "imports/re-exports it and which functions use it in a signature.", ""]
    if not shared:
        L.append("_No cross-module types._")
        return "\n".join(L).rstrip() + "\n"
    L += ["| Type | Module | Consumers | Consumer modules |", "|---|---|---|---|"]
    rows = []
    for (tm, name) in sorted(shared):
        cmods = sorted({c[0] for c in shared[(tm, name)]})
        rows.append("| [`%s`](%s#%s) | %s | %d | %s |" % (
            name, page(tm), slug(name), module_link(tm), len(cmods),
            ", ".join(module_link(c) for c in cmods)))
    L += rows
    L.append("")
    for (tm, name) in sorted(shared):
        s = structs[(tm, name)]
        L.append('<a id="%s"></a>' % slug("%s-%s" % (tm, name)))
        L.append("## `%s` · %s" % (name, s["kind"]))
        L.append("")
        summary = doc_summary(s.get("doc", ""))
        if summary:
            L.append(summary)
            L.append("")
        L.append("- **Defined in** %s" % module_link(tm))
        L.append("")
        bymod = {}
        for (fm, fsym, etype, reex) in sorted(shared[(tm, name)]):
            bymod.setdefault(fm, []).append((fsym, etype, reex))
        L.append("**Consumed by %d module(s)**" % len(bymod))
        L.append("")
        for fm in sorted(bymod):
            detail = []
            for (fsym, etype, reex) in sorted(bymod[fm], key=lambda t: (t[0], t[1])):
                if not fsym:
                    detail.append("imports%s" % (" (re-export)" if reex else ""))
                elif etype == "usesType":
                    detail.append("`%s` uses it" % fsym)
                else:
                    detail.append("`%s` imports it%s" % (fsym, " (re-export)" if reex else ""))
            L.append("- %s — %s" % (module_link(fm), ", ".join(detail)))
    L.append("")
    return "\n".join(L).rstrip() + "\n"


def json_nodes(g):
    """Machine-readable module/file/symbol nodes for graph.json."""
    bymod = {}
    for rel in g["files"]:
        bymod.setdefault(module_of(rel), []).append(rel)
    mods = []
    files = []
    syms = []
    for m in g["module_order"]:
        fin, fout = module_fans(g, m)
        rels = sorted(bymod[m])
        exported = sum(1 for rel in rels
                       for s in g["files"][rel]["symbols"] if s.get("exported"))
        mods.append({"name": m, "files": rels, "exported": exported,
                     "fan_in": len(fin), "fan_out": len(fout)})
        for rel in rels:
            names = [s["name"] for s in g["files"][rel]["symbols"] if s.get("exported")]
            files.append({"path": rel, "module": m, "symbols": names})
            for s in g["files"][rel]["symbols"]:
                if not s.get("exported"):
                    continue
                summary = doc_summary(s.get("doc", ""))
                syms.append({
                    "name": s["name"], "module": m, "file": rel, "kind": s["kind"],
                    "signature": s.get("signature", ""), "summary": summary,
                    "fan_in": len(symbol_fan_in(g, m, s["name"]))})
    mods.sort(key=lambda n: n["name"])
    files.sort(key=lambda n: n["path"])
    syms.sort(key=lambda n: (n["module"], n["name"]))
    return {"modules": mods, "files": files, "symbols": syms}


def emit(outdir: Path, g):
    outdir.mkdir(parents=True, exist_ok=True)
    outdir.joinpath("index.md").write_text(render_index(g))
    for m in g["module_order"]:
        outdir.joinpath(page(m)).write_text(render_module(m, g))
    for fname, content in render_cross(g).items():
        outdir.joinpath(fname).write_text(content)
    outdir.joinpath("interfaces.md").write_text(render_interfaces(g))
    outdir.joinpath("graph.json").write_text(json.dumps(
        {"git_rev": g["git_rev"], "modules": g["module_order"],
         "nodes": json_nodes(g), "edges": g["edges"]},
        indent=2))
    outdir.joinpath("PLAN.md").write_text((ROOT / "docs" / "refman" / "PLAN.md").read_text())


def git_rev():
    try:
        r = subprocess.run(["git", "rev-parse", "HEAD"], cwd=ROOT,
                           capture_output=True, text=True)
        return r.stdout.strip() or "unknown"
    except Exception:
        return "unknown"


def _norm_commit(text: str) -> str:
    """Normalize every embedded git rev so drift-checks ignore the current
    HEAD: the markdown provenance `commit: <rev>` lines and graph.json's
    `"git_rev": "<rev>"` field. A later commit must not false-positive a re-gen."""
    text = re.sub(r"commit: [0-9a-f]{4,40}", "commit: X", text)
    return re.sub(r'"git_rev":\s*"[0-9a-f]{4,40}"', '"git_rev": "X"', text)


def check_guide(emit_dir: Path) -> list:
    """Validate the hand-written PROGRAMMERS_GUIDE.md: every local markdown
    link resolves to a file, and code fences are balanced."""
    problems = []
    guide = emit_dir / "PROGRAMMERS_GUIDE.md"
    if not guide.exists():
        return problems
    txt = guide.read_text(encoding="utf-8")
    # balanced code fences
    infence = 0
    for ln in txt.splitlines():
        if ln.strip().startswith("```"):
            infence += 1
    if infence % 2:
        problems.append("PROGRAMMERS_GUIDE.md: unbalanced code fence")
    # local markdown links
    for m in re.finditer(r"\]\(([^)]+)\)", txt):
        target = m.group(1)
        if target.startswith(("http://", "https://", "mailto:")):
            continue
        filepart = target.split("#", 1)[0].strip()
        if not filepart:
            continue  # pure in-file anchor (heading links)
        p = (emit_dir / filepart) if not filepart.startswith("../") else (ROOT / filepart)
        if not p.exists():
            problems.append("PROGRAMMERS_GUIDE.md: broken link -> %s" % target)
    return problems


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--emit-dir", default=str(DEFAULT_OUT))
    ap.add_argument("--stdlib", default=str(DEFAULT_STDLIB))
    ap.add_argument("--runtime", default=str(DEFAULT_RUNTIME))
    ap.add_argument("--check", action="store_true")
    a = ap.parse_args()

    stdlib = Path(a.stdlib)
    files = {str(p.relative_to(ROOT)): parse_file(p.read_text()) for p in sorted(stdlib.rglob("*.vyb"))}
    g = build_graph(files, runtime_map(Path(a.runtime)))
    g["git_rev"] = git_rev()

    problems = []
    for e in g["edges"]:
        if e["type"] == "import" and not e.get("resolved"):
            problems.append("unresolved import %s::%s (by %s)" % (e["to_module"], e["to_symbol"], e["from_module"]))

    if a.check:
        with tempfile.TemporaryDirectory() as td:
            emit(Path(td), g)
            nd = Path(td)
            for rel in sorted(x.name for x in nd.glob("*")):
                tmp = (nd / rel).read_text()
                cur_dir = Path(a.emit_dir) / rel
                if not cur_dir.exists():
                    problems.append("missing: " + rel)
                elif _norm_commit(cur_dir.read_text()) != _norm_commit(tmp):
                    problems.append("drift: " + rel)
        problems += check_guide(Path(a.emit_dir))
        if problems:
            print("refman --check: %d problem(s)" % len(problems))
            for p in sorted(set(problems)):
                print("  - " + p)
            sys.exit(1)
        print("refman --check OK (%d files, %d modules, %d edges)"
              % (len(files), len(g["module_order"]), len(g["edges"])))
        return

    emit(Path(a.emit_dir), g)
    print("refman: wrote %d files to %s" % (len(list(Path(a.emit_dir).glob('*'))), a.emit_dir))


if __name__ == "__main__":
    main()
