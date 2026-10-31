from pathlib import Path
import shutil,importlib.util,json,hashlib
p=Path(__file__).parent;root=p/'pair-scope/source';shutil.copytree('src',root/'src',dirs_exist_ok=True);shutil.copytree('hip',root/'hip',dirs_exist_ok=True,ignore=shutil.ignore_patterns('modules','*.hsaco','*.exe'));d=root/'Development/HIP';d.mkdir(parents=True,exist_ok=True)
for f in Path('Development/HIP').glob('*.h'):shutil.copyfile(f,d/f.name)
shutil.copyfile('Development/HIP/benchmark_live_capture.cpp',d/'benchmark_live_capture.cpp')
f=root/'hip/deep_fast.hip';s=f.read_text();sig='template<uint MAXT,bool ByteInput=false,bool ByteOut=false,bool VFragment=false,bool QKFragment=false>\nDEV void vit_attention_transposed_score_body';assert s.count(sig)==1;s=s.replace(sig,sig.replace('QKFragment=false>','QKFragment=false,bool PairExponent=false>'))
old='''  for(uint e=0;e<8;e++){
   float af=clampf(a[e]*from_half(0x2dbb)+1.708984375f,1.439453125f,1.9775390625f);
   uint hb=(bits(af)>>13)-0x1c000u;unsigned short u=(unsigned short)(((hb<<4)+0x4000u)&65535u);
   union{unsigned short u;_Float16 h;}cv;cv.u=u;x[e]=cv.h;'''
new='''  uint pair_exp[4]{};
  if constexpr(PairExponent){
   for(uint e=0;e<4;e++){
    float af0=clampf(a[2*e]*from_half(0x2dbb)+1.708984375f,1.439453125f,1.9775390625f);
    float af1=clampf(a[2*e+1]*from_half(0x2dbb)+1.708984375f,1.439453125f,1.9775390625f);
    uint hpair;asm("v_cvt_pkrtz_f16_f32 %0, %1, %2":"=v"(hpair):"v"(af0),"v"(af1));
    pair_exp[e]=(hpair<<4)&0x3ff03ff0u;
   }
   x=__builtin_bit_cast(h8,pair_exp);
  }
  for(uint e=0;e<8;e++){
   unsigned short u;
   if constexpr(PairExponent)u=static_cast<unsigned short>(pair_exp[e/2]>>(16*(e%2)));
   else{float af=clampf(a[e]*from_half(0x2dbb)+1.708984375f,1.439453125f,1.9775390625f);
    uint hb=(bits(af)>>13)-0x1c000u;u=(unsigned short)(((hb<<4)+0x4000u)&65535u);}
   // Do not bit_cast a half-vector component lvalue: current COMGR extracts element0.
   union{unsigned short u;_Float16 h;}cv;cv.u=u;if constexpr(!PairExponent)x[e]=cv.h;'''
assert s.count(old)==2;s=s.replace(old,new,1)
line='WAVE void vit_attention_fused_640_bytein_bout_qkfrag(const float*in,float*out,uint tokens,const uint*reuse_gate){if(reuse_gate&&reuse_gate[0])return;vit_attention_transposed_score_body<640,true,true,true,true>(in,out,tokens);}'
assert s.count(line)==1
s=s.replace(line,line+'\n#if VIT_FAST_NUM==15\nWAVE void vit_attention_fused_640_bytein_bout_qkfrag_rtzpair(const float*in,float*out,uint tokens,const uint*reuse_gate){if(reuse_gate&&reuse_gate[0])return;vit_attention_transposed_score_body<640,true,true,true,true,true>(in,out,tokens);}\n#endif');f.write_text(s)
f=d/'hip_reference_network.h';s=f.read_text();anchor=' void InitVitQKFragment()';i=s.index(anchor);helper=''' bool vit_exp_pair_requested=true,vit_exp_pair_available=false;unsigned long long vit_exp_pair_calls=0;
 void InitVitExpPair(){const char*v=std::getenv("NR_R5_VIT_EXP_PAIR");if(!v||!*v)v=std::getenv("DLSS5_HIP_VIT_EXP_PAIR");vit_exp_pair_requested=!v||!*v||std::strcmp(v,"0")!=0;if(vit_exp_pair_requested&&fast_numeric&&opt.fast_deep&&!opt.integration.select_module&&CompactVitTailDeviceEligible())vit_exp_pair_available=HasFn("deep_fast","vit_attention_fused_640_bytein_bout_qkfrag_rtzpair");}
 bool VitExpPairEligible(U n)const{return vit_exp_pair_requested&&vit_exp_pair_available&&small_active&&W==1920&&H==1152&&n==640&&fast_numeric&&multi_pass==1&&vfrag_ae_off&&!opt.graph&&!temporal_feature_tap_active&&!opt.experimental_temporal&&!pulse_frame_history&&!native_post_output&&!observer&&opt.dump_dir.empty();}
''';s=s[:i]+helper+s[i:]
s=s.replace('InitVitVFragment();InitVitQKFragment();','InitVitVFragment();InitVitQKFragment();InitVitExpPair();')
anchor=' /* fast tier HIP_VIT_ATTN_KSPLIT */Run("deep",fused.c_str()';assert s.count(anchor)==1;s=s.replace(anchor,' if(qkfrag&&VitExpPairEligible(n)&&fused=="vit_attention_fused_640_bytein_bout_qkfrag"){fused+="_rtzpair";++vit_exp_pair_calls;}'+anchor)
s=s.replace('~Network(){std::fprintf(stderr,','~Network(){std::fprintf(stderr,"VIT_EXP_PAIR requested=%u available=%u calls=%llu\\n",unsigned(vit_exp_pair_requested),unsigned(vit_exp_pair_available),vit_exp_pair_calls);std::fprintf(stderr,',1);f.write_text(s)
f=root/'hip/deep_fast.hip';s=f.read_text();block='#if VIT_FAST_NUM==15\nWAVE void vit_attention_fused_640_bytein_bout_qkfrag_rtzpair(const float*in,float*out,uint tokens,const uint*reuse_gate){if(reuse_gate&&reuse_gate[0])return;vit_attention_transposed_score_body<640,true,true,true,true,true>(in,out,tokens);}\n#endif'
assert s.count(block)==1;s=s.replace(block,'');s+='\n'+block.replace('#if VIT_FAST_NUM==15','#if HIP_VIT_ATTN_TRANSPOSED_SCORE && VIT_FAST_NUM==15')+'\n';f.write_text(s)
f=d/'hip_reference_network.h';s=f.read_text();assert s.count('if(qkfrag&&VitExpPairEligible(n)&&fused=="vit_attention_fused_640_bytein_bout_qkfrag"){fused+="_rtzpair";++vit_exp_pair_calls;} /* fast tier HIP_VIT_ATTN_KSPLIT */Run("deep",fused.c_str(),')==1;f.write_text(s.replace('if(qkfrag&&VitExpPairEligible(n)&&fused=="vit_attention_fused_640_bytein_bout_qkfrag"){fused+="_rtzpair";++vit_exp_pair_calls;} /* fast tier HIP_VIT_ATTN_KSPLIT */Run("deep",fused.c_str(),','const char*selected_attention=fused.c_str();if(qkfrag&&VitExpPairEligible(n)&&fused=="vit_attention_fused_640_bytein_bout_qkfrag"){selected_attention="vit_attention_fused_640_bytein_bout_qkfrag_rtzpair";++vit_exp_pair_calls;} /* fast tier HIP_VIT_ATTN_KSPLIT */Run("deep",selected_attention,'))
sp=importlib.util.spec_from_file_location('recipe','Development/tools/llvm-fork/compile-modules.py');m=importlib.util.module_from_spec(sp);sp.loader.exec_module(m);src=next(x[2] for x in m.recipe(root/'hip') if x[0]=='deep_fast-packed-fast');(p/'pair-scope.hip').write_text(src)
r=r'D:\DLSSNR-Lab\yami-round5-20261031';cc=r'D:\DLSSNR-Lab\gap-phase5-20261023\rtc_compile-current.exe';x={'name':'pair-scope','files':[{'path':cc,'sha256':'553385a0ba6465f332208cfd5830224e255db9973101050f760e3117e5575197'},{'path':r+'\\pair-scope.hip','sha256':hashlib.sha256(src.encode()).hexdigest()}],'jobs':[{'label':'pair-scope-compile','exe':cc,'cpu_compile':True,'env':{'RTC_EXTRA_OPTS':''},'arguments':'"{OUT}\\deep_fast-packed-fast.hsaco" "'+r+'\\pair-scope.hip" comgr gfx1201'}]};(p/'pair-scope-compile.json').write_text(json.dumps(x,indent=2))
