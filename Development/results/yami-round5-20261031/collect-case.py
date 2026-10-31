from pathlib import Path
import zipfile,sys
p=Path(__file__).parent;c=sys.argv[1]
with zipfile.ZipFile(p/(c+'-evidence.zip')) as z:
 for f in z.infolist():
  if not f.is_dir():
   dst=p/c/'evidence'/f.filename.replace('\\','/');dst.parent.mkdir(parents=True,exist_ok=True);dst.write_bytes(z.read(f))
