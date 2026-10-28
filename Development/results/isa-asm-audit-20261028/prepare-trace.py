from pathlib import Path
import shutil
R=Path.cwd();O=R/'Development/results/isa-asm-audit-20261028';S=O/'work/trace-source'
shutil.copytree(R/'src',S/'src',dirs_exist_ok=True);shutil.copytree(R/'Development/HIP',S/'Development/HIP',ignore=shutil.ignore_patterns('experiments','*.exe','*.hsaco'),dirs_exist_ok=True)
p=S/'Development/HIP/hip_reference_network.h';s=p.read_text();pos=s.index(' struct Timing{');s=s[:pos]+''' template<class F,class...A>auto AuditLaunch(F fn,Handle h,A...a)->decltype(fn(h,a...)){
  std::string name="UNMAPPED";for(const auto&kv:functions)if(kv.second==h){name=kv.first;break;}std::fprintf(stderr,"AUDIT_KERNEL %s\\n",name.c_str());return fn(h,a...);
 }
'''+s[pos:];s=s.replace('api.hipModuleLaunchKernel(','AuditLaunch(api.hipModuleLaunchKernel,').replace('ext_launch(paired_contract_fn','AuditLaunch(ext_launch,paired_contract_fn');s=s.replace('void Enqueue(void*rgba,void*history,void*rgb_output,U seed){','void Enqueue(void*rgba,void*history,void*rgb_output,U seed){std::fprintf(stderr,"AUDIT_FRAME W=%u H=%u small=%u\\n",W,H,unsigned(small_active));');p.write_text(s)
p=S/'Development/HIP/swin_persistent_network.h';p.write_text(p.read_text().replace('api.hipModuleLaunchKernel(','AuditLaunch(api.hipModuleLaunchKernel,'))
