from pathlib import Path
import hashlib,json,sys
p=Path(__file__).parent;role=sys.argv[1];root=p/role/'evidence';rows=[]
for f in sorted(root.rglob('*')):
 if f.is_file():rows.append({'path':f.relative_to(p).as_posix(),'bytes':f.stat().st_size,'sha256':hashlib.sha256(f.read_bytes()).hexdigest()})
(p/(role+'-evidence-index.json')).write_text(json.dumps(rows,indent=2))
