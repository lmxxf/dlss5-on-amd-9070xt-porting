"""Prepare actual-size post70 random inputs and original output, no pass claim."""
from pathlib import Path
import subprocess,json,argparse
import numpy as np
from native_c32_reference import F
from encode_tinlayout_global import quantize
p=argparse.ArgumentParser();p.add_argument('--valid1080',action='store_true');args=p.parse_args()
root=Path('release/native-rgb-valid1080/post70' if args.valid1080 else 'release/native-post70/game');root.mkdir(exist_ok=False)
rng=np.random.default_rng(3017);h,w=1152,1920
main=F(rng.normal(0,.25,(h//2,w//2,32)).astype(np.float32));skip=F(rng.normal(0,.25,(h,w,32)).astype(np.float32))
color=np.ones((h,w,4),np.float32);color[:,:,:3]=rng.uniform(.1,.9,(h,w,3)).astype(np.float32)
if args.valid1080:
 from decode_tinlayout_global import e4m3fn
 base=Path('release/native-rgb-valid1080');stage=base/'decoder-c32/decoder-block69'
 assert json.loads((stage/'validation.json').read_text())['status']=='pass'
 main=np.fromfile(stage/'oracle.f32',np.float32).reshape(h//2,w//2,32)
 basis=np.fromfile('release/post-skip-basis/matrix.f32','<f4').reshape(2048,2048);mapping=np.argmax(abs(basis),axis=0).reshape(8,8,32)[:4,:4].ravel()
 raw=np.fromfile(base/'block0-main.fp8',np.uint8)
 skip=e4m3fn(raw[:h*w*32].reshape(-1,512)[:,mapping]).reshape(h//4,w//4,4,4,32).transpose(0,2,1,3,4).reshape(h,w,32)
 color=np.pad(np.fromfile(base/'input-hwc.rgba32f',np.float32).reshape(1080,w,4),((0,72),(0,0),(0,0)),mode='reflect')
main.tofile(root/'main.f32');skip.tofile(root/'skip.f32');color.tofile(root/'color.f32')
quantize(main).reshape(h//2,w//2,2,16).transpose(2,0,1,3).copy().tofile(root/'main.fp8')
basis=np.fromfile('release/post-skip-basis/matrix.f32','<f4').reshape(2048,2048);mapping=np.argmax(abs(basis),axis=0).reshape(8,8,32)[:4,:4].ravel()
cells=quantize(skip).reshape(h//4,4,w//4,4,32).transpose(0,2,1,3,4).reshape(-1,512);packed=np.empty_like(cells);packed[:,mapping]=cells;packed.tofile(root/'skip.fp8')
subprocess.run(['/tmp/native-post70-oracle','/tmp/dlssnr-cubins/dlssnr-00.cubin','cc_tinlayout_fused_post_block_swin_1h_32_fp8',str(root/'main.fp8'),str(root/'skip.fp8'),'release/native-post70/smoke/weights.bin','release/native-post70/smoke/blend.bin',str(root/'color.f32'),str(root/'output.f32'),str(w),str(h),'1','1','0.03125','native'],check=True,timeout=30)
out=np.fromfile(root/'output.f32',np.float32);assert out.size==h*w*4 and np.isfinite(out).all()
(root/'capture.json').write_text(json.dumps({'scope':'same valid1080 RGB post70 with reflected base; actual game post texture contract not proven' if args.valid1080 else 'actual-size original post70 random fixture; numerical comparison pending','output_shape':[h,w,4],'finite':True},indent=2)+'\n')
print('Original post output captured, numerical verification pending')
