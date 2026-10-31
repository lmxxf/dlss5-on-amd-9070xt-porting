from pathlib import Path
import json,hashlib,subprocess
p=Path(__file__).parent;r=r'D:\DLSSNR-Lab\yami-round5-20261031';old=r+'\\candidate\\modules';new=r+'\\mh-prob\\modules'
x=json.loads((p/'r2d-gold.json').read_text());x['name']='mh-prob-gold';mods=['c64-wave2','c64-wave2-fast']
for f in x['files']:
 if old in f['path']:
  f['path']=f['path'].replace(old,new);name=f['path'].split('\\')[-1];source=p/'mh-prob/build/gfx1201'/name if name.removesuffix('.hsaco') in mods else Path('Development/results/ksearch-round4-20261031/payload/HIP/gfx1201')/name
  f['sha256']=hashlib.sha256(source.read_bytes()).hexdigest()
for j in x['jobs']:j['label']=j['label'].replace('r2d-gold','mh-prob-gold');j['arguments']=j['arguments'].replace(old,new)
(p/'mh-prob-gold.json').write_text(json.dumps(x,indent=2)+'\n')
setup=["$ErrorActionPreference='Stop'", "if(Test-Path 'D:\\DLSSNR-Lab\\gpu.lock'){throw 'busy'}",f'New-Item -ItemType Directory -Force "{new}"|Out-Null',f'Copy-Item "D:\\DLSSNR-Lab\\ksearch-round4-20261031\\payload\\HIP\\gfx1201\\*" "{new}"']
for mod in mods:setup.append(f'Copy-Item "{r}\\mh-prob-{mod}.hsaco" "{new}\\{mod}.hsaco"')
(p/'mh-prob-setup.ps1').write_text('\n'.join(setup)+'\n')
subprocess.run(['python3',str(p/'prepare-perf.py'),'mh-prob'],check=True)
