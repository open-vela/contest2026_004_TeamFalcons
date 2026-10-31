#!/usr/bin/env python3
"""修正官方模板与新增图片合并后的 JPEG 类型声明。

原模板有大写 JPG / image/.jpg，导出器又添加小写 jpg / image/jpeg。
PowerPoint 将两者按大小写无关判断为重复。本工具只规范 Content_Types，
所有幻灯片、Master、Layout、嵌入字体和媒体内容保持原字节。
"""

from __future__ import annotations

import argparse
import xml.etree.ElementTree as ET
import zipfile
from pathlib import Path

NS = "http://schemas.openxmlformats.org/package/2006/content-types"
ET.register_namespace("", NS)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("source", type=Path)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()
    if args.source.resolve() == args.output.resolve():
        raise SystemExit("输出必须使用不同路径，保留导出原件以便复核")
    with zipfile.ZipFile(args.source) as src:
        root = ET.fromstring(src.read("[Content_Types].xml"))
        seen = {}
        removed = 0
        for child in list(root):
            if child.tag != f"{{{NS}}}Default":
                continue
            ext = child.get("Extension", "").lower()
            child.set("Extension", ext)
            if ext in ("jpg", "jpeg"):
                child.set("ContentType", "image/jpeg")
            if ext in seen:
                if child.get("ContentType") != seen[ext]:
                    raise SystemExit(f"不认识的重复 MIME 类型冲突：{ext}")
                root.remove(child)
                removed += 1
            else:
                seen[ext] = child.get("ContentType")
        normalized = ET.tostring(root, encoding="utf-8", xml_declaration=True)
        args.output.parent.mkdir(parents=True, exist_ok=True)
        with zipfile.ZipFile(args.output, "w") as dest:
            for entry in src.infolist():
                data = normalized if entry.filename == "[Content_Types].xml" else src.read(entry.filename)
                dest.writestr(entry, data)
    print(f"已规范 JPEG 类型声明，删除 {removed} 条重复声明，其余 package part 字节未改。")


if __name__ == "__main__":
    main()
