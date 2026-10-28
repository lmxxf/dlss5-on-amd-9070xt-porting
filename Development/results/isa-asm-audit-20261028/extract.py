from pathlib import Path
import re,subprocess,json,collections,hashlib,yaml,struct
R=Path.cwd();O=R/'Development/results/isa-asm-audit-20261028';W=O/'work';W.mkdir(exist_ok=True);P=R/'Development/results/backfeed-20261028/final-payload/HIP/gfx1201';rows=[];observed=set()
for f in (R/'Development/results/backfeed-20261028/knife1/evidence').glob('trace-*/stdout.log'):
 for m in re.finditer(r'SMALL_TRACE_DISPATCH frame=0 index=\d+ \w+ (\S+)',f.read_text()):observed.add(m[1].split(':')[-1])
for f in sorted(P.glob('*.hsaco')):
 b=f.read_bytes();shoff=struct.unpack_from('<Q',b,40)[0];sh=[struct.unpack_from('<IIQQQQIIQQ',b,shoff+64*i) for i in range(struct.unpack_from('<H',b,60)[0])];bounds={}
 for sec in sh:
  if sec[1]!=2:continue
  st=sh[sec[6]];strings=b[st[4]:st[4]+st[5]]
  for at in range(sec[4],sec[4]+sec[5],sec[9]):
   no,info,other,idx,va,size=struct.unpack_from('<IBBHQQ',b,at)
   if info&15==2:bounds[strings[no:].split(b'\0')[0].decode()]=(va,size)
 s=subprocess.check_output(['/home/lmxxf/work/llvm-build-23/bin/llvm-objdump','-d','--mcpu=gfx1201',str(f)],text=True);parts=re.split(r'^([0-9a-f]+) <([^>]+)>:\n',s,flags=re.M);dest=W/f.stem;dest.mkdir(exist_ok=True)
 notes=subprocess.check_output(['/home/lmxxf/work/llvm-build-23/bin/llvm-readobj','--notes',str(f)],text=True);md=yaml.safe_load(notes.split('AMDGPU Metadata: ---',1)[1].split('...',1)[0]);meta={m['.name']:m for m in md['amdhsa.kernels']}
 for i in range(1,len(parts),3):
  name=parts[i+1];body=parts[i+2]
  if name not in meta:continue
  start,size=bounds[name];body='\n'.join(l for l in body.splitlines() if not (pc:=re.search(r'// ([0-9A-Fa-f]+):',l)) or start<=int(pc[1],16)<start+size)+'\n'
  out=dest/(name+'.s');out.write_text(body);ls=body.splitlines();ops=collections.Counter(re.findall(r'^\s*((?:s_|v_|global_|ds_)[\w]+)',body,re.M));waits=[{'line':j+1,'text':l.strip()} for j,l in enumerate(ls) if 's_wait_loadcnt' in l or 's_wait_dscnt' in l];narrow=[{'line':j+1,'text':l.strip()} for j,l in enumerate(ls) if re.match(r'\s*ds_(?:load|store)_(?:b32|b16|b8|u16|u8)\b',l)]
  m=meta[name];rows.append({'module':f.name,'name':name,'observed_small':name in observed,'vgpr':m['.vgpr_count'],'sgpr':m['.sgpr_count'],'lds':m['.group_segment_fixed_size'],'private':m['.private_segment_fixed_size'],'ops':dict(ops),'waits':waits,'narrow_lds':narrow,'assembly':str(out.relative_to(O))})
(W/'all-kernel-index.json').write_text(json.dumps(rows,indent=2));(O/'observed-small.json').write_text(json.dumps(sorted(observed),indent=2));print(len(rows),'exports;',len(observed),'observed Small kernels')
