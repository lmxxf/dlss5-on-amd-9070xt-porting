from pathlib import Path
import shutil,json,hashlib
p=Path(__file__).parent;root=Path('/home/lmxxf/work/dlss5-phase4-mochi-build');out=p/'payload';shutil.copytree(root/'build/windows/rdna4/network',out/'native-shaders',dirs_exist_ok=True);shutil.copytree(root/'build/aco-shaders',out/'aco/shaders',dirs_exist_ok=True);shutil.copytree(root/'windows/data/aco/records',out/'aco/records',dirs_exist_ok=True);shutil.copyfile(root/'windows/data/aco/shell-aliases.txt',out/'aco/shell-aliases.txt')
rows=[]
for f in(out/'aco/shaders').glob('g_*.spv'):
 h=1469598103934665603
 for b in f.read_bytes():h=((h^b)*1099511628211)&((1<<64)-1)
 rec=out/f'aco/records/{h:016x}.nrp';assert rec.exists();rows.append({'shader':f.name,'record':rec.name,'sha256':hashlib.sha256(f.read_bytes()).hexdigest()})
(p/'aco-record-identity.json').write_text(json.dumps(rows,indent=2));shutil.copyfile('/home/lmxxf/work/dlssnr-310916-analysis/extracted-models/dlssnr-small.bin',out/'dlssnr-small.bin')
