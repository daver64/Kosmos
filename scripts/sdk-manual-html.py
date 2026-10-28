#!/usr/bin/env python3
"""Turns sdk/docs/MANUAL.md into sdk.html on the web site, in the style of modding.html.

Usage: sdk-manual-html.py MANUAL.md WEB_DIR   (WEB_DIR holds modding.html; sdk.html is written beside it)
"""
import re
import sys

import markdown

source, web = sys.argv[1], sys.argv[2]
body = markdown.markdown(open(source).read(), extensions=["fenced_code", "tables"])
body = re.sub(r"<h1>.*?</h1>", "", body, count=1)


def slug(text):
    return re.sub(r"[^a-z0-9]+", "-", re.sub(r"<[^>]+>", "", text).lower()).strip("-")


headings = []


def add_id(m):
    level, text = m.group(1), m.group(2)
    ident = slug(text)
    if level == "2":
        headings.append((ident, text))
    return '<h%s id="%s">%s</h%s>' % (level, ident, text, level)


body = re.sub(r"<h([23])>(.*?)</h\1>", add_id, body)
toc = "".join('  <a href="#%s">%s</a>\n' % (i, t) for i, t in headings)

page = open(web + "/modding.html").read()
head = page[:page.index('<div class="doc">')]
head = head.replace('class="active" href="modding.html"', 'href="modding.html"')
head = head.replace("Modders Manual — KOSMOS", "Kosmos SDK Manual — KOSMOS")
foot = page[page.index("<footer>"):]
out = head + '''<div class="doc">
<aside class="toc">
  <h4>Native addons</h4>
''' + toc + '''  <h4>See also</h4>
  <a href="modding.html">Modders manual</a>
  <a href="download.html#sdk">Download the SDK</a>
</aside>

<article class="manual">
<h1>Kosmos SDK Manual</h1>
<div class="byline">Write native addons: separate programs that talk to Kosmos over KosmosLink. An extension of the <a href="modding.html">Modders manual</a>; most addons do not need it. <a href="download.html#sdk">Download the SDK</a>.</div>
''' + body + '''
</article>
</div>

''' + foot
open(web + "/sdk.html", "w").write(out)
print("wrote", web + "/sdk.html")
