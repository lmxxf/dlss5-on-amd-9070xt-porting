"""Save original C64 reduction instructions for explicit symbolic tracing."""
from pathlib import Path
import subprocess,re,json,hashlib
cubin=Path('/tmp/dlssnr-cubins/dlssnr-01.cubin')
sass=subprocess.check_output(['/usr/local/cuda/bin/cuobjdump','--dump-sass','--function','cc_tinlayout_fused_swin_2h_64_2_fp8',str(cubin)],text=True)
rows=[]
for line in sass.splitlines():
 m=re.search(r'/\*([0-9a-f]+)\*/\s+(.*?)\s*;',line)
 if m and 0x7a00<=int(m[1],16)<=0x8900:rows.append({'pc':m[1],'instruction':m[2]})
assert any(r['pc']=='87d0' and 'MUFU.RCP' in r['instruction'] for r in rows)
root=Path('release/native-rgb-valid1080/encoder-c64/window46-18')
(root/'c64-softmax-instructions.json').write_text(json.dumps({'cubin_sha256':hashlib.sha256(cubin.read_bytes()).hexdigest(),'scope':'disassembly evidence; key-coordinate symbolic mapping still required','instructions':rows},indent=2)+'\n')
print(f'Saved {len(rows)} instructions; reciprocal PCs87d0/87e0, final half sum8780')
