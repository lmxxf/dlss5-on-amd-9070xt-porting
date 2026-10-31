from pathlib import Path
import json,copy,re,sys
p=Path(__file__).parent;r=r'D:\DLSSNR-Lab\yami-round5-20261031'
for role in (sys.argv[1:] or ['vit-fix','c512-den','prob']):
 g=json.loads((p/(role+'-gold.json')).read_text());jobs=[]
 for profile in ['original1088','original960','small1080','small900']:
  for rnd in [1,2,3]:
   for slot,arm in enumerate(['baseline','candidate','candidate','baseline']):
    x=copy.deepcopy(next(j for j in g['jobs'] if j['label']==role+'-gold/'+profile+'-'+arm));x['label']=f'{role}-perf/perf-{profile}-r{rnd}-{slot}-{arm}';a=re.findall(r'"[^"]*"|\S+',x['arguments']);a[4]='240';a[8]='1';x['arguments']=' '.join(a);x['frame_count']=240;x['edges_only']=1;jobs.append(x)
 g['jobs']=jobs;g['name']=role+'-perf';(p/(role+'-perf.json')).write_text(json.dumps(g,indent=2))
 src=json.loads(Path('Development/results/ksearch-round4-20261031/final-kernel.json').read_text());kj=[]
 for j in src['jobs']:
  x=copy.deepcopy(j);arm='baseline' if x['label'].endswith('-baseline') else 'candidate';x['label']=x['label'].replace('final-kernel/',role+'-kernel/')
  # Same final profile binary for both arms, original arguments/modules identity replaced only.
  x['exe']=r'D:\DLSSNR-Lab\ksearch-round4-20261031\payload\final-profile.exe'
  args=re.findall(r'"[^"]*"|\S+',x['arguments']);
  for i,a in enumerate(args):
   if 'modules' in a.lower() or ('\\HIP\\gfx1201' in a):args[i]='"'+r+'\\'+('baseline' if arm=='baseline' else ('prob-direct' if role=='prob' else role))+'\\modules"'
  x['arguments']=' '.join(args);kj.append(x)
 fs=[f for f in src['files'] if not f['path'].endswith('.hsaco') or f['path'].endswith('stamp.hsaco')]+g['files']
 (p/(role+'-kernel.json')).write_text(json.dumps({'name':role+'-kernel','files':fs,'jobs':kj},indent=2))
