from pathlib import Path
import json,hashlib,copy,re
p=Path(__file__).parent;r=r'D:\DLSSNR-Lab\yami-round4-review-20261031';src=json.loads(Path('Development/results/ksearch-round4-20261031/final-gold.json').read_text())
files=[f for f in src['files'] if not f['path'].endswith('.hsaco')];gold=[];perf=[]
for arm in ['baseline','candidate']:
 for f in Path('Development/results/ksearch-round4-20261031/payload/HIP/gfx1201').glob('*.hsaco'):
  local=p/'compile/candidate'/f.name if arm=='candidate' and f.name=='deep_fast-packed-fast.hsaco' else f
  files.append({'path':r+'\\'+arm+'\\modules\\'+f.name,'sha256':hashlib.sha256(local.read_bytes()).hexdigest()})
for j in src['jobs']:
 if not j['label'].endswith('-candidate'):continue
 profile=j['label'].split('gold-')[1].removesuffix('-candidate')
 for arm in ['baseline','candidate']:
  x=copy.deepcopy(j);x['label']='gold/'+profile+'-'+arm;x.pop('compare_to',None)
  x['arguments']=re.sub(r'"D:\\DLSSNR-Lab\\[^"\n]+\\modules"','"'+r.replace('\\','\\\\')+'\\\\'+arm+'\\\\modules"',x['arguments']) if False else x['arguments']
  args=x['arguments'];parts=re.findall(r'"[^"]*"|\S+',args);parts[6]='"'+r+'\\'+arm+'\\modules"';x['arguments']=' '.join(parts)
  gold.append(x)
  if profile in ['original1088','original960','small1080','small900']:
   for rnd in [1,2,3]:
    for slot,a in enumerate(['baseline','candidate','candidate','baseline']):
     if a!=arm:continue
     y=copy.deepcopy(x);y['label']=f'perf/perf-{profile}-r{rnd}-{slot}-{arm}';ps=parts.copy();ps[4]='240';ps[8]='1';y['arguments']=' '.join(ps);y['frame_count']=240;y['edges_only']=1;perf.append(y)
perf.sort(key=lambda x:x['label'])
for n,js in [('gold',gold),('perf',perf)]: (p/(n+'.json')).write_text(json.dumps({'name':'vit-pair-'+n,'files':files,'jobs':js},indent=2))
s=f'''$ErrorActionPreference='Stop'
$r='{r}'
foreach($arm in @('baseline','candidate')){{New-Item -ItemType Directory -Force "$r\\$arm\\modules"|Out-Null;Copy-Item 'D:\\DLSSNR-Lab\\ksearch-round4-20261031\\payload\\HIP\\gfx1201\\*' "$r\\$arm\\modules"}}
Copy-Item "$r\\compile\\candidate\\deep_fast-packed-fast.hsaco" "$r\\candidate\\modules\\deep_fast-packed-fast.hsaco"
'''
(p/'setup.ps1').write_text(s)
