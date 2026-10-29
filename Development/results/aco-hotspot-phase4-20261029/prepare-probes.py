from pathlib import Path
import json,copy,hashlib
p=Path(__file__).parent;r=r'D:\DLSSNR-Lab\aco-hotspot-phase4-20261029';old=json.loads(Path('Development/results/isa-asm-phase3-20261029/final-gold.json').read_text());jobs=[]
for profile in ['small900','small1080']:
 j=copy.deepcopy(next(x for x in old['jobs']if x['label']==f'final/gold-{profile}-baseline'));j['label']='step1-probe/frame-'+profile;j['exe']=r+'\\payload\\frame-timed.exe';j['startup_compile']=True;jobs.append(j)
m=r'D:\DLSSNR-Lab\competitor-timing-20260930\mz';fx=r'D:\DLSSNR-Lab\sync-network-gap1080-20261006';args=f'--plan {r}\\payload\\original1088.plan --model-pack {m}\\dlssnr.bin --spv-dir {r}\\payload\\native-shaders --host-boundary --reuse --source-width 1920 --source-height 1080 --accumulation fp32 --pipeline-cache {r}\\mochi-native-cache.bin --warmup 1 --repeats 2 --chunk 1 --style 1 --img-seed 0 --in-image {fx}\\fixture\\valid.rgba32f --out-image {{OUT}}\\final.rgba32f'
jobs.append({'label':'step1-probe/mochi-aco-original','exe':r+'\\payload\\mochi-native.exe','arguments':args,'startup_compile':True,'env':{'NR_ACO_DIR':r+'\\payload\\aco','NR_ACO_SHELL_DIR':r+'\\payload\\native-shaders','NR_ACO_CACHE':r+'\\aco-cache'}})
files=[{'path':r+'\\payload\\'+f.name,'sha256':hashlib.sha256(f.read_bytes()).hexdigest()}for f in(p/'payload').glob('*.exe')];(p/'step1-probes.json').write_text(json.dumps({'name':'step1-compatibility-and-timestamp-probes','files':files,'jobs':jobs},indent=2))
f=p/'run-jobs.ps1';s=f.read_text().replace('$limit=15;if(', '$limit=15;if($j.startup_compile){$limit=180};if(');f.write_text(s)
