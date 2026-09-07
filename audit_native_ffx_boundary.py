"""Inspect observed FFX-end -> list-close boundary; not an after-submit hook."""
from pathlib import Path
import json,re
root=Path('release/native-game-order');rows=[]
for line in (root/'order-29060.txt').read_text().splitlines():
 r=dict(re.findall(r'(\w+)=([^\s]+)',line))
 if r.get('pid')=='29060' and 'kind' in r:rows.append(r)
checks=[]
for i,r in enumerate(rows):
 if r['kind']!='ffx_end':continue
 following=[]
 for e in rows[i+1:]:
  if e['thread']!=r['thread']:continue
  if e['kind'] in ('ffx_begin','close_api'):
   if e['kind']=='close_api':checks.append(dict(ffx_list=r['list'],closed_api_list=e['list'],
    observed_pointer_delta=int(e['list'],16)-int(r['list'],16),
    intervening_draw_dispatch=sum(x['kind'] in ('draw_api','dispatch_api') for x in following),
    ffx_tick=int(r['tick']),close_tick=int(e['tick'])))
   break
  following.append(e)
report=dict(scope='bounded same-thread FFX end to next close; pointer offset observed, not portable ABI; actual submission return not observed',checks=checks)
(root/'boundary.json').write_text(json.dumps(report,indent=2)+'\n')
print('complete_boundaries',len(checks),'intervening_draw_dispatch',sorted({x['intervening_draw_dispatch'] for x in checks}),'pointer_deltas',sorted({x['observed_pointer_delta'] for x in checks}))
