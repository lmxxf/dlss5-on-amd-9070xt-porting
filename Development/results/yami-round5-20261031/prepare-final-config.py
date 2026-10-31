from pathlib import Path
import json,copy,re,sys,subprocess
p=Path(__file__).parent;r=r'D:\DLSSNR-Lab\yami-round5-20261031';arm=sys.argv[1];assert arm in ['base','norm','c256','full','minimal'];case='final-config-'+arm
first=json.loads((p/'selected-regression.json').read_text());rest=json.loads((p/'selected-regression-rest.json').read_text());panel=json.loads((p/'final-five-long.json').read_text());files={f['path']:f for f in first['files']+rest['files']+panel['files']+(json.loads((p/'minimal-gold.json').read_text())['files']if arm=='minimal'else[])};jobs=[]
for m in [first,rest]:
 for j0 in m['jobs']:
  name=j0['label'].split('/')[-1]
  if m is first and not name.startswith(('fast0-','ae1-','ae3-')):continue
  if name.startswith('history-'):continue
  j=copy.deepcopy(j0);j['label']=case+'/'+name
  if name.endswith('-candidate'):
   a=re.findall(r'"[^"]*"|\S+',j['arguments']);a[6]='"'+r+'\\final-'+arm+'\\modules"';j['arguments']=' '.join(a);j['compare_to']=r+'\\'+case+'\\'+name.removesuffix('-candidate')+'-baseline'
  jobs.append(j)
assert len(jobs)==30
# Exercise the actually changed norm900 route under more than the default profile.
for srcname,dstname in [('fast0-original1088','fast0-original960'),('reuse-ae1-original1088','reuse-ae1-original960'),('reuse-ae3-original1088','reuse-ae3-original960'),('mode1-original1088','mode1-original960'),('f9-switch-1080','f9-switch-900')]:
 for side in ['baseline','candidate']:
  j=copy.deepcopy(next(j for j in jobs if j['label']==case+'/'+srcname+'-'+side));j['label']=case+'/'+dstname+'-'+side;j['flags']+='\nDLSS5_NETWORK_HEIGHT=900\n';j['profile']='original960';j.pop('expected_processing',None);j.pop('expected_tokens',None)
  if side=='candidate':j['compare_to']=r+'\\'+case+'\\'+dstname+'-baseline'
  j['required_stderr']=j.get('required_stderr',[])+(['c32_norm900 scope=0 loaded=0 .*processing=1600x960 fast_numeric=0']if srcname.startswith('fast0-')else['c32_norm900 scope=1 loaded=1 .*processing=1600x960']if not srcname.startswith('f9-')else[])
  jobs.append(j)
assert len(jobs)==40;f=p/(case+'.json');f.write_text(json.dumps(dict(name=case,files=list(files.values()),jobs=jobs),indent=2)+'\n');subprocess.run(['python3',str(p/'pin-inputs.py'),f.name],check=True)
