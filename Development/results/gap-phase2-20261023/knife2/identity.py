import pathlib,struct,hashlib,json,re
W=pathlib.Path('/home/lmxxf/work/dlss5-gap-phase2-20261023/knife2');O=pathlib.Path(__file__).parent
B=pathlib.Path('/home/lmxxf/work/dlss5-five-build/final-payload/HIP/gfx1201')
def parse(p):
 b=p.read_bytes();shoff=struct.unpack_from('<Q',b,40)[0];num,idx=struct.unpack_from('<HH',b,60);ss=[struct.unpack_from('<IIQQQQIIQQ',b,shoff+64*i) for i in range(num)];ns=b[ss[idx][4]:ss[idx][4]+ss[idx][5]];ds={};sy={}
 for s in ss:
  name=ns[s[0]:].split(b'\0')[0].decode();ds[name]=b[s[4]:s[4]+s[5]]
  if s[1] not in (2,11):continue
  st=ss[s[6]];strings=b[st[4]:st[4]+st[5]]
  for off in range(s[4],s[4]+s[5],s[9]):
   no,info,other,idx,va,sz=struct.unpack_from('<IBBHQQ',b,off);n=strings[no:].split(b'\0')[0].decode()
   if n.endswith('.kd') and idx<len(ss):
    section=ss[idx];raw=b[section[4]+va-section[3]:section[4]+va-section[3]+sz]
    if len(raw)>=64:sy[n]=dict(rsrc1=struct.unpack_from('<I',raw,48)[0],rsrc2=struct.unpack_from('<I',raw,52)[0],rsrc3=struct.unpack_from('<I',raw,44)[0],properties=struct.unpack_from('<H',raw,56)[0])
 return {'sha256':hashlib.sha256(b).hexdigest(),'text_sha256':hashlib.sha256(ds['.text']).hexdigest(),'descriptors':sy}
r={}
for mod in ['c32-wave1-fast','c32-wave1-fast-norm900']:
 entries={role:parse(B/(mod+'.hsaco') if role=='baseline' else W/role/'build/gfx1201'/(mod+'.hsaco')) for role in ['baseline','mix','mix-alt']}
 entries['mix_alt_text_identical']=entries['mix']['text_sha256']==entries['mix-alt']['text_sha256']
 changed=[]
 for n,x in entries['baseline']['descriptors'].items():
  y=entries['mix']['descriptors'][n]
  # FP32/FP16 round+denorm mode are RSRC1 bits12:19. Compare all other flags separately.
  if ((x['rsrc1']>>12)&255)!=((y['rsrc1']>>12)&255):changed.append(n)
 entries['float_mode_changed']=changed
 entries['all_descriptor_fields_identical']=entries['baseline']['descriptors']==entries['mix']['descriptors']
 r[mod]=entries
 print(mod,entries['mix_alt_text_identical'],'descriptors',len(entries['baseline']['descriptors']),'floatmodechanged',changed,'all_descriptors_same',entries['all_descriptor_fields_identical'])
(O/'identity.json').write_text(json.dumps(r,indent=2))
