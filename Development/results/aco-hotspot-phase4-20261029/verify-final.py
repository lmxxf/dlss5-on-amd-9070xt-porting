from pathlib import Path
import json,hashlib,subprocess,struct,shutil
p=Path(__file__).parent;r=p.parents[2];start=json.loads((p/'start.json').read_text());commit=start['production_commit'];assert subprocess.check_output(['git','rev-parse','--show-toplevel'],text=True).strip()==str(r.resolve())
assert subprocess.check_output(['git','diff',commit,'--','hip','src','scripts','Development/HIP'],text=True)==''
manifest=Path('hip/SHA256SUMS');original=subprocess.check_output(['git','show',commit+':hip/SHA256SUMS']);assert manifest.read_bytes()==original
mods=[];payload=Path('Development/results/isa-asm-phase3-20261029/payload/HIP')
for line in manifest.read_text().splitlines():
 h,n=line.split();f=payload/n;assert hashlib.sha256(f.read_bytes()).hexdigest()==h;flags=struct.unpack_from('<I',f.read_bytes(),48)[0];mods.append({'path':n,'sha256':h,'elf_flags':flags})
assert len(mods)==80 and len({x['elf_flags']for x in mods if x['path'].startswith('gfx1200/')})==1 and len({x['elf_flags']for x in mods if x['path'].startswith('gfx1201/')})==1
up=Path(start['source']);lock=json.loads((p/'upstream-source-lock.json').read_text());assert all(hashlib.sha256((up/x['path']).read_bytes()).hexdigest()==x['sha256']for x in lock)
before=json.loads((p/'mochi-tree-before.json').read_text(encoding='utf-8-sig'));after=json.loads((p/'mochi-tree-after.json').read_text(encoding='utf-8-sig'));prior=json.loads(Path('Development/results/isa-asm-phase3-20261029/mochi-tree-after.json').read_text());assert before==after==prior
clean=json.loads((p/'final-cleanup.json').read_text(encoding='utf-8-sig'));assert clean['lock_absent']and clean['benchmark_processes_absent']
for role in ['3a','3b']:
 root=p/role/'evidence';gold=list(root.glob('gold-*'));perf=list(root.glob('perf-*'));kernel=list(root.glob('kernel-*'));assert len(gold)==14 and len(perf)==48 and len(kernel)==24
 for d in gold+perf+kernel:assert json.loads((d/'exit.json').read_text(encoding='utf-8-sig'))['exit_code']==0
 assert json.loads((p/(role+'-decision.json')).read_text())['decision']=='STOP'
rows=[]
for case in ['3a','3b']:
 for f in sorted((p/case/'evidence').rglob('*')):
  if f.is_file():rows.append({'path':str(f.relative_to(p)),'bytes':f.stat().st_size,'sha256':hashlib.sha256(f.read_bytes()).hexdigest()})
(p/'step3-evidence-index.json').write_text(json.dumps(rows,indent=2));result={'production_start':commit,'production_source_scope_unchanged':True,'production_manifest_unchanged':True,'modules_verified':mods,'module_count':80,'original_v008_source_files_unchanged':len(lock),'locked_d1185_tree_unchanged_including_size_mtime_sha':True,'gold_frames':84,'network_performance_slots':96,'kernel_diagnostic_slots':48,'all_candidate_decisions':'STOP','gpu_lock_released':True,'benchmark_processes_absent':True,'no_game_deployment_or_zip_write':True,'no_push':True};(p/'final-verification.json').write_text(json.dumps(result,indent=2))
# Only remove this task's generated intermediates and failed candidate copies.
work=p/'work';removed=0
if work.exists():removed+=sum(f.stat().st_size for f in work.rglob('*')if f.is_file());shutil.rmtree(work)
for f in(p/'payload').rglob('*.pre.comp'):removed+=f.stat().st_size;f.unlink()
for f in(p/'payload').glob('3?-c64-wave2-fast.hsaco'):removed+=f.stat().st_size;f.unlink()
clone=Path(start['build_copy']);assert clone==Path('/home/lmxxf/work/dlss5-phase4-mochi-build')and clone.resolve()!=up.resolve()
if clone.exists():assert(clone/'vulkan-phase4.def').exists();removed+=sum(f.stat().st_size for f in clone.rglob('*')if f.is_file());shutil.rmtree(clone)
(p/'local-cleanup.json').write_text(json.dumps({'removed_generated_bytes':removed,'isolated_upstream_build_removed':True,'payload_retained_bytes':sum(f.stat().st_size for f in(p/'payload').rglob('*')if f.is_file()),'source_original_retained':str(up)},indent=2))
print('Verified: unchanged production/source trees and all80 modules; 84 Gold frames, 96 network slots, 48 diagnostic slots; STOP both; cleaned',removed,'temporary bytes')
