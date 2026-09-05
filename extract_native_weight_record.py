"""Extract original record bytes without interpreting a mixed-storage container."""
import argparse,json,struct,hashlib
from pathlib import Path
p=argparse.ArgumentParser();p.add_argument('dll',type=Path);p.add_argument('record');p.add_argument('output',type=Path);a=p.parse_args()
index=json.loads(Path(__file__).with_name('weights-index.json').read_text())
r=next(x for x in index if x['name']==a.record)
base=0x114a160
with a.dll.open('rb') as f:
 f.seek(base);size=struct.unpack('<Q',f.read(8))[0]
 if size<r['payload_offset']+r['payload_size'] or base+size>a.dll.stat().st_size:raise ValueError('archive bounds mismatch')
 f.seek(base+r['payload_offset']);data=f.read(r['payload_size'])
 if len(data)!=r['payload_size']:raise ValueError('truncated record')
a.output.parent.mkdir(parents=True,exist_ok=True);a.output.write_bytes(data)
print(json.dumps({'record':a.record,'bytes':len(data),'sha256':hashlib.sha256(data).hexdigest(),'interpretation':'raw bytes; container dtype does not define every matrix region'}))
