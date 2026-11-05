#!/usr/bin/env python3
"""Validate and package only CHATGPT-owned presentation artifacts."""
from pathlib import Path
import ast,hashlib,json,os,posixpath,re,shutil,subprocess,sys,zipfile
import xml.etree.ElementTree as E
from PIL import Image,ImageDraw,ImageFont

HERE=Path(__file__).resolve().parent
REPO=HERE.parents[2]
ROOT=Path('F:/Documents/velaguard作品提交/build')
FINAL=Path('F:/Documents/velaguard作品提交/决赛答辩-20261105')
SKILL=Path('C:/Users/19y/.config/magpie/library/skills/ppt-master/scripts')
BASE=HERE.parent/'final-defense-20261031/VelaGuard-决赛答辩-20261031.pptx'
NS={'a':'http://schemas.openxmlformats.org/drawingml/2006/main','c':'http://schemas.openxmlformats.org/drawingml/2006/chart'}
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
def write(path,text):path.write_text(text,encoding='utf-8',newline='\n')
def run(c,out):
    r=subprocess.run(c,capture_output=True,env={**os.environ,'PYTHONIOENCODING':'utf-8'})
    out.write_bytes(r.stdout)
    assert r.returncode==0,r.stderr.decode('utf-8','replace')
def preview(work,target,indices,cols=4,tw=480,th=270):
    gap=22;label=36;rows=(len(indices)+cols-1)//cols
    im=Image.new('RGB',(cols*(tw+gap)+gap,rows*(th+label+gap)+gap),'#E9EEF5')
    d=ImageDraw.Draw(im);f=ImageFont.truetype('C:/Windows/Fonts/arial.ttf',24)
    for k,i in enumerate(indices):
        x=gap+(k%cols)*(tw+gap);y=gap+(k//cols)*(th+label+gap)
        d.text((x,y),f'{i:02}',font=f,fill='#253952')
        with Image.open(work/'validation/powerpoint'/f'幻灯片{i}.PNG') as p:
            im.paste(p.resize((tw,th),Image.Resampling.LANCZOS),(x,y+label))
    im.save(target,quality=94)

def main():
    qa=json.loads((HERE/'qa/final-summary.json').read_text(encoding='utf-8'))
    assert qa['unresolved_hard_hits']==0 and qa['unresolved_soft_hits']==0
    FINAL.mkdir(parents=True,exist_ok=True)
    results={};(HERE/'images').mkdir(exist_ok=True)
    for variant,label in [('scene','场景叙事版'),('tech','技术深度版')]:
        w=ROOT/f'chatgpt-defense-{variant}-20261105'
        ppt=max((w/'exports').glob('*.pptx'),key=lambda p:p.stat().st_mtime)
        content=json.loads((HERE/f'content-{variant}.json').read_text(encoding='utf-8'))
        assert len(content['pages'])==14 and sum(content['timing'])==440 and content['timing'][11]==80
        run([sys.executable,str(SKILL/'pptx_delivery_check.py'),str(ppt)],w/'validation/delivery.json')
        run([sys.executable,str(SKILL/'source_to_md/ppt_to_md.py'),str(ppt),'-o',str(w/'validation/readback.md')],w/'validation/readback-console.txt')
        delivery=json.loads((w/'validation/delivery.json').read_text(encoding='utf-8'));assert not delivery['errors']
        texts=[];chart_refs=0;shape_count=0
        with zipfile.ZipFile(ppt) as z,zipfile.ZipFile(BASE) as baseline:
            assert z.testzip() is None
            assert len([n for n in z.namelist() if re.fullmatch(r'ppt/slides/slide\d+\.xml',n)])==14
            preserved=[n for n in baseline.namelist() if re.match(r'ppt/(slideMasters/slideMaster|slideLayouts/slideLayout|theme/theme)\d+\.xml$',n)]
            assert len(preserved)==8 and all(z.read(n)==baseline.read(n) for n in preserved)
            for i,page in enumerate(content['pages'],1):
                slide=E.fromstring(z.read(f'ppt/slides/slide{i}.xml'))
                visible=''.join(n.text or '' for n in slide.findall('.//a:t',NS));texts.append(visible)
                assert 'APPENDIX' not in visible and '问答附录' not in visible
                assert ('THANKS' in visible)==(i==14)
                assert '\u201c' not in visible and '\u201d' not in visible
                if i==12:assert '原型实拍' in visible
                rels=E.fromstring(z.read(f'ppt/slides/_rels/slide{i}.xml.rels'))
                target=next(r.attrib['Target'] for r in rels if r.attrib['Type'].endswith('/notesSlide'))
                part=posixpath.normpath(posixpath.join('ppt/slides',target))
                note=''.join(n.text or '' for n in E.fromstring(z.read(part)).findall('.//a:t',NS))
                assert page['speech'] in note,(variant,i,'speech mismatch')
                chart_refs+=len(slide.findall('.//c:chart',NS))
                shape_count+=len(slide.findall('.//{http://schemas.openxmlformats.org/presentationml/2006/main}sp'))
            assert chart_refs==2 and shape_count>100
        assert all((w/'validation/powerpoint'/f'幻灯片{i}.PNG').is_file() for i in range(1,15))
        name=f'VelaGuard-决赛答辩-{label}-CHATGPT.pptx'
        shutil.copy2(ppt,HERE/name)
        shutil.copy2(ppt,FINAL/name)
        assert sha(ppt)==sha(HERE/name)==sha(FINAL/name)
        preview(w,HERE/'qa'/f'全稿预览-{label}-CHATGPT.jpg',list(range(1,15)))
        shutil.copy2(HERE/'qa'/f'全稿预览-{label}-CHATGPT.jpg',FINAL/f'全稿预览-{label}-CHATGPT.jpg')
        snapshot=HERE/f'authoring-{variant}';snapshot.mkdir(exist_ok=True)
        plan=json.loads((w/'page_plan.json').read_text(encoding='utf-8'))
        for row in plan['pages']:
            src=w/'authoring-svg-flat'/row['svg'];dest=snapshot/src.name
            write(dest,src.read_text(encoding='utf-8'))
            root=E.parse(src).getroot()
            for n in root.iter():
                href=n.get('href') or n.get('{http://www.w3.org/1999/xlink}href')
                if href and href.startswith('../images/'):
                    relative=Path(href.removeprefix('../'));to=HERE/relative
                    to.parent.mkdir(parents=True,exist_ok=True)
                    if to.exists():assert sha(to)==sha(w/relative),f'conflicting image: {relative}'
                    else:shutil.copy2(w/relative,to)
        write(HERE/f'page_plan-{variant}.json',json.dumps(plan,ensure_ascii=False,indent=2)+'\n')
        shutil.copy2(w/'validation/delivery.json',HERE/'qa'/f'delivery-{variant}.json')
        shutil.copy2(w/'validation/svg_quality_report.json',HERE/'qa'/f'svg-quality-{variant}.json')
        results[variant]={'file':name,'sha256':sha(ppt),'bytes':ppt.stat().st_size,'slides':14,'notes':14,
                          'native_charts':chart_refs,'editable_shapes':shape_count,'official_parts_preserved':len(preserved),
                          'content_seconds':440,'video_seconds':80,'reserved_seconds':40,'delivery_errors':[],
                          'candidate':ppt.name,'source_mapping':[p['source_slide'] for p in content['pages']]}
    assert texts[-1].find('THANKS')>=0
    for path in HERE.rglob('*'):
        if path.is_file() and path.suffix in ['.py','.md','.json','.c','.txt','.ps1']:
            text=path.read_text(encoding='utf-8-sig')
            if path.suffix=='.py':ast.parse(text)
            # Source evidence may quote glyphs; authored content was checked above.
            write(path,text)
    for path in HERE.glob('*CHATGPT.md'):shutil.copy2(path,FINAL/path.name)
    shutil.copy2(HERE/'OWNERSHIP.md',FINAL/'制作归属-CHATGPT.md')
    shutil.copy2(REPO/'.debug/chatgpt-defense-20261105/Testing/Temporary/LastTest.log',HERE/'qa/headless-ctest.log')
    verification={'author':'CHATGPT','date':'2026-11-05','input_commit':'a075560','input_sha256':sha(BASE),
                  'route':'ppt-master Edit Native PPTX','outputs':results,
                  'ui_render':{'profile':'board','format':'RGB565','size':[480,272],'frames':13,'ctest_passed':7,
                               'independent_build_reproduced_frames_byte_identical':True,'synthetic_data':'mock-data.json'},
                  'new_board_tests':False,'video_embedded':False,'video_current_state':'prototype still',
                  'visual_qa':qa,'font_advisory':'官方 MiSans 保留；文件检查提示非默认 Office 字体可移植性。',
                  'ownership':'OWNERSHIP.md','shared_logs_included_in_commit':False}
    write(HERE/'verification.json',json.dumps(verification,ensure_ascii=False,indent=2)+'\n')
    write(FINAL/'交付清单-CHATGPT.json',json.dumps(verification,ensure_ascii=False,indent=2)+'\n')
    print(json.dumps(results,ensure_ascii=False,indent=2))

if __name__=='__main__':main()
