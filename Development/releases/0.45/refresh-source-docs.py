from pathlib import Path
import tarfile,io,hashlib,json,shutil
R=Path(__file__).resolve().parents[3];O=Path(__file__).parent;W=Path('/home/lmxxf/work/dlss5-release-045-build');p=W/'re9-presr-source.tar.gz';backup=W/'pre-docfix';backup.mkdir(exist_ok=True);old=backup/p.name
assert not old.exists();shutil.copy2(p,old)
for n in ['preparation-identity.json','FINAL-SCRIPTS-FROZEN.json','source-refresh.json']:
 if (O/n).exists():shutil.copy2(O/n,O/('pre-docfix-'+n))
updates={'third_party/lmxxf/'+n:R/n for n in ['README.md','README.zh-CN.md']}
with tarfile.open(old,'r:gz') as src,tarfile.open(p,'w:gz')as dst:
 for m in src:
  data=src.extractfile(m).read()if m.isfile()else None
  if m.name in updates:data=updates[m.name].read_bytes();m.size=len(data)
  dst.addfile(m,io.BytesIO(data)if data is not None else None)
def entries(path):
 with tarfile.open(path)as t:return [(m, t.extractfile(m).read()if m.isfile()else None)for m in t]
a,b=entries(old),entries(p);assert len(a)==len(b);changed=[]
for (x,u),(y,v)in zip(a,b):
 assert x.name==y.name
 for k in ['mode','uid','gid','uname','gname','mtime','type','linkname','devmajor','devminor','pax_headers']:assert getattr(x,k)==getattr(y,k),(x.name,k)
 if u!=v:changed.append(x.name);assert x.name in updates
 else:assert x.size==y.size
assert set(changed)==set(updates)
h=lambda f:hashlib.sha256(f.read_bytes()).hexdigest();d={'before_sha256':h(old),'after_sha256':h(p),'before_bytes':old.stat().st_size,'after_bytes':p.stat().st_size,'entries':len(a),'changed_entries':changed,'all_other_entry_bytes_and_permissions_identical':True,'old_archive':str(old)};(O/'docfix-source-identity.json').write_text(json.dumps(d,indent=2)+'\n')
i=O/'preparation-identity.json';j=json.loads(i.read_text());j['artifacts'][p.name]={'bytes':p.stat().st_size,'sha256':h(p)};j['docfix_identity']='docfix-source-identity.json';i.write_text(json.dumps(j,indent=2)+'\n')
r=O/'run-package.ps1';s=r.read_text(encoding='utf-8-sig');assert d['before_sha256']in s;s=s.replace(d['before_sha256'],d['after_sha256']);r.write_text(s,encoding='utf-8-sig')
i=O/'source-refresh.json';j=json.loads(i.read_text());j['archive_sha256']=h(p);j['docfix']=d;i.write_text(json.dumps(j,indent=2)+'\n')
f=O/'FINAL-SCRIPTS-FROZEN.json';j=json.loads(f.read_text());j['run-package.ps1']=h(r);j['reseal-docs.ps1']=h(O/'reseal-docs.ps1');f.write_text(json.dumps(j,indent=2)+'\n')
for n in ['preparation-identity.json','docfix-source-identity.json','run-package.ps1','source-refresh.json','FINAL-SCRIPTS-FROZEN.json','reseal-docs.ps1']:shutil.copy2(O/n,W/n)
print(json.dumps(d,indent=2))
