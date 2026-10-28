from pathlib import Path
import json,re,collections
P=Path(__file__).parent;A=Path('/home/lmxxf/work/dlss5-aco-isa-20261023');rows=json.loads((P/'work/all-kernel-index.json').read_text());out={}
for name in ['fswin32','fswin64','fswin128','fswin256','ffwd3','attn','vitattn','gemmvnores','gemmvproj']:
 f=A/(name+'.s')
 if not f.exists():continue
 ls=f.read_text().splitlines();ops=collections.Counter(re.findall(r'^\s*((?:s_|v_|ds_|global_|buffer_)[\w]+)',f.read_text(),re.M));waits=collections.Counter(re.findall(r'\b(s_wait_(?:loadcnt|dscnt)) (0x[0-9a-f]+)',f.read_text()));out[name]={'opcodes':dict(ops),'waits':{a+' '+b:n for (a,b),n in waits.items()}}
(P/'aco-summary.json').write_text(json.dumps(out,indent=2))
hot=['c128_wave2_bi_bo','c256_wave2_bi_bo_w16','c512_qkv_attention_compact_w16','split_ffn_one_w2f8_w16_bin','vit_stream_qkv_frag_bin_w5f8_qkfrag','vit_stream_contract_frag_bstream_bout_compactw_w16']
ex=[]
for r in rows:
 if r['name'] not in hot or r['module'] not in ['c64-wave2-fast.hsaco','c512-m32-mh.hsaco','c512-m32-deep.hsaco','vit-stream-fast.hsaco']:continue
 ls=(P/r['assembly']).read_text().splitlines();idx=[i for i,l in enumerate(ls) if 's_wait_loadcnt 0x0' in l or 's_wait_dscnt 0x0' in l];ex.append({'module':r['module'],'kernel':r['name'],'vgpr':r['vgpr'],'lds':r['lds'],'wait_count':len(r['waits']),'narrow_lds_count':len(r['narrow_lds']),'excerpts':[{'line':i+1,'text':'\n'.join(ls[max(0,i-3):i+4])} for i in idx[:4]]})
(P/'hot-excerpts.json').write_text(json.dumps(ex,indent=2))
