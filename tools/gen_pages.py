#!/usr/bin/env python3
"""Generate the localized website pages (DE/FR/ES/RU) from data/indexEN.html.

Translations live in tools/lang_pages.json, keyed by the exact English text
node found in data/indexEN.html. Run from the repository root:

    python3 tools/gen_pages.py

The EN page is the source of truth for markup and JS; only visible text nodes
are swapped. EN itself is intentionally not generated.
"""
import json
import os
import re

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
EN_PATH = os.path.join(ROOT, 'data', 'indexEN.html')
DICT_PATH = os.path.join(ROOT, 'tools', 'lang_pages.json')

LANGS = [
    ("de", "de", "DE"),
    ("fr", "fr", "FR"),
    ("es", "es", "ES"),
    ("ru", "ru", "RU"),
]

LANG_SWITCH = [("PL", "/index.html"), ("EN", "/indexEN.html"), ("DE", "/indexDE.html"),
               ("FR", "/indexFR.html"), ("ES", "/indexES.html"), ("RU", "/indexRU.html")]


def lang_switch(active):
    out = ['        <div class="lang-switch">']
    for code, href in LANG_SWITCH:
        cls = "lang-link active" if code == active else "lang-link"
        out.append('            <a class="%s" href="%s">%s</a>' % (cls, href, code))
    out.append('        </div>')
    return "\n".join(out)


_SCRIPT_STR = re.compile(r"([\"'])([^\"'\\\n]{6,})\1")


def translate_scripts(html, tr):
    """Swap exact string literals inside <script> blocks (UI status messages)."""
    def repl(m):
        quote = m.group(1)
        val = tr.get(m.group(2), m.group(2))
        val = val.replace('\\', '\\\\')
        val = val.replace(quote, '\\' + quote)
        return quote + val + quote

    def do_block(block):
        return _SCRIPT_STR.sub(repl, block)

    return re.sub(r'<script.*?</script>', lambda m: do_block(m.group(0)), html, flags=re.S)


def translate(html, tr):
    html = translate_scripts(html, tr)
    # Emoji-prefixed labels can sit inside JS string literals (e.g. the button
    # markup) where the generic literal pass cannot match; swap them verbatim.
    # Keys that start with whitespace are NOT emoji labels and must never be
    # swapped as substrings (they would corrupt surrounding sentences) - they
    # are handled only by the exact-literal pass above.
    for key, val in tr.items():
        if key and not key[0].isalnum() and not key[0].isspace():
            html = html.replace(key, val)
    # JS template literals (backticks) are not matched by the quoted-literal pass;
    # swap them only as whole literals (keys include the ${...} placeholders) so a
    # partial match can never corrupt an interpolation expression.
    html = re.sub(r'`([^`]*)`', lambda m: '`' + tr.get(m.group(1), m.group(1)) + '`', html)
    parts = re.split(r'(<script.*?</script>|<style.*?</style>)', html, flags=re.S)
    out = []
    for part in parts:
        if part.startswith('<script') or part.startswith('<style'):
            out.append(part)
            continue

        def repl(m):
            raw = m.group(1)
            key = raw.strip()
            if key in tr:
                lead = raw[:len(raw) - len(raw.lstrip())]
                trail = raw[len(raw.rstrip()):]
                return '>' + lead + tr[key] + trail + '<'
            return m.group(0)

        out.append(re.sub(r'>([^<>]+)<', repl, part))
    return "".join(out)


def build(en_html, tr, code, lang_attr, active):
    html = re.sub(r'        <div class="lang-switch">.*?</div>\n',
                  lang_switch(active) + "\n", en_html, count=1, flags=re.S)
    html = re.sub(r'<html lang="[a-zA-Z-]+"', '<html lang="%s"' % lang_attr, html, count=1)
    # Each language loads its own localized manual from LittleFS.
    html = html.replace('const manualUrl = "/manual.txt";',
                        'const manualUrl = "/manual%s.txt";' % active)
    html = translate(html, tr)
    path = os.path.join(ROOT, 'data', 'index%s.html' % active)
    tmp = path + '.tmp'
    open(tmp, 'w', encoding='utf-8').write(html)
    os.replace(tmp, path)
    return html


def untranslated(html, tr):
    vals = set(tr.values())
    parts = re.split(r'(<script.*?</script>|<style.*?</style>)', html, flags=re.S)
    left = set()
    for part in parts:
        if part.startswith('<script') or part.startswith('<style'):
            continue
        for m in re.finditer(r'>([^<>]+)<', part):
            k = m.group(1).strip()
            if len(k) > 18 and k not in vals and not re.match(r'^[\d\s.,:;%°×()\-/]+$', k):
                left.add(k)
    return left


def main():
    en_html = open(EN_PATH, encoding='utf-8').read()
    dictionaries = json.load(open(DICT_PATH, encoding='utf-8'))
    for code, lang_attr, active in LANGS:
        html = build(en_html, dictionaries[code], code, lang_attr, active)
        left = untranslated(html, dictionaries[code])
        print("built data/index%s.html  untranslated-long-nodes: %d" % (active, len(left)))
        for u in sorted(left):
            print("    ?", u[:110])


if __name__ == '__main__':
    main()
