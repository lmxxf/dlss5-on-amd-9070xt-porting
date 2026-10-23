#!/usr/bin/env python3
"""Refresh unchanged GPL-host bundle with current MIT core; stage docs/config/shaders."""
import argparse,hashlib,io,json,subprocess,tarfile,zipfile
from pathlib import Path
p=argparse.ArgumentParser(__doc__);p.add_argument('--out',type=Path,required=True);p.add_argument('--base',type=Path,required=True);p.add_argument('--commit',required=True);a=p.parse_args();a.out.mkdir(parents=True,exist_ok=True)
updated=[];names=set()
with tarfile.open(a.base,'r:gz') as src,tarfile.open(a.out/'re9-presr-source.tar.gz','w:gz') as dst:
 for m in src:
  names.add(m.name);data=src.extractfile(m).read() if m.isfile() else None
  if m.name.startswith('third_party/lmxxf/'):
   rel=m.name[len('third_party/lmxxf/'):];f=Path(rel)
   if rel=='SOURCE_COMMIT.txt':data=('git_base_commit='+a.commit+'\nsource_dirty=true\n').encode()
   elif f.is_file():data=f.read_bytes();updated.append(rel)
  if data is not None:m.size=len(data);dst.addfile(m,io.BytesIO(data))
  else:dst.addfile(m)
 changed=sorted(str(f) for root in ['src','include','hip','Development/HIP'] for f in Path(root).rglob('*') if f.is_file() and f.suffix in {'.h','.hpp','.cpp','.c','.hip','.inc','.ps1','.py','.sh','.md','.txt'})
 for rel in changed:
  f=Path(rel)
  if not f.is_file() or '/experiments/' in rel:continue
  if rel.startswith('Development/HIP/') and f.parent!=Path('Development/HIP'):continue
  name='third_party/lmxxf/'+rel
  if name not in names:
   m=tarfile.TarInfo(name);data=f.read_bytes();m.size=len(data);m.mode=0o644;dst.addfile(m,io.BytesIO(data));updated.append(rel)
(a.out/'source-refresh.json').write_text(json.dumps(dict(base=str(a.base),git_base_commit=a.commit,source_dirty=True,updated=updated,archive_sha256=hashlib.sha256((a.out/'re9-presr-source.tar.gz').read_bytes()).hexdigest()),indent=2)+'\n')
for folder,filename in [('scripts','config045.zip'),('shaders','source045.zip')]:
 with zipfile.ZipFile(a.out/filename,'w',zipfile.ZIP_DEFLATED) as z:
  for f in Path(folder).rglob('*'):
   if f.is_file():z.write(f,str(f.relative_to(folder)) if folder=='scripts' else str(f))
print('SOURCE_AND_CONFIG_READY',len(updated))
