import pathlib,subprocess,shutil,json,hashlib,importlib.util
R=pathlib.Path('/home/lmxxf/work/ai-theorys-study/wechat/assets/297');W=pathlib.Path('/home/lmxxf/work/dlss5-gap-phase3-20261023/A');O=pathlib.Path(__file__).parent
S=W/'source';shutil.copytree(R/'hip',S/'hip',dirs_exist_ok=True);shutil.copytree(R/'src',S/'src',dirs_exist_ok=True);shutil.copytree(R/'Development/HIP',S/'Development/HIP',dirs_exist_ok=True)
old=subprocess.check_output(['git','-C',str(R),'show','vit-1080-gap:hip/deep_fast.hip'],text=True)
a=old.index('DEV void vit_attn_m32_prob(');b=old.index('\n#endif\ntemplate<uint MAXT,bool ByteInput',a)
body=old[a:b].replace('vit_attn_m32_prob','vit_attn_qt32_prob').replace('vit_attention_m32_body','vit_attention_qt32_body').replace('byte_F(Hrtz(acc','byte_F(vit_attn_hrtz(acc')
body=body.replace('template<uint MAXT>\nDEV void vit_attention_qt32_body','template<uint MAXT,bool Two>\nDEV void vit_attention_qt32_body').replace(' const bool two=first+16<tokens;\n','')
body=body.replace('if(two)','if constexpr(Two)')
for needle in ['a1=__builtin_amdgcn_wmma','vit_attn_qt32_prob(a1,x1,xb1);','sum1=__builtin_amdgcn_wmma','acc1[c]=__builtin_amdgcn_wmma']:
 body=body.replace(needle,'if constexpr(Two)'+needle)
# Explicit provenance: this is a current-baseline re-test of the old exact M32 organization.
body='''// Phase3 A: current FAST1 re-test of 2026-10-01 exact M32 (19 SAME, performance STOP).
// Each query keeps identical score/denominator/AV K order; only independent query chains share K/V loads.
// Export specializes Two=false for the lone tail tile: no Q1 reads, arithmetic, or stores.
#if HIP_VIT_ATTN_TRANSPOSED_SCORE && HIP_VIT_ATTN_NATIVE_HALF && HIP_VIT_ATTN_PROB_PAIR && HIP_VIT_ATTN_TRANSPOSED_AV
'''+body+'''
WAVE void vit_attention_fused_400_bytein_bout_qt32(const float*in,float*out,uint tokens,const uint*reuse_gate){if(reuse_gate&&reuse_gate[0])return;if(bid()/32*32+16<tokens)vit_attention_qt32_body<400,true>(in,out,tokens);else vit_attention_qt32_body<400,false>(in,out,tokens);}
WAVE void vit_attention_fused_640_bytein_bout_qt32(const float*in,float*out,uint tokens,const uint*reuse_gate){if(reuse_gate&&reuse_gate[0])return;if(bid()/32*32+16<tokens)vit_attention_qt32_body<640,true>(in,out,tokens);else vit_attention_qt32_body<640,false>(in,out,tokens);}
#endif
'''
p=S/'hip/deep_fast.hip';s=p.read_text();pos=s.index('// FAST TIER (lossy, 2026-10-01 fast-tier): HIP_VIT_ATTN_KSPLIT');s=s[:pos]+body+s[pos:];p.write_text(s)
p=S/'Development/HIP/hip_reference_network.h';s=p.read_text();needle='   if(kernel=="vit_attention_inverse_fast")count=Count(size_t(count)*16);';assert s.count(needle)==1
s=s.replace(needle,needle+'''\n   if(kernel=="vit_attention_fused_400_bytein_bout_qt32"||kernel=="vit_attention_fused_640_bytein_bout_qt32"){threads=32;groups=((count/512+31)/32)*32;}''')
pos=s.index(' Tensor Vit(') if ' Tensor Vit(' in s else -1
# Place selector beside existing numeric helpers, inside Network.
pos=s.index(' unsigned long long c512_w16_calls=')
selector=''' bool VitQt32Requested()const{static const bool on=[](){const char*v=std::getenv("NR_VIT_QT32");return v&&std::strcmp(v,"1")==0;}();return on;}
 unsigned long long vit_qt32_calls=0;
 void SelectVitQt32(std::string&fused,U n){if(!small_active&&fast_numeric&&VitQt32Requested()&&(n==400||n==640)&&(fused=="vit_attention_fused_400_bytein_bout"||fused=="vit_attention_fused_640_bytein_bout")&&HasFn("deep_fast","vit_attention_fused_400_bytein_bout_qt32")&&HasFn("deep_fast","vit_attention_fused_640_bytein_bout_qt32")){fused+="_qt32";++vit_qt32_calls;}}
'''
s=s[:pos]+selector+s[pos:]
needle='Run("deep",fused.c_str(),size_t(n)*512,P(norm),P(av),n);';assert s.count(needle)==2
s=s.replace(needle,'SelectVitQt32(fused,n);'+needle)
needle='~Network(){if(!pulse_lease.Close())';assert s.count(needle)==1
s=s.replace(needle,'~Network(){std::fprintf(stderr,"VIT_QT32 requested=%u calls=%llu\\n",unsigned(VitQt32Requested()),vit_qt32_calls);if(!pulse_lease.Close())');p.write_text(s)
files=['hip/deep_fast.hip','Development/HIP/hip_reference_network.h'];patch=''
for f in files:patch+=subprocess.run(['diff','-u','--label','a/'+f,'--label','b/'+f,str(R/f),str(S/f)],capture_output=True,text=True).stdout
(O/'candidate.patch').write_text(patch)
(O/'source-identity.json').write_text(json.dumps({'old_m32_ref':subprocess.check_output(['git','-C',str(R),'rev-parse','vit-1080-gap'],text=True).strip(),'files':{f:{'base':hashlib.sha256((R/f).read_bytes()).hexdigest(),'candidate':hashlib.sha256((S/f).read_bytes()).hexdigest()} for f in files}},indent=2))
sp=importlib.util.spec_from_file_location('recipe',R/'Development/tools/llvm-fork/compile-modules.py');mod=importlib.util.module_from_spec(sp);sp.loader.exec_module(mod)
for role,root in [('baseline',R),('candidate',S)]:
 for name,compiler,src,defs,parts,opts in mod.recipe(root/'hip'):
  if name=='deep_fast-packed-fast':(W/(role+'.generated.hip')).write_text(src)
print(S)
