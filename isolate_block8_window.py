"""Reduce actual block8 mismatch to its shifted8x8 source window."""
from pathlib import Path
import numpy as np,json,subprocess
from encode_tinlayout_global import quantize
from decode_tinlayout_global import e4m3fn
from native_c64_reference import block,unpack
base=Path('release/native-rgb-valid1080/encoder-c64');root=base/'window46-18';root.mkdir(exist_ok=False)
# ShiftY4: output y46 lies in padded48..55, hence source44..51; X16..23.
x=np.fromfile(base/'block7-main.f32',np.float32).reshape(288,480,64)[44:52,16:24].copy()
inv=np.argsort(np.load('release/native-c64/view/mapping.npz')['cell_output_to_hwc'])
cells=quantize(x).reshape(2,4,2,4,64).transpose(0,2,1,3,4).reshape(-1,1024);encoded=np.empty_like(cells);encoded[:,inv]=cells;encoded.tofile(root/'input.fp8')
subprocess.run(['/tmp/native-upsample-global-oracle','/tmp/dlssnr-cubins/dlssnr-01.cubin',str(base/'block8.weights'),str(root/'input.fp8'),str(root/'output.fp8'),str(root/'aux.fp8'),'cc_tinlayout_fused_swin_2h_64_2_fp8','8','8','1','1','2','7','0'],check=True,timeout=20)
def decode(raw,h,w):return e4m3fn(raw[:h*w*64].reshape(-1,1024)[:,inv]).reshape(h//4,w//4,4,4,64).transpose(0,2,1,3,4).reshape(h,w,64)
actual=decode(np.fromfile(root/'output.fp8',np.uint8),8,8)
large=decode(np.fromfile(base/'block8-main.fp8',np.uint8),288,480)[44:52,16:24]
expected=block(x.reshape(1,64,64),*unpack(base/'block8.weights')).reshape(8,8,64)
r={'scope':'original cropped8x8 versus full shifted kernel and CPU reference','original_crop_different':int(np.count_nonzero(actual!=large)),'reference_different':int(np.count_nonzero(actual!=expected)),'mismatches':[{'yxc':idx.tolist(),'original':float(actual[tuple(idx)]),'reference':float(expected[tuple(idx)])} for idx in np.argwhere(actual!=expected)]}
x.tofile(root/'input.f32');actual.tofile(root/'oracle.f32');(root/'validation.json').write_text(json.dumps(r,indent=2)+'\n');print(json.dumps(r,indent=2));assert r['original_crop_different']==0
