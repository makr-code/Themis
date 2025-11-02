import os
import shutil
import sys
from pathlib import Path

import yaml


REPO_ROOT = Path(__file__).resolve().parents[1]
DOCS_DIR = REPO_ROOT / "docs"
OUT_DIR = REPO_ROOT / "wiki_out"
MKDOCS_YML = REPO_ROOT / "mkdocs.yml"


def load_nav():
    with open(MKDOCS_YML, "r", encoding="utf-8") as f:
        cfg = yaml.safe_load(f)
    return cfg.get("nav", [])


def ensure_clean_out():
    if OUT_DIR.exists():
        shutil.rmtree(OUT_DIR)
    OUT_DIR.mkdir(parents=True, exist_ok=True)


def copy_docs():
    # Copy all markdown and assets preserving structure
    for root, dirs, files in os.walk(DOCS_DIR):
        rel = Path(root).relative_to(DOCS_DIR)
        (OUT_DIR / rel).mkdir(parents=True, exist_ok=True)
        for fn in files:
            src = Path(root) / fn
            if src.suffix.lower() in {".md", ".png", ".jpg", ".jpeg", ".gif", ".svg", ".webp"}:
                dst = OUT_DIR / rel / fn
                shutil.copy2(src, dst)


def write_sidebar(nav):
    sidebar = []

    def add_item(item, level=0):
        indent = "  " * level
        if isinstance(item, dict):
            for title, value in item.items():
                if isinstance(value, list):
                    sidebar.append(f"{indent}- {title}")
                    for child in value:
                        add_item(child, level + 1)
                else:
                    # value is path
                    link = path_to_wiki_link(value)
                    sidebar.append(f"{indent}- [{title}]({link})")
        else:
            # string path without label
            link = path_to_wiki_link(item)
            sidebar.append(f"{indent}- [{Path(item).stem}]({link})")

    for entry in nav:
        add_item(entry, 0)

    (OUT_DIR / "_Sidebar.md").write_text("\n".join(sidebar) + "\n", encoding="utf-8")


def path_to_wiki_link(p):
    p = str(p)
    if p == "index.md":
        return "Home.md"
    return p


def make_home():
    # index.md -> Home.md
    src = DOCS_DIR / "index.md"
    if src.exists():
        shutil.copy2(src, OUT_DIR / "Home.md")


def main():
    ensure_clean_out()
    copy_docs()
    make_home()
    nav = load_nav()
    write_sidebar(nav)
    print(f"Wiki export prepared at: {OUT_DIR}")


if __name__ == "__main__":
    try:
        main()
    except Exception as e:
        print(f"ERROR: {e}", file=sys.stderr)
        sys.exit(1)
