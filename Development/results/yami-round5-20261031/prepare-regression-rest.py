from pathlib import Path
import json,copy,re,hashlib,subprocess
p=Path(__file__).parent;r=r'D:\DLSSNR-Lab\yami-round5-20261031';m=json.loads((p/'selected-regression.json').read_text());m['name']='selected-regression-rest';jobs=[];exe=r+'\\frame-history-r5-state.exe';m['files'].append(dict(path=exe,sha256=hashlib.sha256((p/'frame-history-r5-state.exe').read_bytes()).hexdigest()))
for j0 in m['jobs']:
 name=j0['label'].split('/')[-1]
 if name.startswith(('fast0-','ae1-','ae3-','history-reset-')):continue
 j=copy.deepcopy(j0);j['label']=j['label'].replace('selected-regression/','selected-regression-rest/')
 if j.get('compare_to'):j['compare_to']=j['compare_to'].replace('selected-regression\\','selected-regression-rest\\')
 if j['exe'].endswith('frame-history-r5.exe'):
  j['exe']=exe;j['required_stderr']=['fast_enabled=1 fast_use=1'];j['history_harness_note']='MODE1 is fast_history, not legacy r.temporal; diagnostic now reports the actual path.'
 jobs.append(j)
for profile in ['original1088','small1080']:
 origin=next(j for j in jobs if j['label']=='selected-regression-rest/mode1-'+profile+'-baseline')
 for reset in [False,True]:
  j=copy.deepcopy(origin);j['label']='selected-regression-rest/history-'+('reset-'if reset else'fixed-')+profile;j['env']['NR_R5_HISTORY_FIXED_SEED']='1';a=re.findall(r'"[^"]*"|\S+',j['arguments']);a[7]='1'if reset else'0';j['arguments']=' '.join(a);j['required_stderr']=['fast_enabled=1 fast_use='+('0'if reset else'1')+' fast_seed=0'];jobs.append(j)
m['jobs']=jobs;assert len(jobs)==22;f=p/'selected-regression-rest.json';f.write_text(json.dumps(m,indent=2)+'\n');subprocess.run(['python3',str(p/'pin-inputs.py'),f.name],check=True)
