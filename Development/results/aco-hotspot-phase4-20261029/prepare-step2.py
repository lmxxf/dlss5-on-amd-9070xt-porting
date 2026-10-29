from pathlib import Path
import json,copy,hashlib
p=Path(__file__).parent;r=r'D:\DLSSNR-Lab\aco-hotspot-phase4-20261029';base=json.loads((p/'step1-nmmn.json').read_text());jobs=[]
for profile in ['original1088','small1080']:
 for rnd in range(1,4):
  for role in ['N-control','N-profile','M-control','M-profile']:
   kind=role[0];j=copy.deepcopy(next(j for j in base['jobs']if j['profile']==profile and j['kind']==kind));j['label']=f'step2/{profile}-r{rnd}-{role}';j['arguments']=j['arguments'].replace(' 80 160 ',' 80 32 ').replace('--repeats 160','--repeats 32')
   if role=='N-profile':j['exe']=r+'\\payload\\pure-profile.exe';j['env']['PHASE4_PROFILE']='1'
   if role=='M-profile':j['arguments']+=' --per-layer --dispatch-grid'
   jobs.append(j)
files=base['files']+[{'path':r+'\\payload\\pure-profile.exe','sha256':hashlib.sha256((p/'payload/pure-profile.exe').read_bytes()).hexdigest()}];(p/'step2-jobs.json').write_text(json.dumps({'name':'step2-production-route-event-diagnostics','files':files,'jobs':jobs},indent=2))
