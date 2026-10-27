#!/usr/bin/env python3
"""Validate the offline catalog, source keys and untranslated UI entry points."""
import collections
import ast
import json
import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parents[1]
LANGUAGES = {"en", "zh", "es", "fr", "pt", "de", "ja"}
errors = []


def unique(pairs):
    result = {}
    for key, value in pairs:
        if key in result:
            errors.append(f"Duplicate catalog key: {key!r}")
        result[key] = value
    return result


catalog = json.loads((ROOT / "romfs/i18n.json").read_text(encoding="utf-8"),
                     object_pairs_hook=unique)
placeholder = re.compile(r"\{\d+\}")
literal = r'"(?:[^"\\]|\\.)*"'
used = set()
for key, values in catalog.items():
    if set(values) != LANGUAGES:
        errors.append(f"{key!r}: language set is {sorted(values)}")
    expected = collections.Counter(placeholder.findall(values["en"]))
    for code, value in values.items():
        if not isinstance(value, str) or not value.strip():
            errors.append(f"{key!r}: empty {code} translation")
        elif collections.Counter(placeholder.findall(value)) != expected:
            errors.append(f"{key!r}: inconsistent {code} placeholders")

for path in (ROOT / "source").rglob("*.cpp"):
    source = path.read_text(encoding="utf-8")
    for match in re.finditer(r'i18n::(?:c|t)\(\s*(' + literal + ')', source):
        used.add(json.loads(match[1]))
    if path.parent.name == "app":
        # These direct entry points must receive translated static messages.
        for match in re.finditer(
                r'(?:ui::(?:message|drawTopChrome)|dialog\.(?:header|title)\s*=)'
                r'\s*\(?\s*(' + literal + ')', source):
            value = json.loads(match[1])
            if re.search(r"[A-Za-z]{2}", value) and value != "SHA-256":
                line = source.count("\n", 0, match.start()) + 1
                errors.append(f"{path.relative_to(ROOT)}:{line}: untranslated {value!r}")

web = (ROOT / "source/core/remote_page.cpp").read_text(encoding="utf-8")
keys = re.search(r'const char \*keys\[\] = \{(.*?)\n    \};', web, re.S)
if not keys:
    errors.append("Missing Receive web dictionary")
else:
    web_keys = {json.loads(s) for s in re.findall(literal, keys[1])}
    used.update(web_keys)
    script = web.split("<script>\n", 1)[1].split("</script>", 1)[0]
    js_literal = r'''(?:"(?:[^"\\]|\\.)*"|'(?:[^'\\]|\\.)*')'''
    called = {ast.literal_eval(m[1]) for m in re.finditer(
        r'\b(?:tr|heading|waiting)\(\s*(' + js_literal + ')', script)}
    # Restrict conditional keys to the first argument of tr(). A later
    # conditional may choose HTML attributes or other non-message values.
    for call in re.finditer(r'\btr\(', script):
        start = call.end()
        depth = 0
        quote = None
        escaped = False
        end = start
        for end in range(start, len(script)):
            char = script[end]
            if quote:
                if escaped:
                    escaped = False
                elif char == "\\":
                    escaped = True
                elif char == quote:
                    quote = None
            elif char in "\"'":
                quote = char
            elif char in "([":
                depth += 1
            elif char in ")]":
                if depth == 0:
                    break
                depth -= 1
            elif char == "," and depth == 0:
                break
        conditional = re.search(r'\?\s*(' + js_literal + r')\s*:\s*(' +
                                js_literal + r')\s*$', script[start:end])
        if conditional:
            called.update(ast.literal_eval(s) for s in conditional.groups())
    for key in sorted(called - web_keys):
        errors.append(f"Receive dictionary missing JavaScript message: {key!r}")
used.update(json.loads(m[1]) for m in re.finditer(
    r'\{"@L\d+@",\s*(' + literal + r')\}', web))
for word in ("entry", "file", "folder", "item", "backup", "link", "CIA",
             "install", "ticket", "title", "seed", "existing file",
             "unused ticket", "deleted ticket", "installed game",
             "unfinished install", "Marked Item"):
    used.add("{0} " + word)
    used.add("{0} " + ("entries" if word == "entry" else word + "s"))
for key in sorted(used - catalog.keys()):
    errors.append(f"Source key missing from catalog: {key!r}")

if errors:
    print("\n".join(errors), file=sys.stderr)
    sys.exit(1)
print(f"i18n: {len(catalog)} messages, {len(LANGUAGES)} complete languages; "
      f"{len(used)} source keys checked")
