from pathlib import Path
import shutil,importlib.util,subprocess,json,hashlib
R=Path.cwd();P=R/'Development/results/isa-asm-phase3-20261029';W=P/'work';S=W/'knife1-source';shutil.copytree(R/'hip',S/'hip',dirs_exist_ok=True);(S/'Development/HIP').mkdir(parents=True,exist_ok=True);shutil.copy(R/'Development/HIP/swin_persistent_types.h',S/'Development/HIP/swin_persistent_types.h')
p=S/'hip/c512_m32_deep.inc';s=p.read_text();s='#ifndef C512_ACT_PAIR\n#define C512_ACT_PAIR 0\n#endif\n#ifndef C512_FFN_PREFETCH\n#define C512_FFN_PREFETCH 0\n#endif\n'+s;a=s.index('template<bool TVEC,bool ByteIn=false>');old='''    i2 a{};for(uint e=0;e<8;e++){float v=Hrtz(ex[e]),gate=clampf(v,-4.f,4.f),poly=__builtin_fmaf(gate,__builtin_fmaf(absf(gate),-.055908203125f,.447265625f),.89453125f);a[e/4]=int(uint(a[e/4])|(uint(byte_F(v*poly))<<(8*(e%4))));}''';assert s[a:].count(old)==1
new='''#if C512_ACT_PAIR
    float av[8];_Pragma("unroll") for(uint e=0;e<8;e++){float v=Hrtz(ex[e]),gate=clampf(v,-4.f,4.f),poly=__builtin_fmaf(gate,__builtin_fmaf(absf(gate),-.055908203125f,.447265625f),.89453125f);av[e]=fp8_add0(v*poly);}i2 a=c5d_pack8f(av);
#else
'''+old+'''\n#endif'''
s=s[:a]+s[a:].replace(old,new,1)
# Separate 1B: two-slot CBW/EBW ring; first weights precede mix quantization, next weights precede activation.
a=s.index('  i2 x8[4];',s.index('template<bool TVEC,bool ByteIn=false>'));s=s[:a]+'''#if C512_FFN_PREFETCH
  w16_i4 pref_c[2][4],pref_e[2][2][2];
  auto prefetch=[&](uint h){uint slot=h&1;_Pragma("unroll") for(uint ob=0;ob<4;ob++)__builtin_memcpy(&pref_c[slot][ob],wc+(ob*8+h)*512+(gr()*16+rc())*16,16);_Pragma("unroll") for(uint hp=0;hp<2;hp++)for(uint k=0;k<2;k++)__builtin_memcpy(&pref_e[slot][hp][k],we+(((h*2+hp)*2+k)*512)+(gr()*16+rc())*16,16);};
  prefetch(0);__builtin_amdgcn_sched_barrier(0);
#endif
'''+s[a:]
a=s.index('  for(uint h2=0;h2<8;h2++){',a);b=s.index('  if constexpr(TVEC)',a);body=s[a:b]
body=body.replace('''   _Pragma("unroll") for(uint ob=0;ob<4;ob++)__builtin_memcpy(&cbw[ob],wc+(ob*8+h2)*512+(gr()*16+rc())*16,16);''','''#if C512_FFN_PREFETCH
   _Pragma("unroll") for(uint ob=0;ob<4;ob++)cbw[ob]=pref_c[h2&1][ob];
#else
   _Pragma("unroll") for(uint ob=0;ob<4;ob++)__builtin_memcpy(&cbw[ob],wc+(ob*8+h2)*512+(gr()*16+rc())*16,16);
#endif''')
old='''     w16_i4 ebw;__builtin_memcpy(&ebw,we+(hb*2+ks2)*512+(gr()*16+rc())*16,16);''';assert old in body;body=body.replace(old,'''     w16_i4 ebw;
#if C512_FFN_PREFETCH
     ebw=pref_e[h2&1][hp][ks2];
#else
     __builtin_memcpy(&ebw,we+(hb*2+ks2)*512+(gr()*16+rc())*16,16);
#endif''')
needle='#if C512_ACT_PAIR';body=body.replace(needle,'''#if C512_FFN_PREFETCH
    if(hp==1&&h2+1<8){prefetch(h2+1);__builtin_amdgcn_sched_barrier(0);}
#endif
'''+needle,1);s=s[:a]+body+s[b:];p.write_text(s)
(P/'knife1.patch').write_text(subprocess.run(['diff','-u','--label','a/hip/c512_m32_deep.inc','--label','b/hip/c512_m32_deep.inc',str(R/'hip/c512_m32_deep.inc'),str(p)],capture_output=True,text=True).stdout)
spec=importlib.util.spec_from_file_location('recipe',R/'Development/tools/llvm-fork/compile-modules.py');m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m);src=next(x[2] for x in m.recipe(S/'hip')if x[0]=='c512-m32-deep');remote=r'D:\DLSSNR-Lab\isa-asm-phase3-20261029';rtc=r'D:\DLSSNR-Lab\gap-phase5-20261023\rtc_compile-current.exe';files=[{'path':rtc,'sha256':'553385a0ba6465f332208cfd5830224e255db9973101050f760e3117e5575197'}];jobs=[]
for role,defs in [('baseline',''),('pair','#define C512_ACT_PAIR 1\n'),('prefetch','#define C512_ACT_PAIR 1\n#define C512_FFN_PREFETCH 1\n')]:
 f=W/(role+'.hip');f.write_text(defs+src);files.append({'path':remote+'\\'+f.name,'sha256':hashlib.sha256(f.read_bytes()).hexdigest()});jobs.append({'label':'knife1/compile-'+role,'exe':rtc,'cpu_compile':True,'env':{'RTC_EXTRA_OPTS':''},'arguments':'"{OUT}\\c512-m32-deep.hsaco" "'+remote+'\\'+f.name+'" comgr gfx1201'})
(P/'knife1-compile.json').write_text(json.dumps({'name':'phase3-ffn-pack-prefetch-compile','files':files,'jobs':jobs},indent=2))
for n in ['run-jobs.ps1','collect.ps1','collect-local.py','analyze.py','inspect-isa.py','cleanup-case.ps1']:
 (P/n).write_text((R/'Development/results/isa-asm-phase2-20261028'/n).read_text().replace('isa-asm-phase2-20261028','isa-asm-phase3-20261029'))
(P/'.gitignore').write_text('/work/\n/payload/\n/**/evidence/\n__pycache__/\n');(P/'.gitattributes').write_text('*.patch whitespace=-blank-at-eol,-blank-at-eof\n')
