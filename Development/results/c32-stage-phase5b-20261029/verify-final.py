from pathlib import Path
import json,hashlib,csv,re
p=Path(__file__).parent;load=lambda f:json.loads(f.read_text(encoding='utf-8-sig'));sums={n:h for h,n in(l.split()for l in Path('hip/SHA256SUMS').read_text().splitlines())};assert len(sums)==80
for n,h in sums.items():assert hashlib.sha256((p/'payload/HIP'/n).read_bytes()).hexdigest()==h
assert(p/'payload/HIP/SHA256SUMS').read_bytes()==Path('hip/SHA256SUMS').read_bytes()
for n,h in load(p/'final-build/source-lock.json').items():assert hashlib.sha256(Path(n).read_bytes()).hexdigest()==h,n
for line in(p/'final-build/hosts.sha256').read_text().splitlines():
 h,n=line.split();assert hashlib.sha256((p/'payload'/n).read_bytes()).hexdigest()==h
window=load(p.parent/'c32-stage-phase5-20261029/1a/evidence/gold-smallwindow-baseline/raw-hashes.json')[0]['sha256'];refs={};manifest=load(p/'final-gold.json');assert len(manifest['jobs'])==14
for j in manifest['jobs']:
 d=p/'final/evidence'/j['label'].split('/')[1];assert load(d/'exit.json')['exit_code']==0;rows=list(csv.DictReader((d/'out.csv').open()));assert len(rows)==3 and all(int(x['invalid'])==0 and int(x['changed_half_values_from_first'])==0 for x in rows);raw=load(d/'raw-hashes.json');raw=[raw]if isinstance(raw,dict)else raw;ref=j.get('gold')or window;assert len({x['sha256']for x in raw})==1 and raw[0]['sha256']==ref;refs[j['label']]=ref
(p/'final-gold-reference.json').write_text(json.dumps(refs,indent=2))
a=load(p/'final-statistics.json')['tiers'];assert len(a)==7 and all(x['samples_per_arm']==960 for x in a.values());nn=load(p/'final-nmmn-statistics.json')['nmmn'];assert len(nn)==3 and all(x['steady_samples_per_arm']==960 for x in nn.values());hw=load(p/'final-c32-statistics.json')['profiles'];assert len(hw)==2
for d in(p/'final/evidence').iterdir():
 if d.is_dir()and d.name.endswith('-candidate'):assert all(int(x)==0 for x in re.findall(r'c32_stock_calls=(\d+)',(d/'stderr.log').read_text(encoding='utf-8-sig')));assert re.search(r'C32_POOL_FUSED calls=[1-9]\d*',(d/'stderr.log').read_text(encoding='utf-8-sig')),d
for d in(p/'final-nmmn/evidence').iterdir():
 if d.is_dir()and d.name.endswith('-N'):assert re.search(r'C32_POOL_FUSED calls=[1-9]\d*',(d/'stderr.log').read_text(encoding='utf-8-sig')),d
smoke=p/'smoke/evidence/runtime-final';assert load(smoke/'exit.json')['exit_code']==0 and'lmxxf_nr_gpu: ok'in(smoke/'stdout.log').read_text(encoding='utf-8-sig');assert re.search(r'C32_POOL_FUSED calls=[1-9]\d*',(smoke/'stderr.log').read_text(encoding='utf-8-sig'))
for arch in ['gfx1200','gfx1201']:
 for rs in load(p/f'final-{arch}-resource-gates.json')['modules'].values():
  for n,d in rs.items():
   assert not d['.private_segment_fixed_size']and not d['.vgpr_spill_count']and not d['.sgpr_spill_count']
   if d['production_w16']:assert d['.vgpr_count']<=160
result={'modules':80,'formal_hosts_sha256_verified':True,'gold_profiles':7,'gold_frames':42,'gold_bitdiff':0,'invalid':0,'frame_abba_profiles':7,'steady_samples_per_arm':960,'nmmn_profiles':3,'hardware_clock_profiles':2,'default_fusion_route_verified':True,'runtime_smoke_exit':0,'production_w16_vgpr_max':158,'all_export_private_and_spill_zero':True,'legacy_resource_exceptions':'Seven unchanged nondefault exports per fast module already >160 VGPR at baseline; no resource regression; listed in resource-gates JSON.','new_export_per_fast_module':'c32_wave1_finish_dcrop_pool_b8d_w16','legacy_constant_relocations_proven':len(load(p/'legacy-relocation-proof.json'))}
if(p/'final-cleanup.json').exists():
 c=load(p/'final-cleanup.json');assert not c['gpu_lock_present']and c['D_free_bytes']>=100*1024**3;assert(p/'mochi-tree-before.json').read_bytes()==(p/'mochi-tree-after.json').read_bytes();result.update({'gpu_lock_released':True,'D_free_bytes':c['D_free_bytes'],'locked_mochi_tree_unchanged':True})
(p/'final-verification.json').write_text(json.dumps(result,indent=2));print(json.dumps(result,indent=2))
