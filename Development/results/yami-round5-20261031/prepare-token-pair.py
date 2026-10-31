from pathlib import Path
import importlib.util,json,hashlib
p=Path(__file__).parent;src=(p/'pair-scope/source/hip/deep_fast.hip').read_text();block='#if HIP_VIT_ATTN_TRANSPOSED_SCORE && VIT_FAST_NUM==15\nWAVE void vit_attention_fused_640_bytein_bout_qkfrag_rtzpair(const float*in,float*out,uint tokens,const uint*reuse_gate){if(reuse_gate&&reuse_gate[0])return;vit_attention_transposed_score_body<640,true,true,true,true,true>(in,out,tokens);}\n#endif';assert src.count(block)==1;src=src.replace(block,'')
old='WAVE void vit_attention_fused_640_bytein_bout_qkfrag(const float*in,float*out,uint tokens,const uint*reuse_gate){if(reuse_gate&&reuse_gate[0])return;vit_attention_transposed_score_body<640,true,true,true,true>(in,out,tokens);}'
new='''WAVE void vit_attention_fused_640_bytein_bout_qkfrag(const float*in,float*out,uint tokens,const uint*reuse_gate){if(reuse_gate&&reuse_gate[0])return;
#if defined(__gfx1201__) && VIT_FAST_NUM==15
 if(tokens==640){vit_attention_transposed_score_body<640,true,true,true,true,true>(in,out,640);return;}
#endif
 vit_attention_transposed_score_body<640,true,true,true,true>(in,out,tokens);}
''';assert src.count(old)==1;src=src.replace(old,new)
sp=importlib.util.spec_from_file_location('r','Development/tools/llvm-fork/compile-modules.py');m=importlib.util.module_from_spec(sp);sp.loader.exec_module(m);gen=next(x[2]for x in m.recipe(Path('hip'))if x[0]=='deep_fast-packed-fast');base=Path('hip/deep_fast.hip').read_text();assert gen.count(base)==1;gen=gen.replace(base,src);(p/'token-pair.hip').write_text(gen);(p/'token-pair-source.hip').write_text(src)
x=json.loads((p/'vit-fix-compile.json').read_text());x['name']='token-scoped-pair';x['files'][1]['path']=x['files'][1]['path'].replace('vit-fix.hip','token-pair.hip');x['files'][1]['sha256']=hashlib.sha256(gen.encode()).hexdigest();x['jobs'][0]['label']='token-pair-compile';x['jobs'][0]['arguments']=x['jobs'][0]['arguments'].replace('vit-fix.hip','token-pair.hip');(p/'token-pair-compile.json').write_text(json.dumps(x,indent=2))
