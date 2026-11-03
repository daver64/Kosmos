#!/usr/bin/env python3
"""Counts downloads of the Kosmos binaries and arcade games from the public web server's Apache logs.

Usage: download-report.py [--host dave@gemini] [--out ~/salyut.report.txt]

Reads /var/log/apache2/access.log* over ssh (the account must be in the adm group), counts complete GETs
(status 200) of files under /<project>/download/ and writes a text report on this machine, outside any web root.
Apache keeps only about two weeks of logs, so the per-day counts are merged into a state file
(~/.local/share/kosmos/download-days.json). Re-running is safe: days found in the logs replace what was stored.
"""
import argparse
import collections
import datetime
import gzip
import json
import os
import re
import subprocess
import sys

LINE = re.compile(r'^(\S+) \S+ \S+ \[(\d+/\w+/\d+):[^\]]+\] "GET (\S+) [^"]*" (\d+) (\S+)(?: "[^"]*" "([^"]*)")?')
BOT = re.compile(r"bot|crawl|spider|slurp|curl|wget|python|go-http|java/|libwww|scrapy|headless|monitor|fetch|axios|okhttp|httpclient|"
                 r"preview|odin|probe|scan|check|archive|facebookexternalhit|aria2|node-|\bgo \d|msh-pdfmin", re.I)
# Scrapers often forge decades-old browser strings; real visitors do not send these.
OLD_BROWSER = re.compile(r"Opera/\d\.|MSIE|Firefox/([1-9]|[1-3]\d)\.|Chrome/([1-9]|[1-5]\d)\.|Android [1-4]\.|Mac OS X 10[_.][0-6][_.\d]|iPhone OS (\d|1[0-2])_|Safari/5[0-3]\d")


def is_bot(agent):
    return not agent or agent == "-" or bool(BOT.search(agent) or OLD_BROWSER.search(agent))
ARCHIVE = re.compile(r'\.(tar\.gz|zip|exe|AppImage|deb|msi|7z)$')
PATH = re.compile(r'^/([\w-]+)/download/([^/]+)$')


def fetch_logs(host):
    script = "for f in /var/log/apache2/access.log*; do echo \"@@@ $f\"; case $f in *.gz) zcat \"$f\";; *) cat \"$f\";; esac; done"
    out = subprocess.run(["ssh", "-o", "BatchMode=yes", host, script], capture_output=True, check=True).stdout
    return out.decode("utf-8", "replace").splitlines()


def parse(lines):
    days = collections.defaultdict(lambda: {"human": collections.defaultdict(lambda: {"n": 0, "ips": set()}),
                                            "bot": collections.defaultdict(lambda: {"n": 0, "ips": set()})})
    for line in lines:
        m = LINE.match(line)
        if not m or m.group(4) != "200":
            continue
        ip, date, path = m.group(1), m.group(2), m.group(3).split("?")[0]
        pm = PATH.match(path)
        if not pm or not ARCHIVE.search(pm.group(2)):
            continue
        day = datetime.datetime.strptime(date, "%d/%b/%Y").date().isoformat()
        entry = days[day]["bot" if is_bot(m.group(6)) else "human"][path]
        entry["n"] += 1
        entry["ips"].add(ip)
    return days


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--host", default="dave@gemini")
    ap.add_argument("--out", default=os.path.expanduser("~/salyut.report.txt"))
    ap.add_argument("--state", default=os.path.expanduser("~/.local/share/kosmos/download-days.json"))
    a = ap.parse_args()

    try:
        lines = fetch_logs(a.host)
    except subprocess.CalledProcessError as e:
        sys.exit("could not read the logs on %s: %s" % (a.host, e.stderr.decode().strip()))
    if not any(l.startswith("@@@") for l in lines):
        sys.exit("no access logs found on " + a.host)
    first_day = None
    for l in lines:
        m = LINE.match(l)
        if m:
            d = datetime.datetime.strptime(m.group(2), "%d/%b/%Y").date().isoformat()
            first_day = d if first_day is None or d < first_day else first_day
    found = parse(lines)

    state = {}
    if os.path.exists(a.state):
        with open(a.state) as f:
            state = json.load(f)
    # Every day the logs cover is rewritten, including days with no downloads.
    today = datetime.date.today().isoformat()
    d = datetime.date.fromisoformat(first_day or today)
    while d.isoformat() <= today:
        kinds = found.get(d.isoformat(), {"human": {}, "bot": {}})
        state[d.isoformat()] = {k: {p: {"n": e["n"], "ips": sorted(e["ips"])} for p, e in kinds[k].items()} for k in ("human", "bot")}
        d += datetime.timedelta(days=1)
    os.makedirs(os.path.dirname(a.state), exist_ok=True)
    with open(a.state, "w") as f:
        json.dump(state, f, indent=1, sort_keys=True)

    now = datetime.date.today()
    tables = {}
    for kind in ("human", "bot"):
        rows = collections.defaultdict(lambda: {"total": 0, "d7": 0, "d30": 0, "ips": set(), "last": ""})
        for day, entry in sorted(state.items()):
            # State saved by the first version of this script has no human/bot split; those days are rewritten while the logs still cover them.
            files = entry.get(kind, {} if "human" in entry or "bot" in entry else (entry if kind == "human" else {}))
            age = (now - datetime.date.fromisoformat(day)).days
            for path, e in files.items():
                r = rows[path]
                r["total"] += e["n"]
                r["d7"] += e["n"] if age < 7 else 0
                r["d30"] += e["n"] if age < 30 else 0
                r["ips"].update(e["ips"])
                r["last"] = max(r["last"], day)
        tables[kind] = rows

    out = ["Download report, generated %s from %s" % (datetime.datetime.now().strftime("%Y-%m-%d %H:%M"), a.host),
           "Counted: complete GETs (HTTP 200) of archives under /<project>/download/. Logs on the server go back to %s; older days come from the saved state." % (first_day or "n/a"),
           "Recorded days: %s to %s" % (min(state) if state else "n/a", max(state) if state else "n/a"),
           "Bots are requests whose user agent is empty or looks automated (crawlers, curl, wget, scripts, link previews); they are listed separately.", ""]
    for kind, title in (("human", "DOWNLOADS (people)"), ("bot", "BOT AND AUTOMATED DOWNLOADS (not counted above)")):
        rows = tables[kind]
        out.append("=" * 8 + " " + title + " " + "=" * 8)
        out.append("")
        by_project = collections.defaultdict(list)
        for path in rows:
            by_project[PATH.match(path).group(1)].append(path)
        width = max([len(p) for p in rows] + [10])
        fmt = "%-" + str(width) + "s %7s %7s %8s %7s  %s"
        grand = 0
        for project in sorted(by_project):
            out.append("[%s]" % project)
            out.append(fmt % ("file", "total", "last 7d", "last 30d", "unique", "last download"))
            for path in sorted(by_project[project], key=lambda p: -rows[p]["total"]):
                r = rows[path]
                out.append(fmt % (path, r["total"], r["d7"], r["d30"], len(r["ips"]), r["last"]))
            sub_total = sum(rows[p]["total"] for p in by_project[project])
            grand += sub_total
            out.append("  project total: %d" % sub_total)
            out.append("")
        out.append("All %s downloads: %d" % ("people's" if kind == "human" else "bot", grand))
        out.append("")
    with open(a.out, "w") as f:
        f.write("\n".join(out) + "\n")
    os.chmod(a.out, 0o600)
    print("wrote", a.out)


if __name__ == "__main__":
    main()
