#!/usr/bin/env python3
"""
Build multi-page Sphinx docs from the workshop README.

Sections are grouped into pages:
  index        — title, gif, What is GIZMO?, toctree
  setup        — Setting up your GIZMO stack / Getting the code / Building GIZMO
  running      — Running test problems / Setting up an ISM cloud / Running the simulation
  visualization — Visualizing the output
  experiments  — Baseline experiments / Further Explorations
"""

import os
import re
import shutil

REPO = os.path.dirname(os.path.abspath(__file__))
WORKSHOP = os.path.normpath(os.path.join(REPO, "..", ".."))
DOCS = os.path.join(REPO, "docs")

os.makedirs(DOCS, exist_ok=True)

# --- sync images ---
src_images = os.path.join(WORKSHOP, "images")
dst_images = os.path.join(DOCS, "images")
if os.path.exists(dst_images):
    shutil.rmtree(dst_images)
shutil.copytree(src_images, dst_images)

# --- parse README into sections ---
readme = open(os.path.join(WORKSHOP, "README.md")).read()

# Split on ## headings; keep the heading line with each block
blocks = re.split(r'(?=^## )', readme, flags=re.MULTILINE)
# blocks[0] is everything before the first ##
preamble = blocks[0]

sections = {}  # heading text -> full block text
for block in blocks[1:]:
    title = re.match(r'^## (.+)', block).group(1).strip()
    sections[title] = block

# --- page definitions ---
PAGES = {
    "setup": [
        "Setting up your GIZMO stack",
    ],
    "running": [
        "Running test problems",
        "Setting up an ISM cloud",
    ],
    "visualization": [
        "Visualization",
    ],
    "experiments": [
        "Baseline experiments",
        "Further Explorations",
    ],
}

PAGE_TITLES = {
    "setup": "Setup",
    "running": "Running Simulations",
    "visualization": "Visualization",
    "experiments": "Experiments",
}

# --- write content pages ---
# Multi-section pages get a single H1 page title; ## section headings stay put.
# Single-section pages promote ## → # directly (no redundant double title).
for slug, heading_list in PAGES.items():
    missing = [h for h in heading_list if h not in sections]
    if missing:
        print(f"WARNING: {slug}.md — section(s) not found in README: {missing}")
    found = [h for h in heading_list if h in sections]
    content = "\n".join(sections[h] for h in found)
    if len(found) == 1:
        # promote all headings one level so ## → # (page title) and ### → ## etc.
        content = re.sub(r'^(#{2,})', lambda m: m.group(1)[1:], content, flags=re.MULTILINE)
    else:
        content = f"# {PAGE_TITLES[slug]}\n\n" + content
    with open(os.path.join(DOCS, f"{slug}.md"), "w") as f:
        f.write(content)

# --- write index ---
toctree = """\
```{toctree}
:maxdepth: 2
:caption: Contents

setup
running
visualization
experiments
```
"""
# preamble has the title + gif + "What is GIZMO?" block
index_content = preamble + sections.get("What is GIZMO?", "") + "\n\n" + toctree
with open(os.path.join(DOCS, "index.md"), "w") as f:
    f.write(index_content)

pages_written = ["index"] + list(PAGES.keys())
print("Done —", ", ".join(f"docs/{p}.md" for p in pages_written))
