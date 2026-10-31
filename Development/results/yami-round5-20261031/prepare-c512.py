from pathlib import Path
import importlib.util,json,hashlib
p=Path(__file__).parent;spec=importlib.util.spec_from_file_location('recipe','Development/tools/llvm-fork/compile-modules.py');m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m)
s=next(x[2] for x in m.recipe(Path('hip')) if x[0]=='c512-m32-mh')
a='  i2 prob[4];\n  _Pragma("unroll 4") for(uint key=0;key<4;key++){\n#if C512_PAIR_PACK';assert s.count(a)==1
cand=s.replace(a,'  const float den_inv=c5c_inv(sums[0][0]+sums[1][0]);\n'+a).replace('float inv=c5c_inv(sums[0][e]+sums[1][e]);','float inv=den_inv;')
r=r'D:\DLSSNR-Lab\yami-round5-20261031';cc=r'D:\DLSSNR-Lab\gap-phase5-20261023\rtc_compile-current.exe';x=json.loads((p/'vit-fix-compile.json').read_text());x['name']='round5-second-compile'
for arm,src in [('base',s),('candidate',cand)]:
 f='c512-'+arm+'.hip';(p/f).write_text(src);x['files'].append({'path':r+'\\'+f,'sha256':hashlib.sha256(src.encode()).hexdigest()});x['jobs'].append({'label':'c512-compile/'+arm,'exe':cc,'cpu_compile':True,'env':{'RTC_EXTRA_OPTS':''},'arguments':'"{OUT}\\c512-m32-mh.hsaco" "'+r+'\\'+f+'" comgr gfx1201'})
(p/'second-compile.json').write_text(json.dumps(x,indent=2))
