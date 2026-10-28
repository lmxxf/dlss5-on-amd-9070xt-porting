from pathlib import Path
import shutil,importlib.util,json,hashlib,subprocess
R=Path.cwd();P=R/'Development/results/isa-asm-phase2-20261028';W=P/'work';S=W/'knife3-source';shutil.copytree(R/'hip',S/'hip',dirs_exist_ok=True);(S/'Development/HIP').mkdir(parents=True,exist_ok=True);shutil.copy(R/'Development/HIP/swin_persistent_types.h',S/'Development/HIP/swin_persistent_types.h')
p=S/'hip/multihead_fast_padded.hip';s=p.read_text();s='#ifndef MH_POOL_ORTHO\n#define MH_POOL_ORTHO 0\n#endif\n'+s;at=s.index('template<uint C,bool Gout=false,bool HalfIn=false,bool ByteOut=false,bool F8W16=false>')
helper='''DEV i2 pool_ortho_pack8(f8 v){_Pragma("unroll 8") for(uint e=0;e<8;e++)v[e]=__builtin_amdgcn_fmed3f(v[e]+0.f,-448.f,448.f);i2 r;r[0]=__builtin_amdgcn_cvt_pk_fp8_f32(v[0],v[1],0,false);r[0]=__builtin_amdgcn_cvt_pk_fp8_f32(v[2],v[3],r[0],true);r[1]=__builtin_amdgcn_cvt_pk_fp8_f32(v[4],v[5],0,false);r[1]=__builtin_amdgcn_cvt_pk_fp8_f32(v[6],v[7],r[1],true);return r;}
''';s=s[:at]+helper+s[at:];a=s.index('DEV void mh_pool_project_group_body(');b=s.index('\nKERNEL ',a);body=s[a:b]
old='''    sum[j]=__builtin_amdgcn_wmma_f32_16x16x16_fp8_fp8_w32_gfx12(a0,i2{b[0],b[1]},sum[j]);
    sum[j]=__builtin_amdgcn_wmma_f32_16x16x16_fp8_fp8_w32_gfx12(a1,i2{b[2],b[3]},sum[j]);''';assert body.count(old)==1
body=body.replace(old,'''    if constexpr(MH_POOL_ORTHO&&ByteOut&&!Gout){sum[j]=__builtin_amdgcn_wmma_f32_16x16x16_fp8_fp8_w32_gfx12(i2{b[0],b[1]},a0,sum[j]);sum[j]=__builtin_amdgcn_wmma_f32_16x16x16_fp8_fp8_w32_gfx12(i2{b[2],b[3]},a1,sum[j]);}else{
'''+old+'\n    }')
needle=' _Pragma("unroll") for(uint e=0;e<8;e++){uint q=first+g*8+e;';pos=body.index(needle);body=body[:pos]+''' if constexpr(MH_POOL_ORTHO&&F8W16&&ByteOut&&!Gout){uint q=first+rc;if(q<M){bool inv=valid_width&&(q%width>=valid_width||q/width>=valid_height);_Pragma("unroll") for(uint j=0;j<4;j++){i2 v=inv?i2{}:pool_ortho_pack8(acc[j]);__builtin_memcpy(__builtin_assume_aligned(reinterpret_cast<unsigned char*>(output)+q*N+colg+j*16+g*8,8),&v,8);}}return;}
'''+body[pos:];s=s[:a]+body+s[b:];p.write_text(s)
p=S/'hip/deep_fast.hip';s=p.read_text();s='#ifndef SPLIT_PROJ_ORTHO\n#define SPLIT_PROJ_ORTHO 0\n#endif\n'+s;a=s.index('   uint b8v[2]{};float fv[8];',s.index('if constexpr(Transposed)'));b=s.index('  return;',a);old=s[a:b];assert old.rstrip().endswith(';}');oldinner=old.rstrip()[:-1]
new='''   f8 xc;_Pragma("unroll") for(uint e=0;e<8;e++)xc[e]=__builtin_amdgcn_fmed3f(VIT_FAST_H(acc[j][e])+0.f,-448.f,448.f);
   i2 bv;bv[0]=__builtin_amdgcn_cvt_pk_fp8_f32(xc[0],xc[1],0,false);bv[0]=__builtin_amdgcn_cvt_pk_fp8_f32(xc[2],xc[3],bv[0],true);bv[1]=__builtin_amdgcn_cvt_pk_fp8_f32(xc[4],xc[5],0,false);bv[1]=__builtin_amdgcn_cvt_pk_fp8_f32(xc[6],xc[7],bv[1],true);
   f8 fv{__builtin_amdgcn_cvt_f32_fp8(bv[0],0),__builtin_amdgcn_cvt_f32_fp8(bv[0],1),__builtin_amdgcn_cvt_f32_fp8(bv[0],2),__builtin_amdgcn_cvt_f32_fp8(bv[0],3),__builtin_amdgcn_cvt_f32_fp8(bv[1],0),__builtin_amdgcn_cvt_f32_fp8(bv[1],1),__builtin_amdgcn_cvt_f32_fp8(bv[1],2),__builtin_amdgcn_cvt_f32_fp8(bv[1],3)};
   __builtin_memcpy(__builtin_assume_aligned(out+r*512+c0,16),&fv,32);__builtin_memcpy(__builtin_assume_aligned(out8+((first/16)*16+c0/32)*512+rc()*32+c0%32,8),&bv,8);
'''
s=s[:a]+'''#if SPLIT_PROJ_ORTHO
   if constexpr(W32){
'''+new+'   }else{\n'+oldinner+'\n   }\n#else\n'+oldinner+'\n#endif\n  }\n'+s[b:]
old='split_projection_frag_w16_body<true>(in8,w,skip,out,out8,nullptr,tokens);';assert s.count(old)==1;s=s.replace(old,'split_projection_frag_w16_body<true,false,false,(SPLIT_PROJ_ORTHO!=0)>(in8,w,skip,out,out8,nullptr,tokens);');old='split_projection_frag_w16_body<true,false,true,false>(in8,w,skip,out,out8,nullptr,tokens);';assert s.count(old)==1;s=s.replace(old,'split_projection_frag_w16_body<true,false,true,(SPLIT_PROJ_ORTHO!=0)>(in8,w,skip,out,out8,nullptr,tokens);');p.write_text(s)
for f in ['hip/deep_fast.hip','hip/multihead_fast_padded.hip']:(P/('knife3-'+Path(f).name+'.patch')).write_text(subprocess.run(['diff','-u','--label','a/'+f,'--label','b/'+f,str(R/f),str(S/f)],capture_output=True,text=True).stdout)
spec=importlib.util.spec_from_file_location('recipe',R/'Development/tools/llvm-fork/compile-modules.py');m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m);sources={x[0]:x[2] for x in m.recipe(S/'hip')};remote=r'D:\DLSSNR-Lab\isa-asm-phase2-20261028';rtc=r'D:\DLSSNR-Lab\gap-phase5-20261023\rtc_compile-current.exe';files=[{'path':rtc,'sha256':'553385a0ba6465f332208cfd5830224e255db9973101050f760e3117e5575197'}];jobs=[]
for role,module,define in [('pool','multihead-fast-padded-wave-packed','MH_POOL_ORTHO 1'),('split','deep_fast-packed-fast','SPLIT_PROJ_ORTHO 1')]:
 f=W/('knife3-'+role+'.hip');f.write_text('#define '+define+'\n'+sources[module]);files.append({'path':remote+'\\'+f.name,'sha256':hashlib.sha256(f.read_bytes()).hexdigest()});jobs.append({'label':'knife3/compile-'+role,'exe':rtc,'cpu_compile':True,'env':{'RTC_EXTRA_OPTS':''},'arguments':'"{OUT}\\'+module+'.hsaco" "'+remote+'\\'+f.name+'" comgr gfx1201'})
(P/'knife3-compile.json').write_text(json.dumps({'name':'phase2-pool-split-compile','files':files,'jobs':jobs},indent=2))
