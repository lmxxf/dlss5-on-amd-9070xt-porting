from pathlib import Path
import struct,json,msgpack,importlib.util,re
p=Path(__file__).parent

def parse(path):
 data=bytearray(path.read_bytes());off=struct.unpack_from('<Q',data,40)[0];sz,n,si=struct.unpack_from('<HHH',data,58);h=[struct.unpack_from('<IIQQQQIIQQ',data,off+i*sz)for i in range(n)];names=data[h[si][4]:h[si][4]+h[si][5]]
 def string(buf,start):return bytes(buf[start:buf.index(0,start)]).decode()
 secs={string(names,x[0]):x for x in h};syms={}
 for sec in h:
  if sec[1]!=2:continue
  st=h[sec[6]];strings=data[st[4]:st[4]+st[5]]
  for a in range(sec[4],sec[4]+sec[5],sec[9]):
   ni,info,other,idx,value,size=struct.unpack_from('<IBBHQQ',data,a)
   if idx and idx<n:syms[string(strings,ni)]=h[idx][4]+value-h[idx][3]
 return data,secs,syms

src=p/'one-plane-isolated/build/gfx1201/c64-wave2-fast.hsaco';stock=Path('Development/results/ksearch-round4-20261031/payload/HIP/gfx1201/c64-wave2-fast.hsaco');data,secs,syms=parse(src);base,_,bsym=parse(stock);changes=[];targets=set()
for name,offset in syms.items():
 if not name.startswith('c128_wave2')or not name.endswith('.kd'):continue
 old=struct.unpack_from('<I',data,offset)[0]
 if old!=8192:continue
 bo=bsym[name];assert struct.unpack_from('<I',base,bo)[0]==16384
 # Do not guess at resource register bit fields. Existing resource2 must match stock.
 assert data[offset+52:offset+56]==base[bo+52:bo+56],name
 struct.pack_into('<I',data,offset,16384);targets.add(name[:-3]);changes.append({'kernel':name[:-3],'descriptor_offset':offset,'old':8192,'new':16384,'rsrc2_unchanged_and_matches_stock':True})
assert len(targets)==5
for sec in secs.values():
 if sec[1]!=7:continue
 cursor=sec[4];end=cursor+sec[5]
 while cursor+12<=end:
  ns,ds,typ=struct.unpack_from('<III',data,cursor);cursor+=12;vendor=bytes(data[cursor:cursor+ns]);cursor+=(ns+3)&~3;desc_start=cursor;desc=bytes(data[cursor:cursor+ds]);cursor+=(ds+3)&~3
  if vendor.rstrip(b'\0')!=b'AMDGPU'or typ!=32:continue
  obj=msgpack.unpackb(desc,raw=False);ks=obj['amdhsa.kernels'];needle=msgpack.packb('.group_segment_fixed_size');positions=[m.end()for m in re.finditer(re.escape(needle),desc)];assert len(positions)==len(ks)
  for k,pos in zip(ks,positions):
   u=msgpack.Unpacker(raw=False);u.feed(desc[pos:]);old=next(u);used=u.tell();assert old==k['.group_segment_fixed_size']
   if k['.name']in targets:
    enc=msgpack.packb(16384);assert old==8192 and len(enc)==used;data[desc_start+pos:desc_start+pos+used]=enc
out=p/'one-plane-padded.hsaco';out.write_bytes(data)
sp=importlib.util.spec_from_file_location('elf','Development/tools/llvm-fork/compare-kernels.py');e=importlib.util.module_from_spec(sp);sp.loader.exec_module(e);ss,sm,sf=e.elf(src);os,om,of=e.elf(out);assert ss['.text']==os['.text']and sf==of
for n in sm:
 expected=dict(sm[n]);
 if n in targets:expected['.group_segment_fixed_size']=16384
 assert expected==om[n],n
(p/'one-plane-padded-proof.json').write_text(json.dumps(dict(source=str(src),output=str(out),all_function_bytes_and_addresses_identical=True,text_section_identical=True,changes=changes,only_metadata_and_descriptor_lds_size_changed=True),indent=2)+'\n');print('5 kernels: .text/address exact; metadata + descriptor LDS8192->16384; resource2 unchanged')
