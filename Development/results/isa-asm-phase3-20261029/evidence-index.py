from pathlib import Path
import json,hashlib
p=Path(__file__).parent;rows=[]
for case in ['knife1A','knife1B','knife1M','knife2P','knife2S','knife3','knife3C','full','final','gap','smoke']:
 root=p/case/'evidence'
 for f in sorted(root.rglob('*')):
  if f.is_file():rows.append({'path':f.relative_to(p).as_posix(),'bytes':f.stat().st_size,'sha256':hashlib.sha256(f.read_bytes()).hexdigest()})
(p/'evidence-index.json').write_text(json.dumps(rows,indent=2));print(len(rows),'evidence files indexed')
