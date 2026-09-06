"""Capture the first remaining 120x72 error window without touching games."""
from pathlib import Path
import os,subprocess
root=Path('release/native-temporal-large')
out=root/'block4-row6-warp1'
out.mkdir(exist_ok=True)
env={k:v for k,v in os.environ.items() if not k.startswith(('DLSS5_PREBLOCK_','DLSS5_TEMPORAL_'))}
env.update(DLSS5_PREBLOCK_PARAMETER_FILE='release/native-game-present/temporal-3536/preblock-live-1.bin',
           DLSS5_PREBLOCK_SEED='0',DLSS5_PREBLOCK_WIDTH='120',DLSS5_PREBLOCK_HEIGHT='72',
           DLSS5_PREBLOCK_SLOT8=str(root/'history.f32'),DLSS5_PREBLOCK_SLOT10=str(root/'motion.f32'),
           DLSS5_TEMPORAL_DEBUG_DIR=str(out),DLSS5_TEMPORAL_DEBUG_BLOCK_X='4',
           DLSS5_TEMPORAL_DEBUG_BLOCK_Y='6',DLSS5_TEMPORAL_DEBUG_THREAD_Y='1')
for pc in ('0x10d0','0x1590','0x1800'):
    env['DLSS5_TEMPORAL_DEBUG_PC']=pc
    result=subprocess.run(['/usr/local/cuda/bin/cuda-gdb','-batch','-x','debug_native_temporal_sample.gdb',
        '--args','/tmp/native-preblock-temporal-oracle','/tmp/dlssnr-cubins/dlssnr-00.cubin',
        'release/native-rgb-game/block0.weights',str(root/'input-hwc.f32'),
        str(out/'main.fp8'),str(out/'down.fp8'),'0'],env=env,capture_output=True,text=True,timeout=30)
    (out/(pc+'.log')).write_text(result.stdout+result.stderr)
    result.check_returncode()
    print('captured',pc,flush=True)
