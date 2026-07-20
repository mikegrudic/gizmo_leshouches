#!/usr/bin/env python3
"""
Set up a Sphinx/MyST-Parser docs directory inside gizmo_leshouches/
from the workshop README.md that lives one level up.
"""

import os
import shutil

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.join(HERE, "wind", "gizmo_leshouches")
DOCS = os.path.join(REPO, "docs")

os.makedirs(DOCS, exist_ok=True)

# Copy workshop README -> docs/index.md
shutil.copy(os.path.join(HERE, "README.md"), os.path.join(DOCS, "index.md"))

# Copy images alongside it
src_images = os.path.join(HERE, "images")
dst_images = os.path.join(DOCS, "images")
if os.path.exists(dst_images):
    shutil.rmtree(dst_images)
shutil.copytree(src_images, dst_images)

# docs/conf.py
conf = """\
project = "Stellar Feedback Experiments with GIZMO"
author = "Mike Grudić"
extensions = [
    "myst_parser",
    "sphinx.ext.mathjax",
]
myst_enable_extensions = ["dollarmath", "colon_fence"]
html_theme = "sphinx_rtd_theme"
source_suffix = {".rst": "restructuredtext", ".md": "markdown"}
"""
with open(os.path.join(DOCS, "conf.py"), "w") as f:
    f.write(conf)

# docs/requirements.txt
reqs = "sphinx\nsphinx-rtd-theme\nmyst-parser\n"
with open(os.path.join(DOCS, "requirements.txt"), "w") as f:
    f.write(reqs)

# .readthedocs.yaml at the repo root
rtd_yaml = """\
version: 2

build:
  os: ubuntu-24.04
  tools:
    python: "3.12"

sphinx:
  configuration: docs/conf.py

python:
  install:
    - requirements: docs/requirements.txt
"""
with open(os.path.join(REPO, ".readthedocs.yaml"), "w") as f:
    f.write(rtd_yaml)

print("Done.")
print(f"  {DOCS}/")
print(f"  {REPO}/.readthedocs.yaml")
