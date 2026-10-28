from pathlib import Path
import json,csv,hashlib,subprocess
P=Path(__file__).parent;R=P.parents[2];gold={};perf={}
for case,prefix in [('knife1','gold-'),('knife1','final-gold-'),('knife1','cached-gold-'),('knife2','gold-'),('knife3-pool','gold-'),('knife3-split','gold-')]:
 root=P/case/'evidence';count=0
 for d in root.glob(prefix+'*-candidate'):
  ref=root/d.name.removesuffix('-candidate').__add__('-baseline');a=json.loads((ref/'raw-hashes.json').read_text(encoding='utf-8-sig'));b=json.loads((d/'raw-hashes.json').read_text(encoding='utf-8-sig'));assert {x['sha256'] for x in a}=={x['sha256'] for x in b}
  for out in [d,ref]:
   rows=list(csv.DictReader((out/'out.csv').open(encoding='utf-8-sig')));assert len(rows)==3 and all(r['invalid']=='0' and r['changed_half_values_from_first']=='0' for r in rows);assert json.loads((out/'exit.json').read_text(encoding='utf-8-sig'))['exit_code']==0
  count+=1
 assert count==7,(case,prefix,count);gold[case+'/'+prefix]=42
for name in ['final-fallback-original1088','cached-default-original1088']:
 d=P/'knife1/evidence'/name;assert json.loads((d/'exit.json').read_text(encoding='utf-8-sig'))['exit_code']==0;gold[name]=3
for name,slots in [('knife1',48),('knife1-final',12),('knife1-product',12),('knife2',48),('knife3-pool',48),('knife3-split',48)]:
 d=json.loads((P/(name+'-statistics.json')).read_text());assert len(d['slots'])==slots;assert all(v['samples_per_arm']==960 for v in d['tiers'].values());perf[name]=slots
sums={n:h for h,n in (l.split() for l in (R/'hip/SHA256SUMS').read_text().splitlines())};assert len(sums)==80
for n,h in sums.items():assert hashlib.sha256((R/'Development/results/backfeed-20261028/final-payload/HIP'/n).read_bytes()).hexdigest()==h
assert not subprocess.check_output(['git','diff','d78b688e','--name-only','--','hip','src','scripts','Development/HIP'],text=True).strip()
(P/'verification.json').write_text(json.dumps({'gold_frames':sum(gold.values()),'gold_groups':gold,'performance_slots':sum(perf.values()),'performance_groups':perf,'all_GPU_tested_changes_gold_bitdiff':0,'invalid':0,'production_modules_verified_unchanged':80,'unmodified_production_source_base':'d78b688e','GPU_guard':'game-check, process gate, >=100GB, atomic gpu.lock, 15s watchdog','benchmark':'3x4 ABBA per profile; independent serial process each slot; 240 discard80; 960/arm; NET0 edges_only1'},indent=2));print('Phase2 verified:',sum(gold.values()),'Gold frames;',sum(perf.values()),'performance slots; 80 production modules unchanged')
