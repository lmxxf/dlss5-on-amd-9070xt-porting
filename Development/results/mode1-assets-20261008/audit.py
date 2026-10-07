import struct,pathlib,hashlib,json,re
root=pathlib.Path('/tmp/mode1-admission-20261008')
def elf(p):
 b=p.read_bytes();off=struct.unpack_from('<Q',b,40)[0];sz,n,si=struct.unpack_from('<HHH',b,58);ss=[struct.unpack_from('<IIQQQQIIQQ',b,off+i*sz) for i in range(n)]
 names=b[ss[si][4]:ss[si][4]+ss[si][5]];out={}
 for s in ss:
  if s[1]!=2:continue
  st=ss[s[6]];txt=b[st[4]:st[4]+st[5]]
  for o in range(s[4],s[4]+s[5],s[9]):
   name,info,other,i,val,size=struct.unpack_from('<IBBHQQ',b,o)
   if not i or i>=n or not size:continue
   name=txt[name:txt.index(b'\0',name)].decode();seg=ss[i];data=b[seg[4]+val-seg[3]:seg[4]+val-seg[3]+size];out[name]={'type':info&15,'bytes':data,'size':size}
 return out
required=['c32_wave1_post_logit','c32_wave1_post_b8_logit','c32_wave1_post_b8_features']
r={}
for p in (root/'modules-final/gfx1201').glob('*.hsaco'):
 sy=elf(p);asm=pathlib.Path(str(p)+'.s').read_text();notes={}
 for block in asm.split('  - .args:')[1:]:
  name=re.search(r'\.name:\s+(\S+)',block)
  if name:
   fields={k:int(re.search(r'\.'+k+r':\s+(\d+)',block).group(1)) for k in ['group_segment_fixed_size','private_segment_fixed_size','kernarg_segment_size','vgpr_count','sgpr_count'] if re.search(r'\.'+k+r':\s+(\d+)',block)}
   if name.group(1) in required:notes[name.group(1)]=fields
 v={'sha256':hashlib.sha256(p.read_bytes()).hexdigest(),'required_exports':{k:k in sy for k in required},'notes':notes,'export_functions':sorted(k for k,x in sy.items() if x['type']==2)}
 if p.name=='c32-wave1-fast.hsaco':
  old=elf(root/p.name);v['installed_legacy_export_diff']={k:{'oldsize':x['size'],'newsize':sy[k]['size'],'code_byte_same':x['bytes']==sy[k]['bytes']} for k,x in old.items() if x['type']==2 and k in sy};v['missing_old_exports']=[k for k,x in old.items() if x['type']==2 and k not in sy];v['legacy_descriptor_diff']={k:x['bytes'][:16]+x['bytes'][24:] == sy[k]['bytes'][:16]+sy[k]['bytes'][24:] for k,x in old.items() if k.endswith('.kd') and k in sy}
 r[p.name]=v
(root/'module-audit.json').write_text(json.dumps(r,indent=2)+'\n')
for k,v in r.items():
 print(k,v['sha256'],v['required_exports'],v['notes'])
 if 'installed_legacy_export_diff'in v:print('legacy',len(v['installed_legacy_export_diff']),'code_same',sum(x['code_byte_same'] for x in v['installed_legacy_export_diff'].values()),'descriptor_same',sum(v['legacy_descriptor_diff'].values()),'/',len(v['legacy_descriptor_diff']),'missing',v['missing_old_exports'])
