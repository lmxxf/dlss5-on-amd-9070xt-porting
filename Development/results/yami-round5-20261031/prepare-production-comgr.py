from pathlib import Path
import importlib.util,json,hashlib
p=Path(__file__).parent;sp=importlib.util.spec_from_file_location('rec','Development/tools/llvm-fork/compile-modules.py');rec=importlib.util.module_from_spec(sp);sp.loader.exec_module(rec);sources={x[0]:x[2]for x in rec.recipe(p/'production-combined-dot/source/hip')};r=r'D:\DLSSNR-Lab\yami-round5-20261031';cc=r'D:\DLSSNR-Lab\gap-phase5-20261023\rtc_compile-current.exe';files=[dict(path=cc,sha256='553385a0ba6465f332208cfd5830224e255db9973101050f760e3117e5575197')];jobs=[]
for mod in ['deep_fast-packed-fast','c512-m32-mh']:
 name='production-'+mod+'.hip';f=p/name;f.write_text(sources[mod]);files.append(dict(path=r+'\\'+name,sha256=hashlib.sha256(f.read_bytes()).hexdigest()))
 for arch in ['gfx1201','gfx1200']:jobs.append(dict(label='production-comgr/'+arch+'/'+mod,exe=cc,cpu_compile=True,env={'RTC_EXTRA_OPTS':''},arguments='"{OUT}\\'+mod+'.hsaco" "'+r+'\\'+name+'" comgr '+arch))
(p/'production-comgr.json').write_text(json.dumps(dict(name='production-comgr',files=files,jobs=jobs),indent=2)+'\n')
