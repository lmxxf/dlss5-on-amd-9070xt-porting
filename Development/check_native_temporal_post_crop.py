"""Discriminate full-size texture effects from the aligned post70 local computation."""
from pathlib import Path
import subprocess,json
import numpy as np
from encode_tinlayout_global import quantize
from native_post70_reference import post,unpack
base=Path('release/native-temporal-valid1080/post70');root=base/'crop';root.mkdir(exist_ok=True)
y,x,h,w=448,224,16,16
main=np.memmap(base/'main.f32',np.float32,'r',shape=(576,960,32))[y//2:y//2+8,x//2:x//2+8].copy()
skip=np.memmap(base/'skip.f32',np.float32,'r',shape=(1152,1920,32))[y:y+h,x:x+w].copy()
color=np.memmap(base/'color.f32',np.float32,'r',shape=(1152,1920,4))[y:y+h,x:x+w].copy()
quantize(main).reshape(8,8,2,16).transpose(2,0,1,3).copy().tofile(root/'main.fp8')
basis=np.fromfile('release/post-skip-basis/matrix.f32',np.float32).reshape(2048,2048)
mapping=np.argmax(abs(basis),axis=0).reshape(8,8,32)[:4,:4].ravel()
cells=quantize(skip).reshape(4,4,4,4,32).transpose(0,2,1,3,4).reshape(-1,512)
packed=np.empty_like(cells);packed[:,mapping]=cells;packed.tofile(root/'skip.fp8');color.tofile(root/'color.f32')
subprocess.run(['/tmp/native-post70-oracle','/tmp/dlssnr-cubins/dlssnr-00.cubin','cc_tinlayout_fused_post_block_swin_1h_32_fp8',
 str(root/'main.fp8'),str(root/'skip.fp8'),'release/native-post70/smoke/weights.bin','release/native-post70/smoke/blend.bin',
 str(root/'color.f32'),str(root/'output.f32'),'16','16','1','1','0.03125','native'],check=True,timeout=20)
actual=np.fromfile(root/'output.f32',np.float32).reshape(h,w,4)[:,:,:3]
full=np.memmap(base/'output.f32',np.float32,'r',shape=(1152,1920,4))[y:y+h,x:x+w,:3]
expected=post(main,skip,color[:,:,:3],unpack('release/native-post70/smoke/weights.bin'))
report={'scope':'aligned crop experiment, not replacement full oracle','crop_vs_full':int(np.count_nonzero(actual!=full)),
 'crop_vs_cpu':int(np.count_nonzero(actual!=expected)),'target_original':float(actual[6,8,1]),'target_cpu':float(expected[6,8,1])}
(root/'validation.json').write_text(json.dumps(report,indent=2)+'\n');print(report)
