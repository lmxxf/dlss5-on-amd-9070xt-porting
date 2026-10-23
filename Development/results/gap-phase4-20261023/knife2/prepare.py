import pathlib,shutil,subprocess,hashlib,json
R=pathlib.Path('/home/lmxxf/work/ai-theorys-study/wechat/assets/297');W=pathlib.Path('/home/lmxxf/work/dlss5-gap-phase4-20261023/knife2');O=pathlib.Path(__file__).parent
for role in ['baseline','candidate']:
 S=W/(role+'-source')
 for d in ['src','Development/HIP']:shutil.copytree(R/d,S/d,dirs_exist_ok=True)
p=W/'candidate-source/Development/HIP/hip_reference_network.h';s=p.read_text()
needle=' bool CompactVitTailDeviceEligible()'
helper=''' // Central shape admission for the paired contract/projection tail paths.
 bool CompactVitTailGeometryEligible()const{
  if(W==1920&&H==1152)return true; // Existing Original and Small 1080 path.
  if(!small_active)return (W==1920&&H==1088)||(W==1600&&H==960);
  return (W==1600&&H==1024)||(W==1344&&H==768)||(W==1920&&H==1088);
 }
'''
assert s.count(needle)==1;s=s.replace(needle,helper+needle)
old='((W==1920&&H==1152)||(!small_active&&((W==1920&&H==1088)||(W==1600&&H==960))))'
assert s.count(old)==2;s=s.replace(old,'CompactVitTailGeometryEligible()');p.write_text(s)
d=subprocess.run(['diff','-u','--label','a/Development/HIP/hip_reference_network.h','--label','b/Development/HIP/hip_reference_network.h',str(R/'Development/HIP/hip_reference_network.h'),str(p)],capture_output=True,text=True).stdout;(O/'candidate.patch').write_text(d)
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
r={'base_header':sha(R/'Development/HIP/hip_reference_network.h'),'candidate_header':sha(p),'module_manifest_sha256':sha(R/'hip/SHA256SUMS'),'module_root':'/home/lmxxf/work/dlss5-gap-phase4-20261023/knife1/final-payload/HIP','all_80_modules_reused':True,'hip_sources_unchanged':{n:sha(R/'hip'/n) for n in ['deep_fast.hip','vit_stream.inc']},'vfrag_small_scope_unchanged':True}
(O/'source-identity.json').write_text(json.dumps(r,indent=2));print(json.dumps(r,indent=2))
