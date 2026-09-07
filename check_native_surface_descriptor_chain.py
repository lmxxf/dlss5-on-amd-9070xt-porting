"""Resolve first post surface through observed single-descriptor copy events.

Does not claim history image provenance or coverage of arbitrary descriptor ranges.
"""
from pathlib import Path
import argparse,json,re,struct
p=argparse.ArgumentParser();p.add_argument('--pid',type=int,required=True);args=p.parse_args()
root=Path('release/native-game-history-contract/descriptor-chain')
def rows(path):
 for number,line in enumerate(path.read_text().splitlines()):
  row=dict(re.findall(r'(\w+)=([^\s]+)',line))
  if row.get('pid')==str(args.pid):yield number,line,row
views={};tables={};history=[];devices=set()
for number,line,r in rows(root/'native-resource-view-events.txt'):
 device=r.get('api_device')
 if device:devices.add(device)
 if line.startswith('view_init'):views[(device,r['view'])]=r['resource']
 elif line.startswith('descriptor_update'):
  key=(device,r['table'])
  if all(r[k]=='0' for k in ('binding','array_offset','element')) and r['count']=='1':
   tables[key]=views.get((device,r['view']))
  else:tables[key]=None
  history.append((int(r['tick']),r['table'],tables[key],number))
 elif line.startswith('descriptor_copy'):
  key=(device,r['dest'])
  simple=r['count']=='1' and all(r[k]=='0' for k in ('source_binding','source_array','dest_binding','dest_array'))
  tables[key]=tables.get((device,r['source'])) if simple else None
  history.append((int(r['tick']),r['dest'],tables[key],number))
blob=(root/'post70.bin').read_bytes();assert len(blob)==184
assert len(devices)==1,'Multiple API devices require explicit identity mapping'
surface=struct.unpack_from('<Q',blob,16)[0]
returns=[r for _,line,r in rows(root/'native-nvapi-texture-contract.txt') if line.startswith('independent_return') and r['type']=='0' and r['status']=='0' and int(r['handle'],16)==surface]
assert returns
first=returns[0];table=hex(0xf000000000000000|int(first['desc'],16))[2:];tick=int(first['tick'])
events=[x for x in history if x[1]==table and x[0]<=tick]
assert events,'No observed descriptor chain'
last_tick=events[-1][0];candidates={x[2] for x in events if x[0]==last_tick}
assert None not in candidates and len(candidates)==1,'Ambiguous or unobserved descriptor owner'
report={'scope':'first post surface conversion, observed single-descriptor chain; not history content/update proof',
 'pid':args.pid,'surface':hex(surface),'virtual_table':table,'resource':next(iter(candidates)),
 'surface_tick':tick,'descriptor_tick':last_tick,'supporting_events':[x for x in events if x[0]==last_tick]}
(root/'surface-chain-validation.json').write_text(json.dumps(report,indent=2)+'\n');print(report)
