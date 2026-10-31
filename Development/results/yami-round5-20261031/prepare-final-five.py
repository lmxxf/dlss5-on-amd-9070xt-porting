from pathlib import Path
import json,copy,re,shutil,hashlib,subprocess
p=Path(__file__).parent;r=r'D:\DLSSNR-Lab\yami-round5-20261031';dest=p/'final-c256/HIP';shutil.copytree(p/'final-base/HIP',dest,dirs_exist_ok=True)
for arch in ['gfx1201','gfx1200']:
 for mod in ['c64-wave2','c64-wave2-fast']:shutil.copyfile(p/'production-combined-norm900-c256/build'/arch/(mod+'.hsaco'),dest/arch/(mod+'.hsaco'))
old=json.loads((p/'final-panel-gold.json').read_text());files={f['path']:f for f in old['files']};remote=r+'\\final-c256\\modules'
for f in (dest/'gfx1201').glob('*.hsaco'):files[remote+'\\'+f.name]=dict(path=remote+'\\'+f.name,sha256=hashlib.sha256(f.read_bytes()).hexdigest())
gold=[]
for j0 in old['jobs']:
 if '/base-'not in j0['label']:continue
 j=copy.deepcopy(j0);j['label']=j['label'].replace('final-panel-gold/base-','final-c256-gold/c256-');j['arguments']=j['arguments'].replace('\\final-base\\modules','\\final-c256\\modules');gold.append(j)
(p/'final-c256-gold.json').write_text(json.dumps(dict(name='final-c256-gold',files=list(files.values()),jobs=gold),indent=2)+'\n')
(p/'final-c256-setup.ps1').write_text("$ErrorActionPreference='Stop'\nif(Test-Path 'D:\\DLSSNR-Lab\\gpu.lock'){throw 'busy'}\n"+f'New-Item -ItemType Directory -Force "{remote}"|Out-Null\nCopy-Item "{r}\\final-base\\modules\\*" "{remote}"\n'+''.join(f'Copy-Item "{r}\\final-full-{m}.hsaco" "{remote}\\{m}.hsaco"\n'for m in ['c64-wave2','c64-wave2-fast']))
proto=json.loads((p/'final-panel-long.json').read_text());files.update({f['path']:f for f in proto['files']});jobs=[];arms=['stock','base','norm','c256','full']
for prof in ['original1088','original1152','original960','smallwindow','small720','small900','small1080']:
 for rnd in [1,2,3]:
  seq=arms[rnd-1:]+arms[:rnd-1];seq+=seq[::-1]
  for slot,arm in enumerate(seq):
   j=copy.deepcopy(next(j for j in proto['jobs']if j['label']==f'final-panel-long/perf-{prof}-r1-'+str(['stock','base','norm','full'].index(arm if arm!='c256'else'base'))+'-'+(arm if arm!='c256'else'base')))
   if arm=='c256':j['arguments']=j['arguments'].replace('\\final-base\\modules','\\final-c256\\modules')
   j['label']=f'final-five-long/perf-{prof}-r{rnd}-{slot}-{arm}';jobs.append(j)
assert len(jobs)==210;(p/'final-five-long.json').write_text(json.dumps(dict(name='final-five-long',files=list(files.values()),jobs=jobs),indent=2)+'\n');subprocess.run(['python3',str(p/'pin-inputs.py'),'final-c256-gold','final-five-long'],check=True)
(p/'final-five-plan.md').write_text('''# Final five-arm qualification (fixed before data)

This supersedes the prepared but NEVER RUN final-panel-long.json (four arms). Add the missing C256-only composition to complete the constructive2×2, avoiding an assumption that norm900 and C256 gains add.

Arms: stock b60; base=ViTperm+C512clamp; norm=base+guardeddotparONLYnorm900; c256=base+C256one-planeRC; full=bothadditions. Same stock hosts. Sevenprofiles,3rotatedsymmetric10-slotrounds,800framesdiscard160,3840samples/arm/profile,210slots. Inputs/fullassettreespre-pinned; nootherremoteoperationsduringtiming. AllfourcandidatepayloadspasssevenGoldfirst.

Common acceptance:all7pooledmeanregressions<=0.5%,pooledp99regressions<=1%,no3/3roundmeanregression>0.5%;sum7mean deltas<0;Small1080all3mean/p99improvevsstock. Norm-bearingpayloads additionallyrequireOriginal960all3mean/p99improve. C256-bearingpayloads additionallyrequireOriginal1088all3meanimproveandpooledp99notworse.

A norm addition is justified only when its conditionalcomparison(norm-base orfull-c256)winsOriginal960meanall3andpooledp99,andall7noninferioritybounds. A C256addition is justified only when its conditionalcomparison(c256-base orfull-norm)winsmeanall3onBOTHOriginal1088andSmall1080,pooledp99notworseonboth,andall7bounds.

Eligiblefullmustjustifybothconditionaladditions;otherwiseeligiblepartialpayloadsjustifytheirrespectiveaddition. Amongeligiblepayloadschoosemostnegativeequal-weightedsum7mean deltas;preferfewerchangesifwithin1us sum. Ifnoneeligible,retainb60. Keepallrawresults;noidenticalreruntoerasefailures. Reportnoninferiorityasnoninferiority,not7strictwins;theseareengineeringgates,notglobalperformance-ceilingproof.
''')
