from pathlib import Path
import json,hashlib,copy
p=Path(__file__).parent;r=r'D:\DLSSNR-Lab\yami-round5-20261031';old=r+'\\candidate\\modules'
setup=["$ErrorActionPreference='Stop'",f"$r='{r}'"]
for role,mod,local in [('vit-fix','deep_fast-packed-fast.hsaco',p/'vit-fix-compile/deep_fast-packed-fast.hsaco'),('c512-den','c512-m32-mh.hsaco',p/'c512-compile/candidate/c512-m32-mh.hsaco')]:
 x=json.loads((p/'r2d-gold.json').read_text());new=r+'\\'+role+'\\modules';x['name']=role+'-gold'
 for f in x['files']:
  if old in f['path']:
   f['path']=f['path'].replace(old,new);name=f['path'].split('\\')[-1];sf=local if name==mod else Path('Development/results/ksearch-round4-20261031/payload/HIP/gfx1201')/name;f['sha256']=hashlib.sha256(sf.read_bytes()).hexdigest()
 for j in x['jobs']:j['label']=j['label'].replace('r2d-gold',role+'-gold');j['arguments']=j['arguments'].replace(old,new)
 (p/(role+'-gold.json')).write_text(json.dumps(x,indent=2))
 source=r+'\\vit-fix-compile\\'+mod if role=='vit-fix' else r+'\\c512-compile\\candidate\\'+mod
 setup += [f'New-Item -ItemType Directory -Force "{new}"|Out-Null',f'Copy-Item "D:\\DLSSNR-Lab\\ksearch-round4-20261031\\payload\\HIP\\gfx1201\\*" "{new}"',f'Copy-Item "{source}" "{new}\\{mod}"']
(p/'extra-setup.ps1').write_text('\n'.join(setup))
