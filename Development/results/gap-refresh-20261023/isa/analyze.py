import pathlib,struct,subprocess,json,re,hashlib,collections
R=pathlib.Path('/home/lmxxf/work/dlss5-gap-source-20261023/DLSSNR-AMD-a9b13dd4cdb5f4bf43db358492d72e6e5e2acd67')
W=pathlib.Path('/home/lmxxf/work/dlss5-aco-isa-20261023');W.mkdir(exist_ok=True)
O=pathlib.Path(__file__).resolve().parent
OB='/home/lmxxf/work/llvm-build-23/bin/llvm-objdump'
RO='/home/lmxxf/work/llvm-build-23/bin/llvm-readobj'
GL='/home/lmxxf/work/aco-isa/mz/toolchain/glslang/bin/glslang'
def fnv(b):
 h=1469598103934665603
 for x in b:h=((h^x)*1099511628211)&((1<<64)-1)
 return h
def elf(code):
 names=b'\0.text\0.shstrtab\0';so=64+len(code);shoff=(so+len(names)+7)&~7
 ident=b'\x7fELF\x02\x01\x01'+bytes(9)
 hdr=struct.pack('<16sHHIQQQIHHHHHH',ident,1,224,1,0,0,shoff,0x4e,64,0,0,64,3,2)
 sh=lambda *v:struct.pack('<IIQQQQIIQQ',*v)
 return hdr+code+names+bytes(shoff-so-len(names))+bytes(64)+sh(1,1,6,0,64,len(code),0,0,4,0)+sh(7,3,0,0,so,len(names),0,0,1,0)
def counts(text):
 ops=[]
 for l in text.splitlines():
  # objdump amdgpu format has mnemonic first, address/bytes trailing comment
  m=re.match(r'\s*((?:s_|v_|ds_|global_|flat_|buffer_|image_|scratch_)[A-Za-z0-9_]+)\b',l)
  if m:ops.append(m[1])
 c=collections.Counter(ops)
 def n(pred):return sum(v for k,v in c.items() if pred(k))
 return dict(instructions=len(ops),wmma=n(lambda k:'wmma' in k),valu=n(lambda k:k.startswith('v_') and 'wmma' not in k),vopd=n(lambda k:k.startswith('v_dual')),vmem_load=n(lambda k:k.startswith(('global_','buffer_','flat_')) and ('load' in k)),vmem_store=n(lambda k:k.startswith(('global_','buffer_','flat_')) and 'store' in k),lds_load=n(lambda k:k.startswith('ds_') and ('load' in k or 'read' in k)),lds_store=n(lambda k:k.startswith('ds_') and ('store' in k or 'write' in k)),wait=n(lambda k:k.startswith('s_wait')),barrier=n(lambda k:'barrier' in k),opcodes=dict(c))
pipes=json.loads((R/'linux/shaders/rdna4/pipelines.json').read_text())['pipelines']
names=['fswin32','fswin32nh','fswindsp32','fswin64','fswin128','fswin256','attn','ffwd3','gemmproj5','gemmprojc','vitattn','gemmwide','gemmcontract','qkv']
# Build all network pipelines to establish exact record name/hash identities.
rows=[]
for name,p in pipes.items():
 src=R/'linux/shaders/rdna4'/p['source'];out=W/f'g_{name}.spv'
 cmd=[GL,'-V','--target-env','vulkan1.3','-I'+str(R/'linux/shaders/rdna4/include'),'-I'+str(R/'linux/shaders/passes/include')]+['-D'+d for d in p.get('defines',[])]+[str(src),'-o',str(out)]
 cp=subprocess.run(cmd,capture_output=True,text=True)
 if cp.returncode:rows.append(dict(name=name,error=cp.stdout+cp.stderr));continue
 b=out.read_bytes();h=fnv(b);rec=R/'windows/data/aco/records'/f'{h:016x}.nrp'
 row=dict(name=name,spv_hash=f'{h:016x}',spv_sha256=hashlib.sha256(b).hexdigest(),record_exists=rec.exists())
 if rec.exists():
  data=rec.read_bytes();v=struct.unpack_from('<QQ13I',data,8);spvh,codeh,*ints=v
  rsrc1,rsrc2,rsrc3,ex,wg,shared,push,user,set0,pushreg,grid,n,sz=ints
  code=data[96+8*n:];assert len(code)==sz and fnv(code)==codeh
  e=W/f'{name}.elf';e.write_bytes(elf(code[:ex]));cp=subprocess.run([OB,'-d','--mcpu=gfx1201',str(e)],capture_output=True,text=True);assert cp.returncode==0,cp.stderr
  (W/f'{name}.s').write_text(cp.stdout)
  row.update(record_sha256=hashlib.sha256(data).hexdigest(),code_sha256=hashlib.sha256(code).hexdigest(),exec_bytes=ex,wg=wg,lds=shared,vgpr_alloc=((rsrc1&63)+1)*8,rsrc1=hex(rsrc1),rsrc2=hex(rsrc2),**counts(cp.stdout))
 rows.append(row)
(O/'aco-stats.json').write_text(json.dumps(rows,indent=2))
print('pipelines',len(rows),'mapped',sum(x.get('record_exists',False) for x in rows),'errors',sum('error'in x for x in rows))
import yaml
H=pathlib.Path('/home/lmxxf/work/dlss5-five-build/final-payload/HIP/gfx1201')
hip=[]
for module in ['c32-wave1-fast','c64-wave2-fast','c512-m32-deep','c512-m32-mh','deep_fast-packed-fast','multihead-fast-padded-wave-packed','vit-stream-fast','swin-persistent']:
 path=H/(module+'.hsaco')
 if not path.exists():print('missing',path);continue
 notes=subprocess.run([RO,'--notes',str(path)],capture_output=True,text=True).stdout
 y=notes.split('AMDGPU Metadata: ---',1)[1].split('...',1)[0];meta=yaml.safe_load(y)['amdhsa.kernels']
 dis=subprocess.run([OB,'-d','--mcpu=gfx1201',str(path)],capture_output=True,text=True).stdout
 (W/(module+'.s')).write_text(dis)
 sections=re.split(r'^([0-9a-f]+) <([^>]+)>:\n',dis,flags=re.M)
 functions={sections[i+1]:sections[i+2] for i in range(1,len(sections),3)}
 for m in meta:
  name=m['.name'];txt=functions.get(name,'')
  raw=path.read_bytes();shoff=struct.unpack_from('<Q',raw,40)[0];shnum=struct.unpack_from('<H',raw,60)[0]
  shs=[struct.unpack_from('<IIQQQQIIQQ',raw,shoff+64*i) for i in range(shnum)]
  symbols={}
  for sh in shs:
   if sh[1] not in (2,11):continue
   st=shs[sh[6]];strings=raw[st[4]:st[4]+st[5]]
   for off in range(sh[4],sh[4]+sh[5],sh[9]):
    no,info,other,si,va,sz=struct.unpack_from('<IBBHQQ',raw,off)
    if (info&15)==2:symbols[strings[no:].split(b'\0')[0].decode()]=(va,sz)
  start,size=symbols[name]
  txt='\n'.join(l for l in txt.splitlines() if (a:=re.search(r'// ([0-9A-Fa-f]+):',l)) and start<=int(a[1],16)<start+size)
  c=counts(txt)
  hip.append(dict(module=module,module_sha256=hashlib.sha256(path.read_bytes()).hexdigest(),name=name,vgpr=m['.vgpr_count'],sgpr=m['.sgpr_count'],lds=m['.group_segment_fixed_size'],private=m['.private_segment_fixed_size'],wg_max=m['.max_flat_workgroup_size'],wave=m['.wavefront_size'],wgp=m.get('.workgroup_processor_mode'),**c))
(O/'hip-stats.json').write_text(json.dumps(hip,indent=2))
print('HIP kernels',len(hip))
# Full export catalog stays outside repository; keep trace-selected entries if available.
(W/'hip-all-842-stats.json').write_text(json.dumps(hip,indent=2))
trace_paths=[pathlib.Path('/tmp/dlss5-trace-current.log'),pathlib.Path('/tmp/dlss5-trace-candidate.log')]
if all(p.exists() for p in trace_paths):
 aliases={'c32_wave1':'c32-wave1-fast','c64_wave2':'c64-wave2-fast','c512_m32_deep':'c512-m32-deep','c512_m32_mh':'c512-m32-mh','deep_fast':'deep_fast-packed-fast','mh_fast':'multihead-fast-padded-wave-packed','vit_stream':'vit-stream-fast','sp':'swin-persistent'}
 selected=set()
 for p in trace_paths:
  for m in re.finditer(r'GAP_DISPATCH frame=0 index=\d+ \S+ (\w+):(\w+)',p.read_text()):selected.add((aliases.get(m[1],m[1]),m[2]))
 (O/'hip-stats.json').write_text(json.dumps([x for x in hip if (x['module'],x['name']) in selected],indent=2))
