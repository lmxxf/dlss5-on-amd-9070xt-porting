from pathlib import Path
import json,hashlib,copy,re,shutil,subprocess
p=Path(__file__).parent;r=r'D:\DLSSNR-Lab\yami-round5-20261031';stock=Path('Development/results/ksearch-round4-20261031/payload/HIP');roles={'base':'production-combined','norm':'production-combined-norm900','full':'production-combined-norm900-c256'};g0=json.loads((p/'r2d-gold.json').read_text());refs=json.loads((p/'c32-reduction-gold-reference.json').read_text())['hashes'];files={f['path']:f for f in g0['files']if '\\candidate\\modules\\'not in f['path']};gold=[];setup=["$ErrorActionPreference='Stop'","if(Test-Path 'D:\\DLSSNR-Lab\\gpu.lock'){throw 'busy'}"]
for arm,source in roles.items():
 dest=p/('final-'+arm)/'HIP';shutil.copytree(stock,dest,dirs_exist_ok=True)
 changed=['deep_fast-packed-fast','c512-m32-mh']
 for arch in ['gfx1201','gfx1200']:
  for mod in ['deep_fast-packed-fast','c512-m32-mh']:shutil.copyfile(p/'production-comgr'/arch/mod/(mod+'.hsaco'),dest/arch/(mod+'.hsaco'))
  if arm in ['norm','full']:
   for mod in ['c32-wave1-fast','c32-wave1-fast-norm900']:shutil.copyfile(p/source/'build'/arch/(mod+'.hsaco'),dest/arch/(mod+'.hsaco'))
  if arm=='full':
   for mod in ['c64-wave2','c64-wave2-fast']:shutil.copyfile(p/source/'build'/arch/(mod+'.hsaco'),dest/arch/(mod+'.hsaco'))
 if arm in ['norm','full']:changed+=['c32-wave1-fast','c32-wave1-fast-norm900']
 if arm=='full':changed+=['c64-wave2','c64-wave2-fast']
 remote=r+'\\final-'+arm+'\\modules';setup+=[f'New-Item -ItemType Directory -Force "{remote}"|Out-Null',f'Copy-Item "D:\\DLSSNR-Lab\\ksearch-round4-20261031\\payload\\HIP\\gfx1201\\*" "{remote}"']
 for mod in changed:setup.append(f'Copy-Item "{r}\\final-{arm}-{mod}.hsaco" "{remote}\\{mod}.hsaco"')
 for f in (dest/'gfx1201').glob('*.hsaco'):files[remote+'\\'+f.name]=dict(path=remote+'\\'+f.name,sha256=hashlib.sha256(f.read_bytes()).hexdigest())
 for j0 in g0['jobs']:
  if not j0['label'].endswith('-candidate'):continue
  j=copy.deepcopy(j0);prof=j['label'].split('/')[-1].removesuffix('-candidate');j['label']='final-panel-gold/'+arm+'-'+prof;j['arguments']=j['arguments'].replace(r+'\\candidate\\modules',remote);j['gold']=refs[prof];j.pop('compare_to',None);gold.append(j)
(p/'final-panel-setup.ps1').write_text('\n'.join(setup)+'\n');m=dict(name='final-panel-gold',files=list(files.values()),jobs=gold);(p/'final-panel-gold.json').write_text(json.dumps(m,indent=2)+'\n')
jobs=[];arms=['stock','base','norm','full']
for prof in ['original1088','original1152','original960','smallwindow','small720','small900','small1080']:
 for rnd in [1,2,3]:
  seq=arms[rnd-1:]+arms[:rnd-1];seq+=seq[::-1]
  for slot,arm in enumerate(seq):
   j=copy.deepcopy(next(j for j in g0['jobs']if j['label']=='r2d-gold/'+prof+'-baseline'))if arm=='stock'else copy.deepcopy(next(j for j in gold if j['label']=='final-panel-gold/'+arm+'-'+prof))
   j['label']=f'final-panel-long/perf-{prof}-r{rnd}-{slot}-{arm}';a=re.findall(r'"[^"]*"|\S+',j['arguments']);a[4]='800';a[8]='1';j['arguments']=' '.join(a);j['frame_count']=800;j['edges_only']=1;j['gold']=refs[prof];j.pop('compare_to',None);jobs.append(j)
m=dict(name='final-panel-long',files=list(files.values()),jobs=jobs);assert len(jobs)==168;(p/'final-panel-long.json').write_text(json.dumps(m,indent=2)+'\n')
subprocess.run(['python3',str(p/'pin-inputs.py'),'final-panel-gold','final-panel-long'],check=True)
(p/'final-panel-plan.md').write_text('''# Final payload qualification — fixed before execution

A stock b60; B fixed ViT perm + C512 probability clamp removal; C B + exact guarded four-accumulator dot2 ONLY in the existing norm900 module; D C + C256 one-plane/RC-refresh modules. Main C32 module is unchanged. Canonical guarded sources are compiled; gfx1201 code/metadata match the independently tested candidates; gfx1200 executable code is unchanged.

Seven profiles, three rotated symmetric eight-slot rounds, 800 frames/slot discard160,3840samples/arm/profile,168slots. Actual input fixtures and full asset trees are SHA-pinned before the run. No other remote operations during timing. All candidates first pass seven staticGold references. Earlier batches and failures remain intact.

Qualification contract (engineering thresholds, not proof of no regression):
- Every accepted candidate: all7 pooledmean regression<=0.5%, pooledp99 regression<=1%, no profile with all3 roundmeans regression>0.5%; sum of7 mean deltas<0; Small1080 all3 mean and p99 improve.
- C/D additionally: Original960 all3 mean and p99 improve versus A.
- Prefer C over B only if C versus B wins Original960 mean all3 rounds and pooledp99, with all7 noninferiority bounds intact.
- Prefer D over C only if D versus C wins mean all3 rounds on BOTH Original1088 and Small1080, with pooledp99 not worse on both and all7 bounds intact.
- Evaluate each candidate against its own observations. If none qualifies, retain b60. No repeat of this identical payload to erase unfavorable data.

These target-specific gates reflect the measured specialization: norm900 targets Original960 among standard profiles. They do not require untargeted profiles to show fictitious speedups. Report every positive delta, and never label a noninferiority result “7/7 strict wins”. Any chosen payload still needs full configuration/ABI validation before integration.
''')
print('Prepared3payloads,21Goldjobs,168performancejobs')
