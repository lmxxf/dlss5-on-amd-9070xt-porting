import pathlib,struct,hashlib,json
W=pathlib.Path('/home/lmxxf/work/dlss5-gap-phase3-20261023/A');O=pathlib.Path(__file__).parent
def read(p):
 b=p.read_bytes();so=struct.unpack_from('<Q',b,40)[0];sn=struct.unpack_from('<H',b,60)[0];ss=[struct.unpack_from('<IIQQQQIIQQ',b,so+64*i) for i in range(sn)];f={};kd={}
 for s in ss:
  if s[1] not in (2,11):continue
  st=ss[s[6]];strings=b[st[4]:st[4]+st[5]]
  for o in range(s[4],s[4]+s[5],s[9]):
   no,info,ot,idx,va,sz=struct.unpack_from('<IBBHQQ',b,o);name=strings[no:].split(b'\0')[0].decode()
   if not idx or idx>=len(ss):continue
   sec=ss[idx];raw=b[sec[4]+va-sec[3]:sec[4]+va-sec[3]+sz]
   if info&15==2:f[name]={'size':sz,'sha256':hashlib.sha256(raw).hexdigest()}
   if name.endswith('.kd') and len(raw)==64:
    # Entry address/offset changes with inserted functions: retain only resource/config fields.
    kd[name]={'segment_and_kernarg':raw[:16].hex(),'rsrc_and_properties':raw[44:].hex(),'rsrc1':struct.unpack_from('<I',raw,48)[0]}
 return {'sha256':hashlib.sha256(b).hexdigest(),'functions':f,'descriptors':kd}
a=read(W/'comgr-baseline/deep_fast-packed-fast.hsaco');b=read(W/'comgr-candidate/deep_fast-packed-fast.hsaco')
diff=[n for n,v in a['functions'].items() if b['functions'].get(n)!=v];kd=[n for n,v in a['descriptors'].items() if b['descriptors'].get(n)!=v]
r={'baseline_sha256':a['sha256'],'candidate_sha256':b['sha256'],'old_functions':len(a['functions']),'new_functions':len(b['functions']),'old_code_differences':diff,'old_resource_differences':kd,'added_functions':sorted(set(b['functions'])-set(a['functions'])),'baseline':a,'candidate':b}
(O/'old-exports-identity.json').write_text(json.dumps(r,indent=2));print({k:v for k,v in r.items() if k not in ('baseline','candidate')})
