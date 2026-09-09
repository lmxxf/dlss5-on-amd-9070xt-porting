"""Controlled attention tests distinguish zero padding from invalid-key masking."""
import argparse,subprocess,json
from pathlib import Path
import numpy as np
from decode_tinlayout_global import e4m3fn
p=argparse.ArgumentParser();p.add_argument('folder',type=Path);a=p.parse_args();a.folder.mkdir(parents=True,exist_ok=True)
width,height=64,32
sm=np.fromfile('release/post-skip-basis/matrix.f32','<f4').reshape(2048,2048);cm=np.argmax(np.abs(sm),axis=0).reshape(8,8,32)[:4,:4]
source=np.zeros((height//4,width//4,512),np.uint8);logical=np.zeros((4,4,32),np.uint8);logical[:,:,0]=0x38;source[:,:,cm]=logical;source.tofile(a.folder/'constant-cells.fp8')
l=np.load('release/preblock-attention-layout/matrix-layout.npz');w=np.zeros(10336,'<f2');w[4104:4136]=1;w.view('<f4')[9776//2]=1
vslot=np.flatnonzero((l['v_output']==0)&(l['v_input']==0))[0];pslot=np.flatnonzero((l['projection_output']==0)&(l['projection_input']==0))[0]
w.view(np.uint8)[10336+vslot]=0x38;w.view(np.uint8)[19568+pslot]=0x38;w.tofile(a.folder/'control.weights')
reports=[]
for mask in [0,1,2,3]:
 out=a.folder/f'mask{mask}.fp8';aux=a.folder/f'mask{mask}-aux.fp8'
 subprocess.run(['/tmp/native-c32-global-oracle','/tmp/dlssnr-cubins/dlssnr-00.cubin',str(a.folder/'control.weights'),str(a.folder/'constant-cells.fp8'),str(out),str(aux),'cc_tinlayout_fused_swin_1h_32_1_fp8',str(width),str(height),str(width//8+bool(mask&1)),str(height//8+bool(mask&2)),'1','4',str(mask)],check=True,capture_output=True)
 raw=np.fromfile(out,np.uint8)[:width*height*32];y=e4m3fn(raw.reshape(-1,512)[:,cm]).reshape(height//4,width//4,4,4,32).transpose(0,2,1,3,4).reshape(height,width,32)
 assert np.all(y[:,:,1:]==0)
 reports.append({'shift_mask':mask,'channel0_values':np.unique(y[:,:,0]).tolist(),'corner':float(y[0,0,0]),'center':float(y[16,32,0])})
print(json.dumps(reports,indent=2))
