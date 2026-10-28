from pathlib import Path
import subprocess,struct,json,hashlib,yaml,sys
f=Path(sys.argv[1]);out=Path(sys.argv[2]);b=f.read_bytes();o=struct.unpack_from('<Q',b,40)[0];ss=[struct.unpack_from('<IIQQQQIIQQ',b,o+64*i) for i in range(struct.unpack_from('<H',b,60)[0])];codes={}
for sh in ss:
 if sh[1]!=2:continue
 st=ss[sh[6]];strings=b[st[4]:st[4]+st[5]]
 for pos in range(sh[4],sh[4]+sh[5],sh[9]):
  no,info,other,idx,va,sz=struct.unpack_from('<IBBHQQ',b,pos)
  if info&15==2:
   name=strings[no:].split(b'\0')[0].decode();sec=ss[idx];codes[name]=hashlib.sha256(b[sec[4]+va-sec[3]:sec[4]+va-sec[3]+sz]).hexdigest()
t=subprocess.check_output(['/home/lmxxf/work/llvm-build-23/bin/llvm-readobj','--notes',str(f)],text=True);md=yaml.safe_load(t.split('AMDGPU Metadata: ---',1)[1].split('...',1)[0]);kernels={m['.name']:dict(code_sha256=codes[m['.name']],vgpr=m['.vgpr_count'],sgpr=m['.sgpr_count'],lds=m['.group_segment_fixed_size'],private=m['.private_segment_fixed_size'],vgpr_spill=m.get('.vgpr_spill_count',0),sgpr_spill=m.get('.sgpr_spill_count',0)) for m in md['amdhsa.kernels']}
out.write_text(json.dumps({'path':str(f),'sha256':hashlib.sha256(b).hexdigest(),'target':md.get('amdhsa.target'),'elf_flags':struct.unpack_from('<I',b,48)[0],'kernels':kernels},indent=2))
