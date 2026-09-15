#!/usr/bin/env python3
"""检查报告结构，并通过文档技能渲染器与 Windows Word 生成逐页校样。"""

from __future__ import annotations

import argparse
import hashlib
import importlib.util
import json
import os
import sys
import zipfile
from pathlib import Path

from PIL import Image, ImageChops

import build_tech_report as build
import report_content as content


def package_changes(template: Path, docx: Path) -> dict:
    def inventory(path):
        with zipfile.ZipFile(path) as package:
            return {
                name: hashlib.sha256(package.read(name)).hexdigest()
                for name in package.namelist()
            }
    before, after = inventory(template), inventory(docx)
    return {
        "changed": sorted(name for name in before.keys() & after.keys() if before[name] != after[name]),
        "added": sorted(after.keys() - before.keys()),
        "removed": sorted(before.keys() - after.keys()),
    }


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--docx", type=Path, required=True)
    parser.add_argument("--pdf", type=Path, required=True)
    parser.add_argument("--output-dir", type=Path, required=True)
    parser.add_argument("--renderer", type=Path, required=True)
    parser.add_argument("--poppler-dir", type=Path, required=True)
    args = parser.parse_args()
    evidence = json.loads((build.SUB / "evidence/report-evidence.json").read_text(encoding="utf-8"))
    parts = content.sections(evidence)
    stats = build.validate_artifacts(args.docx, args.pdf, parts)
    template = build.default_publish_dir() / build.TEMPLATE_NAME
    if build.digest(template) != build.TEMPLATE_SHA256:
        raise RuntimeError("Original template hash changed")
    stats["template_sha256"] = build.digest(template)
    stats["package_changes"] = package_changes(template, args.docx)
    changes = stats["package_changes"]
    if changes["removed"] or set(build.PRESERVE_PARTS) & set(changes["changed"]):
        raise RuntimeError("A preserve-only template part was changed or removed")
    stats["renderer"] = "Document skill render_docx.py; Windows Word conversion adapter; bundled Poppler"
    os.environ["PATH"] = str(args.poppler_dir) + os.pathsep + os.environ.get("PATH", "")

    spec = importlib.util.spec_from_file_location("document_skill_renderer", args.renderer)
    if spec is None or spec.loader is None:
        raise RuntimeError("Cannot load the document skill renderer")
    renderer = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(renderer)

    def word_convert(doc_path, user_profile, convert_tmp_dir, stem, verbose=False):
        target = Path(convert_tmp_dir) / f"{stem}.pdf"
        build.export_pdf(Path(doc_path), target)
        return str(target), "Converted with an isolated, hidden Word instance"

    # Windows 捆绑依赖不含 LibreOffice，仅替换转换回调，保留官方逐页渲染流程。
    renderer.convert_to_pdf = word_convert
    original_argv = sys.argv
    try:
        sys.argv = [
            str(args.renderer), str(args.docx),
            "--output_dir", str(args.output_dir), "--dpi", "144",
        ]
        renderer.main()
    finally:
        sys.argv = original_argv
    images = sorted(args.output_dir.glob("page-*.png"), key=lambda p: int(p.stem.split("-")[-1]))
    if len(images) != stats["pages"]:
        raise RuntimeError("Rendered page count differs from exported PDF")
    for path in images:
        with Image.open(path).convert("RGB") as image:
            if ImageChops.difference(image, Image.new("RGB", image.size, "white")).getbbox() is None:
                raise RuntimeError(f"Blank rendered page: {path}")
    stats["rendered_pages"] = len(images)
    stats["visual_review"] = "Required: inspect every rendered page before delivery"
    (args.output_dir / "qa-summary.json").write_text(
        json.dumps(stats, ensure_ascii=False, indent=2) + "\n", encoding="utf-8"
    )
    print(json.dumps(stats, ensure_ascii=False, indent=2))


if __name__ == "__main__":
    main()
