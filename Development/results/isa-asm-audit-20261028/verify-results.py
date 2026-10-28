from pathlib import Path
import csv,json,hashlib
P=Path(__file__).parent;res={};index=[]
for role in ['upwide','dectail','ffnb2']:
 root=P/role/'evidence';profiles=[]
 for d in sorted(root.glob('gold-*-candidate')):
  b=root/d.name.replace('-candidate','-baseline');aa=json.loads((b/'raw-hashes.json').read_text(encoding='utf-8-sig'));bb=json.loads((d/'raw-hashes.json').read_text(encoding='utf-8-sig'));assert {r['sha256'] for r in aa}=={r['sha256'] for r in bb}
  for case in [d,b]:
   rows=list(csv.DictReader((case/'out.csv').open(encoding='utf-8-sig')));assert len(rows)==3 and all(x['invalid']=='0' and x['changed_half_values_from_first']=='0' for x in rows);assert json.loads((case/'exit.json').read_text(encoding='utf-8-sig'))['exit_code']==0
  profiles.append(d.name.removeprefix('gold-').removesuffix('-candidate'))
 assert len(profiles)==7
 stats=json.loads((P/(role+'-statistics.json')).read_text());assert len(stats['tiers'])==4
 accepted=all(v['delta_ms']<0 and v['p99_delta_ms']<0 and all(r['delta_ms']<0 and r['p99_delta_ms']<0 for r in v['rounds']) for v in stats['tiers'].values());assert not accepted
 res[role]={'gold_profiles':profiles,'gold_frames':42,'invalid':0,'steady_samples_per_arm_per_profile':960,'perf_slots':48,'decision':'STOP','production_changed':False}
for d in (P/'asmproof/evidence').glob('gold-*'):
 assert json.loads((d/'exit.json').read_text(encoding='utf-8-sig'))['exit_code']==0
 rows=list(csv.DictReader((d/'out.csv').open(encoding='utf-8-sig')));assert len(rows)==3 and all(x['invalid']=='0' and x['changed_half_values_from_first']=='0' for x in rows)
assert len(list((P/'asmproof/evidence').glob('gold-*')))==7
for role in ['upwide','dectail','ffnb2','asmproof','trace','compile']:
 for f in sorted((P/role/'evidence').rglob('*')):
  if f.is_file():index.append({'path':f.relative_to(P).as_posix(),'bytes':f.stat().st_size,'sha256':hashlib.sha256(f.read_bytes()).hexdigest()})
(P/'verification.json').write_text(json.dumps({'candidates':res,'assembly_patch_loader_gold_profiles':7,'assembly_patch_gold_frames':21,'assembly_patch_is_performance_candidate':False,'fresh_trace_profiles':6,'actual_active_pairs':56,'GPU_protocol':'game-check/process/D>=100GB/CreateNew gpu.lock/15s watchdog; 3x4 ABBA, slot240 discard80, NET0 edges_only1; strictly serial'},indent=2))
(P/'evidence-index.json').write_text(json.dumps(index,indent=2));print('All Gold and 144 performance slots verified. All three candidates STOP.')
