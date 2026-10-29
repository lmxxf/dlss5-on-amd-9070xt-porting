from pathlib import Path
import json,copy,hashlib,numpy as np
p=Path(__file__).parent;r=r'D:\DLSSNR-Lab\aco-hotspot-phase4-20261029';pay=p/'payload';v=np.fromfile((p/'work/valid.rgba32f') if (p/'work/valid.rgba32f').exists() else (p/'payload/original1088-valid.rgba32f'),dtype='<f4').reshape(1080,1920,4);fixtures={}
for profile,w,h,ph in [('original1088',1920,1080,1088),('small1080',1920,1080,1152),('small900',1600,900,1024)]:
 a=v if w==1920 else v[(np.arange(h)*1080//h)[:,None],(np.arange(w)*1920//w)[None,:]]
 a.astype('<f4').tofile(pay/(profile+'-valid.rgba32f'));np.pad(a,((0,ph-h),(0,0),(0,0)),mode='reflect').astype('<f4').tofile(pay/(profile+'-processing.rgba32f'));fixtures[profile]=(w,h,ph)
base=json.loads(Path('Development/results/isa-asm-phase3-20261029/final-gold.json').read_text());jobs=[];cold=[];files=[];mods=r'D:\DLSSNR-Lab\isa-asm-phase3-20261029\payload\HIP\gfx1201'
for profile,(w,h,ph) in fixtures.items():
 j=next(x for x in base['jobs']if x['label']==f'final/gold-{profile}-baseline');flags=j['flags'];(pay/(profile+'.flags')).write_text(flags)
 n={'exe':r+'\\payload\\pure.exe','arguments':f'"D:\\DLSSNR-Lab\\'+('small-model-20261018' if profile.startswith('small')else 'history-trial-041a-20261006')+f'\\assets" "{mods}" "{r}\\payload\\{profile}.flags" "{r}\\payload\\{profile}-processing.rgba32f" "{{OUT}}" 80 160 {ph}'+(' small'if profile.startswith('small')else ''),'env':{},'startup_compile':True}
 model=r+'\\payload\\dlssnr-small.bin'if profile.startswith('small')else r'D:\DLSSNR-Lab\competitor-timing-20260930\mz\dlssnr.bin'
 m={'exe':r+'\\payload\\mochi-native.exe','arguments':f'--plan {r}\\payload\\{profile}.plan --model-pack {model} --spv-dir {r}\\payload\\native-shaders --host-boundary --reuse --source-width {w} --source-height {h} --accumulation fp32 --pipeline-cache {r}\\native-cache.bin --warmup 80 --repeats 160 --chunk 1 --style 1 --img-seed 0 --in-image {r}\\payload\\{profile}-valid.rgba32f --out-image {{OUT}}\\final.rgba32f','env':{'NR_ACO_DIR':r+'\\payload\\aco','NR_ACO_SHELL_DIR':r+'\\payload\\native-shaders','NR_ACO_CACHE':r+'\\aco-cache'},'startup_compile':True}
 for kind,job in [('N',n),('M',m)]:
  x=copy.deepcopy(job);x['label']='step1-cold/'+profile+'-'+kind;x['arguments']=x['arguments'].replace(' 80 160 ',' 1 2 ').replace('--warmup 80 --repeats 160','--warmup 1 --repeats 2');cold.append(x)
 for rnd in range(1,4):
  for slot,kind in enumerate('NMMN'):
   x=copy.deepcopy(n if kind=='N'else m);x['label']=f'step1-nmmn/{profile}-r{rnd}-{slot}-{kind}';x['profile']=profile;x['round']=rnd;x['kind']=kind;jobs.append(x)
for f in pay.rglob('*'):
 if f.is_file():files.append({'path':r+'\\payload\\'+str(f.relative_to(pay)).replace('/',chr(92)),'sha256':hashlib.sha256(f.read_bytes()).hexdigest()})
for h,n in(l.split()for l in Path('hip/SHA256SUMS').read_text().splitlines()):
 if n.startswith('gfx1201/'):files.append({'path':r'D:\DLSSNR-Lab\isa-asm-phase3-20261029\payload\HIP'+chr(92)+n.replace('/',chr(92)),'sha256':h})
for name,js in [('step1-cold',cold),('step1-nmmn',jobs)]:
 (p/(name+'.json')).write_text(json.dumps({'name':name,'files':files,'jobs':js},indent=2))
split=[]
for profile in ['small900','small1080']:
 for rnd in range(1,4):
  for slot,arm in enumerate(['baseline','candidate','candidate','baseline']):
   j=copy.deepcopy(next(x for x in base['jobs']if x['label']==f'final/gold-{profile}-baseline'));j['label']=f'step1-split/perf-{profile}-r{rnd}-{slot}-{arm}';j['arguments']=j['arguments'].replace(' 3 0 ',' 240 0 ').replace(' 0 0 0 0',' 0 1 0 0');j['frame_count']=240;j['edges_only']=1
   if arm=='candidate':j['exe']=r+'\\payload\\frame-timed.exe'
   split.append(j)
(p/'step1-split.json').write_text(json.dumps({'name':'step1-split','files':files,'jobs':split},indent=2))
