from pathlib import Path
import numpy as np,json
r=Path(__file__).parent
out={}
for operand in ['fp8','f16']:
 results={}
 for variant in ['k32','split','partial','late-C']:
  kind='partial' if variant=='partial' else 'full'
  for dtype,ext in [('<f4','f32'),('<f2','f16')]:
   a=np.fromfile(r/f'{operand}-{variant}.{ext}',dtype);b=np.fromfile(r/f'gold-{kind}.{ext}',dtype)
   assert a.shape==b.shape and a.size==5*256
   assert np.isfinite(a).all()
   bits='<u4' if ext=='f32' else '<u2'
   results[f'{variant}.{ext}']={'bitdiff':int(np.count_nonzero(a.view(bits)!=b.view(bits))),'maxabs':float(np.max(np.abs(a.astype('f8')-b.astype('f8'))))}
 assert all(v['maxabs']==0 for v in results.values()), results
 out[operand]=results
print(json.dumps(out,indent=2));(r/'numeric-result.json').write_text(json.dumps(out,indent=2)+'\n')
