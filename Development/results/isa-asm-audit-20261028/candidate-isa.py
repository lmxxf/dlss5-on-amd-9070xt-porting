from pathlib import Path
import re,json,subprocess,collections,difflib,struct
P=Path(__file__).parent;out={}
for role,file,mod,names in [('upwide',P/'work/upwide-final/gfx1201/c64-wave2-fast.hsaco','c64-wave2-fast',['c64_wave2_up_lb_sh','c128_wave2_up_lb_sh']),('dectail',P/'work/dectail.hsaco','deep_fast-packed-fast',['decoder_project2x_h16w_byteout_w_bin_f8w16','decoder_project2x_h16w_w_bin_f8w16']),('ffnb2',P/'work/ffnb2/gfx1201/c64-wave2-fast.hsaco','c64-wave2-fast',['c128_wave2_bi_bo','c256_wave2_bi_bo_w16'])]:
 data=file.read_bytes();off=struct.unpack_from('<Q',data,40)[0];ss=[struct.unpack_from('<IIQQQQIIQQ',data,off+64*i) for i in range(struct.unpack_from('<H',data,60)[0])];bounds={}
 for sh in ss:
  if sh[1]!=2:continue
  st=ss[sh[6]];strings=data[st[4]:st[4]+st[5]]
  for at in range(sh[4],sh[4]+sh[5],sh[9]):
   no,info,other,idx,va,size=struct.unpack_from('<IBBHQQ',data,at)
   if info&15==2:bounds[strings[no:].split(b'\0')[0].decode()]=(va,size)
 s=subprocess.check_output(['/home/lmxxf/work/llvm-build-23/bin/llvm-objdump','-d','--mcpu=gfx1201',str(file)],text=True);sp=re.split(r'^([0-9a-f]+) <([^>]+)>:\n',s,flags=re.M);fn={sp[i+1]:sp[i+2] for i in range(1,len(sp),3)};r={}
 for n in names:
  base=(P/'work'/mod/(n+'.s')).read_text();start,size=bounds[n];cand='\n'.join(l for l in fn[n].splitlines() if not (pc:=re.search(r'// ([0-9A-Fa-f]+):',l)) or start<=int(pc[1],16)<start+size);(P/'work'/(role+'-'+n+'.s')).write_text(cand)
  # Drop addresses and padding after the last endpgm for a readable schedule diff.
  norm=lambda x:[re.sub(r'\s*//.*','',l).strip() for l in x.splitlines() if re.match(r'\s*(s_|v_|ds_|global_)',l)]
  a=norm(base);b=norm(cand);diff=list(difflib.unified_diff(a,b,fromfile='production/'+n,tofile=role+'/'+n,lineterm=''));(P/'work'/(role+'-'+n+'.diff')).write_text('\n'.join(diff))
  ops=lambda lines:dict(collections.Counter(l.split()[0] for l in lines));r[n]={'baseline':ops(a),'candidate':ops(b),'diff_lines':len(diff),'diff_excerpt':diff[:100]}
 out[role]=r
(P/'candidate-isa-diffs.json').write_text(json.dumps(out,indent=2))
for role,r in out.items():
 for n,v in r.items():print(role,n,'baseline',sum(v['baseline'].values()),'candidate incl padding',sum(v['candidate'].values()),'diff lines',v['diff_lines'])
