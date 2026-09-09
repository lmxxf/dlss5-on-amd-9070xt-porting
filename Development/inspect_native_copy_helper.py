"""Static PE evidence for a candidate internal CopyResource wrapper; no hooks."""
from pathlib import Path
import hashlib,json,struct,subprocess
dll=Path('/home/lmxxf/work/tmp-test/nvngx_dlssnr.dll');data=dll.read_bytes()
pe=struct.unpack_from('<I',data,0x3c)[0];assert data[pe:pe+4]==b'PE\0\0'
count=struct.unpack_from('<H',data,pe+6)[0];optional=pe+24
assert struct.unpack_from('<H',data,optional)[0]==0x20b
image=struct.unpack_from('<Q',data,optional+24)[0]
start=optional+struct.unpack_from('<H',data,pe+20)[0]
sections=[]
for i in range(count):
 o=start+40*i;vs,va,rs,rp=struct.unpack_from('<4I',data,o+8)
 sections.append((va,vs,rp,rs,struct.unpack_from('<I',data,o+36)[0]))
def rva_offset(rva):
 for va,vs,rp,rs,_ in sections:
  if va<=rva<va+rs:return rp+rva-va
 raise ValueError('RVA not file-backed')
def offset_rva(offset):
 for va,vs,rp,rs,_ in sections:
  if rp<=offset<rp+rs:return va+offset-rp
 raise ValueError('Offset not section-backed')
rva=0x5c070;offset=rva_offset(rva);needle=struct.pack('<Q',image+rva)
occurrences=[];pos=0
while True:
 pos=data.find(needle,pos)
 if pos<0:break
 occurrences.append({'file_offset':hex(pos),'rva':hex(offset_rva(pos))});pos+=1
out=Path('release/native-copy-static');out.mkdir(exist_ok=True)
assembly=subprocess.run(['x86_64-w64-mingw32-objdump','-d',f'--start-address={image+rva}',f'--stop-address={image+0x5c0ec}',str(dll)],check=True,text=True,capture_output=True).stdout
(out/'copy-helper.asm').write_text(assembly)
report={'scope':'static candidate wrapper only; no live call/history proof','dll_sha256':hashlib.sha256(data).hexdigest(),
 'wrapper_rva':hex(rva),'prologue32':data[offset:offset+32].hex(),'pointer_occurrences':occurrences,
 'inferred_arguments':['backend context','command list','destination resource','source resource'],
 'evidence':'RDX saved as RBX/list, R8 as RSI/destination, R9 as RDI/source; at RVA5c0c1 load list vtable slot0x88 and call guard dispatch; returns int0 or -5'}
if len(occurrences)==1:
 entry=int(occurrences[0]['file_offset'],16);begin=entry
 def code_pointer(value):return any((flags&0x20000000) and image+va<=value<image+va+vs for va,vs,rp,rs,flags in sections)
 while begin>=8 and code_pointer(struct.unpack_from('<Q',data,begin-8)[0]):begin-=8
 report['contiguous_function_table_candidate']={'base_rva':hex(offset_rva(begin)),'slot_offset':hex(entry-begin),'slot_index':(entry-begin)//8}
(out/'validation.json').write_text(json.dumps(report,indent=2)+'\n');print(report)
