from pathlib import Path
import json,hashlib,copy,re,subprocess
p=Path(__file__).parent;r=r'D:\DLSSNR-Lab\yami-round5-20261031';f=p/'selected-regression.json';old=p/'selected-regression-original-manifest.json'
if not old.exists():old.write_bytes(f.read_bytes())
m=json.loads(old.read_text());exe=r+'\\frame-history-r5.exe';m['files'].append(dict(path=exe,sha256=hashlib.sha256((p/'frame-history-r5.exe').read_bytes()).hexdigest()))
for j in m['jobs']:
 if j['exe'].endswith('frame-history.exe'):
  j['exe']=exe;j['required_stderr']=['R5_HISTORY_STATE temporal=1 motion=1 reset=0 use_history=1'];j['history_harness_note']='New deterministic changing-input1280x720 harness, both arms identical; not a claimed byte-identical rebuild of missing Round3 harness.'
for profile in ['original1088','small1080']:
 j=copy.deepcopy(next(j for j in m['jobs']if j['label']=='selected-regression/mode1-'+profile+'-baseline'));j['label']='selected-regression/history-reset-'+profile;a=re.findall(r'"[^"]*"|\S+',j['arguments']);a[7]='1';j['arguments']=' '.join(a);j['required_stderr']=['R5_HISTORY_STATE temporal=1 motion=1 reset=1 use_history=0'];j.pop('compare_to',None);m['jobs'].append(j)
f.write_text(json.dumps(m,indent=2)+'\n');subprocess.run(['python3',str(p/'pin-inputs.py'),f.name],check=True)
(p/'selected-regression-preflight-note.md').write_text('Initial regression manifest failed before any GPU job because it retained an unused deleted Round3 r3-profile.exe pin. Read-only inventory also found the actually required old frame-history.exe missing. Unused pins were removed, actual input/model-tree hashes were added, and a new current-source changing-input history harness was built, explicitly paired for both arms. Two reset-every-frame controls were added to verify history actually affects output. Earlier performance manifests did not pin every input fixture; current input hashes are recorded post hoc for those batches and must not be described as pre-run hashes. New qualification batches pin inputs before execution.\n')
