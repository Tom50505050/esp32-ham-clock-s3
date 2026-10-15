#!/usr/bin/env python3
"""Eksportuje dziennik zdarzen OpenHands do czytelnego pliku Markdown.

Uzycie:
    python3 tools/export_conversation.py <katalog_konwersacji> <plik_wyjsciowy.md>

Celowo NIE czyta base_state.json (zawiera klucz API / sekrety) - korzysta
wylacznie z katalogu events/. Dlugie wyniki narzedzi sa przycinane.
"""
import glob
import json
import os
import re
import sys

MAX_TEXT = 4000     # maks. dlugosc tekstu wiadomosci
MAX_TOOL = 3000     # maks. dlugosc komendy/argumentow narzedzia
MAX_OUT = 3000      # maks. dlugosc wyniku narzedzia

# Wzorce sekretow do zredagowania w zapisie (klucze API, tokeny).
# Bez tego pelny log zawiera zywe klucze z base_state.json / odpowiedzi API.
_SECRET_PATTERNS = [
    re.compile(r"gAAAAA[A-Za-z0-9_\-=]{4,}"),           # token Fernet (takze fragmenty)
    re.compile(r"ghp_[A-Za-z0-9]{20,}"),                 # GitHub PAT (classic)
    re.compile(r"github_pat_[A-Za-z0-9_]{20,}"),         # GitHub PAT (fine-grained)
    re.compile(r"sk-[A-Za-z0-9]{20,}"),                  # klucze OpenAI-style
    re.compile(r"(?i)(api[_-]?key\"?\s*[:=]\s*\"?)[^\",\s]{16,}"),
    re.compile(r"(?i)(bearer\s+)[A-Za-z0-9._\-]{16,}"),
]


def redact(s):
    s = "" if s is None else str(s)
    for pat in _SECRET_PATTERNS:
        s = pat.sub(lambda m: (m.group(1) if m.lastindex else "") + "<secret-hidden>", s)
    return s


def clip(s, n):
    s = redact(s)
    if len(s) > n:
        return s[:n] + f"\n... [obcieto, lacznie {len(s)} znakow]"
    return s


def text_of(content):
    parts = []
    if isinstance(content, list):
        for c in content:
            if isinstance(c, dict) and c.get("type") == "text":
                parts.append(c.get("text", ""))
            elif isinstance(c, str):
                parts.append(c)
    elif isinstance(content, str):
        parts.append(content)
    return "\n".join(p for p in parts if p)


def fmt_action(action, tool_name):
    if not isinstance(action, dict):
        return clip(action, MAX_TOOL)
    for key in ("command", "url", "path", "query"):
        if action.get(key):
            return clip(action[key], MAX_TOOL)
    return clip(json.dumps(action, ensure_ascii=False), MAX_TOOL)


def fmt_observation(obs):
    if isinstance(obs, dict):
        if "content" in obs:
            return clip(text_of(obs["content"]), MAX_OUT)
        for key in ("text", "output", "message"):
            if obs.get(key):
                return clip(obs[key], MAX_OUT)
    return clip(obs, MAX_OUT)


def main():
    if len(sys.argv) < 3:
        print(__doc__)
        sys.exit(1)
    conv_dir, out_path = sys.argv[1], sys.argv[2]
    only_dialog = "--dialog" in sys.argv  # pomija komendy/wyniki narzedzi
    ev_dir = os.path.join(conv_dir, "events")
    files = sorted(glob.glob(os.path.join(ev_dir, "event-*.json")))

    lines = ["# Zapis rozmowy", "",
             f"- Katalog: `{os.path.basename(conv_dir.rstrip('/'))}`",
             f"- Zdarzen: {len(files)}",
             f"- Tryb: {'tylko dialog' if only_dialog else 'pelny'}",
             "", "---", ""]

    n = 0
    for fn in files:
        try:
            d = json.load(open(fn, encoding="utf-8"))
        except Exception:
            continue
        kind = d.get("kind")
        ts = d.get("timestamp", "")
        if kind == "MessageEvent":
            role = d.get("llm_message", {}).get("role", "?")
            who = {"user": "UZYTKOWNIK", "assistant": "AGENT",
                   "system": "SYSTEM", "tool": "NARZEDZIE"}.get(role, role.upper())
            body = text_of(d.get("llm_message", {}).get("content"))
            if not body.strip():
                continue
            n += 1
            lines += [f"## [{n}] {who} — {ts}", "", clip(body, MAX_TEXT), ""]
        elif kind == "ActionEvent":
            if only_dialog:
                continue
            tool = d.get("tool_name", "?")
            n += 1
            lines += [f"## [{n}] 🔧 {tool} — {ts}", "",
                      "```", fmt_action(d.get("action"), tool), "```", ""]
        elif kind == "ObservationEvent":
            if only_dialog:
                continue
            n += 1
            lines += [f"### [{n}] wynik ({d.get('tool_name', '?')}) — {ts}", "",
                      "```", fmt_observation(d.get("observation")), "```", ""]
        elif kind == "AgentErrorEvent":
            n += 1
            lines += [f"## [{n}] BLAD AGENTA — {ts}", "",
                      clip(d.get("error") or d, MAX_TOOL), ""]
        elif kind == "ConversationErrorEvent":
            n += 1
            lines += [f"## [{n}] BLAD KONWERSACJI — {ts}", "",
                      clip(d.get("detail") or d.get("code") or d, MAX_TOOL), ""]

    os.makedirs(os.path.dirname(os.path.abspath(out_path)), exist_ok=True)
    with open(out_path, "w", encoding="utf-8") as f:
        f.write("\n".join(lines))
    print(f"Zapisano {out_path} ({os.path.getsize(out_path)} B, wpisow: {n})")


if __name__ == "__main__":
    main()
