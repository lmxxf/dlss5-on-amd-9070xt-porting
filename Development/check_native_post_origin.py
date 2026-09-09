"""Compare observed origin against the zero-padded shifted post reference."""
from pathlib import Path
import json,os,subprocess,numpy as np
from native_post70_reference import post,unpack
root=Path('release/native-temporal-valid1080/post70');crop=root/'crop'
main=np.memmap(root/'main.f32',np.float32,'r',shape=(576,960,32))[224:232,112:120].copy()
skip=np.memmap(root/'skip.f32',np.float32,'r',shape=(1152,1920,32))[448:464,224:240].copy()
color=np.fromfile(crop/'color.f32',np.float32).reshape(16,16,4)[:,:,:3]
params=unpack('release/native-post70/smoke/weights.bin');checks=[]
for origin,name in [(0,'origin-zero.f32'),(-4,'origin-minus4.f32')]:
 env=dict(os.environ,DLSS5_POST_TEST_ORIGIN=str(origin))
 subprocess.run(['/tmp/native-post70-oracle','/tmp/dlssnr-cubins/dlssnr-00.cubin','cc_tinlayout_fused_post_block_swin_1h_32_fp8',str(crop/'main.fp8'),str(crop/'skip.fp8'),'release/native-post70/smoke/weights.bin','release/native-post70/smoke/blend.bin',str(crop/'color.f32'),str(crop/name),'16','16','1','1','0.03125','native'],env=env,check=True,timeout=20)
 actual=np.fromfile(crop/name,np.float32).reshape(16,16,4)[:,:,:3]
 expected=post(main,skip,color,params,origin=(origin,origin))
 checks.append({'origin':origin,'different':int(np.count_nonzero(actual!=expected)),'max_abs':float(np.abs(actual-expected).max()),'finite':bool(np.isfinite(actual).all() and np.isfinite(expected).all())})
report={'scope':'16x16 controlled post origin only; not full live post contract','checks':checks}
(crop/'origin-validation.json').write_text(json.dumps(report,indent=2)+'\n');print(report)
assert all(c['different']==0 and c['finite'] for c in checks)
