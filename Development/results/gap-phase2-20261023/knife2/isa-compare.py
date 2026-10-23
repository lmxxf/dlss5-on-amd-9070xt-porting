import pathlib,subprocess,re,json,struct,collections,yaml,hashlib
W=pathlib.Path('/home/lmxxf/work/dlss5-gap-phase2-20261023/knife2');O=pathlib.Path(__file__).parent
OB='/home/lmxxf/work/llvm-build-23/bin/llvm-objdump';RO='/home/lmxxf/work/llvm-build-23/bin/llvm-readobj'
def symbols(b):
 shoff=struct.unpack_from('<Q',b,40)[0];num=struct.unpack_from('<H',b,60)[0];ss=[struct.unpack_from('<IIQQQQIIQQ',b,shoff+64*i) for i in range(num)];d={}
 for s in ss:
  if s[1] not in (2,11):continue
  st=ss[s[6]];strings=b[st[4]:st[4]+st[5]]
  for off in range(s[4],s[4]+s[5],s[9]):
   no,info,other,idx,va,sz=struct.unpack_from('<IBBHQQ',b,off)
   if (info&15)==2:d[strings[no:].split(b'\0')[0].decode()]=(va,sz)
 return d
def analyze(p):
 b=p.read_bytes();sy=symbols(b)
 notes=subprocess.check_output([RO,'--notes',str(p)],text=True);meta=yaml.safe_load(notes.split('AMDGPU Metadata: ---',1)[1].split('...',1)[0])['amdhsa.kernels']
 dis=subprocess.check_output([OB,'-d','--mcpu=gfx1201',str(p)],text=True);(W/('baseline-'+p.stem+'.objdump.txt') if p.parent==pathlib.Path('/home/lmxxf/work/dlss5-five-build/final-payload/HIP/gfx1201') else p.parent/(p.stem+'.objdump.txt')).write_text(dis)
 sections=re.split(r'^([0-9a-f]+) <([^>]+)>:\n',dis,flags=re.M);funcs={sections[i+1]:sections[i+2] for i in range(1,len(sections),3)};out={}
 for m in meta:
  n=m['.name']
  if not n.startswith('c32_wave1_') or 'w16' not in n:continue
  st,size=sy[n];ls=[l for l in funcs[n].splitlines() if (a:=re.search(r'// ([0-9a-fA-F]+):',l)) and st<=int(a[1],16)<st+size]
  ops=collections.Counter(x[1] for l in ls if (x:=re.match(r'\s*((?:s_|v_|ds_|global_|buffer_|flat_)[A-Za-z0-9_]+)',l)))
  get=lambda pred:sum(v for k,v in ops.items() if pred(k))
  out[n]=dict(vgpr=m['.vgpr_count'],sgpr=m['.sgpr_count'],lds=m['.group_segment_fixed_size'],spill=m['.private_segment_fixed_size'],code_bytes=size,instructions=sum(ops.values()),vopd=get(lambda k:k.startswith('v_dual')),valu=get(lambda k:k.startswith('v_') and 'wmma' not in k),wait=get(lambda k:k.startswith('s_wait')),wmma=get(lambda k:'wmma' in k),opcodes=dict(ops))
 return dict(sha256=hashlib.sha256(b).hexdigest(),kernels=out)
r={}
for role in ['baseline','mix','mix-alt']:
 r[role]={}
 for mod in ['c32-wave1-fast','c32-wave1-fast-norm900']:
  p=pathlib.Path('/home/lmxxf/work/dlss5-five-build/final-payload/HIP/gfx1201')/(mod+'.hsaco') if role=='baseline' else W/role/'build/gfx1201'/(mod+'.hsaco')
  r[role][mod]=analyze(p)
(O/'isa-comparison.json').write_text(json.dumps(r,indent=2))
for mod in ['c32-wave1-fast','c32-wave1-fast-norm900']:
 print(mod)
 for fn in ['c32_wave1_chain_w16','c32_wave1_prefix_b8d_w16','c32_wave1_finish_dcrop_b8d_w16','c32_wave1_post_b8_w16','c32_wave1_up_w16']:
  print(fn)
  for role in r:
   k=r[role][mod]['kernels'].get(fn)
   if k:print(role,{a:k[a] for a in ['vgpr','sgpr','lds','spill','code_bytes','instructions','vopd','valu','wmma','wait']})
