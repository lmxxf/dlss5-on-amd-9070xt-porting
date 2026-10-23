import pathlib,shutil,subprocess,hashlib,json,importlib.util
R=pathlib.Path('/home/lmxxf/work/ai-theorys-study/wechat/assets/297');W=pathlib.Path('/home/lmxxf/work/dlss5-gap-phase4-20261023/knife1');O=pathlib.Path(__file__).parent;S=W/'source'
for f in ['hip','src','Development/HIP']:shutil.copytree(R/f,S/f,dirs_exist_ok=True)
p=S/'hip/vit_stream.inc';s=p.read_text();a=s.index('KERNEL __attribute__((amdgpu_flat_work_group_size(160,160))) void vit_stream_qkv_frag_bin_w5f8(');b=s.index('{',a);level=1;i=b+1
while level:level+=(s[i]=='{')-(s[i]=='}');i+=1
new=s[a:i].replace('void vit_stream_qkv_frag_bin_w5f8(','void vit_stream_qkv_frag_bin_w5f8_vfrag(',1)
old=''' #pragma unroll
 for(uint j=0;j<2;j++)for(uint e=0;e<8;e++)out[(part*tokens+first+gr()*8+e)*1024+row+j*16+rc()]=byte_F(acc[j][e]);'''
assert new.count(old)==1
new=new.replace(old,''' // Q/K remain row-major. V stores exactly the two B fragments this same lane will consume.
 if(part==2){
  uint __attribute__((ext_vector_type(4))) packed{};
  #pragma unroll
  for(uint j=0;j<2;j++)for(uint e=0;e<8;e++)packed[j*2+e/4]|=uint(byte_F(acc[j][e]))<<(8*(e%4));
  const size_t off=size_t(2)*tokens*1024+(size_t(row/32)*(tokens/16)+first/16)*512+lane*16;
  __builtin_memcpy(__builtin_assume_aligned(out+off,16),&packed,16);
 }else{
  #pragma unroll
  for(uint j=0;j<2;j++)for(uint e=0;e<8;e++)out[(part*tokens+first+gr()*8+e)*1024+row+j*16+rc()]=byte_F(acc[j][e]);
 }''')
s=s[:i]+'\n// Paired with attention *_vfrag; total QKV byte capacity is unchanged.\n'+new+s[i:];p.write_text(s)
p=S/'hip/deep_fast.hip';s=p.read_text();needle='template<uint MAXT,bool ByteInput=false,bool ByteOut=false>\nDEV void vit_attention_transposed_score_body';assert s.count(needle)==1
s=s.replace(needle,needle.replace('ByteOut=false>','ByteOut=false,bool VFragment=false>'))
a=s.index('DEV void vit_attention_transposed_score_body');b=s.index('\n#endif\n// FAST TIER (lossy, 2026-10-01',a);body=s[a:b]
old='''  for(uint c=0;c<2;c++){i2 y{};for(uint e=0;e<8;e++){uint vkey=key+gr()*8+e;if constexpr(ByteInput){uint b=in8[(2*tokens+vkey)*1024+head*32+c*16+rc()];y[e/4]=int(uint(y[e/4])|(b<<(8*(e%4))));}else pack(y,e,in[(2*tokens+vkey)*1024+head*32+c*16+rc()]);}'''
assert body.count(old)==1
body=body.replace(old,'''  uint __attribute__((ext_vector_type(4))) vpair{};
  if constexpr(VFragment){static_assert(ByteInput,"V fragment requires byte QKV");const size_t off=size_t(2)*tokens*1024+(size_t(head)*(tokens/16)+key/16)*512+(gr()*16+rc())*16;__builtin_memcpy(&vpair,__builtin_assume_aligned(in8+off,16),16);}
  for(uint c=0;c<2;c++){i2 y{};
   if constexpr(VFragment){y=i2{int(vpair[c*2]),int(vpair[c*2+1])};}
   else for(uint e=0;e<8;e++){uint vkey=key+gr()*8+e;if constexpr(ByteInput){uint b=in8[(2*tokens+vkey)*1024+head*32+c*16+rc()];y[e/4]=int(uint(y[e/4])|(b<<(8*(e%4))));}else pack(y,e,in[(2*tokens+vkey)*1024+head*32+c*16+rc()]);}''')
body+='''
WAVE void vit_attention_fused_400_bytein_bout_vfrag(const float*in,float*out,uint tokens,const uint*reuse_gate){if(reuse_gate&&reuse_gate[0])return;vit_attention_transposed_score_body<400,true,true,true>(in,out,tokens);}
WAVE void vit_attention_fused_640_bytein_bout_vfrag(const float*in,float*out,uint tokens,const uint*reuse_gate){if(reuse_gate&&reuse_gate[0])return;vit_attention_transposed_score_body<640,true,true,true>(in,out,tokens);}
'''
s=s[:a]+body+s[b:];p.write_text(s)
p=S/'Development/HIP/hip_reference_network.h';s=p.read_text();pos=s.index(' Tensor vit_in8;')
helper=''' bool vfrag_requested=false,vfrag_available=false,vfrag_ae_off=false;unsigned long long vfrag_producer_calls=0,vfrag_consumer_calls=0;
 void InitVitVFragment(){const char*v=std::getenv("NR_VIT_VFRAG");vfrag_requested=v&&std::strcmp(v,"1")==0;const char*ae=std::getenv("DLSS5_VIT_ADAPTIVE");vfrag_ae_off=ae&&std::strcmp(ae,"0")==0;if(vfrag_requested)vfrag_available=HasFn("vit_stream","vit_stream_qkv_frag_bin_w5f8_vfrag")&&HasFn("deep_fast","vit_attention_fused_400_bytein_bout_vfrag")&&HasFn("deep_fast","vit_attention_fused_640_bytein_bout_vfrag");}
 bool VitVFragmentEligible(U n){
  const bool shape=(W==1600&&H==960)||(W==1920&&(H==1088||H==1152));
  if(!vfrag_requested||!vfrag_available||!vfrag_ae_off||!shape||small_active||!fast_numeric||multi_pass!=1||adaptive_active||opt.graph||!opt.skip_blocks.empty()||!opt.dup_prefix.empty()||observer||!opt.dump_dir.empty()||opt.vit_byte_stream||opt.vit_half_stream||vit_stream_active!=3||!vit_contract_byte_edge||!opt.vit_qkv_fused||!opt.vit_qkv_frag||!opt.vit_qkv_fp8||!opt.vit_attn_fused||(n!=400&&n!=640))return false;
  if(HIP_VIT_ATTN_KSPLIT_HOST&&HasFn("deep_fast",std::string(n==400?"vit_attention_fused_400_bytein_bout":"vit_attention_fused_640_bytein_bout")+"_ks2"))return false;
  return true;
 }
'''
s=s[:pos]+helper+s[pos:]
needle='if(kernel=="vit_stream_qkv_frag_bin_w5f8")';assert s.count(needle)==1;s=s.replace(needle,'if(kernel=="vit_stream_qkv_frag_bin_w5f8"||kernel=="vit_stream_qkv_frag_bin_w5f8_vfrag")')
needle='auto qkv=opt.vit_qkv_fused?Tensor{}:New(size_t(n)*3072),norm=';assert s.count(needle)==1;s=s.replace(needle,'const bool vfrag=VitVFragmentEligible(n);'+needle)
needle='if(opt.vit_qkv_fused)Run("deep",';assert s.count(needle)==1;s=s.replace(needle,'if(opt.vit_qkv_fused){Run("deep",')
needle='vit_contract_byte_edge?"vit_stream_qkv_frag_bin_w5f8":';assert s.count(needle)==1;s=s.replace(needle,'vit_contract_byte_edge?(vfrag?"vit_stream_qkv_frag_bin_w5f8_vfrag":"vit_stream_qkv_frag_bin_w5f8"):')
needle='size_t(n)*3072,P(contract),qw,P(norm),n);else{Run';assert s.count(needle)==1;s=s.replace(needle,'size_t(n)*3072,P(contract),qw,P(norm),n);if(vfrag)++vfrag_producer_calls;}else{Run')
needle='if(vit_stream_active&1)fused+="_bout";';assert s.count(needle)==1;s=s.replace(needle,needle+'if(vfrag){fused+="_vfrag";++vfrag_consumer_calls;}')
needle='if(multi_pass>1)PrepareMultiPassFeeds();if(multi_skin)PrepareSkin();}catch';assert s.count(needle)==1;s=s.replace(needle,'InitVitVFragment();'+needle)
needle='~Network(){if(!pulse_lease.Close())';assert s.count(needle)==1;s=s.replace(needle,'~Network(){std::fprintf(stderr,"VIT_VFRAG requested=%u available=%u explicit_ae0=%u producers=%llu consumers=%llu\\n",unsigned(vfrag_requested),unsigned(vfrag_available),unsigned(vfrag_ae_off),vfrag_producer_calls,vfrag_consumer_calls);if(!pulse_lease.Close())')
p.write_text(s)
files=['hip/deep_fast.hip','hip/vit_stream.inc','Development/HIP/hip_reference_network.h'];patch=''
for f in files:patch+=subprocess.run(['diff','-u','--label','a/'+f,'--label','b/'+f,str(R/f),str(S/f)],capture_output=True,text=True).stdout
(O/'candidate.patch').write_text(patch);(O/'source-identity.json').write_text(json.dumps({f:{'base':hashlib.sha256((R/f).read_bytes()).hexdigest(),'candidate':hashlib.sha256((S/f).read_bytes()).hexdigest()}for f in files},indent=2))
spec=importlib.util.spec_from_file_location('recipe',R/'Development/tools/llvm-fork/compile-modules.py');m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m)
for role,root in [('baseline',R),('candidate',S)]:
 for name,compiler,src,defs,parts,opts in m.recipe(root/'hip'):
  if name in ['deep_fast-packed-fast','vit-stream-fast']:(W/(name+'.'+role+'.hip')).write_text(src)
print(S)
