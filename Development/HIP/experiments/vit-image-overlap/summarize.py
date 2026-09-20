from pathlib import Path
import csv,json,re,sys,zipfile,statistics
archives=Path(sys.argv[1]);out=Path(sys.argv[2]);out.mkdir(parents=True,exist_ok=True);data=archives/'image-schedule-data'
for p in archives.glob('vit-adaptive-image-*.zip'):
 with zipfile.ZipFile(p) as z:z.extractall(data/p.stem.removeprefix('vit-adaptive-'))
result=[];identities=[]
for kind in [1,2]:
 root=data/f'image-schedule-{kind}-timing'
 for h in [900,1080]:
  r=[next(csv.DictReader((root/f'timing-{i}-{h}-s1/summary.csv').open(encoding='utf-8-sig'))) for i in range(4)]
  b=(float(r[0]['mean_ms'])+float(r[3]['mean_ms']))/2;c=(float(r[1]['mean_ms'])+float(r[2]['mean_ms']))/2
  result.append(dict(kind=kind,height=h,baseline_ms=b,candidate_ms=c,added_ms=c-b,added_percent=(c-b)/b*100,baseline_drift_ms=abs(float(r[0]['mean_ms'])-float(r[3]['mean_ms'])),runs=r))
 rows=list(csv.DictReader((data/f'image-schedule-{kind}-smoke/identity.csv').open(encoding='utf-8-sig')))
 assert len(rows)==36 and all(r['identical'].lower()=='true' and r['base_sha']==r['candidate_sha'] for r in rows)
 identities+=rows
spans=[]
for kind in [0,1,2]:
 s=(data/'image-span'/f'span-{kind}.log').read_text(errors='replace')
 rows=[tuple(map(float,m)) for m in re.findall(r'hip_span gpu_ms=([-\d.]+) cpu_enqueue_ms=([-\d.]+) sync=0 status=0',s)]
 assert len(rows)==39,(kind,len(rows))
 warm=rows[10:];spans.append(dict(kind=kind,warm_samples=len(warm),gpu_span_reported_mean_ms=statistics.mean(x[0] for x in warm),gpu_span_usable=all(x[0]>=0 for x in rows),negative_gpu_samples=sum(x[0]<0 for x in rows),cpu_enqueue_mean_ms=statistics.mean(x[1] for x in warm),samples=rows))
(out/'results.json').write_text(json.dumps(dict(timing=result,span_diagnostic=spans,identity_pairs=len(identities),all_identical=True),indent=2)+'\n')
with (out/'identity.csv').open('w') as f:
 w=csv.DictWriter(f,fieldnames=list(identities[0]));w.writeheader();w.writerows(identities)
for r in result:print(r['kind'],r['height'],f"{r['baseline_ms']:.6f} -> {r['candidate_ms']:.6f}, added {r['added_ms']:.6f}ms ({r['added_percent']:.3f}%), drift {r['baseline_drift_ms']:.6f}")
for r in spans:print('SPAN',r['kind'],r['gpu_span_reported_mean_ms'],r['cpu_enqueue_mean_ms'], 'GPU timing usable:',r['gpu_span_usable'])
