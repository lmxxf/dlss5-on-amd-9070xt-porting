import pathlib,struct,json,re,subprocess,yaml,hashlib,collections,sys
O=pathlib.Path(__file__).parent;W=pathlib.Path('/home/lmxxf/work/dlss5-gap-phase5-20261023/knife1');OB='/home/lmxxf/work/llvm-build-23/bin/llvm-objdump';RO='/home/lmxxf/work/llvm-build-23/bin/llvm-readobj'
def read(p):
 b=p.read_bytes();off=struct.unpack_from('<Q',b,40)[0];num=struct.unpack_from('<H',b,60)[0];ss=[struct.unpack_from('<IIQQQQIIQQ',b,off+64*i) for i in range(num)];sy={}
 for sh in ss:
  if sh[1] not in (2,11):continue
  st=ss[sh[6]];strings=b[st[4]:st[4]+st[5]]
  for o in range(sh[4],sh[4]+sh[5],sh[9]):
   no,info,other,idx,va,sz=struct.unpack_from('<IBBHQQ',b,o)
   if info&15==2:
    name=strings[no:].split(b'\0')[0].decode();sec=ss[idx];sy[name]=(va,sz,b[sec[4]+va-sec[3]:sec[4]+va-sec[3]+sz])
 dis=subprocess.check_output([OB,'-d','--mcpu=gfx1201',str(p)],text=True);(W/(p.parent.name+'-'+p.name+'.objdump.txt')).write_text(dis)
 split=re.split(r'^([0-9a-f]+) <([^>]+)>:\n',dis,flags=re.M);funcs={split[i+1]:split[i+2] for i in range(1,len(split),3)}
 nt=subprocess.check_output([RO,'--notes',str(p)],text=True);md={m['.name']:m for m in yaml.safe_load(nt.split('AMDGPU Metadata: ---',1)[1].split('...',1)[0])['amdhsa.kernels']};res={}
 for name in ['c512_qkv_attention_compact_w16']:
  if name not in sy:continue
  st,sz,raw=sy[name];ls=[l for l in funcs[name].splitlines() if (a:=re.search(r'// ([0-9a-fA-F]+):',l)) and st<=int(a[1],16)<st+sz]
  ops=collections.Counter(m[1] for l in ls if (m:=re.match(r'\s*((?:s_|v_|global_|ds_|buffer_|flat_)[A-Za-z0-9_]+)',l)));m=md[name]
  n=lambda f:sum(v for k,v in ops.items() if f(k))
  res[name]=dict(vgpr=m['.vgpr_count'],sgpr=m['.sgpr_count'],lds=m['.group_segment_fixed_size'],private=m['.private_segment_fixed_size'],size=sz,code_sha=hashlib.sha256(raw).hexdigest(),instructions=sum(ops.values()),wmma=n(lambda k:'wmma'in k),valu=n(lambda k:k.startswith('v_')and'wmma'not in k),vopd=n(lambda k:k.startswith('v_dual')),vmem_load=n(lambda k:k.startswith(('global_','buffer_','flat_'))and'load'in k),wait=n(lambda k:k.startswith('s_wait')),opcodes=dict(ops))
 return {'path':str(p),'sha':hashlib.sha256(b).hexdigest(),'kernels':res}
paths=[pathlib.Path(x) for x in sys.argv[1:]] or [W/'cpu-baseline/gfx1201/c512-m32-mh.hsaco',W/'cpu-candidate/gfx1201/c512-m32-mh.hsaco']
r=[read(p) for p in paths];out=O/('comgr-isa.json' if sys.argv[1:] else 'cpu-isa.json');out.write_text(json.dumps(r,indent=2))
for row in r:
 print(row['path'])
 for n,k in row['kernels'].items():print(n,{a:v for a,v in k.items() if a!='opcodes'})
