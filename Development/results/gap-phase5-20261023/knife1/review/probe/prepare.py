from pathlib import Path
import re,hashlib,json,shutil
r=Path('/home/lmxxf/work/ai-theorys-study/wechat/assets/297');d=Path(__file__).parent;w=Path('/home/lmxxf/work/dlss5-gap-phase5-20261023/knife1');candidate=(w/'source/hip/c512_qkv_attention_compact.inc').read_text();baseline=(r/'hip/c512_qkv_attention_compact.inc').read_text();mh=(r/'hip/multihead_fast_padded.hip').read_text()
def fun(s,name):
 m=re.search(r'DEV\s+\w+\s+'+name+r'\(',s);assert m,name;i=s.index('{',m.start());depth=1;j=i+1
 while depth:
  depth+=(s[j]=='{')-(s[j]=='}');j+=1
 return s[m.start():j]
parts=['#pragma clang fp contract(off)\n#pragma clang fp reassociate(off)\nusing uint=unsigned int;using i2=int __attribute__((ext_vector_type(2)));using f8=float __attribute__((ext_vector_type(8)));\n#define DEV __attribute__((device)) __attribute__((always_inline))\n#define KERNEL extern "C" __attribute__((global))\n']
parts += [fun(mh,'q8'),fun(baseline,'c5c_shuffle'),fun(candidate,'c5c_norm_broadcast'),fun(baseline,'c5c_serial_norm').replace('c5c_serial_norm','norm_old'), '#define C512_NORM_PERMLANE 1',fun(candidate,'c5c_serial_norm').replace('c5c_serial_norm','norm_new')]
parts += [fun(baseline,n) for n in ['c5c_fp8','c5c_q8mz','c5c_bits']]
parts += [fun(candidate,n) for n in ['c5c_pack8_pairs','c5c_pair_qkv_input','c5c_pair_plain_input']]
parts.append(r'''
KERNEL __attribute__((amdgpu_flat_work_group_size(32,32))) void norm_lane_probe(const float*input,const uint*tags,uint*out,uint lanes){
 uint gid=__builtin_amdgcn_workgroup_id_x()*32+__builtin_amdgcn_workitem_id_x();if(gid>=lanes)return;
 f8 lo{},hi{};for(uint e=0;e<8;e++){lo[e]=input[gid*16+e];hi[e]=input[gid*16+8+e];}
 out[gid*6]=__builtin_bit_cast(uint,norm_old(lo,hi));out[gid*6+1]=__builtin_bit_cast(uint,norm_new(lo,hi));
 const uint rc=__builtin_amdgcn_workitem_id_x()&15u;float tag=__builtin_bit_cast(float,tags[gid]);
 out[gid*6+2]=__builtin_bit_cast(uint,c5c_shuffle(tag,rc));out[gid*6+3]=__builtin_bit_cast(uint,c5c_norm_broadcast(tag,false));
 out[gid*6+4]=__builtin_bit_cast(uint,c5c_shuffle(tag,rc+16));out[gid*6+5]=__builtin_bit_cast(uint,c5c_norm_broadcast(tag,true));
}
KERNEL __attribute__((amdgpu_flat_work_group_size(128,128))) void scalar_pair_probe(const float*input,const float*scales,uint*out,uint lanes){
 uint gid=__builtin_amdgcn_workgroup_id_x()*128+__builtin_amdgcn_workitem_id_x();if(gid>=lanes)return;
 f8 q{},p{},v{};i2 qo{},po{},vo{};
 for(uint e=0;e<8;e++){float a=input[gid*8+e],b=scales[gid*8+e];
  c5c_bits(qo,e,c5c_q8mz(a,b));q[e]=c5c_pair_qkv_input(a,b);
  float probability=a*b;c5c_bits(po,e,c5c_fp8(probability));p[e]=c5c_pair_plain_input(probability);
  c5c_bits(vo,e,c5c_fp8(a));v[e]=c5c_pair_plain_input(a);
 }
 const i2 qn=c5c_pack8_pairs(q),pn=c5c_pack8_pairs(p),vn=c5c_pack8_pairs(v);
 for(uint k=0;k<2;k++){out[gid*12+k]=uint(qo[k]);out[gid*12+2+k]=uint(qn[k]);out[gid*12+4+k]=uint(po[k]);out[gid*12+6+k]=uint(pn[k]);out[gid*12+8+k]=uint(vo[k]);out[gid*12+10+k]=uint(vn[k]);}
}
''')
s='\n\n'.join(parts)+'\n';(d/'probe.generated.hip').write_text(s);(w/'probe.generated.hip').write_text(s)
(d/'source-receipt.json').write_text(json.dumps({'baseline_source_sha256':hashlib.sha256(baseline.encode()).hexdigest(),'candidate_source_sha256':hashlib.sha256(candidate.encode()).hexdigest(),'generated_sha256':hashlib.sha256(s.encode()).hexdigest(),'helpers_extracted_verbatim_except_norm_names':True,'norm_arithmetic_from_actual_old_and_new':True,'pack_paths':'current NOF1 QKV/probability/AV; NOF0 not instantiated by this probe','gpu_executed':False},indent=2)+'\n');print(w/'probe.generated.hip')
