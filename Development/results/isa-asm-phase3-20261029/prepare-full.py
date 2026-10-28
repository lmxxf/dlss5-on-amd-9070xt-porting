from pathlib import Path
import importlib.util,json,hashlib
R=Path.cwd();P=R/'Development/results/isa-asm-phase3-20261029';W=P/'work/full-generated';W.mkdir(parents=True,exist_ok=True);spec=importlib.util.spec_from_file_location('recipe',R/'Development/tools/llvm-fork/compile-modules.py');m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m);remote=r'D:\DLSSNR-Lab\isa-asm-phase3-20261029';rtc=r'D:\DLSSNR-Lab\gap-phase5-20261023\rtc_compile-current.exe';files=[{'path':rtc,'sha256':'553385a0ba6465f332208cfd5830224e255db9973101050f760e3117e5575197'}];jobs=[];rows=[]
for name,compiler,src,defs,parts,opts in m.recipe(R/'hip'):
 row={'module':name,'compiler':compiler or 'driver COMGR','defines':defs,'sources':parts,'source_sha256':hashlib.sha256(src.encode()).hexdigest(),'effective_options':opts if compiler else [],'recipe_optional_options':opts};rows.append(row)
 if compiler:continue
 f=W/(name+'.hip');f.write_text(src);files.append({'path':remote+'\\'+f.name,'sha256':hashlib.sha256(f.read_bytes()).hexdigest()})
 for arch in ['gfx1200','gfx1201']:jobs.append({'label':'full/compile-'+arch+'-'+name,'exe':rtc,'cpu_compile':True,'env':{'RTC_EXTRA_OPTS':'-mllvm=-amdgpu-sched-strategy=max-ilp' if arch=='gfx1200' and name=='c512-m32-deep' else ''},'arguments':'"{OUT}\\'+name+'.hsaco" "'+remote+'\\'+f.name+'" comgr '+arch})
assert len(rows)==40 and len(jobs)==68
(P/'full-effective-recipes.json').write_text(json.dumps({'rows':rows,'note':'effective per-target stock code: LLVM23 RowOpts/-real-true16; COMGR empty except gfx1200 c512-m32-deep max-ilp. gfx1201 c512 remains empty. Initial build plus correction manifests retained.'},indent=2));(P/'full-compile.json').write_text(json.dumps({'name':'full-production-80-rebuild','files':files,'jobs':jobs},indent=2))
