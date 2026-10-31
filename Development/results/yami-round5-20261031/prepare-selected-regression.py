from pathlib import Path
import json,re,copy,sys
p=Path(__file__).parent;r=r'D:\DLSSNR-Lab\yami-round5-20261031';role=sys.argv[1]if len(sys.argv)>1 else'combined';case=sys.argv[2]if len(sys.argv)>2 else'selected-regression'
g=json.loads((p/'three-arm-long.json').read_text());source_files={};jobs={}
for file in ['multi_reg-gold.json','multi_reg-part2-gold.json']:
 m=json.loads((Path('Development/results/ksearch-round3-20261031')/file).read_text());source_files.update({f['path']:f for f in m['files']if not f['path'].endswith('.hsaco')});jobs.update({j['label']:j for j in m['jobs']})
m=json.loads(Path('Development/results/ksearch-round4-20261031/ae_reuse_reg-gold.json').read_text());source_files.update({f['path']:f for f in m['files']if not f['path'].endswith('.hsaco')});jobs.update({j['label']:j for j in m['jobs']})
source_files.update({f['path']:f for f in g['files']});out=[]
for old,j0 in jobs.items():
 j=copy.deepcopy(j0);arm='baseline'if old.endswith('-baseline')else'candidate';kind,name=old.split('/');name=('reuse-'+name)if kind=='ae_reuse_reg'else name;j['label']=case+'/'+name;j['arm']=arm
 leaf=j['exe'].split('\\')[-1]
 if leaf in ['frame-standard.exe','frame-window.exe']:j['exe']=r'D:\DLSSNR-Lab\ksearch-round4-20261031\payload'+'\\'+leaf
 a=re.findall(r'"[^"]*"|\S+',j['arguments']);assert len(a)>6;a[6]='"'+r+'\\'+('baseline'if arm=='baseline'else role)+'\\modules"';j['arguments']=' '.join(a);j.pop('gold',None)
 if arm=='candidate':j['compare_to']=r+'\\'+case+'\\'+name.removesuffix('-candidate')+'-baseline'
 out.append(j)
assert len(out)==30
for j in out:assert j['exe']in source_files,j['exe']
(p/(case+'.json')).write_text(json.dumps(dict(name=case,files=list(source_files.values()),jobs=out),indent=2)+'\n')
a=json.loads(Path('Development/results/ksearch-round3-20261031/final-build/source-lock.json').read_text());b=json.loads(Path('Development/results/ksearch-round4-20261031/final-build/source-lock.json').read_text())
(p/'regression-host-source-identity.json').write_text(json.dumps({k:dict(round3=a[k],round4=b[k],equal=a[k]==b[k])for k in ['Development/HIP/hip_reference_network.h','Development/HIP/benchmark_live_capture.cpp']},indent=2)+'\n')
print('15 configurations,30 paired jobs; standard/window use b60 hosts; history harness explicitly pinned')
