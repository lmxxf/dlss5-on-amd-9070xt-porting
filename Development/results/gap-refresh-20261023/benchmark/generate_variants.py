from pathlib import Path
import shutil,difflib
base=Path('/home/lmxxf/work/dlss5-gap-refresh-20261023')
r=Path('/home/lmxxf/work/ai-theorys-study/wechat/assets/297')
d=r/'Development/results/gap-refresh-20261023/benchmark'
orig=base/'source-snapshot'
trace=base/'trace-source'
shutil.copytree(orig,trace,dirs_exist_ok=True)
p=trace/'Development/HIP/hip_reference_network.h';s=p.read_text()
insert=r'''
 // Isolated CPU dispatch receipt only; observer/dump/profile remain untouched.
 bool gap_trace_enabled=false;unsigned gap_trace_frame=0;std::vector<std::string>gap_trace;
 template<class F,class...A>auto GapLaunch(const char*kind,F fn,Handle h,A...a)->decltype(fn(h,a...)){
  if(gap_trace_enabled){std::string n="UNMAPPED";for(const auto&kv:functions)if(kv.second==h){n=kv.first;break;}gap_trace.push_back(std::string(kind)+" "+n);}
  return fn(h,a...);
 }
 public:
 void GapTraceBegin(unsigned frame){gap_trace_frame=frame;gap_trace.clear();gap_trace_enabled=true;}
 void GapTraceEnd(){gap_trace_enabled=false;std::printf("GAP_PROFILE frame=%u W=%u H=%u model=%u effective=%u compact_contract=%u compact_projection=%u contract_w16=%u projection_w16=%u contract_bstream=%u projection_bout=%u vit_contract_byte_edge=%u C512ByteStreamOk=%u VitEntryExitByteOk=%u pdl_effective=%u swin_run=%u\\n",gap_trace_frame,W,H,ModelMode(),EffectiveModelMode(),unsigned(compact_contract_fn!=nullptr),unsigned(compact_projection_fn!=nullptr),unsigned(compact_contract_w16),unsigned(compact_projection_w16),unsigned(compact_contract_bstream_fn!=nullptr),unsigned(compact_projection_bout_fn!=nullptr),unsigned(vit_contract_byte_edge),unsigned(C512ByteStreamOk()),unsigned(VitEntryExitByteOk()),unsigned(pdl_effective),unsigned(SwinRunActive()));for(size_t i=0;i<gap_trace.size();++i)std::printf("GAP_DISPATCH frame=%u index=%zu %s\\n",gap_trace_frame,i,gap_trace[i].c_str());std::printf("GAP_COUNT frame=%u dispatches=%zu\\n",gap_trace_frame,gap_trace.size());}
 private:
'''
insert=insert.replace(chr(92)*2+'n',chr(92)+'n')
pos=s.index(' struct Timing{');s=s[:pos]+insert+s[pos:]
s=s.replace('api.hipModuleLaunchKernel(','GapLaunch("ordered",api.hipModuleLaunchKernel,').replace('ext_launch(paired_contract_fn','GapLaunch("anyorder",ext_launch,paired_contract_fn')
p.write_text(s)
p=trace/'Development/HIP/swin_persistent_network.h';s=p.read_text().replace('api.hipModuleLaunchKernel(','GapLaunch("ordered",api.hipModuleLaunchKernel,');p.write_text(s)
s=(d/'benchmark.cpp').read_text().replace('||!warm||!measured||measured>1000','||warm||!measured||measured>3')
a=s.index(' Handle begin{},end{};');b=s.index(' auto download=',a)
s=s[:a]+s[b:];a=s.index(' run();auto first=');b=s.index('}catch(',a)
s=s[:a]+''' std::vector<float>first;for(U i=0;i<measured;i++){net.GapTraceBegin(i);net.Enqueue(in,nullptr,out,0);net.Synchronize();net.GapTraceEnd();auto v=download((std::string("frame-")+std::to_string(i)+".rgb32f").c_str());if(i==0)first=v;else if(v.size()!=first.size()||memcmp(v.data(),first.data(),v.size()*4))throw std::runtime_error("repeated frozen trace output differs");}
 printf("GAP_TRACE_DONE frames=%u finite=1 repeat_bitdiff=0 timing_not_collected=1\\n",measured);api.hipFree(in);api.hipFree(out);return 0;
'''+s[b:]
(d/'trace.cpp').write_text(s)
(d/'trace-header.patch').write_text(''.join(difflib.unified_diff((orig/'Development/HIP/hip_reference_network.h').read_text().splitlines(True),(trace/'Development/HIP/hip_reference_network.h').read_text().splitlines(True),fromfile='stock/hip_reference_network.h',tofile='trace/hip_reference_network.h'))+''.join(difflib.unified_diff((orig/'Development/HIP/swin_persistent_network.h').read_text().splitlines(True),(trace/'Development/HIP/swin_persistent_network.h').read_text().splitlines(True),fromfile='stock/swin_persistent_network.h',tofile='trace/swin_persistent_network.h')))

for name,src in [('candidate-source','source-snapshot'),('candidate-trace-source','trace-source')]:
 p=base/name;shutil.copytree(base/src,p,dirs_exist_ok=True);h=p/'Development/HIP/hip_reference_network.h';s=h.read_text();needle='W==1920&&H==1152&&multi_pass==1&&!opt.graph&&!opt.experimental_temporal&&!opt.sparse_weights&&vit_contract_byte_edge';assert s.count(needle)==2;s=s.replace(needle,needle.replace('H==1152','(H==1152||H==1088)'));h.write_text(s)
(d/'compact1088.patch').write_text(''.join(difflib.unified_diff((base/'source-snapshot/Development/HIP/hip_reference_network.h').read_text().splitlines(True),(base/'candidate-source/Development/HIP/hip_reference_network.h').read_text().splitlines(True),fromfile='stock/hip_reference_network.h',tofile='candidate/hip_reference_network.h')))
