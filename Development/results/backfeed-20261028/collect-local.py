from pathlib import Path
import zipfile
p=Path(__file__).parent
with zipfile.ZipFile(p/'knife1-evidence.zip') as z:
 for f in z.infolist():
  if not f.is_dir():
   dst=p/'knife1/evidence'/f.filename.replace('\\','/');dst.parent.mkdir(parents=True,exist_ok=True);dst.write_bytes(z.read(f))
(p/'knife1-evidence.zip').unlink()
