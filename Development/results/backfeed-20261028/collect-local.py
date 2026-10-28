from pathlib import Path
import zipfile,sys
p=Path(__file__).parent;case=sys.argv[1] if len(sys.argv)>1 else 'knife1'
with zipfile.ZipFile(p/(case+'-evidence.zip')) as z:
 for f in z.infolist():
  if not f.is_dir():
   dst=p/case/'evidence'/f.filename.replace('\\','/');dst.parent.mkdir(parents=True,exist_ok=True);dst.write_bytes(z.read(f))
(p/(case+'-evidence.zip')).unlink()
