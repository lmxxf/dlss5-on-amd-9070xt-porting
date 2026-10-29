from pathlib import Path
import json,copy,hashlib,ntpath,sys,shutil
p=Path(__file__).parent;r=r'D:\DLSSNR-Lab\c32-stage-phase5-20261029';cfg=json.loads(Path(sys.argv[1]).read_text());role=cfg['role'];mods=cfg.get('modules',{});base=json.loads(Path('Development/results/backfeed-20261028/final-default.json').read_text());sums={n:h for h,n in(l.split()for l in Path('hip/SHA256SUMS').read_text().splitlines())};files=[f for f in base['files']if not f['path'].endswith('.hsaco')]
for name,local in mods.items():shutil.copyfile(local,p/'payload'/(role+'-'+name))
for n,h in sums.items():
 if not n.startswith('gfx1201/'):continue
 name=n.split('/')[1]
 for arm in ['baseline',role]:files.append({'path':ntpath.join(r,arm,'modules',name),'sha256':hashlib.sha256(Path(mods[name]).read_bytes()).hexdigest()if arm==role and name in mods else h})
jobs=[]
for j in base['jobs']:
 profile=j['label'].split('/final-')[1].removesuffix('-candidate')
 for arm in ['baseline','candidate']:
  x=copy.deepcopy(j);x['label']=f'{role}/gold-{profile}-{arm}';x['env']=cfg.get('env',{})if arm=='candidate'else{};x.pop('compare_to',None);x['arguments']=x['arguments'].replace(r'D:\DLSSNR-Lab\gap-phase6-20261023\knife1\modules',ntpath.join(r,'baseline'if arm=='baseline'else role,'modules'))
  if cfg.get('same_host')and 'frame_exe'in cfg:x['exe']=cfg['frame_window_exe']if profile=='smallwindow'else cfg['frame_exe']
  if arm=='candidate':
   x['compare_to']=ntpath.join(r,role,f'gold-{profile}-baseline','out-first.f16')
   if 'frame_exe'in cfg:x['exe']=cfg['frame_window_exe']if profile=='smallwindow'else cfg['frame_exe']
  jobs.append(x)
(p/(role+'-gold.json')).write_text(json.dumps({'name':role+'-gold','files':files+cfg.get('files',[]),'jobs':jobs},indent=2));perf=[]
for profile in cfg.get('profiles',['small900','small1080','original960','original1088']):
 for rnd in [1,2,3]:
  for slot,arm in enumerate(['baseline','candidate','candidate','baseline']):
   x=copy.deepcopy(next(j for j in jobs if j['label']==f'{role}/gold-{profile}-{arm}'));x['label']=f'{role}/perf-{profile}-r{rnd}-{slot}-{arm}';x['arguments']=x['arguments'].replace(' 3 0 ',' 240 0 ').replace(' 0 0 0 0',' 0 1 0 0');x['frame_count']=240;x['edges_only']=1;perf.append(x)
(p/(role+'-perf.json')).write_text(json.dumps({'name':role+'-perf','files':files+cfg.get('files',[]),'jobs':perf},indent=2))
# Verified device-clock host and synthetic fixtures from Phase4; only module paths differ.
src=json.loads(Path('Development/results/aco-hotspot-phase4-20261029/3a-kernel.json').read_text());kjs=[]
for j in src['jobs']:
 x=copy.deepcopy(j);x['label']=x['label'].replace('3a/',role+'/');x['arguments']=x['arguments'].replace(r'D:\DLSSNR-Lab\aco-hotspot-phase4-20261029\baseline\modules',ntpath.join(r,'baseline','modules')).replace(r'D:\DLSSNR-Lab\aco-hotspot-phase4-20261029\3a\modules',ntpath.join(r,role,'modules'))
 if cfg.get('same_host')and 'profile_exe'in cfg:x['exe']=cfg['profile_exe']
 if '-candidate'in x['label']:
  x['env'].update(cfg.get('env',{}))
  if 'profile_exe'in cfg:x['exe']=cfg['profile_exe']
 kjs.append(x)
kfiles=[f for f in src['files']if not f['path'].endswith('.hsaco')or f['path'].endswith('stamp.hsaco')];kfiles += [f for f in files if f['path'].endswith('.hsaco')]+cfg.get('files',[])
(p/(role+'-kernel.json')).write_text(json.dumps({'name':role+'-kernel','files':kfiles,'jobs':kjs},indent=2))
setup=f"$ErrorActionPreference='Stop';$r='{r}'\nif(Test-Path 'D:\\DLSSNR-Lab\\gpu.lock'){{throw 'GPU busy'}}\nforeach($arm in @('baseline','{role}')){{New-Item -ItemType Directory -Force \"$r\\$arm\\modules\"|Out-Null;Copy-Item 'D:\\DLSSNR-Lab\\isa-asm-phase3-20261029\\payload\\HIP\\gfx1201\\*' \"$r\\$arm\\modules\"}}\n"
for n in mods:setup+=f'Copy-Item "$r\\payload\\{role}-{n}" "$r\\{role}\\modules\\{n}"\n'
(p/(role+'-setup.ps1')).write_text(setup)
