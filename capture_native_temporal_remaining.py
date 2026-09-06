"""Capture the first remaining 120x72 error window without touching games."""
from pathlib import Path
import os,subprocess,argparse
parser=argparse.ArgumentParser()
parser.add_argument('--pcs',nargs='+',default=['0x10d0','0x1590','0x1800'])
parser.add_argument('--block',nargs=2,type=int,default=[4,6])
parser.add_argument('--warp',type=int,default=1)
parser.add_argument('--valid1080',action='store_true')
parser.add_argument('--timeout',type=float,default=30)
args=parser.parse_args()
root=Path('release/native-temporal-valid1080' if args.valid1080 else 'release/native-temporal-large')
bx,by=args.block
out=root/f'block{bx}-row{by}-warp{args.warp}'
out.mkdir(exist_ok=True)
env={k:v for k,v in os.environ.items() if not k.startswith(('DLSS5_PREBLOCK_','DLSS5_TEMPORAL_'))}
env.update(DLSS5_PREBLOCK_PARAMETER_FILE='release/native-game-present/temporal-3536/preblock-live-1.bin',
           DLSS5_PREBLOCK_SEED='0',DLSS5_PREBLOCK_WIDTH='120',DLSS5_PREBLOCK_HEIGHT='72',
           DLSS5_PREBLOCK_SLOT8=str(root/'history.f32'),DLSS5_PREBLOCK_SLOT10=str(root/'motion.f32'),
           DLSS5_TEMPORAL_DEBUG_DIR=str(out),DLSS5_TEMPORAL_DEBUG_BLOCK_X=str(bx),
           DLSS5_TEMPORAL_DEBUG_BLOCK_Y=str(by),DLSS5_TEMPORAL_DEBUG_THREAD_Y=str(args.warp))
if args.valid1080:env.update(DLSS5_PREBLOCK_WIDTH='1920',DLSS5_PREBLOCK_HEIGHT='1152',DLSS5_PREBLOCK_GAME_TEXTURE='1')
for pc in args.pcs:
    env['DLSS5_TEMPORAL_DEBUG_PC']=pc
    result=subprocess.run(['/usr/local/cuda/bin/cuda-gdb','-batch','-x','debug_native_temporal_sample.gdb',
        '--args','/tmp/native-preblock-temporal-oracle','/tmp/dlssnr-cubins/dlssnr-00.cubin',
        'release/native-rgb-game/block0.weights',str(root/('input.f32' if args.valid1080 else 'input-hwc.f32')),
        str(out/'main.fp8'),str(out/'down.fp8'),'0'],env=env,capture_output=True,text=True,timeout=args.timeout)
    (out/(pc+'.log')).write_text(result.stdout+result.stderr)
    result.check_returncode()
    print('captured',pc,flush=True)
