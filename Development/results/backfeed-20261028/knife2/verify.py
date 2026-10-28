from pathlib import Path
import json,sys
p=Path(__file__).parent;root=p/'evidence';n=0
for d in root.glob('gold-*-candidate'):
 base=root/d.name.replace('-candidate','-baseline');a=json.loads((base/'raw-hashes.json').read_text(encoding='utf-8-sig'));b=json.loads((d/'raw-hashes.json').read_text(encoding='utf-8-sig'));assert {x['sha256'] for x in a}=={x['sha256'] for x in b};n+=1
assert n==6
print('six-profile cross-arm Gold matched')
