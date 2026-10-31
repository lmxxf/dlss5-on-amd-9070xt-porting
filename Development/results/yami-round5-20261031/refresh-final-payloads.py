from pathlib import Path
import importlib.util,json,hashlib,shutil,zipfile
p=Path(__file__).parent;sp=importlib.util.spec_from_file_location('rec','Development/tools/llvm-fork/compile-modules.py');rec=importlib.util.module_from_spec(sp);sp.loader.exec_module(rec);sp=importlib.util.spec_from_file_location('elf','Development/tools/llvm-fork/compare-kernels.py');elf=importlib.util.module_from_spec(sp);sp.loader.exec_module(elf)
old={x[0]:x for x in rec.recipe(p/'r2d-both/source/hip')};sha=lambda x:hashlib.sha256(x).hexdigest();index={}
for build in ['production-combined-norm900/build','production-combined-norm900-c256/build','production-rebuild-llvm']:
 m=json.loads((p/build/'manifest.json').read_text())
 for row in m['modules']:index[(row['target'],row['module'],row['source_sha256'])]=p/build/row['target']/(row['module']+'.hsaco')
for prefix,mods in [('production-comgr',['deep_fast-packed-fast','c512-m32-mh']),('production-rebuild-comgr',json.loads((p/'production-rebuild-plan.json').read_text())['remaining_comgr_modules'])]:
 for mod in mods:
  src=p/(('production-'if prefix=='production-comgr'else'rebuild-')+mod+'.hip')
  for arch in ['gfx1201','gfx1200']:index[(arch,mod,sha(src.read_bytes()))]=p/prefix/arch/mod/(mod+'.hsaco')
for arch in ['gfx1200']:
 index[(arch,'c512-m32-deep',sha((p/'rebuild-c512-m32-deep.hip').read_bytes()))]=p/'production-rebuild-rowopts'/arch/'c512-m32-deep/c512-m32-deep.hsaco'
roles={'base':'production-combined','norm':'production-combined-norm900','c256':'production-combined-c256','full':'production-combined-norm900-c256'};stock=Path('Development/results/ksearch-round4-20261031/payload/HIP');ledger=[];m=json.loads((p/'final-five-long.json').read_text());previous={x['path'].lower():x['sha256']for x in m['files']};remote=r'D:\DLSSNR-Lab\yami-round5-20261031';delta={}
for arm,role in roles.items():
 rows=list(rec.recipe(p/role/'source/hip'));assert len(rows)==40;dest=p/('final-'+arm)/'HIP';changes=[]
 for arch in ['gfx1201','gfx1200']:
  for mod,compiler,src,defs,parts,opts in rows:
   sh=sha(src.encode());origin=stock/arch/(mod+'.hsaco')if src==old[mod][2]else index[(arch,mod,sh)];assert origin.exists(),origin
   target=dest/arch/(mod+'.hsaco');shutil.copyfile(origin,target)
   ref=stock/arch/(mod+'.hsaco')
   if arch=='gfx1201':
    if mod=='deep_fast-packed-fast':ref=p/'vit-fix-compile'/(mod+'.hsaco')
    elif mod=='c512-m32-mh':ref=p/'c512-prob-compile'/(mod+'.hsaco')
    elif mod=='c32-wave1-fast-norm900'and arm in ['norm','full']:ref=p/'c32-dotpar/build/gfx1201'/(mod+'.hsaco')
    elif mod in ['c64-wave2','c64-wave2-fast']and arm in ['c256','full']:ref=p/'one-plane-c256-rc/build/gfx1201'/(mod+'.hsaco')
   ts,tm,tf=elf.elf(target);rs,rm,rf=elf.elf(ref);assert tm==rm and tf==rf and all(ts.get(k)==rs.get(k)for k in ['.text','.rodata','.data']), (arm,arch,mod)
   objsha=sha(target.read_bytes());path=remote+'\\final-'+arm+'\\modules\\'+mod+'.hsaco'
   if arch=='gfx1201'and previous[path.lower()]!=objsha:changes.append(target)
   ledger.append(dict(arm=arm,arch=arch,module=mod,source_sha256=sh,compiler_route=compiler or'COMGR',recipe_backend_options=opts,effective_backend_options=rec.effective_row_options(arch,mod,opts),sha256=objsha,origin=str(origin),runtime_reference=str(ref),code_addresses_metadata_rodata_data_identical=True))
 (dest/'SHA256SUMS').write_text(''.join(f"{sha(f.read_bytes())}  {f.relative_to(dest).as_posix()}\n"for f in sorted(dest.glob('gfx*/*.hsaco'))))
 delta[arm]=[f.name for f in changes]
 if changes:
  with zipfile.ZipFile(p/('refresh-'+arm+'.zip'),'w',zipfile.ZIP_DEFLATED)as z:
   for f in changes:z.write(f,f.name)
for f in m['files']:
 for arm in roles:
  prefix=remote+'\\final-'+arm+'\\modules\\'
  if f['path'].startswith(prefix):f['sha256']=sha((p/('final-'+arm)/'HIP/gfx1201'/f['path'].split('\\')[-1]).read_bytes())
m['canonical_rebuild_note']='All changed translation units rebuilt from exact staged source; unchanged ones copied only when flattened source is identical. Runtime code/data/metadata/addresses audited against tested candidates or baseline.';(p/'final-five-long.json').write_text(json.dumps(m,indent=2)+'\n')
(p/'canonical-artifact-ledger.json').write_text(json.dumps(ledger,indent=2)+'\n');(p/'canonical-refresh-delta.json').write_text(json.dumps(delta,indent=2)+'\n')
setup=["$ErrorActionPreference='Stop'","$r='"+remote+"'","if(Test-Path 'D:\\DLSSNR-Lab\\gpu.lock'){throw 'busy'}"]
for arm,files in delta.items():
 if files:setup.append(f'Expand-Archive -Path "$r\\refresh-{arm}.zip" -DestinationPath "$r\\final-{arm}\\modules" -Force')
(p/'refresh-final-payloads.ps1').write_text('\n'.join(setup)+'\n');print({k:len(v)for k,v in delta.items()});print('320 canonical objects verified')
