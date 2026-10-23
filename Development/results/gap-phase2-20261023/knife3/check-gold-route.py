from pathlib import Path
import json,csv,re
r=Path(__file__).resolve().parent/'evidence';rows=[]
for d in sorted(x for x in r.glob('gold-*') if x.is_dir()):
 s=(d/'stderr.log').read_text(encoding='utf-8-sig');m=re.search(r'C512_NPAIR requested=(\d+) calls=(\d+)',s);assert m
 req,calls=map(int,m.groups());expected=0 if d.name.endswith('baseline') or 'missing-exports' in d.name else 64;assert calls==expected,(d.name,calls)
 rr=list(csv.DictReader((d/'out.csv').open()));assert len(rr)==3 and all(int(x['invalid'])==0 and int(x['changed_half_values_from_first'])==0 for x in rr)
 raw=json.loads((d/'raw-hashes.json').read_text(encoding='utf-8-sig'));assert len({x['sha256'] for x in raw})==1
 rows.append(dict(case=d.name,requested=req,calls=calls,sha=raw[0]['sha256'],frames=3))
assert len(rows)==22
for x in rows:
 if x['case'].endswith('candidate'):
  ref=next(y for y in rows if y['case']==x['case'].replace('candidate','baseline'));assert x['sha']==ref['sha']
for d in (x for x in r.glob('perf-*') if x.is_dir()):
 s=(d/'stderr.log').read_text(encoding='utf-8-sig');req,calls=map(int,re.search(r'C512_NPAIR requested=(\d+) calls=(\d+)',s).groups());assert calls==(3856 if d.name.endswith('candidate') else 0)
(r.parent/'gold-route-check.json').write_text(json.dumps(dict(gold_frames=66,cases=rows,performance_actual_calls_verified=True),indent=2));print('KNIFE3_GOLD_ROUTE_PASS')
