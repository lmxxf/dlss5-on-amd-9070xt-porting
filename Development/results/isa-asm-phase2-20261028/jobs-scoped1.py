from pathlib import Path
import json,hashlib,copy,ntpath
P=Path(__file__).parent;R=P.parents[2];remote=r'D:\DLSSNR-Lab\isa-asm-phase2-20261028';base=json.loads((P/'knife1-gold.json').read_text());files=[f for f in base['files'] if not f['path'].endswith(('.exe','.hsaco'))];sums={n:h for h,n in (l.split() for l in (R/'hip/SHA256SUMS').read_text().splitlines())}
for n,h in sums.items():
 if n.startswith('gfx1201/'):
  name=n.split('/')[1];sha=hashlib.sha256((P/'work/scoped-final-gfx1201.hsaco').read_bytes()).hexdigest() if name=='c512-m32-mh.hsaco'else h;files.append({'path':ntpath.join(remote,'knife1','scoped-modules',name),'sha256':sha})
for g in ['standard','window']:
 name='frame-scoped-'+g+'.exe';files.append({'path':remote+'\\'+name,'sha256':hashlib.sha256((P/'work'/name).read_bytes()).hexdigest()})
jobs=[]
for j in base['jobs']:
 x=copy.deepcopy(j);profile=x['label'].split('/gold-')[1].rsplit('-',1)[0];arm=x['label'].rsplit('-',1)[1];x['label']='knife1/final-gold-'+profile+'-'+arm;x['exe']=remote+'\\frame-scoped-'+('window' if profile=='smallwindow' else 'standard')+'.exe';x['env']={'NR_C512_VFRAG':str(int(arm=='candidate'))};x['arguments']=x['arguments'].replace(r'D:\DLSSNR-Lab\gap-phase6-20261023\knife1\modules',remote+r'\knife1\scoped-modules').replace(remote+r'\knife1\modules',remote+r'\knife1\scoped-modules');x.pop('compare_to',None)
 if arm=='candidate':x['compare_to']=remote+f'\\knife1\\final-gold-{profile}-baseline\\out-first.f16'
 x['required_stderr']=[r'C512_VFRAG available=1 calls='+('64' if profile=='original1088' and arm=='candidate'else'0')];jobs.append(x)
out={'name':'phase2-scoped-vfrag-final-gold','files':files,'jobs':jobs};(P/'knife1-final-gold.json').write_text(json.dumps(out,indent=2));perf=[]
for rnd in range(1,4):
 for slot,arm in enumerate(['baseline','candidate','candidate','baseline']):
  j=copy.deepcopy(next(x for x in jobs if x['label']==f'knife1/final-gold-original1088-{arm}'));j['label']=f'knife1/final-perf-original1088-r{rnd}-{slot}-{arm}';j['arguments']=j['arguments'].replace(' 3 0 ',' 240 0 ').replace(' 0 0 0 0',' 0 1 0 0');j['frame_count']=240;j['edges_only']=1;j['required_stderr']=[r'C512_VFRAG available=1 calls='+('3856' if arm=='candidate' else'0')];perf.append(j)
out.update(name='phase2-scoped-vfrag-final-perf',jobs=perf);(P/'knife1-final-perf.json').write_text(json.dumps(out,indent=2))
