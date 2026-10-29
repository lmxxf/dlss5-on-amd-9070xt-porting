from pathlib import Path
import json,hashlib
p=Path(__file__).parent;root=Path('/home/lmxxf/work/dlss5-mochi-008');rows=json.loads((p/'upstream-source-lock.json').read_text());assert rows
for x in rows:assert hashlib.sha256((root/x['path']).read_bytes()).hexdigest()==x['sha256'],x['path']
(p/'upstream-verification.json').write_text(json.dumps({'root':str(root),'files':len(rows),'all_sha256_unchanged':True},indent=2));print('Upstream v008 source tree unchanged:',len(rows))
