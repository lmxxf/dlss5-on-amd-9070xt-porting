from pathlib import Path
import json,hashlib,csv,re
p=Path(__file__).parent;load=lambda f:json.loads(f.read_text(encoding='utf-8-sig'));payload=p/'payload/HIP';sums={n:h for h,n in(l.split()for l in Path('hip/SHA256SUMS').read_text().splitlines())};assert len(sums)==80
for n,h in sums.items():assert hashlib.sha256((payload/n).read_bytes()).hexdigest()==h,n
assert (payload/'SHA256SUMS').read_bytes()==Path('hip/SHA256SUMS').read_bytes()
source=load(p/'final-build/source-lock.json')
for n,h in source.items():assert hashlib.sha256(Path(n).read_bytes()).hexdigest()==h,n
for line in(p/'final-build/hosts.sha256').read_text().splitlines():
 h,n=line.split();assert hashlib.sha256((p/'payload'/n).read_bytes()).hexdigest()==h,n
manifest=load(p/'final-gold.json');assert len(manifest['jobs'])==14
for j in manifest['jobs']:
 d=p/'final/evidence'/j['label'].split('/')[1];assert load(d/'exit.json')['exit_code']==0;rows=list(csv.DictReader((d/'out.csv').open()));assert len(rows)==3 and all(int(x['invalid'])==0 and int(x['changed_half_values_from_first'])==0 for x in rows);raw=load(d/'raw-hashes.json');raw=[raw]if isinstance(raw,dict)else raw;assert len({x['sha256']for x in raw})==1 and raw[0]['sha256']==(j.get('gold')or load(p/'1a/evidence/gold-smallwindow-baseline/raw-hashes.json')[0]['sha256'])
refs={j['label']:j.get('gold')or load(p/'1a/evidence/gold-smallwindow-baseline/raw-hashes.json')[0]['sha256']for j in manifest['jobs']};(p/'final-gold-reference.json').write_text(json.dumps({'references':refs,'window_provenance':'paired original host plus Phase5 initial 1a stock baseline raw SHA; six normal tiers pre-pinned in executed manifest'},indent=2))
a=load(p/'final-statistics.json')['tiers'];assert len(a)==7 and all(x['samples_per_arm']==960 for x in a.values());n=load(p/'final-nmmn-statistics.json')['nmmn'];assert len(n)==3 and all(x['steady_samples_per_arm']==960 for x in n.values());smoke=p/'smoke/evidence/runtime-final';assert load(smoke/'exit.json')['exit_code']==0 and'lmxxf_nr_gpu: ok'in(smoke/'stdout.log').read_text(encoding='utf-8-sig');assert load(p/'upstream-verification.json')['all_sha256_unchanged']
assert Path('Development/HIP/hip_reference_network.h').read_bytes()==__import__('subprocess').check_output(['git','show','ba057a5d:Development/HIP/hip_reference_network.h'])
assert not __import__('subprocess').check_output(['git','diff','ba057a5d','--','src','hip','scripts','Development/HIP'])
result={'production_source_unchanged_from_phase5_start':True,'production_c32_changes_accepted':False,'normhoist_qualification_revoked_by_integration':True,'modules':80,'formal_host_source_and_binary_sha256_verified':True,'gold_profiles':7,'gold_frames':42,'gold_bitdiff':0,'invalid':0,'frame_abba_profiles':7,'frame_samples_per_arm':960,'nn_nmmn_profiles':3,'nn_samples_per_arm':960,'runtime_smoke_exit':0,'all_formal_modules_preserve_baseline_function_code_and_resources':not load(p/'full-code-comparison.json'),'note':'gfx1200 build/ELF identity verification only; GPU tests gfx1201. Failed Knife3 remains rejected.'};
if(p/'final-cleanup.json').exists():
 cleanup=load(p/'final-cleanup.json');assert not cleanup['gpu_lock_present']and cleanup['D_free_bytes']>=100*1024**3;assert(p/'mochi-tree-before.json').read_bytes()==(p/'mochi-tree-after.json').read_bytes();assert(p/'native-assets-before.json').read_bytes()==(p/'native-assets-after.json').read_bytes();result.update({'gpu_lock_released':True,'D_free_bytes':cleanup['D_free_bytes'],'locked_mochi_and_native_assets_unchanged':True})
(p/'final-verification.json').write_text(json.dumps(result,indent=2));print(json.dumps(result,indent=2))
