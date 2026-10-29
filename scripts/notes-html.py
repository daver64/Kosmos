#!/usr/bin/env python3
"""Renders RELEASE_NOTES.md as a site page: notes-html.py IN.md OUT.html (needs the markdown module)."""
import sys, markdown

body = markdown.markdown(open(sys.argv[1], encoding="utf-8").read())
page = """<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>Kosmos release notes</title>
<link rel="stylesheet" href="css/site.css?v=7">
</head>
<body>
<header class="topbar">
  <a class="brand" href="index.html">KOS<span>M</span>OS</a>
  <nav>
    <a href="index.html">Home</a>
    <a href="gallery.html">Gallery</a>
    <a href="manual.html">Flight manual</a>
    <a href="modding.html">Modders manual</a>
    <a href="sdk.html">SDK manual</a>
    <a href="developers.html">Developers manual</a>
    <a href="download.html">Download</a>
    <a href="kav/index.html">KosmosAV</a>
  </nav>
</header>
<main class="wrap" style="max-width:860px;margin:40px auto;padding:0 24px">
<h1>Release notes</h1>
%s
</main>
</body>
</html>
""" % body
open(sys.argv[2], "w", encoding="utf-8").write(page)
