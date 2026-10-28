from pathlib import Path
import re,json,collections
P=Path(__file__).parent;rows=json.loads((P/'work/all-kernel-index.json').read_text());res={}
mapto={'c32-wave1-fast':'fswin32','c64-wave2-fast':'fswin64','c512-m32-mh':'attn','c512-m32-deep':'ffwd3','deep_fast-packed-fast':'vitattn','vit-stream-fast':'gemmvproj','swin-persistent':'fswin256'}
want=['sp_run256_w16','decoder_project2x_h16w_byteout_w_bin_f8w16','c64_wave2_up_lb_sh','c128_wave2_up_lb_sh','c32_wave1_up_b8_w16','c64_wave2_bi_bo','c128_wave2_bi_bo','c256_wave2_bi_bo_w16','c32_wave1_prefix_b8d_w16','c32_wave1_post_b8_w16','c32_wave1_chain_w16','c32_wave1_finish_b8_w16','c32_wave1_mapped_b8_w16','c512_qkv_attention_compact_w16','split_ffn_one_w2f8_w16_bin','vit_stream_qkv_frag_bin_w5f8_vfrag','vit_stream_qkv_frag_bin_w5f8_qkfrag','vit_attention_fused_640_bytein_bout_vfrag','vit_attention_fused_640_bytein_bout_qkfrag','vit_stream_contract_frag_bstream_bout_compactw_w16','vit_stream_project_n64_bb_bout_compactw_w16']
def stats(ls):
 ops=collections.Counter(re.findall(r'^\s*((?:s_|v_|ds_|global_|buffer_)[\w]+)','\n'.join(ls),re.M));cnt=lambda f:sum(n for k,n in ops.items() if f(k));return {'instructions':sum(ops.values()),'wmma':cnt(lambda k:'wmma' in k),'VALU':cnt(lambda k:k.startswith('v_') and 'wmma' not in k),'VOPD':cnt(lambda k:k.startswith('v_dual')),'VMEM':cnt(lambda k:k.startswith(('global_','buffer_'))),'LDS':cnt(lambda k:k.startswith('ds_')),'wait':cnt(lambda k:k.startswith('s_wait')),'addr64':cnt(lambda k:k in ['v_add_co_u32','v_add_co_ci_u32_e64','v_lshl_add_u64','v_ashrrev_i32','v_lshlrev_b64_e32']),'move':cnt(lambda k:'mov_b32' in k),'bpermute':ops['ds_bpermute_b32'],'opcodes':dict(ops)}
for r in rows:
 mod=r['module'].removesuffix('.hsaco')
 if mod not in mapto or r['name'] not in want:continue
 ls=(P/r['assembly']).read_text().splitlines();bounds=[0]+[i+1 for i,l in enumerate(ls) if re.search(r'\bs_barrier_signal\b|\bs_barrier_wait\b|\bs_barrier\b',l)]+[len(ls)];segs=[]
 for a,b in zip(bounds,bounds[1:]):
  if a<b:segs.append({'from_line':a+1,'to_line':b,'counts':stats(ls[a:b])})
 res[r['name']]={'module':mod,'vgpr':r['vgpr'],'lds':r['lds'],'private':r['private'],'counts':stats(ls),'barrier_segments':segs,'source_assembly':r['assembly'],'waits':r['waits'],'narrow_lds':r['narrow_lds']}
(P/'anatomy.json').write_text(json.dumps(res,indent=2));print(len(res),'target kernels indexed')
