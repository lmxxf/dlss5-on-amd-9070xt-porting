from pathlib import Path
import csv,json,sys
root=Path(sys.argv[1]);items=[]
for variant in ['plain','fragment','spatial']:
 for h in [900,1080]:
  for t in (['0.15','0.5','1'] if variant=='spatial' else ['-1','0.5','1']):
   ps=[root/variant/f'timing-{t}--{kind}-{i}-{h}-p0-summary.csv' for kind,i in [('base',0),('select',1),('select',2),('base',3)]]
   r=[next(csv.DictReader(p.open(encoding='utf-8-sig'))) for p in ps]
   assert r[0]['hash']==r[3]['hash'] and r[1]['hash']==r[2]['hash']
   if t=='-1':assert r[0]['hash']==r[1]['hash']
   b=(float(r[0]['median_ms'])+float(r[3]['median_ms']))/2;c=(float(r[1]['median_ms'])+float(r[2]['median_ms']))/2
   items.append(dict(variant=variant,height=h,threshold=float(t),baseline_ms=b,candidate_ms=c,saved_ms=b-c,reduction_percent=100*(b-c)/b,baseline_drift_ms=abs(float(r[0]['median_ms'])-float(r[3]['median_ms']))))
(root/'timing-summary.json').write_text(json.dumps(items,indent=2)+'\n');print(json.dumps(items,indent=2))
