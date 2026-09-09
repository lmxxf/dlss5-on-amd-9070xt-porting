"""Controlled value/bias probes of the plain C256 H4 attention boundary."""
from pathlib import Path
import json,os,subprocess,argparse
import numpy as np
from decode_tinlayout_global import e4m3fn
root=Path('release/native-c256');folder=root/'half-attention';folder.mkdir(parents=True,exist_ok=True)
parser=argparse.ArgumentParser();parser.add_argument('--height',type=int,choices=(4,12),default=4);args=parser.parse_args();height=args.height
cell=np.load(root/'view/mapping.npz')['cell_output_to_hwc'];inverse=np.argsort(cell)
al=np.load(root/'attention-layout/matrix-layout.npz');bl=np.load(root/'attention-layout/bias-layout.npz')
w=np.zeros(689232,np.uint8);w.view('<f2')[0x58010//2:0x58210//2]=1;w.view('<f4')[0x98220//4:0x98220//4+8]=1
w[al['v_offsets'][(al['v_output']==0)&(al['v_input']==0)]]=0x38
w[0x98240+np.flatnonzero((al['p_output']==0)&(al['p_input']==0))]=0x38
env={k:v for k,v in os.environ.items() if not k.startswith('DLSS5_NATIVE_SCAN_')}
for pattern in ('constant','point8'):
 x=np.zeros((height,8,256),np.uint8)
 if pattern=='constant':x[:,:,0]=0x38
 else:x[height-3,0,0]=0x38
 packed=x.reshape(height//4,4,2,4,256).transpose(0,2,1,3,4).reshape(height//4*2,4096)[:,cell]
 packed.tofile(folder/'input.fp8')
 for key in (-1,8,40):
  weights=w.copy()
  if key>=0:
   slot=np.flatnonzero((bl['head']==0)&(bl['query']==0)&(bl['key']==key));assert len(slot)==1
   weights.view('<f2')[0x88220//2+slot]=8
  weights.tofile(folder/'weights.bin')
  subprocess.run(['/tmp/native-c32-global-oracle','/tmp/dlssnr-cubins/dlssnr-03.cubin',str(folder/'weights.bin'),str(folder/'input.fp8'),str(folder/'output.fp8'),str(folder/'aux.fp8'),'cc_tinlayout_fused_swin_8h_256_8_fp8','8',str(height),'1',str((height+7)//8),'8','7','0'],env=env,check=True,capture_output=True)
  raw=np.fromfile(folder/'output.fp8',np.uint8)[:height*8*256]
  result=e4m3fn(raw.reshape(height//4*2,4096)[:,inverse]).reshape(height//4,2,4,4,256).transpose(0,2,1,3,4).reshape(height,8,256)
  assert not np.any(result[:,:,1:])
  expected={4:{'constant':{-1:1.,8:1.,40:1.},'point8':{-1:.03125,8:.875,40:.875}},12:{'constant':{-1:.5,8:.9375,40:.0625},'point8':{-1:.015625,8:.875,40:.001953125}}}
  assert float(result[height-4,0,0])==expected[height][pattern][key], 'half-window control changed'
  print(json.dumps({'height':height,'pattern':pattern,'biased_key':key,'tail_query0':float(result[height-4,0,0]),'channel0_values':np.unique(result[:,:,0]).tolist()}),flush=True)
