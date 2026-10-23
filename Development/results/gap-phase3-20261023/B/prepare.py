import pathlib,shutil,subprocess,json,hashlib,importlib.util
R=pathlib.Path('/home/lmxxf/work/ai-theorys-study/wechat/assets/297');W=pathlib.Path('/home/lmxxf/work/dlss5-gap-phase3-20261023/B');O=pathlib.Path(__file__).parent
S=W/'source';shutil.copytree(R/'hip',S/'hip',dirs_exist_ok=True);shutil.copytree(R/'src',S/'src',dirs_exist_ok=True);shutil.copytree(R/'Development/HIP',S/'Development/HIP',dirs_exist_ok=True)
p=S/'hip/c512_m32_deep.inc';s=p.read_text();pos=s.index('#endif\n#ifndef C512_FFN_PROJ_FUSE',s.index('void split_ffn_one_w2f8_w16_bin('))
producer='''
#ifndef C512_CHAIN_TRACE
#define C512_CHAIN_TRACE 0
#endif
KERNEL __attribute__((amdgpu_flat_work_group_size(64,64))) void split_ffn_one_w2f8_w16_bin_chain(const float*in,const float*w,unsigned char*out8,uint tokens,unsigned long long*ready,unsigned long long*trace){
 c512_ffn_one_w2f8_w16_body<false,true>(in,w,out8,tokens);
 // Both waves, including an invalid tail wave, join both barriers.
 __builtin_amdgcn_s_barrier();
 __builtin_amdgcn_fence(__ATOMIC_RELEASE,"agent");
 __builtin_amdgcn_s_barrier();
 const uint tid=__builtin_amdgcn_workitem_id_x(),wave=tid/32,first=bid()/8*32+wave*16;
 if((tid&31u)==0&&first<tokens){
#if C512_CHAIN_TRACE
  if(trace)trace[bid()*2+wave]=__builtin_amdgcn_s_sendmsg_rtnl(131);
#endif
  __hip_atomic_fetch_add(ready+first/16,1ull,__ATOMIC_ACQ_REL,__HIP_MEMORY_SCOPE_AGENT);
 }
}
'''
s=s[:pos]+producer+s[pos:];p.write_text(s)
p=S/'hip/deep_fast.hip';s=p.read_text();old='template<bool W32,bool F16Res=false,bool ByteSkip=false,bool Transposed=false> DEV void split_projection_frag_w16_body';new=old.replace('Transposed=false>','Transposed=false,bool ChainTrace=false>');assert s.count(old)==1;s=s.replace(old,new)
start=s.index(new);arg='uint tokens){';pos=s.index(arg,start);s=s[:pos]+s[pos:].replace(arg,'uint tokens,unsigned long long*chain_trace=nullptr){',1)
# Insert data-dependent diagnostic marker only in the non-transposed branch.
a=s.index(' f8 acc[4]{};\n for(uint j=0;j<4;j++)for(uint e=0;e<8;e++)',start)
wmma='   acc[j]=__builtin_amdgcn_wmma_f32_16x16x16_fp8_fp8_w32_gfx12(a0,b0,acc[j]);'
pos=s.index(wmma,a)+len(wmma)
marker='''
   if constexpr(ChainTrace){if(chain_trace&&gs==0&&j==0&&rc()==0&&gr()==0){reinterpret_cast<volatile uint*>(chain_trace)[bid()*2]=bits(acc[j][0]);__builtin_amdgcn_fence(__ATOMIC_RELEASE,"agent");chain_trace[bid()]=__builtin_amdgcn_s_sendmsg_rtnl(131);}}
'''
s=s[:pos]+marker+s[pos:]
pos=s.index('WAVE void split_projection_frag_w16_nof32(',start)
consumer='''
#ifndef C512_CHAIN_TRACE
#define C512_CHAIN_TRACE 0
#endif
WAVE void split_projection_frag_w16_bskip_chain(const unsigned char*in8,const float*w,const float*skip,float*out,unsigned char*out8,uint tokens,const unsigned long long*ready,unsigned long long target,uint*status,uint wait_limit,unsigned long long*trace){
 const uint first=bid()/8*16,row=bid()%8*64,lane=__builtin_amdgcn_workitem_id_x();
 if(first>=tokens)return;
 uint ok=0;
 if(lane==0){for(uint tries=0;tries<wait_limit;tries++){if(__hip_atomic_load(ready+first/16,__ATOMIC_ACQUIRE,__HIP_MEMORY_SCOPE_AGENT)>=target){ok=1;break;}__builtin_amdgcn_s_sleep(1);}}
 ok=__builtin_amdgcn_readfirstlane(ok);
 if(!ok){
  // Do not read producer data, skip, or weights on failure. Define every owned output, reject the whole experiment.
  for(uint j=0;j<4;j++)for(uint e=0;e<8;e++){uint r=first+gr()*8+e,c=row+j*16+rc();out[r*512+c]=0.f;out8[((r/16)*16+c/32)*512+(r%16)*32+c%32]=0;}
  if(lane==0)__hip_atomic_fetch_or(status,1u,__ATOMIC_RELAXED,__HIP_MEMORY_SCOPE_AGENT);
  return;
 }
 __builtin_amdgcn_fence(__ATOMIC_ACQUIRE,"agent");
 split_projection_frag_w16_body<true,false,true,false,(C512_CHAIN_TRACE!=0)>(in8,w,skip,out,out8,nullptr,tokens,trace);
}
'''
s=s[:pos]+consumer+s[pos:];p.write_text(s)
# Host methods are maintained as a separate readable fragment.
p=S/'Development/HIP/hip_reference_network.h';s=p.read_text().replace('#include <atomic>','#include <atomic>\n#include <array>',1);pos=s.index(' unsigned long long c512_w16_calls=');s=s[:pos]+(O/'host-fragment.inc').read_text()+s[pos:]
needle='kernel=="split_projection_frag_w16_bskip"||';assert s.count(needle)==1;s=s.replace(needle,needle+'kernel=="split_projection_frag_w16_bskip_chain"||')
needle='kernel=="split_ffn_one_w2f8_w16_bin"||';assert s.count(needle)==1;s=s.replace(needle,needle+'kernel=="split_ffn_one_w2f8_w16_bin_chain"||')
needle='if(pdl_anyorder){++pdl_calls;';assert s.count(needle)==1
s=s.replace(needle,'if(kernel=="split_projection_frag_w16_bskip_chain"){if(chain_ordered){++chain_ordered_calls;api.Check(api.hipModuleLaunchKernel(Fn(module,kernel),gg,1,1,threads,1,1,0,stream,argv,nullptr),"C512 chain ordered projection");}else{++chain_anyorder_calls;api.Check(chain_ext_launch(Fn(module,kernel),gg*threads,1,1,threads,1,1,0,stream,argv,nullptr,nullptr,nullptr,1u),"C512 chain anyorder projection");}}else '+needle)
needle='  if(ffn_w16)++c512_ffn_w16_calls;else ++c512_ffn_stock_calls;';assert s.count(needle)==1
s=s.replace(needle,needle+'''\n  const int chain_slot=C512ChainSlot(block);
  const bool chain_this=PrepareC512Chain(block,n,byte_in&&ffn_w16&&ffn_w16m==1&&!fpf);if(chain_this){chain_keep[chain_slot][0]=compact;chain_keep[chain_slot][1]=contract8;}
''')
needle='  if(fpf)Run("c512_m32_deep","split_ffn_proj_fused"';pos=s.index(needle);s=s[:pos]+'''  if(chain_this)Run("c512_m32_deep","split_ffn_one_w2f8_w16_bin_chain",size_t((n+31)/32*32)*256,P(compact),PackedSplitFfnWeightMixFp8(Block(block,"ffwd"),true),P(contract8),n,ChainCounter(chain_slot),ChainProducerTrace(chain_slot));
  else'''+s[pos+2:]
s=s.replace('elseif(fpf)','else if(fpf)')
needle='  if(!fpf)Run("deep",w16_fb8?';pos=s.index(needle);s=s[:pos]+'''  if(chain_this)Run("deep","split_projection_frag_w16_bskip_chain",size_t(n)*512,P(contract8),PackedSplitProjectionFrag(Block(block,"ffwd-projection"),true),P(compact),P(ffn),P(ffn8),n,ChainCounter(chain_slot),ChainWaitTarget(chain_slot),static_cast<unsigned*>(P(chain_status)),chain_wait_limit,ChainConsumerTrace(chain_slot));
  else '''+s[pos+2:]
needle='if(multi_pass>1)PrepareMultiPassFeeds();if(multi_skin)PrepareSkin();}catch';assert s.count(needle)==1;s=s.replace(needle,'InitC512Chain();'+needle)
# All chain allocations/retentions destroyed only after the existing stream drain.
needle='pdl_keep.clear();if(pdl_flags)';assert s.count(needle)==1;s=s.replace(needle,'pdl_keep.clear();ReleaseChainFrame();chain_ready.reset();chain_status.reset();chain_prod_trace.reset();chain_cons_trace.reset();if(pdl_flags)')
needle='if(pdl_flags){api.hipStreamSynchronize(stream);api.hipFree(pdl_flags);pdl_flags=nullptr;}for(auto&m:modules)';assert s.count(needle)==1;s=s.replace(needle,'if(chain_ready||chain_status||chain_prod_trace||chain_cons_trace){api.hipStreamSynchronize(stream);chain_ready.reset();chain_status.reset();chain_prod_trace.reset();chain_cons_trace.reset();}'+needle)
# Release warmup holds only after its existing synchronization, before producer-wait admission.
assert s.count('Enqueue(scratch,hist,out,0);Synchronize();')==2
s=s.replace('Enqueue(scratch,hist,out,0);Synchronize();','Enqueue(scratch,hist,out,0);Synchronize();ReleaseChainFrame();')
# Fail before a synchronous allocation if a lab candidate ever escapes cold pool preparation.
needle='if(graph_capturing)throw std::runtime_error("graph capture requires warmed allocation pool");'
assert s.count(needle)==1
s=s.replace(needle,needle+'if(chain_requested&&external_input_wait)throw std::runtime_error("chain pool was not prepared before external wait");')
# Bound cold chain pool preparation; never relax the external-wait allocation guard.
old='   Enqueue(scratch,hist,out,0);Synchronize();ReleaseChainFrame();\n   if(saved_adaptive&&ae&&strcmp(ae,"0")){SetAdaptiveReuseAllowed(false);Enqueue(scratch,hist,out,0);Synchronize();ReleaseChainFrame();SetAdaptiveReuseAllowed(saved_adaptive);}'
assert s.count(old)==1
s=s.replace(old,(O/'warm-fragment.inc').read_text().rstrip())
old='if(chain_requested&&external_input_wait)throw std::runtime_error("chain pool was not prepared before external wait");'
new='if(chain_requested&&external_input_wait){size_t total=0,held=0,max_free=0;for(const auto&t:pool){total+=t->capacity;if(t.use_count()>1)++held;else max_free=std::max(max_free,t->capacity);}throw std::runtime_error("chain pool was not prepared before external wait bytes="+std::to_string(bytes)+" pool="+std::to_string(pool.size())+" pool_bytes="+std::to_string(total)+" held="+std::to_string(held)+" max_free="+std::to_string(max_free));}'
assert s.count(old)==1;s=s.replace(old,new)
p.write_text(s)
for f in ['src/native_game_frame.h','src/native_hip_network.h','Development/HIP/hip_d3d12_bridge.h']:
 p=S/f;s=p.read_text().replace('#ifdef DLSS5_BENCH_BRIDGE_ISOLATE','#if defined(DLSS5_BENCH_BRIDGE_ISOLATE) || defined(DLSS5_C512_CHAIN_LAB)').replace('#if defined(DLSS5_BENCH_BRIDGE_ISOLATE) && defined(DLSS5_USE_HIP)','#if (defined(DLSS5_BENCH_BRIDGE_ISOLATE) || defined(DLSS5_C512_CHAIN_LAB)) && defined(DLSS5_USE_HIP)');p.write_text(s)
p=S/'Development/HIP/benchmark_live_capture.cpp';s=p.read_text();needle='submit.Flush();double elapsed=std::chrono::duration<double,std::milli>'
assert s.count(needle)==1
s=s.replace(needle,'submit.Flush();if(const char*v=std::getenv("NR_C512_CHAIN");v&&std::strcmp(v,"1")==0)frame.DiagnosticNetwork().ReleaseChainFrame();double elapsed=std::chrono::duration<double,std::milli>')
needle='fclose(csv);save_frame(prefix,last,W,H);submit.Flush();';assert s.count(needle)==1
s=s.replace(needle,needle+'frame.DiagnosticNetwork().CheckChainStatus();');p.write_text(s)
files=['hip/deep_fast.hip','hip/c512_m32_deep.inc','Development/HIP/hip_reference_network.h','Development/HIP/benchmark_live_capture.cpp','src/native_game_frame.h','src/native_hip_network.h','Development/HIP/hip_d3d12_bridge.h'];patch=''
for f in files:patch+=subprocess.run(['diff','-u','--label','a/'+f,'--label','b/'+f,str(R/f),str(S/f)],capture_output=True,text=True).stdout
(O/'candidate.patch').write_text(patch)
(O/'source-identity.json').write_text(json.dumps({f:{'base':hashlib.sha256((R/f).read_bytes()).hexdigest(),'candidate':hashlib.sha256((S/f).read_bytes()).hexdigest()}for f in files},indent=2))
spec=importlib.util.spec_from_file_location('recipe',R/'Development/tools/llvm-fork/compile-modules.py');m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m)
for name,_,src,defs,parts,opts in m.recipe(S/'hip'):
 if name in ['deep_fast-packed-fast','c512-m32-deep']:
  (W/(name+'.generated.hip')).write_text(src)
  (W/(name+'.trace.generated.hip')).write_text('#define C512_CHAIN_TRACE 1\n'+src)
print(S)
