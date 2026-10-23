from pathlib import Path
import shutil,re,importlib.util,subprocess,hashlib,json
R=Path('/home/lmxxf/work/ai-theorys-study/wechat/assets/297');W=Path('/home/lmxxf/work/dlss5-gap-phase5-20261023');O=Path(__file__).parent;B=W/'baseline-source';S=W/'knife1/pair-only-source'
# Preview only. Do not touch the four frozen experiment inputs or production.
shutil.copytree(B/'hip',S/'hip',dirs_exist_ok=True);(S/'Development/HIP').mkdir(parents=True,exist_ok=True);shutil.copy2(B/'Development/HIP/swin_persistent_types.h',S/'Development/HIP/swin_persistent_types.h')
s=(W/'knife1/source/hip/c512_qkv_attention_compact.inc').read_text()
s=s.replace('// Phase5 isolated switches. No production default changes.','// Paired FP8 conversion preserves the path-specific float operations before packing.')
s=s.replace('#ifndef C512_NORM_PERMLANE\n#define C512_NORM_PERMLANE 0\n#endif\n','')
lines=s.splitlines(keepends=True);out=[];i=0
while i<len(lines):
 if lines[i].strip()!='#if C512_NORM_PERMLANE':out.append(lines[i]);i+=1;continue
 j=i+1;depth=1;else_at=None
 while depth:
  q=lines[j].strip()
  if q.startswith(('#if ','#ifdef ','#ifndef ')):depth+=1
  elif q=='#endif':depth-=1
  elif q=='#else'and depth==1:else_at=j
  j+=1
 if else_at is not None:out.extend(lines[else_at+1:j-1])
 i=j
s=''.join(out);assert 'C512_NORM_PERMLANE'not in s and 'c5c_norm_broadcast'not in s
p=S/'hip/c512_qkv_attention_compact.inc';p.write_text(s)
p=S/'hip/build-modules.ps1';s=p.read_text();old="@{ name = 'c512-m32-mh'; defines = @(";assert s.count(old)==1;s=s.replace(old,old+"'C512_PAIR_PACK 1',");p.write_text(s)
patch=''
for f in ['hip/c512_qkv_attention_compact.inc','hip/build-modules.ps1']:
 patch+=subprocess.run(['diff','-u','--label','a/'+f,'--label','b/'+f,str(B/f),str(S/f)],capture_output=True,text=True).stdout
(O/'pair-only-preview.patch').write_text(patch)
spec=importlib.util.spec_from_file_location('recipe',R/'Development/tools/llvm-fork/compile-modules.py');m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m)
row=next(x for x in m.recipe(S/'hip')if x[0]=='c512-m32-mh');name,compiler,generated,defines,sources,opts=row
p=W/'knife1/pair-only-preview.generated.hip';p.write_text(generated)
r={'status':'PREVIEW_NOT_APPLIED_NOT_GPU_TESTED','source_root':str(S),'generated':str(p),'sha256':hashlib.sha256(p.read_bytes()).hexdigest(),'module':name,'targets':['gfx1200','gfx1201'],'RTC_EXTRA_OPTS':'','defines':defines,'sources':sources,'compiler_rows':compiler,'preserves_original_serial_norm':True,'requires_text_metadata_match_to_measured_pair_before_acceptance':True}
(O/'pair-only-preview-recipe.json').write_text(json.dumps(r,indent=2));print(json.dumps(r,indent=2))
