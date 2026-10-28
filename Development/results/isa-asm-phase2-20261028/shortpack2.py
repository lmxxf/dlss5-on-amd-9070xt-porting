from pathlib import Path
import importlib.util,json,hashlib
R=Path.cwd();P=R/'Development/results/isa-asm-phase2-20261028';W=P/'work';S=W/'knife2-source';p=S/'hip/deep_fast.hip';s=p.read_text();at=s.index('DEV f8 ortho_unpack8')
helper='''DEV i2 ortho_pack8_short(f8 v){i2 r;
#define OCP(X) __builtin_amdgcn_fmed3f((X)+0.f,-448.f,448.f)
 r[0]=__builtin_amdgcn_cvt_pk_fp8_f32(OCP(v[0]),OCP(v[1]),0,false);r[0]=__builtin_amdgcn_cvt_pk_fp8_f32(OCP(v[2]),OCP(v[3]),r[0],true);r[1]=__builtin_amdgcn_cvt_pk_fp8_f32(OCP(v[4]),OCP(v[5]),0,false);r[1]=__builtin_amdgcn_cvt_pk_fp8_f32(OCP(v[6]),OCP(v[7]),r[1],true);
#undef OCP
 return r;}
''';s=s[:at]+helper+s[at:];a=s.index('DEV void vit_contract_ortho_w16');b=s.index('// Fragment-weight twin',a);s=s[:a]+s[a:b].replace('ortho_pack8(v)','ortho_pack8_short(v)')+s[b:];p.write_text(s)
p=S/'hip/vit_stream.inc';s=p.read_text();a=s.index('DEV void vit_project_ortho_w16');b=s.index('WAVE void vit_stream_contract_frag_bstream_bout_compactw_w16',a);s=s[:a]+s[a:b].replace('ortho_pack8(v)','ortho_pack8_short(v)')+s[b:];p.write_text(s)
spec=importlib.util.spec_from_file_location('recipe',R/'Development/tools/llvm-fork/compile-modules.py');m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m);src=next(x[2] for x in m.recipe(S/'hip')if x[0]=='vit-stream-fast');remote=r'D:\DLSSNR-Lab\isa-asm-phase2-20261028';rtc=r'D:\DLSSNR-Lab\gap-phase5-20261023\rtc_compile-current.exe';files=[{'path':rtc,'sha256':'553385a0ba6465f332208cfd5830224e255db9973101050f760e3117e5575197'}];jobs=[]
for role,mask in [('contract',2),('project',4)]:
 f=W/('knife2-'+role+'-short.hip');f.write_text('#define VIT_ORTHO_MASK '+str(mask)+'\n'+src);files.append({'path':remote+'\\'+f.name,'sha256':hashlib.sha256(f.read_bytes()).hexdigest()});jobs.append({'label':'knife2/compile-'+role+'-short','exe':rtc,'cpu_compile':True,'env':{'RTC_EXTRA_OPTS':''},'arguments':'"{OUT}\\vit-stream-fast.hsaco" "'+remote+'\\'+f.name+'" comgr gfx1201'})
(P/'knife2-short-compile.json').write_text(json.dumps({'name':'phase2-pair-lifetime-compile','files':files,'jobs':jobs},indent=2))
