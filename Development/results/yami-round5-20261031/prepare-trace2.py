from pathlib import Path
import json,hashlib
p=Path(__file__).parent;r=r'D:\DLSSNR-Lab\yami-round5-20261031'
files=[];jobs=[];cc=r'D:\DLSSNR-Lab\gap-phase5-20261023\rtc_compile-current.exe'
files.append({'path':cc,'sha256':'553385a0ba6465f332208cfd5830224e255db9973101050f760e3117e5575197'})
for arm in ['baseline','candidate']:
 s=Path('Development/results/yami-round4-review-20261031/'+arm+'.hip').read_text()
 begin=s.index('DEV void vit_attention_transposed_score_body');end=s.index('\n#endif',s.index('float inv=vit_inv(sum[0]);',begin))
 part=s[begin:end]
 anchor='#if HIP_VIT_ATTN_TRANSPOSED_AV\n  sum='
 trace='''  {uint*tr=reinterpret_cast<uint*>(out)+(size_t(bid())*(tokens/16)+key/16)*32*32+__builtin_amdgcn_workitem_id_x()*32;
   _Pragma("unroll") for(uint e=0;e<8;e++){tr[e]=bits(a[e]);tr[8+e]=__builtin_bit_cast(unsigned short,x[e]);tr[18+e]=bits(prob[e]);}tr[16]=uint(xb[0]);tr[17]=uint(xb[1]);}
'''
 assert part.count(anchor)==1;part=part.replace(anchor,trace+anchor)
 anchor2='#if HIP_VIT_ATTN_TRANSPOSED_AV\n // Query';assert anchor2 in part;part=part.replace(anchor2,'return;\n'+anchor2)
 s=s[:begin]+part+s[end:];(p/(arm+'-trace2.hip')).write_text(s)
 files.append({'path':r+'\\'+arm+'-trace2.hip','sha256':hashlib.sha256(s.encode()).hexdigest()})
 jobs.append({'label':'trace2-compile/'+arm,'exe':cc,'cpu_compile':True,'env':{'RTC_EXTRA_OPTS':''},'arguments':'"{OUT}\\trace.hsaco" "'+r+'\\'+arm+'-trace.hip" comgr gfx1201'})
(p/'trace2-compile.json').write_text(json.dumps({'name':'trace-compile','files':files,'jobs':jobs},indent=2))
