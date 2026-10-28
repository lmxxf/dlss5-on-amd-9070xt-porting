from pathlib import Path
import json
P=Path(__file__).parent;a=json.loads((P/'work/all-kernel-index.json').read_text());d=json.loads((P/'fresh-dispatches.json').read_text());mapping={'c32_norm900':'c32-wave1-fast-norm900.hsaco','c32_wave1':'c32-wave1-fast.hsaco','c64_wave2':'c64-wave2-fast.hsaco','c512_m32_deep':'c512-m32-deep.hsaco','c512_m32_mh':'c512-m32-mh.hsaco','deep_fast':'deep_fast-packed-fast.hsaco','mh_fast':'multihead-fast-padded-wave-packed.hsaco','sp':'swin-persistent.hsaco','vit_stream':'vit-stream-fast.hsaco'}
names=sorted({k for v in d.values() for f in v[-3:] for k in f['kernels']});rows=[]
for k in names:
 role,n=k.split(':');matches=[r for r in a if r['name']==n and r['module']==mapping[role]];assert len(matches)==1,(k,mapping[role]);r=matches[0];rows.append({'role':role,**{x:y for x,y in r.items() if x not in ['waits','narrow_lds','observed_small']}})
(P/'active-summary.json').write_text(json.dumps(rows,indent=2));print('fresh active physical module/function pairs:',len(rows))
