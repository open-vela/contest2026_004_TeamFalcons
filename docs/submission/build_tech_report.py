#!/usr/bin/env python3
"""从同一正文源生成报告，保留官方模板的基本格式。"""

from __future__ import annotations

import argparse
import copy
import hashlib
import json
import os
import re
import shutil
import subprocess
import sys
import tempfile
import unicodedata
import zipfile
from pathlib import Path

from docx import Document
from docx.enum.style import WD_STYLE_TYPE
from docx.enum.table import WD_CELL_VERTICAL_ALIGNMENT, WD_TABLE_ALIGNMENT
from docx.enum.text import WD_ALIGN_PARAGRAPH
from docx.oxml import OxmlElement
from docx.oxml.ns import qn
from docx.shared import Inches, Pt, RGBColor
from docx.opc.constants import RELATIONSHIP_TYPE as RT
from pypdf import PdfReader

import report_content as content
from render_figures import FIGURE_NAMES, render_all

SUB = Path(__file__).resolve().parent
ROOT = SUB.parents[1]
GONGWEN_DIR = SUB / "gongwen"
REPORT_NAME = "VelaGuard-技术报告"
TEMPLATE_NAME = "2026 首届 openvela AI 硬件开发者大赛 - 作品提交模板.docx"
TEMPLATE_SHA256 = "94247bfe0ed960629b138108316dccdd2827761bbe9cc8d5fe48e1c78ad396a8"
TEXT_WIDTH_PT = 11905 / 20 - 180
REF_PATTERN = re.compile(r"\[(\d+)\]")
PRESERVE_PARTS = (
    "_rels/.rels", "word/numbering.xml", "word/settings.xml",
    "word/_rels/header1.xml.rels",
)


def default_publish_dir() -> Path:
    return Path("F:/Documents/velaguard作品提交") if os.name == "nt" else Path("/mnt/f/Documents/velaguard作品提交")


def digest(path: Path) -> str:
    with path.open("rb") as stream:
        return hashlib.file_digest(stream, "sha256").hexdigest()


def windows_path(path: Path) -> str:
    if os.name == "nt":
        return str(path.resolve())
    return subprocess.check_output(["wslpath", "-w", str(path.resolve())], text=True).strip()


def normalize(text: str) -> str:
    return re.sub(r"\s+", "", unicodedata.normalize("NFKC", text)).replace("\u00ad", "")


def pdf_body_text(text: str) -> str:
    """去掉页眉页脚，避免跨页段落在抽出的文本里被切开。

    朴素版页脚是「第 N 页」，公文版是「— N —」一字线；页眉在两种版式里
    都是「VelaGuard 技术报告」。三者都只出现在页面上下缘，正文不会用到。
    """
    text = re.sub(r"(?m)^[ \t]*第[ \t]*\d+[ \t]*页[ \t]*$", "", text)
    text = re.sub(r"[—–][ \t]*\d+[ \t]*[—–]", "", text)
    return text.replace("VelaGuard 技术报告", "")


def plain_cell(cell) -> str:
    return cell["text"] if isinstance(cell, dict) else str(cell)


def xml_text(element) -> str:
    return "".join(element.xpath(".//w:t/text()"))


def validate_content(parts: list[dict], evidence: dict) -> None:
    if len(content.ABSTRACT) > 300:
        raise ValueError(f"Abstract exceeds 300 characters: {len(content.ABSTRACT)}")
    if len(parts) != 7 or len(evidence["pull_requests"]) != 8:
        raise ValueError("Seven official sections and eight verified PRs are required")
    headless = evidence.get("headless_20260913", {}).get("excerpts", [])
    if sum(item["text"] == "Test Passed." for item in headless) != 5:
        raise ValueError("The original five passing headless tests must be present")
    for i, part in enumerate(parts, 1):
        if not part["heading"].startswith(f"3.{i} ") or not part["blocks"]:
            raise ValueError("Official section order is incomplete")
        for block in part["blocks"]:
            if block["kind"] == "table":
                if len(block["headers"]) != len(block["widths"]) or abs(sum(block["widths"]) - 1) > 0.001:
                    raise ValueError(f"Invalid table layout: {block['caption']}")
                if any(len(row) != len(block["headers"]) for row in block["rows"]):
                    raise ValueError(f"Invalid table row: {block['caption']}")
    all_text = json.dumps(parts, ensure_ascii=False) + content.ABSTRACT
    if "\u201c" in all_text or "\u201d" in all_text:
        raise ValueError("Curved Chinese quotation marks are not permitted")
    for number in REF_PATTERN.findall(all_text):
        if number not in content.REF_URLS:
            raise ValueError(f"Unknown reference {number}")
    for _, path in content.REFERENCES:
        if path.startswith(("http://", "https://")):
            continue
        if not (ROOT / path).exists():
            raise ValueError(f"Missing local evidence source: {path}")


def add_hyperlink(paragraph, label: str, url: str, size: float = 10.5):
    relation = paragraph.part.relate_to(url, RT.HYPERLINK, is_external=True)
    link = OxmlElement("w:hyperlink")
    link.set(qn("r:id"), relation)
    run = OxmlElement("w:r")
    props = OxmlElement("w:rPr")
    color = OxmlElement("w:color")
    color.set(qn("w:val"), "24588D")
    props.append(color)
    sz = OxmlElement("w:sz")
    sz.set(qn("w:val"), str(round(size * 2)))
    props.append(sz)
    run.append(props)
    node = OxmlElement("w:t")
    node.text = label
    node.set(qn("xml:space"), "preserve")
    run.append(node)
    link.append(run)
    paragraph._p.append(link)


def add_text(paragraph, value: str, size: float = 11):
    offset = 0
    for match in REF_PATTERN.finditer(value):
        if match.start() > offset:
            paragraph.add_run(value[offset:match.start()])
        add_hyperlink(paragraph, match.group(0), content.REF_URLS[match.group(1)], size)
        offset = match.end()
    if offset < len(value):
        paragraph.add_run(value[offset:])


def template_samples(doc):
    def find(prefix):
        return next(p for p in doc.paragraphs if p.text.startswith(prefix))
    body = find("300 字以内")
    return {
        "body": copy.deepcopy(body._p.pPr),
        "run": copy.deepcopy(body.runs[0]._r.rPr),
        "info_table": copy.deepcopy(doc.tables[2]._tbl),
        "title": copy.deepcopy(doc.paragraphs[0]._p.pPr),
    }


def define_styles(doc, samples):
    specs = {
        "Normal": (11, False, 6, 6, False),
        "Title": (26, True, 24, 16, True),
        "Heading 1": (15, True, 15, 6, True),
        "Heading 2": (14, True, 13, 6, True),
        "Heading 3": (11, True, 10, 4, True),
        "Caption": (10, False, 3, 9, False),
        "Report Reference": (9, False, 0, 1, False),
        "Report Small": (10, False, 4, 6, False),
    }
    for name, (size, bold, before, after, keep) in specs.items():
        style = doc.styles.add_style(name, WD_STYLE_TYPE.PARAGRAPH)
        style.element.append(copy.deepcopy(samples["body"]))
        style.element.append(copy.deepcopy(samples["run"]))
        if name == "Normal":
            style.element.set(qn("w:default"), "1")
        style.font.size = Pt(size)
        style.font.bold = bold
        style.font.color.rgb = RGBColor(0, 0, 0)
        fmt = style.paragraph_format
        fmt.space_before, fmt.space_after = Pt(before), Pt(after)
        fmt.line_spacing = 1.2
        if name == "Report Reference":
            fmt.line_spacing = 1.1
            fmt.tab_stops.add_tab_stop(Pt(TEXT_WIDTH_PT / 3))
            fmt.tab_stops.add_tab_stop(Pt(TEXT_WIDTH_PT * 2 / 3))
        fmt.keep_with_next = keep
        fmt.widow_control = True
        fmt.alignment = WD_ALIGN_PARAGRAPH.LEFT
        if name.startswith("Heading"):
            level = OxmlElement("w:outlineLvl")
            level.set(qn("w:val"), str(int(name[-1]) - 1))
            style.element.get_or_add_pPr().append(level)


def set_table_style(table, widths):
    table.alignment = WD_TABLE_ALIGNMENT.LEFT
    table.autofit = False
    for i, fraction in enumerate(widths):
        width = Pt(TEXT_WIDTH_PT * fraction)
        table.columns[i].width = width
        for row in table.rows:
            row.cells[i].width = width
    props = table._tbl.tblPr
    borders = props.find(qn("w:tblBorders"))
    if borders is not None:
        props.remove(borders)
    borders = OxmlElement("w:tblBorders")
    for side in ("top", "left", "bottom", "right", "insideH", "insideV"):
        edge = OxmlElement(f"w:{side}")
        for key, val in (("val", "single"), ("sz", "4"), ("color", "DEE0E3")):
            edge.set(qn(f"w:{key}"), val)
        borders.append(edge)
    props.append(borders)
    for i, row in enumerate(table.rows):
        trpr = row._tr.get_or_add_trPr()
        no_split = OxmlElement("w:cantSplit")
        trpr.append(no_split)
        if i == 0:
            header = OxmlElement("w:tblHeader")
            trpr.append(header)
        for cell in row.cells:
            cell.vertical_alignment = WD_CELL_VERTICAL_ALIGNMENT.CENTER
            tcpr = cell._tc.get_or_add_tcPr()
            old = tcpr.find(qn("w:tcMar"))
            if old is not None:
                tcpr.remove(old)
            margins = OxmlElement("w:tcMar")
            for name, val in (("top", 65), ("bottom", 65), ("left", 110), ("right", 110)):
                elem = OxmlElement(f"w:{name}")
                elem.set(qn("w:w"), str(val))
                elem.set(qn("w:type"), "dxa")
                margins.append(elem)
            tcpr.append(margins)
            for paragraph in cell.paragraphs:
                paragraph.style = "Normal"
                fmt = paragraph.paragraph_format
                fmt.space_before = Pt(3)
                fmt.space_after = Pt(3)
                fmt.line_spacing = 1.15
                fmt.keep_with_next = False
                for run in paragraph.runs:
                    run.font.size = Pt(10.5)
                    run.font.bold = i == 0


def append_table(doc, block):
    caption = doc.add_paragraph(block["caption"], "Caption")
    caption.paragraph_format.keep_with_next = True
    table = doc.add_table(rows=len(block["rows"]) + 1, cols=len(block["headers"]))
    for i, row in enumerate([block["headers"], *block["rows"]]):
        for j, value in enumerate(row):
            paragraph = table.cell(i, j).paragraphs[0]
            if isinstance(value, dict):
                add_hyperlink(paragraph, value["text"], value["url"])
            else:
                add_text(paragraph, str(value), 10.5)
    set_table_style(table, block["widths"])
    return table


def fill_docx(template: Path, target: Path, parts: list[dict], figure_dir: Path):
    doc = Document(str(template))
    samples = template_samples(doc)
    doc._body.clear_content()
    define_styles(doc, samples)
    section = doc.sections[0]
    section.top_margin = section.bottom_margin = Inches(1)
    section.left_margin = section.right_margin = Inches(1.25)
    section.header_distance = section.footer_distance = Inches(0.5)
    header = section.header
    for child in list(header._element):
        header._element.remove(child)
    header._element.append(OxmlElement("w:p"))
    footer = section.footer
    for child in list(footer._element):
        footer._element.remove(child)
    footer._element.append(OxmlElement("w:p"))
    page = footer.paragraphs[0]
    page.style = "Report Small"
    page.alignment = WD_ALIGN_PARAGRAPH.CENTER
    page.add_run("第 ")
    field = OxmlElement("w:fldSimple")
    field.set(qn("w:instr"), "PAGE")
    page._p.append(field)
    page.add_run(" 页")

    doc.core_properties.title = content.TITLE
    doc.core_properties.subject = "2026 首届 openvela AI 硬件开发者大赛技术报告"
    doc.core_properties.author = "叶培林 / Team Falcons"
    doc.add_paragraph(content.TITLE, "Title")
    doc.add_paragraph("2026 首届 openvela AI 硬件开发者大赛", "Report Small")
    doc.add_paragraph("1、信息表", "Heading 1")
    doc._body._body.insert(len(doc._body._body) - 1, samples["info_table"])
    info = doc.tables[0]
    for i, (label, value) in enumerate(content.INFO, 1):
        info.cell(i, 0).text = label
        info.cell(i, 1).text = value
    set_table_style(info, [0.24, 0.76])
    doc.add_paragraph("2、摘要", "Heading 1")
    doc.add_paragraph(content.ABSTRACT, "Normal")
    p = doc.add_paragraph(f"资料核对日期为 {content.DATE}。专属仓为 ", "Report Small")
    add_hyperlink(p, "contest2026_004_TeamFalcons", content.REPO_URL, 10)
    body_heading = doc.add_paragraph("3、正文", "Heading 1")
    body_heading.paragraph_format.page_break_before = True
    for part in parts:
        heading = doc.add_paragraph(part["heading"], "Heading 2")
        if part["heading"].startswith("3.5 "):
            heading.paragraph_format.page_break_before = True
        for block in part["blocks"]:
            kind = block["kind"]
            if kind == "subheading":
                doc.add_paragraph(block["text"], "Heading 3")
            elif kind == "paragraph":
                para = doc.add_paragraph(style="Normal")
                add_text(para, block["text"])
                for ref in block.get("refs", []):
                    add_hyperlink(para, f"[{ref}]", content.REF_URLS[str(ref)])
            elif kind == "table":
                append_table(doc, block)
            elif kind == "figure":
                image = figure_dir / (block["name"] + ".png")
                if not image.is_file():
                    raise FileNotFoundError(image)
                para = doc.add_paragraph()
                para.paragraph_format.keep_with_next = True
                para.paragraph_format.space_after = Pt(0)
                run = para.add_run()
                picture = run.add_picture(str(image), width=Pt(TEXT_WIDTH_PT))
                picture._inline.docPr.set("descr", block["caption"])
                caption = doc.add_paragraph(block["caption"], "Caption")
                caption.alignment = WD_ALIGN_PARAGRAPH.CENTER
            elif kind == "references":
                for i, (title, _) in enumerate(content.REFERENCES, 1):
                    number = str(i)
                    if i % 3 == 1:
                        para = doc.add_paragraph(style="Report Reference")
                    else:
                        para.add_run("\t")
                    add_hyperlink(para, f"[{number}] {title}", content.REF_URLS[number], 9)
            else:
                raise ValueError(f"Unknown report block: {kind}")
    doc.save(str(target))
    preserve_template_parts(template, target)


def preserve_template_parts(template: Path, target: Path):
    rewritten = target.with_suffix(".preserved.docx")
    with zipfile.ZipFile(template) as original, zipfile.ZipFile(target) as built:
        with zipfile.ZipFile(rewritten, "w", compression=zipfile.ZIP_DEFLATED) as output:
            for item in built.infolist():
                data = original.read(item.filename) if item.filename in PRESERVE_PARTS else built.read(item.filename)
                output.writestr(item, data)
    rewritten.replace(target)


def markdown_text(value):
    if isinstance(value, dict):
        return f"[{value['text']}]({value['url']})"
    return REF_PATTERN.sub(
        lambda m: f"[{m.group(1)}]({content.REF_URLS[m.group(1)]})", str(value)
    )


def write_markdown(target: Path, parts: list[dict]):
    out = [
        f"# {content.TITLE}", "",
        "2026 首届 openvela AI 硬件开发者大赛", "",
        "## 1、信息表", "", "| 项目 | 内容 |", "| --- | --- |",
        *[f"| {label} | {value} |" for label, value in content.INFO],
        "", "## 2、摘要", "", content.ABSTRACT, "",
        f"资料核对日期为 {content.DATE}；专属仓为 [contest2026_004_TeamFalcons]({content.REPO_URL})。", "", "## 3、正文", "",
    ]
    for part in parts:
        out.extend([f"### {part['heading']}", ""])
        for block in part["blocks"]:
            kind = block["kind"]
            if kind == "subheading":
                out.extend([f"#### {block['text']}", ""])
            elif kind == "paragraph":
                refs = "".join(f" [{i}]({content.REF_URLS[str(i)]})" for i in block.get("refs", []))
                out.extend([markdown_text(block["text"]) + refs, ""])
            elif kind == "figure":
                out.extend([f"![{block['caption']}](figures/{block['name']}.png)", "", block["caption"], ""])
            elif kind == "table":
                out.extend([block["caption"], "", "| " + " | ".join(block["headers"]) + " |",
                            "| " + " | ".join("---" for _ in block["headers"]) + " |"])
                for row in block["rows"]:
                    cells = [markdown_text(value).replace("\n", "<br>").replace("|", "\\|") for value in row]
                    out.append("| " + " | ".join(cells) + " |")
                out.append("")
            elif kind == "references":
                for i, (title, path) in enumerate(content.REFERENCES, 1):
                    out.append(f"- [{i} {title}]({content.REF_URLS[str(i)]})，路径为 `{path}`")
                out.append("")
    target.write_text("\n".join(out) + "\n", encoding="utf-8")


def powershell_script(script: Path, *arguments: str, timeout: int = 180) -> str:
    shell = "powershell.exe" if os.name == "nt" else "/mnt/c/Windows/System32/WindowsPowerShell/v1.0/powershell.exe"
    command = [
        shell, "-NoProfile", "-NonInteractive", "-ExecutionPolicy", "Bypass",
        "-File", windows_path(script), *arguments,
    ]
    options = {"creationflags": subprocess.CREATE_NO_WINDOW} if os.name == "nt" else {}
    result = subprocess.run(command, capture_output=True, timeout=timeout, **options)
    if result.returncode != 0:
        error = result.stderr.decode("utf-8", errors="replace")
        raise RuntimeError(f"{script.name} failed ({result.returncode}): {error}")
    return result.stdout.decode("utf-8", errors="replace").strip()


def export_pdf(docx: Path, pdf: Path):
    if pdf.exists():
        raise FileExistsError(f"PDF output must be fresh: {pdf}")
    powershell_script(
        SUB / "export_report_pdf.ps1",
        "-InputDocx", windows_path(docx), "-OutputPdf", windows_path(pdf),
    )
    if not pdf.is_file() or pdf.stat().st_size < 1000:
        raise RuntimeError("Word did not produce a valid new PDF")


def run_python_step(script: Path, *arguments: Path) -> str:
    result = subprocess.run(
        [sys.executable, "-X", "utf8", str(script), *[str(item) for item in arguments]],
        capture_output=True, timeout=180,
    )
    if result.returncode != 0:
        error = result.stderr.decode("utf-8", errors="replace")
        raise RuntimeError(f"{script.name} failed ({result.returncode}): {error}")
    return result.stdout.decode("utf-8", errors="replace").strip()


def apply_gongwen(docx: Path, pdf: Path, staging: Path):
    """公文版式：重建样式表与页面，再经 Word 更新域，最后修主题字体并导出。"""
    steps = GONGWEN_DIR
    run_python_step(steps / "restyle.py", docx, docx)
    run_python_step(steps / "headers_footers.py", docx, docx)
    # Word 在这一步展开目录域并重编号样式 ID，必须排在 theme_fonts 之前。
    intermediate = staging / "gongwen-stage.pdf"
    powershell_script(
        steps / "word_render.ps1",
        "-Docx", windows_path(docx), "-Pdf", windows_path(intermediate),
    )
    run_python_step(steps / "theme_fonts.py", docx)
    if pdf.exists():
        raise FileExistsError(f"PDF output must be fresh: {pdf}")
    export_pdf(docx, pdf)


def expected_strings(parts):
    yield content.ABSTRACT
    for _, value in content.INFO:
        yield value
    for part in parts:
        yield part["heading"]
        for block in part["blocks"]:
            if block["kind"] in ("paragraph", "subheading"):
                yield block["text"]
            elif block["kind"] == "table":
                yield block["caption"]
                for row in [block["headers"], *block["rows"]]:
                    yield from (plain_cell(value) for value in row)
            elif block["kind"] == "figure":
                yield block["caption"]


def validate_artifacts(docx: Path, pdf: Path, parts):
    doc = Document(str(docx))
    full_text = xml_text(doc._element)
    if len(doc.tables) != 10 or len(doc.inline_shapes) != len(FIGURE_NAMES):
        raise ValueError(
            f"Expected the information table, nine data tables and {len(FIGURE_NAMES)} figures, "
            f"found {len(doc.tables)} tables and {len(doc.inline_shapes)} figures"
        )
    if any(p.text.strip().startswith("|") for p in doc.paragraphs):
        raise ValueError("Raw Markdown table text found in DOCX")
    if any(text in full_text for text in ("一、需提交材料清单", "三、评审维度对照", "300 字以内", "\u201c", "\u201d")):
        raise ValueError("Template guidance or forbidden punctuation remains")
    if any(header._element.xpath(".//w:drawing|.//w:pict") for header in [doc.sections[0].header]):
        raise ValueError("Template watermark remains")
    doc_norm = normalize(full_text)
    for expected in expected_strings(parts):
        if normalize(expected) not in doc_norm:
            raise ValueError(f"DOCX content is missing: {expected[:90]}")
    reader = PdfReader(str(pdf))
    if not reader.pages:
        raise ValueError("PDF contains no pages")
    pdf_norm = normalize("".join(pdf_body_text(page.extract_text() or "") for page in reader.pages))
    missing = [text for text in expected_strings(parts) if normalize(text) not in pdf_norm]
    if missing:
        raise ValueError(f"PDF content differs from DOCX: {missing[:3]}")
    return {
        "pages": len(reader.pages), "native_tables": len(doc.tables),
        "figures": len(doc.inline_shapes), "abstract_characters": len(content.ABSTRACT),
        "hyperlinks": len(doc._element.xpath(".//w:hyperlink")),
    }


def publish(staging: Path, destinations: list[Path]):
    relatives = [
        Path("tech-report.md"), Path(REPORT_NAME + ".docx"), Path(REPORT_NAME + ".pdf"),
        *[Path("figures") / (name + ".png") for name in FIGURE_NAMES],
    ]
    for index, destination in enumerate(destinations):
        destination.mkdir(parents=True, exist_ok=True)
        for relative in relatives:
            if index > 0 and relative.name == "tech-report.md":
                continue
            target = destination / relative
            target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(staging / relative, target)
            if digest(target) != digest(staging / relative):
                raise IOError(f"Published copy differs: {target}")


def build(template: Path, output_dir: Path, publish_dir: Path | None, evidence_file: Path,
          gongwen: bool = False):
    original_hash = digest(template)
    if original_hash != TEMPLATE_SHA256:
        raise ValueError("The official template changed; audit it before rebuilding")
    evidence = json.loads(evidence_file.read_text(encoding="utf-8"))
    parts = content.sections(evidence)
    validate_content(parts, evidence)
    with tempfile.TemporaryDirectory(prefix="velaguard-report-") as temporary:
        staging = Path(temporary).resolve()
        if staging.parent != Path(tempfile.gettempdir()).resolve():
            raise ValueError("Unexpected staging directory")
        render_all(staging / "figures")
        write_markdown(staging / "tech-report.md", parts)
        docx = staging / (REPORT_NAME + ".docx")
        pdf = staging / (REPORT_NAME + ".pdf")
        fill_docx(template, docx, parts, staging / "figures")
        if gongwen:
            apply_gongwen(docx, pdf, staging)
        else:
            export_pdf(docx, pdf)
        stats = validate_artifacts(docx, pdf, parts)
        if digest(template) != original_hash:
            raise RuntimeError("The original template was modified")
        destinations = [output_dir]
        if publish_dir is not None and publish_dir.resolve() != output_dir.resolve():
            destinations.append(publish_dir)
        publish(staging, destinations)
    print(json.dumps(stats, ensure_ascii=False))
    print(f"Report: {output_dir / (REPORT_NAME + '.docx')}")
    print(f"PDF: {output_dir / (REPORT_NAME + '.pdf')}")
    return stats


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--template", type=Path, default=default_publish_dir() / TEMPLATE_NAME)
    parser.add_argument("--output-dir", type=Path, default=SUB)
    parser.add_argument("--publish-dir", type=Path, default=default_publish_dir())
    parser.add_argument("--no-publish", action="store_true")
    parser.add_argument("--evidence", type=Path, default=SUB / "evidence/report-evidence.json")
    parser.add_argument("--gongwen", action="store_true",
                        help="按公文版式重建：重建样式表、加页眉页码与目录，再经 Word 更新域")
    args = parser.parse_args()
    build(args.template, args.output_dir, None if args.no_publish else args.publish_dir,
          args.evidence, args.gongwen)


if __name__ == "__main__":
    main()
