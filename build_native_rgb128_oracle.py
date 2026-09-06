"""Build a larger RGB-to-split oracle without the unverified 4x2 split call.

This does not resolve the original dispatcher's logical/physical-size policy.
"""
from pathlib import Path
import os,json,subprocess,argparse
import numpy as np
parser=argparse.ArgumentParser();parser.add_argument('--size',type=int,choices=[128,256,512],default=128);parser.add_argument('--through-pool',action='store_true');args=parser.parse_args()
if args.through_pool and args.size<256:parser.error('Pool continuation requires size >=256; 2x2 output is unverified')
size=args.size
folder=Path(f'release/native-rgb{size}');folder.mkdir(parents=True,exist_ok=True)
if size==128:tiles=np.fromfile('release/preblock-branch-audit/input.rgba32f','<f4').reshape(256,8,8,4)
else:
 tiles=np.random.default_rng(29701).random((size*size//64,8,8,4),dtype=np.float32);tiles[...,3]=1
tiles.tofile(folder/'input-tiles.rgba32f')
tiles.reshape(size//8,size//8,8,8,4).transpose(0,2,1,3,4).copy().tofile(folder/'input-hwc.rgba32f')
env={k:v for k,v in os.environ.items() if not k.startswith(('DLSS5_PREBLOCK_','DLSS5_NATIVE_SCAN_','DLSS5_SPLIT_'))}
env.update(DLSS5_PREBLOCK_WIDTH=str(size),DLSS5_PREBLOCK_HEIGHT=str(size),DLSS5_PREBLOCK_SEED='0',DLSS5_PREBLOCK_PARAMETER_FILE=str(Path('release/live-preblock-v2/preblock-live-0.bin').resolve()))
subprocess.run(['/tmp/preblock-branch-oracle','/tmp/dlssnr-cubins/dlssnr-00.cubin','/tmp/block0.weights',str(folder/'input-hwc.rgba32f'),str(folder/'block0-main.fp8'),str(folder/'block0-ds.fp8'),'0','0'],env=env,check=True,capture_output=True)
source=folder/'block0-ds.fp8';width=height=size//2
sequence=[(i,32,s,i==4) for i,s in [(1,0),(2,3),(3,1),(4,2)]]
sequence += [(i,64,s,i==8) for i,s in [(5,0),(6,3),(7,1),(8,2)]]
sequence += [(i,128,s,i==14) for i,s in [(9,0),(10,3),(11,1),(12,2),(13,0),(14,3)]]
sequence += [(i,256,s,i==22) for i,s in [(15,0),(16,3),(17,1),(18,2),(19,0),(20,3),(21,1),(22,2)]]
for i,C,shift,ds in sequence:
 first=i in (1,5,9,15);heads=C//32
 mode=(6 if ds else 5 if first else 4) if C==32 else (8 if ds else 7)
 suffix='ds' if ds else 'inpview' if first else ''
 symbol=f'cc_tinlayout_fused_swin_{heads}h_{C}_{heads}'+('_'+suffix if suffix else '')+'_fp8'
 cubin=f'/tmp/dlssnr-cubins/dlssnr-{heads.bit_length()-1:02d}.cubin'
 main=folder/f'block{i}-main.fp8';down=folder/f'block{i}-ds.fp8'
 subprocess.run(['/tmp/native-c32-global-oracle',cubin,f'release/native-c{C}/block{i}.weights',str(source),str(main),str(down),symbol,str(width),str(height),str((width+(4 if shift&1 else 0)+7)//8),str((height+(4 if shift&2 else 0)+7)//8),str(heads),str(mode),str(shift)],env=env,check=True,capture_output=True)
 raw=np.fromfile(main,np.uint8);count=width*height*C
 assert not np.any(raw[count:]) and not np.any((raw[:count]&127)==127)
 source=down if ds else main
 if ds:
  width//=2;height//=2
  raw=np.fromfile(down,np.uint8);count=width*height*C*2
  assert not np.any(raw[count:]) and not np.any((raw[:count]&127)==127)
 print(json.dumps({'block':i,'next_extent':[width,height],'next_file':source.name}),flush=True)
assert (width,height)==(size//32,size//32)
output=folder/'block23-main.fp8'
subprocess.run(['/tmp/native-split-global-oracle',str(source),str(output),*[f'release/native-c512/block23-{i}.weights' for i in range(4)],str(width),str(height),'0','native-inpview'],env=env,check=True,capture_output=True)
count=width*height*512;raw=np.fromfile(output,np.uint8);assert not np.any(raw[count:]) and not np.any((raw[:count]&127)==127) and np.any(raw[:count])
if args.through_pool:
 source=output
 for block,shift in [(24,3),(25,1),(26,2),(27,0),(28,3),(29,1),(30,2)]:
  output=folder/f'block{block}-main.fp8'
  subprocess.run(['/tmp/native-split-global-oracle',str(source),str(output),*[f'release/native-c512/block{block}-{i}.weights' for i in range(4)],str(width),str(height),str(shift),'native-plain'],env=env,check=True,capture_output=True)
  raw=np.fromfile(output,np.uint8);assert not np.any(raw[count:]) and not np.any((raw[:count]&127)==127) and np.any(raw[:count])
  source=output
  print(json.dumps({'block':block,'extent':[width,height,512],'original_output':'finite_nonzero'}),flush=True)
 subprocess.run(['/tmp/native-split-pool-oracle',str(output)+'.attn',str(output)+'.ffn','release/native-c512/block30-3.weights',str(folder/'block30-pool-main.fp8'),str(folder/'block30-pool.fp8'),str(width),str(height)],check=True,capture_output=True)
 assert (folder/'block30-pool-main.fp8').read_bytes()==output.read_bytes()
 pooled=np.fromfile(folder/'block30-pool.fp8',np.uint8);count//=4
 assert not np.any(pooled[count:]) and not np.any((pooled[:count]&127)==127) and np.any(pooled[:count])
print(json.dumps({'RGB_extent':[size,size],'original_chain':'block0..30 pool' if args.through_pool else 'block0..23','split_extent':[width,height,512],'status':'generated_nonzero_finite_oracle','AMD_comparison':'pending'}))
