from pathlib import Path
import json,hashlib,ntpath,subprocess,sys
p=Path(__file__).parent;r=r'D:\DLSSNR-Lab\c32-stage-phase5-20261029';mask=sys.argv[1]if len(sys.argv)>1 else'0';names=['3-frame.exe','3-frame-window.exe','3-profile.exe'];cfg={'role':'3','modules':{n:str(p/'work/3-build/gfx1201'/n)for n in ['c32-wave1-fast.hsaco','c32-wave1-fast-norm900.hsaco']},'same_host':True,'frame_exe':ntpath.join(r,'payload',names[0]),'frame_window_exe':ntpath.join(r,'payload',names[1]),'profile_exe':ntpath.join(r,'payload',names[2]),'env':{'NR_C32_POOL':'1','NR_C32_NORM_EXPAND_MASK':mask},'files':[{'path':ntpath.join(r,'payload',n),'sha256':hashlib.sha256((p/'payload'/n).read_bytes()).hexdigest()}for n in names]};(p/'3-config.json').write_text(json.dumps(cfg,indent=2));subprocess.run(['python3',str(p/'make-case.py'),str(p/'3-config.json')],check=True)
for name in ['3-gold.json','3-perf.json','3-kernel.json']:
 f=p/name;d=json.loads(f.read_text())
 for j in d['jobs']:
  candidate=j['label'].endswith('-candidate');j['env'].update({'NR_C32_POOL':'1'if candidate else'0','NR_C32_NORM_EXPAND_MASK':mask})
  if candidate:j['required_stderr']=[r'C32_POOL_FUSED calls=[1-9]\d*']
  if name=='3-kernel.json'and candidate:
   count=int(j['env']['PHASE4_DISPATCHES']);j['env']['PHASE4_DISPATCHES']=str(count-1);j['arguments']=j['arguments'].replace(f'80 {count*2} ',f'80 {(count-1)*2} ')
 f.write_text(json.dumps(d,indent=2))
