from pathlib import Path
import json,hashlib,shutil
p=Path(__file__).parent;m=json.loads((p/'start.json').read_text());src=Path(m['source']);dst=Path(m['build_copy']);assert not dst.exists(),'do not overwrite an existing isolated work tree'
for x in json.loads((p/'upstream-source-lock.json').read_text()):assert hashlib.sha256((src/x['path']).read_bytes()).hexdigest()==x['sha256'],x['path']
shutil.copytree(src,dst,ignore=shutil.ignore_patterns('.git','__pycache__'));inc=dst/'phase4-include';inc.mkdir()
for n in ['vulkan','vk_video']:shutil.copytree(Path('/usr/include')/n,inc/n)
g=dst/'toolchain/glslang/bin';g.mkdir(parents=True,exist_ok=True);(g/'glslang').symlink_to('/usr/bin/glslang')
