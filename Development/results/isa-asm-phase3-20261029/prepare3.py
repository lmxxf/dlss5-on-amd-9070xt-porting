from pathlib import Path
import shutil,subprocess,importlib.util,json,hashlib
R=Path.cwd();P=R/'Development/results/isa-asm-phase3-20261029';W=P/'work';S=W/'knife3-source';shutil.copytree(R/'hip',S/'hip',dirs_exist_ok=True);(S/'Development/HIP').mkdir(parents=True,exist_ok=True);shutil.copy(R/'Development/HIP/swin_persistent_types.h',S/'Development/HIP/swin_persistent_types.h')
for f,func,shuffle,rowexpr in [('hip/wave_owned_mh.inc','w2_serial_norm','w2_shuffle','w2_rc()'),('hip/c512_qkv_attention_compact.inc','c5c_serial_norm','c5c_shuffle','__builtin_amdgcn_workitem_id_x()&15u')]:
 p=S/f;s=p.read_text();s='#ifndef SERIAL_NORM_SQUARE_CACHE\n#define SERIAL_NORM_SQUARE_CACHE 0\n#endif\n'+s;a=s.index('DEV float '+func+'(');b=s.index('{',a);i=b+1;depth=1
 while depth:depth+=(s[i]=='{')-(s[i]=='}');i+=1
 old=s[b+1:i-1];new='''
#if SERIAL_NORM_SQUARE_CACHE
#pragma clang fp contract(off)
 uint row='''+rowexpr+''';f8 lsq,hsq;_Pragma("unroll 8")for(uint e=0;e<8;e++){lsq[e]=lo[e]*lo[e];hsq[e]=hi[e]*hi[e];}float sum=0.f;
 _Pragma("unroll 8")for(uint e=0;e<8;e++)sum+=lsq[e];sum='''+shuffle+'''(sum,row);
 _Pragma("unroll 8")for(uint e=0;e<8;e++)sum+=lsq[e];sum='''+shuffle+'''(sum,row+16);
 _Pragma("unroll 8")for(uint e=0;e<8;e++)sum+=hsq[e];sum='''+shuffle+'''(sum,row);
 _Pragma("unroll 8")for(uint e=0;e<8;e++)sum+=hsq[e];return '''+shuffle+'''(sum,row+16);
#else
'''+old+'\n#endif\n';s=s[:b+1]+new+s[i-1:];p.write_text(s);(P/('knife3-'+Path(f).name+'.patch')).write_text(subprocess.run(['diff','-u','--label','a/'+f,'--label','b/'+f,str(R/f),str(p)],capture_output=True,text=True).stdout)
spec=importlib.util.spec_from_file_location('recipe',R/'Development/tools/llvm-fork/compile-modules.py');m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m);sources={x[0]:x[2] for x in m.recipe(S/'hip')};remote=r'D:\DLSSNR-Lab\isa-asm-phase3-20261029';rtc=r'D:\DLSSNR-Lab\gap-phase5-20261023\rtc_compile-current.exe';files=[{'path':rtc,'sha256':'553385a0ba6465f332208cfd5830224e255db9973101050f760e3117e5575197'}];jobs=[]
for name in ['c512-m32-mh','swin-persistent']:
 f=W/('knife3-'+name+'.hip');f.write_text('#define SERIAL_NORM_SQUARE_CACHE 1\n'+sources[name]);files.append({'path':remote+'\\'+f.name,'sha256':hashlib.sha256(f.read_bytes()).hexdigest()});jobs.append({'label':'knife3/compile-'+name,'exe':rtc,'cpu_compile':True,'env':{'RTC_EXTRA_OPTS':''},'arguments':'"{OUT}\\'+name+'.hsaco" "'+remote+'\\'+f.name+'" comgr gfx1201'})
(P/'knife3-compile.json').write_text(json.dumps({'name':'phase3-norm-square-compile','files':files,'jobs':jobs},indent=2))
p=S/'hip/build-modules.ps1';s=p.read_text();needle="@{ name = 'c64-wave2-fast'; defines = @(";assert s.count(needle)==1;s=s.replace(needle,needle+"'SERIAL_NORM_SQUARE_CACHE 1',");p.write_text(s)
