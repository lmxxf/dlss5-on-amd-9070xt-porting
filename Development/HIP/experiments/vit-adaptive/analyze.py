from pathlib import Path
import csv,json,sys
import numpy as np
root=Path(sys.argv[1]);out=Path(sys.argv[2]);out.mkdir(parents=True,exist_ok=True)
def load(p):
 x=np.fromfile(p,'<f2').reshape(720,1296,4).astype(np.float32)
 assert np.isfinite(x).all(),p
 return x[:,:,:3]
def display(x):
 x=np.clip(x,0,1);return np.where(x<=.0031308,12.92*x,1.055*x**(1/2.4)-.055)
rows=[]
for d in sorted(root.glob('adaptive-*-s*')):
 _,h,seq=d.name.split('-');gates=list(csv.reader((d/'gate.csv').open()));base=root/f'base-{h}-{seq}';files=list(d.glob('rgb-frame-*.f16'))
 assert len(files)==len(gates)
 for f,g in enumerate(gates):
  a=load(base/f'rgb-frame-{f}.f16');b=load(d/f'rgb-frame-{f}.f16');same=np.array_equal(a,b)
  assert int(g[0])>=1,('invalid GPU epoch frame',d,f,g)
  if int(g[1])==0: assert same,('recomputed frame mismatch',d,f)
  delta=np.abs(display(a)-display(b));rows.append(dict(height=int(h),sequence=int(seq[1:]),frame=f,reuse=int(g[1]),age=int(g[2]),reason=int(g[3]),global_change=float(g[4]),local_change=float(g[5]),exact=bool(same),mae_255=float(delta.mean()*255),p99_255=float(np.quantile(delta,.99)*255),max_255=float(delta.max()*255)))
summary=[]
for h,s in sorted({(r['height'],r['sequence']) for r in rows}):
 r=[x for x in rows if x['height']==h and x['sequence']==s]
 summary.append(dict(height=h,sequence=s,reuse_count=sum(x['reuse'] for x in r),frames=len(r),mae_mean=float(np.mean([x['mae_255'] for x in r])),mae_max=max(x['mae_255'] for x in r),full_frames=[x['frame'] for x in r if not x['reuse']],reasons=[x['reason'] for x in r]))
(out/'quality.json').write_text(json.dumps(dict(summary=summary,frames=rows),indent=2)+'\n')
timings=[]
for h,s in sorted({(d.name.split('-')[2],d.name.split('-')[3]) for d in root.glob('timing-*-*-s*')}):
 runs=[next(csv.DictReader((root/f'timing-{i}-{h}-{s}/summary.csv').open())) for i in range(4)];b=(float(runs[0]['mean_ms'])+float(runs[3]['mean_ms']))/2;c=(float(runs[1]['mean_ms'])+float(runs[2]['mean_ms']))/2
 timings.append(dict(height=int(h),sequence=int(s[1:]),base_ms=b,candidate_ms=c,saved_ms=b-c,reduction_percent=100*(b-c)/b,baseline_drift_ms=abs(float(runs[0]['mean_ms'])-float(runs[3]['mean_ms'])),runs=runs))
(out/'timing.json').write_text(json.dumps(timings,indent=2)+'\n');print(json.dumps(summary,indent=2));print(json.dumps(timings,indent=2))
