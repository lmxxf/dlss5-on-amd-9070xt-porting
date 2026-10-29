from pathlib import Path
import json,hashlib,copy,ntpath,subprocess
p=Path(__file__).parent;r=r'D:\DLSSNR-Lab\c32-stage-phase5b-20261029';q=p.parent/'aco-hotspot-phase4-20261029';r4=r'D:\DLSSNR-Lab\aco-hotspot-phase4-20261029';mods={f.name:str(f)for f in(p/'payload/HIP/gfx1201').glob('*.hsaco')};assert len(mods)==40
names=['frame-standard.exe','frame-window.exe','pure.exe'];files=[{'path':ntpath.join(r,'payload',n),'sha256':hashlib.sha256((p/'payload'/n).read_bytes()).hexdigest()}for n in names];cfg={'role':'final','modules':mods,'frame_exe':ntpath.join(r,'payload',names[0]),'frame_window_exe':ntpath.join(r,'payload',names[1]),'profiles':['original1152','original1088','original960','small1080','small900','small720','smallwindow'],'files':files};(p/'final-config.json').write_text(json.dumps(cfg,indent=2));subprocess.run(['python3',str(p/'make-case.py'),str(p/'final-config.json')],check=True)
stock=json.loads((p/'start.json').read_text())['modules']
for name in ['final-gold.json','final-perf.json']:
 f=p/name;d=json.loads(f.read_text())
 for x in d['files']:
  if '\\baseline\\modules\\'in x['path']:x['sha256']=stock['gfx1201/'+ntpath.basename(x['path'])]
 f.write_text(json.dumps(d,indent=2))
# Only runtime files still present after Phase4 cleanup are required; no .pre.comp build artifacts.
src=json.loads((q/'step1-nmmn.json').read_text());jobs=[];needed=set()
for j in src['jobs']:
 x=copy.deepcopy(j);x['label']=x['label'].replace('step1-nmmn/','final-nmmn/');kind=x['kind'];profile=x['profile'];d=q/'step1-nmmn/evidence'/f'{profile}-r1-{0 if kind=="N"else 1}-{kind}';raw=json.loads((d/'raw-hashes.json').read_text(encoding='utf-8-sig'));raw=[raw]if isinstance(raw,dict)else raw;x['gold']=raw[0]['sha256']
 if kind=='N':x['exe']=ntpath.join(r,'payload','pure.exe');x['arguments']=x['arguments'].replace(r'D:\DLSSNR-Lab\isa-asm-phase3-20261029\payload\HIP\gfx1201',ntpath.join(r,'payload','HIP','gfx1201'))
 else:
  x['arguments']=x['arguments'].replace(r4+'\\native-cache.bin',r+'\\native-cache.bin');x['env']['NR_ACO_CACHE']=r+'\\aco-cache'
 jobs.append(x)
# Explicit runtime input locks, with NRP/SPV/index files for ACO loading.
nfiles=list(files)
for f in(q/'payload').rglob('*'):
 if f.is_file()and(f.suffix in ['.plan','.flags']or f.name.endswith(('-valid.rgba32f','-processing.rgba32f'))or f.name in ['mochi-native.exe','dlssnr-small.bin']or 'aco'in f.relative_to(q/'payload').parts or 'native-shaders'in f.relative_to(q/'payload').parts):nfiles.append({'path':ntpath.join(r4,'payload',str(f.relative_to(q/'payload')).replace('/',chr(92))),'sha256':hashlib.sha256(f.read_bytes()).hexdigest()})
for f in(p/'payload/HIP/gfx1201').glob('*.hsaco'):nfiles.append({'path':ntpath.join(r,'payload','HIP','gfx1201',f.name),'sha256':hashlib.sha256(f.read_bytes()).hexdigest()})
# Locked model pack is read only.
nfiles.append({'path':r'D:\DLSSNR-Lab\competitor-timing-20260930\mz\dlssnr.bin','sha256':'2b41c888cf4155b8958c665ba64018ab0bd25c85fc71a2b6db86d0d04d1f7fbd'})
(p/'final-nmmn.json').write_text(json.dumps({'name':'phase5-final-three-model-NMMN','files':nfiles,'jobs':jobs},indent=2))
# Formal runtime DLL smoke, same isolated assets and explicit Small normal path.
d=json.loads((p.parent/'isa-asm-phase3-20261029/smoke.json').read_text());
for f in d['files']:
 if f['path'].endswith('LmxxfNrRuntime.dll'):f['path']=ntpath.join(r,'payload','LmxxfNrRuntime.dll');f['sha256']=hashlib.sha256((p/'payload/LmxxfNrRuntime.dll').read_bytes()).hexdigest()
for j in d['jobs']:
 j['arguments']=j['arguments'].replace('isa-asm-phase3-20261029','c32-stage-phase5b-20261029');j['label']='smoke/runtime-final';j['startup_compile']=True;j['env']['DLSS5_NETWORK_HEIGHT']='1080';j['env']['LMXXF_SHADER_DIR']=ntpath.join(r,'payload','shaders');j['flags']=j['flags'].replace('DLSS5_NETWORK_HEIGHT=900','DLSS5_NETWORK_HEIGHT=1080')
(p/'smoke.json').write_text(json.dumps(d,indent=2))

for f in Path('shaders').rglob('*'):
 if f.is_file():d['files'].append({'path':ntpath.join(r,'payload','shaders',str(f.relative_to('shaders')).replace('/',chr(92))),'sha256':hashlib.sha256(f.read_bytes()).hexdigest()})
(p/'smoke.json').write_text(json.dumps(d,indent=2))

# Prove the production-default fusion route for every final model/geometry.
for name in ['final-gold.json','final-perf.json']:
 f=p/name;d=json.loads(f.read_text())
 for j in d['jobs']:
  if j['label'].endswith('-candidate'):j['required_stderr']=[r'C32_POOL_FUSED calls=[1-9]\d*']
 f.write_text(json.dumps(d,indent=2))
f=p/'final-nmmn.json';d=json.loads(f.read_text())
for j in d['jobs']:
 if j['kind']=='N':j['required_stderr']=[r'C32_POOL_FUSED calls=[1-9]\d*']
f.write_text(json.dumps(d,indent=2))
f=p/'smoke.json';d=json.loads(f.read_text())
for j in d['jobs']:j['required_stderr']=[r'C32_POOL_FUSED calls=[1-9]\d*']
f.write_text(json.dumps(d,indent=2))
# Same diagnostic host in both arms; baseline is the locked stock module set.
f=p/'final-kernel.json';d=json.loads(f.read_text());profile_exe=ntpath.join(r,'payload','final-profile.exe');d['files'].append({'path':profile_exe,'sha256':hashlib.sha256((p/'payload/final-profile.exe').read_bytes()).hexdigest()})
for x in d['files']:
 if '\\baseline\\modules\\'in x['path']:x['sha256']=stock['gfx1201/'+ntpath.basename(x['path'])]
for j in d['jobs']:
 candidate=j['label'].endswith('-candidate');j['exe']=profile_exe;j['env']['NR_C32_POOL']='1'if candidate else'0'
 if candidate:
  count=int(j['env']['PHASE4_DISPATCHES']);j['env']['PHASE4_DISPATCHES']=str(count-1);j['arguments']=j['arguments'].replace(f'80 {count*2} ',f'80 {(count-1)*2} ');j['required_stderr']=[r'C32_POOL_FUSED calls=[1-9]\d*']
f.write_text(json.dumps(d,indent=2))
