from pathlib import Path
import shutil,subprocess,importlib.util,json,hashlib
R=Path.cwd();P=R/'Development/results/isa-asm-phase3-20261029';W=P/'work';S=W/'knife2-source';shutil.copytree(R/'hip',S/'hip',dirs_exist_ok=True);(S/'Development/HIP').mkdir(parents=True,exist_ok=True);shutil.copy(R/'Development/HIP/swin_persistent_types.h',S/'Development/HIP/swin_persistent_types.h')
p=S/'hip/wave_owned_mh.inc';s=p.read_text();s='#ifndef W2_FOLD_EXPANDED\n#define W2_FOLD_EXPANDED 0\n#endif\n#ifndef W2_CONTRACT_PREFETCH\n#define W2_CONTRACT_PREFETCH 0\n#endif\n'+s;a=s.index('   f8 expanded[B][T]{};');b=s.index('  _Pragma("unroll") for(uint qi=0;qi<B;qi++)for(uint ci=0;ci<2;ci++){',a);old=s[a:b]
fold='''   if constexpr(W2_FOLD_EXPANDED&&W16&&T==2){
    i2 hidden[2][B];w2_i4 cbw[2]{};
    _Pragma("unroll") for(uint tile=0;tile<2;tile++){
     f8 ex[B]{};
     for(uint kt=0;kt<C/16;kt+=2){i2 in0[B],in1[B];
      _Pragma("unroll") for(uint qi=0;qi<B;qi++){in0[qi]=w2_load<C>(plane0,qb+qi,kt);in1[qi]=w2_load<C>(plane0,qb+qi,kt+1);}
      w2_i4 w=w2_weight16(fw,head*128+(ht+tile)*16+rc,C,kt*16);i2 b0={w[0],w[1]},b1={w[2],w[3]};
      _Pragma("unroll") for(uint qi=0;qi<B;qi++)ex[qi]=__builtin_amdgcn_wmma_f32_16x16x16_fp8_fp8_w32_gfx12(b0,in0[qi],ex[qi]);
      _Pragma("unroll") for(uint qi=0;qi<B;qi++)ex[qi]=__builtin_amdgcn_wmma_f32_16x16x16_fp8_fp8_w32_gfx12(b1,in1[qi],ex[qi]);
     }
     if(tile==1){_Pragma("unroll") for(uint ci=0;ci<2;ci++)cbw[ci]=w2_weight16(fw+4*C*C,head*32+ci*16+rc,4*C,head*128+ht*16);__builtin_amdgcn_sched_barrier(0);}
     _Pragma("unroll") for(uint qi=0;qi<B;qi++){
      W2_Q8_DECL(packed);for(uint e=0;e<8;e++){float a=ex[qi][e],g=clampf(a,-4.f,4.f),q=__builtin_fmaf(absf(g),-.055908203125f,.447265625f),poly=__builtin_fmaf(g,q,.89453125f);W2_Q8_SETF(packed,e,a,poly);}W2_Q8_END(packed);hidden[tile][qi]=packed;
     }
     __asm__ volatile("":::"memory");__builtin_amdgcn_sched_barrier(0);
    }
    _Pragma("unroll") for(uint ci=0;ci<2;ci++){i2 b0={cbw[ci][0],cbw[ci][1]},b1={cbw[ci][2],cbw[ci][3]};
     _Pragma("unroll") for(uint qi=0;qi<B;qi++)contract[qi][ci]=__builtin_amdgcn_wmma_f32_16x16x16_fp8_fp8_w32_gfx12(b0,hidden[0][qi],contract[qi][ci]);
     _Pragma("unroll") for(uint qi=0;qi<B;qi++)contract[qi][ci]=__builtin_amdgcn_wmma_f32_16x16x16_fp8_fp8_w32_gfx12(b1,hidden[1][qi],contract[qi][ci]);
    }
   }else{
'''
old2=old;needle='''   _Pragma("unroll") for(uint tile=0;tile<T;tile++){
    i2 hidden[B];''';assert old2.count(needle)==1;old2=old2.replace(needle,'''   _Pragma("unroll") for(uint tile=0;tile<T;tile++){
    i2 cb[2];if constexpr(W2_CONTRACT_PREFETCH){__builtin_amdgcn_sched_barrier(0);_Pragma("unroll") for(uint ci=0;ci<2;ci++)cb[ci]=w2_weight(fw+4*C*C,head*32+ci*16+rc,4*C,head*128+(ht+tile)*16);__builtin_amdgcn_sched_barrier(0);}
    i2 hidden[B];''')
needle='''     i2 b=w2_weight(fw+4*C*C,head*32+ci*16+rc,4*C,head*128+(ht+tile)*16);''';assert old2.count(needle)==1;old2=old2.replace(needle,'''     i2 b;if constexpr(W2_CONTRACT_PREFETCH)b=cb[ci];else b=w2_weight(fw+4*C*C,head*32+ci*16+rc,4*C,head*128+(ht+tile)*16);''');s=s[:a]+fold+old2+'   }\n'+s[b:];p.write_text(s)
(P/'knife2.patch').write_text(subprocess.run(['diff','-u','--label','a/hip/wave_owned_mh.inc','--label','b/hip/wave_owned_mh.inc',str(R/'hip/wave_owned_mh.inc'),str(p)],capture_output=True,text=True).stdout)
spec=importlib.util.spec_from_file_location('recipe',R/'Development/tools/llvm-fork/compile-modules.py');m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m);row=next(x for x in m.recipe(S/'hip')if x[0]=='swin-persistent');f=W/'knife2-sp.hip';f.write_text('#define W2_FOLD_EXPANDED 1\n#define W2_CONTRACT_PREFETCH 1\n'+row[2]);remote=r'D:\DLSSNR-Lab\isa-asm-phase3-20261029';rtc=r'D:\DLSSNR-Lab\gap-phase5-20261023\rtc_compile-current.exe';(P/'knife2-sp-compile.json').write_text(json.dumps({'name':'phase3-fold-sp-compile','files':[{'path':rtc,'sha256':'553385a0ba6465f332208cfd5830224e255db9973101050f760e3117e5575197'},{'path':remote+r'\knife2-sp.hip','sha256':hashlib.sha256(f.read_bytes()).hexdigest()}],'jobs':[{'label':'knife2/compile-sp','exe':rtc,'cpu_compile':True,'env':{'RTC_EXTRA_OPTS':''},'arguments':'"{OUT}\\swin-persistent.hsaco" "'+remote+'\\knife2-sp.hip" comgr gfx1201'}]},indent=2))
p=S/'hip/build-modules.ps1';s=p.read_text();needle="@{ name = 'c64-wave2-fast'; defines = @(";assert s.count(needle)==1;s=s.replace(needle,needle+"'W2_FOLD_EXPANDED 1','W2_CONTRACT_PREFETCH 1',");p.write_text(s)
