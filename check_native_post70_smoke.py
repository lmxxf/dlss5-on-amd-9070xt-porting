"""Bounded zero-feature original post-head call; not a visual port test."""
from pathlib import Path
import json,subprocess
import numpy as np
root=Path('release/native-post70/smoke');root.mkdir(parents=True,exist_ok=False)
report={'status':'running','scope':'original post70 zero-feature call only'}
def save():(root/'validation.json').write_text(json.dumps(report,indent=2)+'\n')
save()
try:
    for name,file in [('block70.layer0.layer','weights.bin'),('block70.layer0.blend_scale','blend.bin')]:
        subprocess.run(['python3','extract_native_weight_record.py','/home/lmxxf/work/tmp-test/nvngx_dlssnr.dll',name,str(root/file)],check=True,timeout=5)
    (root/'main.fp8').write_bytes(bytes(8*8*32));(root/'skip.fp8').write_bytes(bytes(16*16*32))
    color=np.full((16,16,4),.25,np.float32);color[:,:,3]=1;color.tofile(root/'color.f32')
    result=subprocess.run(['timeout','--kill-after=2s','15s','/tmp/native-post70-oracle','/tmp/dlssnr-cubins/dlssnr-00.cubin',
        'cc_tinlayout_fused_post_block_swin_1h_32_fp8',str(root/'main.fp8'),str(root/'skip.fp8'),str(root/'weights.bin'),str(root/'blend.bin'),str(root/'color.f32'),str(root/'output.f32'),'16','16','1','1','0.03125','native'],capture_output=True,text=True,timeout=20)
    (root/'stdout.txt').write_text(result.stdout);(root/'stderr.txt').write_text(result.stderr);print(result.stdout+result.stderr)
    assert result.returncode==0,f'post caller exit {result.returncode}'
    output=np.fromfile(root/'output.f32','<f4');assert output.size==16*16*4 and np.isfinite(output).all()
    error=np.abs(output.reshape(color.shape)[:,:,:3]-.25)
    report.update(rgb_different_from_base=int(np.count_nonzero(error)),rgb_max_error=float(error.max()),values=int(output.size))
    assert not np.any(error),'zero-feature post changes constant base RGB'
    report['status']='zero_smoke_pass'
except Exception as error:
    report.update(status='fail',error=str(error));raise
finally:
    save();print(json.dumps(report,indent=2))
