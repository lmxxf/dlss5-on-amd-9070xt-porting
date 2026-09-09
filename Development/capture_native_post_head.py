"""Read original final projection registers for the isolated temporal post crop."""
from pathlib import Path
import os,subprocess
root=Path('release/native-temporal-valid1080/post70/crop')
for pc in ('0xc0a0','0xc100','0xc130','0xc140'):
 env=dict(os.environ,DLSS5_POST_DEBUG_PC=pc)
 result=subprocess.run(['/usr/local/cuda/bin/cuda-gdb','-batch','-x','debug_native_post_head.gdb','--args',
  '/tmp/native-post70-oracle','/tmp/dlssnr-cubins/dlssnr-00.cubin','cc_tinlayout_fused_post_block_swin_1h_32_fp8',
  str(root/'main.fp8'),str(root/'skip.fp8'),'release/native-post70/smoke/weights.bin','release/native-post70/smoke/blend.bin',
  str(root/'color.f32'),str(root/'debug-output.f32'),'16','16','1','1','0.03125','native'],env=env,text=True,capture_output=True,timeout=30)
 (root/(pc+'.log')).write_text(result.stdout+result.stderr);result.check_returncode();print('captured',pc,flush=True)
