from pathlib import Path
import shutil,hashlib,json,difflib
R=Path('/home/lmxxf/work/ai-theorys-study/wechat/assets/297')
W=Path('/home/lmxxf/work/dlss5-gap-phase5-20261023/knife2')
O=Path(__file__).resolve().parent
if (W/'source-identity.json').exists():raise SystemExit('refuse overwriting frozen candidate; edit existing file explicitly')
for role in ['baseline','candidate']:
 for sub in ['src','Development/HIP']:shutil.copytree(R/sub,W/(role+'-source')/sub,dirs_exist_ok=True)
p=W/'candidate-source/Development/HIP/hip_reference_network.h';s=p.read_text();base=s
old='''  const bool shape=(W==1600&&H==960)||(W==1920&&(H==1088||H==1152));
  if(!vfrag_requested||!vfrag_available||!vfrag_ae_off||!shape||small_active||!fast_numeric||multi_pass!=1||adaptive_active||opt.graph||!opt.skip_blocks.empty()||!opt.dup_prefix.empty()||observer||!opt.dump_dir.empty()||opt.vit_byte_stream||opt.vit_half_stream||vit_stream_active!=3||!vit_contract_byte_edge||!opt.vit_qkv_fused||!opt.vit_qkv_frag||!opt.vit_qkv_fp8||!opt.vit_attn_fused||(n!=400&&n!=640))return false;
  if(HIP_VIT_ATTN_KSPLIT_HOST&&HasFn("deep_fast",std::string(n==400?"vit_attention_fused_400_bytein_bout":"vit_attention_fused_640_bytein_bout")+"_ks2"))return false;'''
new='''  // Phase5 knife2: only admission expands; reuse_gate, AE decisions/anchors and GPU byte layout stay unchanged.
  const bool shape=small_active?
   ((W==1600&&H==1024&&n==448)||(W==1344&&H==768&&n==288)||(W==1920&&(H==1088||H==1152)&&n==640)):
   ((W==1600&&H==960&&n==400)||(W==1920&&(H==1088||H==1152)&&n==640));
  if(!vfrag_requested||!vfrag_available||!shape||!fast_numeric||multi_pass!=1||opt.graph||!opt.skip_blocks.empty()||!opt.dup_prefix.empty()||observer||!opt.dump_dir.empty()||opt.vit_byte_stream||opt.vit_half_stream||vit_stream_active!=3||!vit_contract_byte_edge||!opt.vit_qkv_fused||!opt.vit_qkv_frag||!opt.vit_qkv_fp8||!opt.vit_attn_fused)return false;
  if(HIP_VIT_ATTN_KSPLIT_HOST&&HasFn("deep_fast",std::string(n<=400?"vit_attention_fused_400_bytein_bout":"vit_attention_fused_640_bytein_bout")+"_ks2"))return false;'''
assert s.count(old)==1;s=s.replace(old,new)
s=s.replace('// Default-on only in the already verified Original/FAST1/MP1/explicit-AE0 scope below.','// Isolated phase5 candidate: Original/Small FAST1 single-pass, AE gated by the existing shared reuse state.')
p.write_text(s)
(O/'candidate.patch').write_text(''.join(difflib.unified_diff(base.splitlines(True),s.splitlines(True),fromfile='a/Development/HIP/hip_reference_network.h',tofile='b/Development/HIP/hip_reference_network.h')))
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
id={'baseline':'phase4 final production only','base_header':sha(R/'Development/HIP/hip_reference_network.h'),'candidate_header':sha(p),'module_manifest_sha256':sha(R/'hip/SHA256SUMS'),'module_root':'/home/lmxxf/work/dlss5-gap-phase4-20261023/knife1/final-payload/HIP','gpu_source_sha256':{n:sha(R/'hip'/n)for n in ['deep_fast.hip','vit_stream.inc']},'GPU_sources_or_modules_changed':False,'scope':'Small four geometries plus Original three existing geometries; FAST1/one network evaluation; existing AE modes preserved','GPU_not_authorized_until_knife1_complete':True}
(O/'source-identity.json').write_text(json.dumps(id,indent=2)+'\n');(W/'source-identity.json').write_text(json.dumps(id,indent=2)+'\n');print(json.dumps(id,indent=2))
