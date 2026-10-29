from pathlib import Path
import json,hashlib,importlib.util,copy,ntpath
p=Path(__file__).parent;r=r'D:\DLSSNR-Lab\c32-stage-phase5-20261029';old=p.parent/'isa-asm-phase3-20261029';w=p/'work/full-generated';w.mkdir(parents=True,exist_ok=True)
sp=importlib.util.spec_from_file_location('recipe','Development/tools/llvm-fork/compile-modules.py');m=importlib.util.module_from_spec(sp);sp.loader.exec_module(m);rtc=r'D:\DLSSNR-Lab\gap-phase5-20261023\rtc_compile-current.exe';files=[{'path':rtc,'sha256':'553385a0ba6465f332208cfd5830224e255db9973101050f760e3117e5575197'}];rows=[];jobs=[]
for name,compiler,src,defs,parts,opts in m.recipe(Path('hip')):
 rows.append({'module':name,'compiler':compiler or'driver COMGR','defines':defs,'sources':parts,'source_sha256':hashlib.sha256(src.encode()).hexdigest(),'effective_options':opts if compiler else[]})
 if compiler:continue
 f=w/(name+'.hip');f.write_text(src);files.append({'path':ntpath.join(r,'full-generated',f.name),'sha256':hashlib.sha256(f.read_bytes()).hexdigest()})
 for arch in ['gfx1200','gfx1201']:
  jobs.append({'label':'full/compile-'+arch+'-'+name,'exe':rtc,'cpu_compile':True,'env':{'RTC_EXTRA_OPTS':'-mllvm=-amdgpu-sched-strategy=max-ilp'if arch=='gfx1200'and name=='c512-m32-deep'else''},'arguments':'"{OUT}\\'+name+'.hsaco" "'+ntpath.join(r,'full-generated',f.name)+'" comgr '+arch})
assert len(rows)==40 and len(jobs)==68;(p/'full-effective-recipes.json').write_text(json.dumps(rows,indent=2));(p/'full-compile.json').write_text(json.dumps({'name':'phase5-final-80-rebuild','files':files,'jobs':jobs},indent=2))
