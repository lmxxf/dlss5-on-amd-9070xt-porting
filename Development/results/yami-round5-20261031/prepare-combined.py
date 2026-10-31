from pathlib import Path
import json,hashlib,copy,re
p=Path(__file__).parent;r=r'D:\DLSSNR-Lab\yami-round5-20261031';old=r+'\\c512-prob\\modules';new=r+'\\combined\\modules';x=json.loads((p/'c512-prob-gold.json').read_text());x['name']='combined-gold'
for f in x['files']:
 if old in f['path']:
  f['path']=f['path'].replace(old,new)
  if f['path'].endswith('deep_fast-packed-fast.hsaco'):f['sha256']=hashlib.sha256((p/'vit-fix-compile/deep_fast-packed-fast.hsaco').read_bytes()).hexdigest()
for j in x['jobs']:j['label']=j['label'].replace('c512-prob-gold/','combined-gold/');j['arguments']=j['arguments'].replace(old,new)
(p/'combined-gold.json').write_text(json.dumps(x,indent=2)+'\n');y=copy.deepcopy(x);y['name']='combined-long';y['jobs']=[]
for profile in ['original1088','original1152','original960','smallwindow','small720','small900','small1080']:
 for rnd in [1,2,3]:
  for slot,arm in enumerate(['baseline','candidate','candidate','baseline']):
   j=copy.deepcopy(next(j for j in x['jobs']if j['label']=='combined-gold/'+profile+'-'+arm));j['label']=f'combined-long/perf-{profile}-r{rnd}-{slot}-{arm}';a=re.findall(r'"[^"]*"|\S+',j['arguments']);a[4]='800';a[8]='1';j['arguments']=' '.join(a);j['frame_count']=800;j['edges_only']=1;y['jobs'].append(j)
(p/'combined-long.json').write_text(json.dumps(y,indent=2)+'\n')
(p/'combined-setup.ps1').write_text("$ErrorActionPreference='Stop'\nif(Test-Path 'D:\\DLSSNR-Lab\\gpu.lock'){throw 'busy'}\n"+f'New-Item -ItemType Directory -Force "{new}"|Out-Null\nCopy-Item "D:\\DLSSNR-Lab\\ksearch-round4-20261031\\payload\\HIP\\gfx1201\\*" "{new}"\nCopy-Item "{r}\\c512-prob-compile\\c512-m32-mh.hsaco" "{new}\\c512-m32-mh.hsaco"\nCopy-Item "{r}\\vit-fix-compile\\deep_fast-packed-fast.hsaco" "{new}\\deep_fast-packed-fast.hsaco"\n')
(p/'combined-plan.md').write_text('Stock-host combined lossless candidate, fixed before measurements\n\nCombine the fixed ViT perm lowering with C512 probability-clamp-only removal. Both have independently passed seven Gold profiles; the former has measured100MHz attention gains, the latter retainedSmall1080 mean/p99 gains in an independentlongbatch. No new host, export, runtime selector, or function-map entry. Do not add old timings to predict combined results. C512+denominator combination was rejected (+2.900us mean,+18.833us p99 vsclamp-only,3/3meansregress), so it is excluded.\n\nRun seven paired Gold then one fixedseven-tier3ABBA,800frames/slot,discard160,3840samples/arm. Keep15swatchdog; no other remote operations while timing. Primary benefit gate:Small1080all3mean/p99negative andOriginal1088pooledmeannegative; sum of seven pooledmean deltas must be negative. Across all sevenprofiles, no pooledmeanregression>0.5% orpooledp99regression>1%, norall3roundmeansregress>0.5%. These are predeclared engineering noninferiority thresholds, not a claim of seven strict wins or statistical proof of zero performance regression. If gates fail do not promote this binary or repeat it to select a favorable run.\n')
