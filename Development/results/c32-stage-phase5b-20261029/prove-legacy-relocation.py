from pathlib import Path
import re,subprocess,struct,importlib.util,json
p=Path(__file__).parent;sp=importlib.util.spec_from_file_location('m','Development/tools/llvm-fork/compare-kernels.py');m=importlib.util.module_from_spec(sp);sp.loader.exec_module(m);name='c32_fast_ffn_attention_fused_half_prefix_finish_main8';out=[]
def read_va(f,va):
 b=f.read_bytes();off=struct.unpack_from('<Q',b,40)[0]
 for i in range(struct.unpack_from('<H',b,60)[0]):
  h=struct.unpack_from('<IIQQQQIIQQ',b,off+64*i)
  if h[1]!=8 and h[3]<=va and va+4<=h[3]+h[5]:return b[h[4]+va-h[3]:h[4]+va-h[3]+4].hex()
 raise AssertionError('unmapped PC target')
def instructions(f):
 _,ks,sy=m.elf(f);va,sz,code=sy[name];s=subprocess.check_output(['/home/lmxxf/work/llvm-build-23/bin/llvm-objdump','-d','--mcpu='+f.parent.name,'--disassemble-symbols='+name,str(f)],text=True);ls=[];pcs=[]
 for l in s.splitlines():
  q=re.search(r'// ([0-9A-Fa-f]+):',l)
  if q and va<=int(q[1],16)<va+sz:ls.append(l.split('//')[0].strip());pcs.append(int(q[1],16))
 return ls,pcs,ks[name]
for arch in ['gfx1200','gfx1201']:
 for mod in ['c32-wave1-fast','c32-wave1-fast-norm900']:
  a=p.parent/'isa-asm-phase3-20261029/payload/HIP'/arch/(mod+'.hsaco');b=p/'payload/HIP'/arch/(mod+'.hsaco');aa,ap,am=instructions(a);bb,bp,bm=instructions(b);assert am==bm and len(aa)==len(bb);diff=[i for i,(x,y)in enumerate(zip(aa,bb))if x!=y];assert len(diff)==1,(arch,mod,diff);i=diff[0];vals=[]
  for ls,pcs,f in [(aa,ap,a),(bb,bp,b)]:
   q=re.fullmatch(r's_add_co_u32 s2, s2, (0x[0-9a-f]+)',ls[i]);assert q,ls[i];j=max(j for j in range(i)if ls[j]=='s_getpc_b64 s[2:3]');assert i-j<10;imm=int(q[1],16);imm-=1<<32 if imm>>31 else 0;target=pcs[j]+4+imm;vals.append({'instruction':ls[i],'getpc_next':pcs[j]+4,'target_va':target,'target_4_bytes':read_va(f,target)})
  assert vals[0]['target_4_bytes']==vals[1]['target_4_bytes']=='0000003c';out.append({'arch':arch,'module':mod,'kernel':name,'all_other_instructions_identical':True,'full_metadata_identical':True,'baseline':vals[0],'candidate':vals[1]})
(p/'legacy-relocation-proof.json').write_text(json.dumps(out,indent=2));print('Four legacy-prefix functions: only PC-relative constant relocation; loaded float 1/128 and all other instructions/metadata identical')
