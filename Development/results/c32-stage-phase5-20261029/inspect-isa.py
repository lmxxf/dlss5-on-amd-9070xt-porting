from pathlib import Path
import subprocess,struct,re,collections,json,sys
f=Path(sys.argv[1]);out=Path(sys.argv[2]);b=f.read_bytes();o=struct.unpack_from('<Q',b,40)[0];ss=[struct.unpack_from('<IIQQQQIIQQ',b,o+64*i)for i in range(struct.unpack_from('<H',b,60)[0])];spans={}
for sh in ss:
 if sh[1]!=2:continue
 st=ss[sh[6]];strings=b[st[4]:st[4]+st[5]]
 for p in range(sh[4],sh[4]+sh[5],sh[9]):
  no,info,other,idx,va,sz=struct.unpack_from('<IBBHQQ',b,p);name=strings[no:].split(b'\0')[0].decode()
  if info&15==2 and name.endswith('_w16'):spans[name]=(va,va+sz)
t=subprocess.check_output(['/home/lmxxf/work/llvm-build-23/bin/llvm-objdump','-d','--mcpu=gfx1201',str(f)],text=True);counts={k:collections.Counter()for k in spans}
for l in t.splitlines():
 m=re.search(r'^\s*(\w+)\s.*?// ([0-9A-Fa-f]+):',l)
 if not m:continue
 pc=int(m[2],16)
 for k,(a,z)in spans.items():
  if a<=pc<z:counts[k][m[1]]+=1;break
out.write_text(json.dumps({'module':str(f),'note':'Static encoded opcodes bounded by symbol size; not dynamic transactions/bytes or time. Loop trip counts must be considered separately.','kernels':{k:dict(v)for k,v in counts.items()}},indent=2))
