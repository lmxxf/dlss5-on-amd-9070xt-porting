from pathlib import Path
import json,hashlib,copy
P=Path(__file__).parent;remote=r'D:\DLSSNR-Lab\isa-asm-audit-20261028';d=json.loads((P.parent/'backfeed-20261028/final-default.json').read_text());files=list(d['files']);jobs=[]
for f in d['files']:
 if 'gap-phase6-20261023\\knife1\\modules\\' in f['path']:
  x=copy.deepcopy(f);x['path']=x['path'].replace(r'gap-phase6-20261023\knife1\modules',r'isa-asm-audit-20261028\asmproof\modules')
  if x['path'].endswith('c32-wave1-fast.hsaco'):x['sha256']=hashlib.sha256((P/'work/roundtrip/patched.hsaco').read_bytes()).hexdigest()
  files.append(x)
for j in d['jobs']:
 profile=j['label'].split('/final-')[1].removesuffix('-candidate');j['label']='asmproof/gold-'+profile;j['arguments']=j['arguments'].replace(r'gap-phase6-20261023\knife1\modules',r'isa-asm-audit-20261028\asmproof\modules');j['env']={};j['compare_to']=remote+f'\\upwide\\gold-{profile}-baseline\\out-first.f16';jobs.append(j)
(P/'asmproof-gold.json').write_text(json.dumps({'name':'asm-real-edit-loader-gold','files':files,'jobs':jobs},indent=2))
