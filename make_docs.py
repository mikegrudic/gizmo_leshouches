#!/usr/bin/env python3
"""Update docs/index.md from the workshop README two levels up."""

import os
import shutil

REPO = os.path.dirname(os.path.abspath(__file__))
WORKSHOP = os.path.normpath(os.path.join(REPO, "..", ".."))
DOCS = os.path.join(REPO, "docs")

os.makedirs(DOCS, exist_ok=True)

shutil.copy(os.path.join(WORKSHOP, "README.md"), os.path.join(DOCS, "index.md"))

src_images = os.path.join(WORKSHOP, "images")
dst_images = os.path.join(DOCS, "images")
if os.path.exists(dst_images):
    shutil.rmtree(dst_images)
shutil.copytree(src_images, dst_images)

print("Done — docs/index.md updated.")
