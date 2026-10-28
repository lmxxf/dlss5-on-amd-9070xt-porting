from pathlib import Path
import subprocess,struct,json,re,collections,yaml,sys
P=Path(__file__).parent;role=sys.argv[1];f=Path(sys.argv[2]);b=f.read_bytes();off=struct.unpack_from('<Q',b,40)[0];ss=[struct.unpack_from('<IIQQQQIIQQ',b,off+64*i) for i in range(struct.unpack_from('<H',b,60)[0])];sy={}
for sh in ss:
 if sh[1]!=2:continue
 st=ss[sh[6]];strings=b[st[4]:st[4]+st[5]]
 for at in range(sh[4],sh[4]+sh[5],sh[9]):
  no,info,other,idx,va,sz=struct.unpack_from('<IBBHQQ',b,at)
  if info&15==2:sy[strings[no:].split(b'\0')[0].decode()]=(va,sz)
s=subprocess.check_output(['/home/lmxxf/work/llvm-build-23/bin/llvm-objdump','-d','--mcpu=gfx1201',str(f)],text=True);sp=re.split(r'^([0-9a-f]+) <([^>]+)>:\n',s,flags=re.M);fn={sp[i+1]:sp[i+2] for i in range(1,len(sp),3)};nt=subprocess.check_output(['/home/lmxxf/work/llvm-build-23/bin/llvm-readobj','--notes',str(f)],text=True);md=yaml.safe_load(nt.split('AMDGPU Metadata: ---',1)[1].split('...',1)[0]);out={}
for m in md['amdhsa.kernels']:
 n=m['.name'];va,sz=sy[n];ls=[l for l in fn[n].splitlines() if (pc:=re.search(r'// ([0-9A-Fa-f]+):',l)) and va<=int(pc[1],16)<va+sz];ops=collections.Counter(re.findall(r'^\s*((?:v_|s_|ds_|global_)[\w]+)','\n'.join(ls),re.M));out[n]={'vgpr':m['.vgpr_count'],'sgpr':m['.sgpr_count'],'lds':m['.group_segment_fixed_size'],'private':m['.private_segment_fixed_size'],'vgpr_spill':m.get('.vgpr_spill_count',0),'sgpr_spill':m.get('.sgpr_spill_count',0),'instructions':sum(ops.values()),'opcodes':dict(ops)}
(P/(role+'-isa.json')).write_text(json.dumps(out,indent=2))
for n,d in out.items():
 if n=='c512_qkv_attention_compact_w16':print(role,n,{k:v for k,v in d.items() if k!='opcodes'},{k:v for k,v in d['opcodes'].items() if k.startswith('ds_load') or k=='v_lshl_or_b32'})
