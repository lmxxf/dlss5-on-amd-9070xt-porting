from pathlib import Path
import shutil,subprocess,hashlib,json,importlib.util
R=Path('/home/lmxxf/work/ai-theorys-study/wechat/assets/297');W=Path('/home/lmxxf/work/dlss5-gap-phase6-20261023/knife1');O=Path(__file__).parent;B=W/'baseline-source';S=W/'source'
if not B.exists():
 for f in ['hip','src','Development/HIP']:shutil.copytree(R/f,B/f)
for f in ['hip','src','Development/HIP']:shutil.copytree(B/f,S/f,dirs_exist_ok=True)
p=S/'hip/vit_stream.inc';s=p.read_text();a=s.index('KERNEL __attribute__((amdgpu_flat_work_group_size(160,160))) void vit_stream_qkv_frag_bin_w5f8_vfrag(');b=s.index('{',a);i=b+1;depth=1
while depth:depth+=(s[i]=='{')-(s[i]=='}');i+=1
new=s[a:i].replace('_w5f8_vfrag(', '_w5f8_qkfrag(',1)
old='''  #pragma unroll
  for(uint j=0;j<2;j++)for(uint e=0;e<8;e++)out[(part*tokens+first+gr()*8+e)*1024+row+j*16+rc()]=byte_F(acc[j][e]);'''
assert new.count(old)==1
new=new.replace('// Q/K remain row-major. V stores exactly the two B fragments this same lane will consume.','// Q/K are head-major paired A fragments; V keeps the paired B fragment layout.')
new=new.replace(old,'''  // Each live wave owns its raw tile. Finish every norm read before reusing it.
  WG_FENCE(3);__builtin_amdgcn_wave_barrier();WG_FENCE(2);
  unsigned char*tb=reinterpret_cast<unsigned char*>(raw);
  #pragma unroll
  for(uint j=0;j<2;j++)for(uint e=0;e<8;e++)tb[(gr()*8+e)*32+j*16+rc()]=byte_F(acc[j][e]);
  WG_FENCE(3);__builtin_amdgcn_wave_barrier();WG_FENCE(2);
  i2 lo{},hi{};
  __builtin_memcpy(&lo,tb+rc()*32+gr()*8,8);
  __builtin_memcpy(&hi,tb+rc()*32+16+gr()*8,8);
  uint __attribute__((ext_vector_type(4))) packed{uint(lo[0]),uint(lo[1]),uint(hi[0]),uint(hi[1])};
  const size_t off=size_t(part)*tokens*1024+(size_t(row/32)*(tokens/16)+first/16)*512+lane*16;
  __builtin_memcpy(__builtin_assume_aligned(out+off,16),&packed,16);''')
s=s[:i]+'\n// QK fragment variant must be paired with attention *_qkfrag.\n'+new+s[i:];p.write_text(s)
p=S/'hip/deep_fast.hip';s=p.read_text();s=s.replace('bool VFragment=false>\nDEV void vit_attention_transposed_score_body','bool VFragment=false,bool QKFragment=false>\nDEV void vit_attention_transposed_score_body')
a=s.index('DEV void vit_attention_transposed_score_body');b=s.index('\nWAVE void vit_attention_fused_400_bytein_bout_vfrag',a);body=s[a:b]
old=' i2 q[2]{};for(uint k=0;k<32;k+=16){if constexpr(ByteInput)'
assert body.count(old)==1
body=body.replace(old,''' i2 q[2]{};
 if constexpr(QKFragment){static_assert(ByteInput&&VFragment,"QK fragment requires full byte fragment layout");uint __attribute__((ext_vector_type(4))) qp;const size_t off=(size_t(head)*(tokens/16)+first/16)*512+(gr()*16+rc())*16;__builtin_memcpy(&qp,__builtin_assume_aligned(in8+off,16),16);q[0]=i2{int(qp[0]),int(qp[1])};q[1]=i2{int(qp[2]),int(qp[3])};}
 else for(uint k=0;k<32;k+=16){if constexpr(ByteInput)''')
needle='for(uint key=0,tile=0;key<tokens;key+=16,tile^=1u){f8 a{};'
body=body.replace(needle,needle+'''
  uint __attribute__((ext_vector_type(4))) kp{};
  if constexpr(QKFragment){const size_t off=size_t(tokens)*1024+(size_t(head)*(tokens/16)+key/16)*512+(gr()*16+rc())*16;__builtin_memcpy(&kp,__builtin_assume_aligned(in8+off,16),16);}''')
body=body.replace('for(uint k=0;k<32;k+=16){i2 y{};if constexpr(ByteInput)','for(uint k=0;k<32;k+=16){i2 y{};if constexpr(QKFragment)y=i2{int(kp[k/8]),int(kp[k/8+1])};else if constexpr(ByteInput)')
s=s[:a]+body+s[b:];needle='WAVE void vit_attention_fused_640_bytein_bout_vfrag';a=s.index(needle);i=s.index('\n',a)
s=s[:i]+'''\nWAVE void vit_attention_fused_400_bytein_bout_qkfrag(const float*in,float*out,uint tokens,const uint*reuse_gate){if(reuse_gate&&reuse_gate[0])return;vit_attention_transposed_score_body<400,true,true,true,true>(in,out,tokens);}
WAVE void vit_attention_fused_640_bytein_bout_qkfrag(const float*in,float*out,uint tokens,const uint*reuse_gate){if(reuse_gate&&reuse_gate[0])return;vit_attention_transposed_score_body<640,true,true,true,true>(in,out,tokens);}
'''+s[i:];p.write_text(s)
p=S/'Development/HIP/hip_reference_network.h';s=p.read_text();s=s.replace('kernel=="vit_stream_qkv_frag_bin_w5f8_vfrag")','kernel=="vit_stream_qkv_frag_bin_w5f8_vfrag"||kernel=="vit_stream_qkv_frag_bin_w5f8_qkfrag")')
pos=s.index(' bool vfrag_requested=');s=s[:pos]+''' bool qkfrag_requested=false,qkfrag_available=false;unsigned long long qkfrag_producer_calls=0,qkfrag_consumer_calls=0;
 void InitVitQKFragment(){const char*v=std::getenv("NR_VIT_QKFRAG");qkfrag_requested=v&&std::strcmp(v,"1")==0;if(qkfrag_requested)qkfrag_available=HasFn("vit_stream","vit_stream_qkv_frag_bin_w5f8_qkfrag")&&HasFn("deep_fast","vit_attention_fused_400_bytein_bout_qkfrag")&&HasFn("deep_fast","vit_attention_fused_640_bytein_bout_qkfrag");}
'''+s[pos:]
s=s.replace('const bool vfrag=VitVFragmentEligible(n);','const bool vfrag=VitVFragmentEligible(n);const bool qkfrag=vfrag&&qkfrag_requested&&qkfrag_available;')
s=s.replace('(vfrag?"vit_stream_qkv_frag_bin_w5f8_vfrag":"vit_stream_qkv_frag_bin_w5f8")','(qkfrag?"vit_stream_qkv_frag_bin_w5f8_qkfrag":vfrag?"vit_stream_qkv_frag_bin_w5f8_vfrag":"vit_stream_qkv_frag_bin_w5f8")')
s=s.replace('if(vfrag)++vfrag_producer_calls;','if(vfrag)++vfrag_producer_calls;if(qkfrag)++qkfrag_producer_calls;')
s=s.replace('if(vfrag){fused+="_vfrag";++vfrag_consumer_calls;}','if(vfrag){fused+=qkfrag?"_qkfrag":"_vfrag";++vfrag_consumer_calls;if(qkfrag)++qkfrag_consumer_calls;}')
s=s.replace('InitVitVFragment();','InitVitVFragment();InitVitQKFragment();')
s=s.replace('~Network(){','~Network(){std::fprintf(stderr,"VIT_QKFRAG requested=%u available=%u producers=%llu consumers=%llu\\n",unsigned(qkfrag_requested),unsigned(qkfrag_available),qkfrag_producer_calls,qkfrag_consumer_calls);',1);p.write_text(s)
files=['hip/deep_fast.hip','hip/vit_stream.inc','Development/HIP/hip_reference_network.h'];patch=''
for f in files:patch+=subprocess.run(['diff','-u','--label','a/'+f,'--label','b/'+f,str(B/f),str(S/f)],capture_output=True,text=True).stdout
(O/'candidate.patch').write_text(patch);(O/'source-identity.json').write_text(json.dumps({f:{'base':hashlib.sha256((B/f).read_bytes()).hexdigest(),'candidate':hashlib.sha256((S/f).read_bytes()).hexdigest()}for f in files},indent=2))
spec=importlib.util.spec_from_file_location('recipe',R/'Development/tools/llvm-fork/compile-modules.py');m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m);recipes=[]
for role,root in [('baseline',B),('candidate',S)]:
 for name,compiler,src,defs,parts,opts in m.recipe(root/'hip'):
  if name in ['deep_fast-packed-fast','vit-stream-fast']:
   f=W/(name+'.'+role+'.hip');f.write_text(src);recipes.append({'role':role,'module':name,'generated':str(f),'sha256':hashlib.sha256(f.read_bytes()).hexdigest(),'RTC_EXTRA_OPTS':''})
(O/'generated-recipe.json').write_text(json.dumps(recipes,indent=2));print(json.dumps(recipes,indent=2))
