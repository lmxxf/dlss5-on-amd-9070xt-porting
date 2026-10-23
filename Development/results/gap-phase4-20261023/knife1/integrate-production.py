import pathlib,hashlib,json,shutil,subprocess,importlib.util
R=pathlib.Path('/home/lmxxf/work/ai-theorys-study/wechat/assets/297');W=pathlib.Path('/home/lmxxf/work/dlss5-gap-phase4-20261023/knife1');O=pathlib.Path(__file__).parent;S=W/'source';ident=json.loads((O/'source-identity.json').read_text());files=['hip/deep_fast.hip','hip/vit_stream.inc','Development/HIP/hip_reference_network.h']
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
for f in files:assert sha(R/f)==ident[f]['base'],('production base changed',f,sha(R/f),ident[f]['base'])
D=W/'production-before'
for f in files:
 (D/f).parent.mkdir(parents=True,exist_ok=True);shutil.copy2(R/f,D/f)
 data=(S/f).read_text()
 if f.endswith('hip_reference_network.h'):
  old='void InitVitVFragment(){const char*v=std::getenv("NR_VIT_VFRAG");vfrag_requested=v&&std::strcmp(v,"1")==0;'
  new='void InitVitVFragment(){const char*v=std::getenv("NR_VIT_VFRAG");if(!v||!*v)v=std::getenv("DLSS5_HIP_VIT_VFRAG");vfrag_requested=!v||!*v||std::strcmp(v,"1")==0;'
  assert data.count(old)==1;data=data.replace(old,new)
  data=data.replace(' bool vfrag_requested=false,',' // Default-on only in the already verified Original/FAST1/MP1/explicit-AE0 scope below.\n // NR diagnostic override > DLSS5_HIP_VIT_VFRAG > built-in 1; non-empty values other than 1 disable.\n bool vfrag_requested=false,',1)
 (R/f).write_text(data)
patch=''
for f in files:patch+=subprocess.run(['diff','-u','--label','a/'+f,'--label','b/'+f,str(D/f),str(R/f)],capture_output=True,text=True).stdout
(O/'production.patch').write_text(patch)
spec=importlib.util.spec_from_file_location('recipe',R/'Development/tools/llvm-fork/compile-modules.py');m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m)
(W/'production-modules').mkdir(exist_ok=True)
receipt={'production_files':{f:sha(R/f) for f in files},'gpu_generated':{},'scope':'Original FAST1 MP1 explicit AE0, exact same eligibility as measured','default_priority':'nonempty NR_VIT_VFRAG > nonempty DLSS5_HIP_VIT_VFRAG > 1'}
for name,compiler,src,defs,parts,opts in m.recipe(R/'hip'):
 if name in ['deep_fast-packed-fast','vit-stream-fast']:
  p=W/'production-modules'/(name+'.generated.hip');p.write_text(src);tested=W/(name+'.candidate.hip');assert p.read_bytes()==tested.read_bytes(),('GPU source drift',name)
  receipt['gpu_generated'][name]={'sha256':sha(p),'identical_to_tested_headmajor':True,'same_source_for':['gfx1201','gfx1200'],'path':str(p)}
(O/'production-identity.json').write_text(json.dumps(receipt,indent=2));print(json.dumps(receipt,indent=2))
