from pathlib import Path
import subprocess,yaml,re,json,collections,hashlib
P=Path(__file__).parent;bin=Path('/home/lmxxf/work/llvm-build-23/bin');result={}
for role in ['baseline','contract','project','qkv']:
 f=P/(role+'.hsaco');notes=subprocess.check_output([str(bin/'llvm-readobj'),'--notes',str(f)],text=True);meta=yaml.safe_load(notes.split('AMDGPU Metadata: ---',1)[1].split('...',1)[0]);md={m['.name']:m for m in meta['amdhsa.kernels']};dis=subprocess.check_output([str(bin/'llvm-objdump'),'-d','--mcpu=gfx1201',str(f)],text=True);parts=re.split(r'^[0-9a-f]+ <([^>]+)>:\n',dis,flags=re.M);fn=dict(zip(parts[1::2],parts[2::2]));out={}
 for name in ['vit_stream_contract_frag_bstream_bout_compactw_w16','vit_stream_project_n64_bb_bout_compactw_w16','vit_stream_qkv_frag_bin_w5f8_vfrag']:
  m=md[name];ops=collections.Counter(re.findall(r'^\s*((?:global_|s_|v_|ds_)[\w]+)',fn[name],re.M));out[name]={'resources':m,'ops':dict(ops)}
 result[role]={'sha256':hashlib.sha256(f.read_bytes()).hexdigest(),'kernels':out}
(P/'isa.json').write_text(json.dumps(result,indent=2))
for role,r in result.items():
 print(role,r['sha256'])
 for n,k in r['kernels'].items():print(n,{x:k['resources'].get(x) for x in ['.vgpr_count','.sgpr_count','.group_segment_fixed_size','.private_segment_fixed_size','.vgpr_spill_count','.sgpr_spill_count']},{x:y for x,y in k['ops'].items() if x.startswith('global_store')})
