"""Separate C256 plain-kernel arithmetic from half-window boundary behavior."""
from pathlib import Path
import os,json,subprocess
import numpy as np
from native_c64_reference import unpack,block
from decode_tinlayout_global import e4m3fn
root=Path('release/native-c256');inverse=np.argsort(np.load(root/'view/mapping.npz')['cell_output_to_hwc'])
def decode(path):
 raw=np.fromfile(path,np.uint8)[:8192]
 return e4m3fn(raw.reshape(2,4096)[:,inverse]).reshape(1,2,4,4,256).transpose(0,2,1,3,4).reshape(4,8,256)
source=root/'block15-output.fp8';x=decode(source);original=np.fromfile(root/'block16.weights',np.uint8)
source_padded=root/'plain-padded-input.fp8';np.pad(np.fromfile(source,np.uint8)[:8192],(0,8192)).tofile(source_padded)
env={k:v for k,v in os.environ.items() if not k.startswith('DLSS5_NATIVE_SCAN_')}
for shift in (0,3):
 px=py=4 if shift else 0;hh=(4+py+7)//8*8;ww=(8+px+7)//8*8
 padded=np.pad(x,((py,hh-4-py),(px,ww-8-px),(0,0)))
 tiles=padded.reshape(hh//8,8,ww//8,8,256).transpose(0,2,1,3,4).reshape(-1,64,256)
 for mode in ('full','ffn_only','attention_only','uniform','bias_only','qk_only'):
  w=original.copy()
  if mode=='ffn_only':w[0x58220:0x88220]=0;w[0x98240:0xa8240]=0;w.view('<f2')[0xa8240//2:0xa8440//2]=1
  if mode in ('attention_only','uniform','bias_only','qk_only'):w[:0x58000]=0;w.view('<f2')[0x58010//2:0x58210//2]=1
  if mode in ('uniform','bias_only'):
   offsets=np.load(root/'attention-layout/matrix-layout.npz')['v_offsets']
   w[offsets-0x800]=0;w[offsets-0x400]=0
  if mode in ('uniform','qk_only'):w[0x88220:0x98220]=0
  path=root/'plain-diagnostic.weights';w.tofile(path);out=root/'plain-diagnostic-output.fp8'
  subprocess.run(['/tmp/native-c32-global-oracle','/tmp/dlssnr-cubins/dlssnr-03.cubin',str(path),str(source),str(out),str(root/'plain-diagnostic-aux.fp8'),'cc_tinlayout_fused_swin_8h_256_8_fp8','8','4',str(ww//8),str(hh//8),'8','7',str(shift)],env=env,check=True,capture_output=True)
  target=decode(out)
  result=block(tiles,*unpack(path)).reshape(hh//8,ww//8,8,8,256).transpose(0,2,1,3,4).reshape(hh,ww,256)[py:py+4,px:px+8]
  err=np.abs(result-target)
  print(json.dumps({'shift':shift,'mode':mode,'exact_fraction':float(np.mean(result==target)),'mae':float(err.mean()),'max_error':float(err.max())}),flush=True)
  if mode in ('full','uniform'):
   out8=root/'plain-padded-output.fp8'
   subprocess.run(['/tmp/native-c32-global-oracle','/tmp/dlssnr-cubins/dlssnr-03.cubin',str(path),str(source_padded),str(out8),str(root/'plain-diagnostic-aux.fp8'),'cc_tinlayout_fused_swin_8h_256_8_fp8','8','8',str(ww//8),str((8+py+7)//8),'8','7',str(shift)],env=env,check=True,capture_output=True)
   padded_target=decode(out8)
   print(json.dumps({'shift':shift,'mode':mode,'H4_vs_H8_crop_exact':float(np.mean(target==padded_target)),'CPU_vs_H8_crop_exact':float(np.mean(result==padded_target))}),flush=True)
