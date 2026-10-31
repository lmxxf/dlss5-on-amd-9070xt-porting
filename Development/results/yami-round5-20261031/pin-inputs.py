from pathlib import Path
import json,re,sys
p=Path(__file__).parent;known={f['path'].lower():f for f in json.loads((p/'actual-input-files.json').read_text(encoding='utf-8-sig'))if not f.get('missing')};assets=json.loads((p/'asset-files-before-final.json').read_text(encoding='utf-8-sig'))
for name in sys.argv[1:]:
 f=p/(name if name.endswith('.json')else name+'.json');m=json.loads(f.read_text());old={x['path'].lower():x for x in m['files']};required=set();dirs=set()
 for j in m['jobs']:
  required.add(j['exe'].lower())
  for val in re.findall(r'"([^"]+)"',j['arguments']):
   if '{OUT}'in val:continue
   if val.lower().endswith('\\modules'):required.update(k for k in old if k.startswith(val.lower()+'\\')and k.endswith('.hsaco'))
   elif val.lower()in known:required.add(val.lower())
   elif '.'in val.split('\\')[-1] and val.lower()in old:required.add(val.lower())
   else:dirs.add(val.lower().rstrip('\\'))
 old.update(known)
 for a in assets:
  if a['root'].lower().rstrip('\\')in dirs:old[a['path'].lower()]={k:a[k]for k in ['path','sha256']};required.add(a['path'].lower())
 assert required<=old.keys(),required-old.keys();m['files']=[{k:old[key][k]for k in ['path','sha256']}for key in sorted(required)];m['input_pin_note']='Actual inputs and complete used asset trees pinned before this run; not retroactive provenance for earlier batches.';f.write_text(json.dumps(m,indent=2)+'\n');print(f.name,len(m['files']))
