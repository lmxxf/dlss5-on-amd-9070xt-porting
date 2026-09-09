"""Original encoder23..30 continuation, with CPU checks for each stage."""
from pathlib import Path
import argparse,subprocess,sys
p=argparse.ArgumentParser();p.add_argument('--base',type=Path,default=Path('release/native-rgb-valid1080'));args=p.parse_args()
previous=args.base/'encoder-c256/block22-down.fp8'
for block,shift in zip(range(23,31),(0,3,1,2,0,3,1,2)):
    subprocess.run([sys.executable,'check_native_decoder_split.py','--block',str(block),'--shift',str(shift),
        '--input',str(previous),'--output-root',str(args.base/'encoder-split'),'--game-extent'],check=True)
    previous=args.base/f'encoder-split/decoder-block{block}/output.fp8'
