#!/usr/bin/env python3
"""Report daily Kosmos website page hits from the public Apache access logs."""

import argparse
import collections
import datetime
import json
import os
from pathlib import Path
import runpy
import subprocess
import tempfile
from urllib.parse import unquote, urlsplit

download_report = runpy.run_path(str(Path(__file__).with_name("download-report.py")))
LINE = download_report["LINE"]
is_bot = download_report["is_bot"]


def page_path(target):
    path = unquote(urlsplit(target).path)
    if path == "/kosmos":
        path = "/kosmos/"
    if not path.startswith("/kosmos/") or any(p in (".", "..") for p in path.split("/")):
        return None
    if path.endswith("/"):
        path += "index.html"
    if not path.lower().endswith((".html", ".htm", ".php")):
        return None
    return path


def parse(lines):
    days = collections.defaultdict(lambda: {"human": collections.Counter(), "bot": collections.Counter()})
    log_days = set()
    for line in lines:
        match = LINE.match(line)
        if not match:
            continue
        day = datetime.datetime.strptime(match.group(2), "%d/%b/%Y").date().isoformat()
        log_days.add(day)
        page = page_path(match.group(3))
        if page is not None and match.group(4) in ("200", "304"):
            days[day]["bot" if is_bot(match.group(6)) else "human"][page] += 1
    return days, log_days


def fetch(host):
    command = (
        'set -e; date "+@@@ TODAY %F"; '
        'for f in /var/log/apache2/access.log*; do '
        '[ -f "$f" ] || continue; echo "@@@ LOG"; '
        'case "$f" in *.gz) gzip -cd -- "$f";; *) cat -- "$f";; esac; done'
    )
    result = subprocess.run(["ssh", "-o", "BatchMode=yes", host, command],
                            capture_output=True, text=True, check=True)
    lines = result.stdout.splitlines()
    if not lines or not lines[0].startswith("@@@ TODAY ") or "@@@ LOG" not in lines:
        raise ValueError("server did not return its date and access logs")
    return datetime.date.fromisoformat(lines[0][len("@@@ TODAY "):]), lines[1:]


def atomic_write(path, text):
    path = Path(path).expanduser()
    path.parent.mkdir(parents=True, exist_ok=True)
    name = None
    try:
        with tempfile.NamedTemporaryFile(mode="w", encoding="utf-8", dir=path.parent, delete=False) as out:
            name = out.name
            out.write(text)
        os.chmod(name, 0o600)
        os.replace(name, path)
    finally:
        if name and os.path.exists(name):
            os.unlink(name)


def merge(state, found, log_days, today):
    if not log_days:
        raise ValueError("no dated GET requests found in server access logs")
    first = datetime.date.fromisoformat(min(log_days))
    day = first
    while day < today:
        key = day.isoformat()
        # The oldest rotated log may start partway through its first day.
        if day != first or key not in state:
            counts = found.get(key, {"human": {}, "bot": {}})
            state[key] = {kind: dict(counts[kind]) for kind in ("human", "bot")}
        day += datetime.timedelta(days=1)
    return state


def render(state, host, today):
    out = [
        f"Kosmos website page report, generated {today.isoformat()} from {host}",
        "Completed days in the web server's timezone; successful GETs (HTTP 200 or 304).",
        "Directory paths count as index.html. Static assets, downloads and failed requests are excluded.",
        "Likely bots are classified by user agent; counts are hits, not unique visitors.",
        "",
    ]
    for day in sorted(state, reverse=True):
        counts = state[day]
        human = counts["human"]
        bot = counts["bot"]
        paths = sorted(set(human) | set(bot), key=lambda p: (-human.get(p, 0), -bot.get(p, 0), p))
        out.append(f"=== {day}  people: {sum(human.values())}  bots: {sum(bot.values())} ===")
        if not paths:
            out.append("No page hits.")
        else:
            width = max(len(p) for p in paths)
            out.append(f'{"page":<{width}}  {"people":>8}  {"bots":>8}')
            for path in paths:
                out.append(f"{path:<{width}}  {human.get(path, 0):>8}  {bot.get(path, 0):>8}")
        out.append("")
    return "\n".join(out) + "\n"


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--host", default="dave@gemini")
    parser.add_argument("--out", default="~/kosmos-page-report.txt")
    parser.add_argument("--state", default="~/.local/share/kosmos/page-days.json")
    args = parser.parse_args()
    try:
        today, lines = fetch(args.host)
        found, log_days = parse(lines)
        state_path = Path(args.state).expanduser()
        state = json.loads(state_path.read_text(encoding="utf-8")) if state_path.exists() else {}
        merge(state, found, log_days, today)
        atomic_write(state_path, json.dumps(state, indent=1, sort_keys=True) + "\n")
        atomic_write(args.out, render(state, args.host, today))
    except (OSError, ValueError, subprocess.CalledProcessError) as error:
        detail = error.stderr.strip() if isinstance(error, subprocess.CalledProcessError) and error.stderr else str(error)
        parser.exit(1, f"page-report: {detail}\n")
    print(f"wrote {Path(args.out).expanduser()}")


if __name__ == "__main__":
    main()
