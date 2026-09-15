# 技术报告重建与核查

本目录仅维护技术报告。正文源是 `report_content.py`，`tech-report.md`
是生成的底稿，不要分别修改 DOCX、Markdown 和脚本中的内容。
需求与固定口径仍以 `REQUIREMENTS.md` 为准。

## 重建

需要 Python 3.11 以上、python-docx、Pillow、pypdf，以及 Windows Word。
Codex 中使用桌面应用提供的捆绑 Python；在仓库根目录运行：

```powershell
& "$env:USERPROFILE\.cache\codex-runtimes\codex-primary-runtime\dependencies\python\python.exe" -X utf8 docs/submission/build_tech_report.py
```

WSL 已安装相同 Python 依赖时，也可运行：

```bash
python3 docs/submission/build_tech_report.py
```

## 公文版式

成品使用公文版式，必须加 `--gongwen`：

```bash
python3 -X utf8 docs/submission/build_tech_report.py --gongwen
```

该开关在填好模板后依次执行 `gongwen/restyle.py`（重建样式表、页边距、
表格框线与目录域）、`gongwen/headers_footers.py`（页眉与公文式页码）、
Word 更新域并另存、`gongwen/theme_fonts.py`（修主题字体），最后导出 PDF。
顺序不能调换：Word 保存时会重编号样式 ID，`restyle.py` 只能在其之前运行，
`theme_fonts.py` 只能在其之后运行。不加 `--gongwen` 得到的是无目录、
无页眉页码的朴素版本，页数也不同。

默认读取 F 盘官方空模板，将报告写入仓内，并将 DOCX、PDF 和三张插图
同步至 `F:\Documents\velaguard作品提交`。Windows/WSL 路径由脚本转换，
Word 只接收 Windows 路径。重建不编译、不烧录、不测试开发板，也不打包或推送。

仅生成供检查的副本：

```powershell
& "$env:USERPROFILE\.cache\codex-runtimes\codex-primary-runtime\dependencies\python\python.exe" -X utf8 docs/submission/build_tech_report.py --no-publish --output-dir "$env:TEMP\velaguard-report-preview"
```

Word 导出在新的临时目录中进行。模板哈希、正文完整性或 PDF 导出检查失败时，
不会用旧 PDF 判成功，也不会替换正式目录里的上一版报告。原始空模板始终保留。
插图读取系统字体，不再依赖本目录旧版 `fonts/` 副本。

## 证据快照

`evidence/report-evidence.json` 保留取证日期、原始记录的文件哈希与行号、
公开 PR 状态、会话来源统计及已有镜像文件大小。正文保留测试原日期；
收集证据不等于重新执行测试。

`collect_report_evidence.py` 的默认操作会重新读取本地资料并查询 GitHub PR。
只在重新核查全文口径后执行，不能把新快照套到未经复核的旧测试描述上。
`--complete-local` 只补入本地测试记录，并要求原快照的源码未变化，
不重算既有会话数量或 PR 状态。

代码和证据链接使用专属仓的提交分支。新增文件需要随仓提交后，
评委才能通过 GitHub 链接访问；本次重建不代替该提交步骤。
原始 AI Coding JSONL 不做任何删改。

## 检查

```powershell
& "$env:USERPROFILE\.cache\codex-runtimes\codex-primary-runtime\dependencies\python\python.exe" -X utf8 docs/submission/test_tech_report.py
```

`qa_tech_report.py` 接收 `--docx`、`--pdf`、`--output-dir`、
`--renderer` 和 `--poppler-dir`，运行内容核对和逐页渲染。
后两项分别指向文档技能的 `render_docx.py` 与捆绑 Poppler 的可执行目录。
Windows 使用隐藏 Word 实例完成转换，不依赖本机另外安装的 LibreOffice。

每次改变正文或排版后，都要重新检查全部页面。自动检查不能代替
中文可读性、表格分页、图注和文字重叠的视觉核查。
