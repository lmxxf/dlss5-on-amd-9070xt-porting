from pathlib import Path
import json,subprocess,hashlib,shutil
p=Path(__file__).parent;old=p.parent/'aco-hotspot-phase4-20261029'
for n in ['run-jobs.ps1','collect.ps1','collect-local.py','cleanup-case.ps1','analyze.py','analyze3-kernel.py']:
 (p/n).write_text((old/n).read_text().replace('aco-hotspot-phase4-20261029','c32-stage-phase5-20261029'))
(p/'.gitignore').write_text('/work/\n/payload/\n/**/evidence/\n__pycache__/\n');(p/'.gitattributes').write_text('*.patch whitespace=-blank-at-eol,-blank-at-eof,-space-before-tab\n');(p/'work').mkdir(exist_ok=True);(p/'payload').mkdir(exist_ok=True)
(p/'start.json').write_text(json.dumps({'commit':subprocess.check_output(['git','rev-parse','HEAD'],text=True).strip(),'manifest_sha256':hashlib.sha256(Path('hip/SHA256SUMS').read_bytes()).hexdigest(),'modules':{n:h for h,n in(l.split()for l in Path('hip/SHA256SUMS').read_text().splitlines())}},indent=2))
# Recipe reader needs the shared types even when selecting one LLVM row.
d=p/'work/Development/HIP';d.mkdir(parents=True,exist_ok=True);shutil.copyfile('Development/HIP/swin_persistent_types.h',d/'swin_persistent_types.h')
