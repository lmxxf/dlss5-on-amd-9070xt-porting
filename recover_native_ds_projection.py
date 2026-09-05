"""Recover learned DS projection input/output coordinates from byte probes."""
from pathlib import Path
import argparse,os,subprocess,json
import numpy as np
from native_c64_reference import contract
parser=argparse.ArgumentParser();parser.add_argument('--channels',type=int,choices=(64,128),default=128);args=parser.parse_args()
C=args.channels;OC=2*C;heads=C//32;N=OC*C
size,begin=(69936,0xf130) if C==64 else (229936,0x30230)
root=Path(f'release/native-c{C}');folder=root/'ds-layout';folder.mkdir(parents=True,exist_ok=True)
cell=np.load(root/'view/mapping.npz')['cell_output_to_hwc'];fs,scale,p,b,ats=contract(C)
w=np.zeros(size,np.uint8);w.view('<f2')[fs//2:fs//2+C]=1;w.view('<f2')[ats//2:ats//2+C]=1;w.view('<f4')[scale//4:scale//4+heads]=1
w.tofile(folder/'identity.weights')
env={k:v for k,v in os.environ.items() if not k.startswith('DLSS5_NATIVE_SCAN_')};env.update(DLSS5_NATIVE_SCAN_OFFSET=str(begin),DLSS5_NATIVE_SCAN_COUNT=str(N))
cubin='/tmp/dlssnr-cubins/dlssnr-01.cubin' if C==64 else '/tmp/dlssnr-cubins/dlssnr-02.cubin'
input_index=np.zeros(N,np.int32);output_index=None
for bit in [-1,*range(C.bit_length()-1)]:
 chosen=np.ones(C,bool) if bit<0 else (np.arange(C)&(1<<bit))!=0
 logical=np.broadcast_to(np.where(chosen,0x38,0).astype(np.uint8),(4,4,C))
 np.tile(logical.ravel()[cell],4).tofile(folder/'input.fp8')
 subprocess.run(['/tmp/native-c32-global-oracle',cubin,str(folder/'identity.weights'),str(folder/'input.fp8'),str(folder/'scan-ds.fp8'),str(folder/'unused.fp8'),f'cc_tinlayout_fused_swin_{heads}h_{C}_{heads}_ds_fp8','8','8','1','1',str(heads),'8','0'],env=env,check=True,capture_output=True)
 raw=np.fromfile(folder/'scan-ds.fp8',np.uint8).reshape(N,OC//16,4,4,16).transpose(0,2,3,1,4).reshape(N,16,OC)
 assert np.all((raw==0)|(raw==0x38))
 present=np.any(raw!=0,axis=1);active=np.any(present,axis=1)
 if bit<0:
  assert np.all(present.sum(1)==1) and np.all(np.count_nonzero(raw,axis=(1,2))==16)
  physical=np.argmax(present,axis=1)
  # HWC channel convention used by the next stage's independently probed view.
  output_index=(physical&~3)|((physical&1)<<1)|((physical&2)>>1)
 else:input_index|=active.astype(np.int32)<<bit
 print('DS probe',bit,'active',int(active.sum()),flush=True)
assert np.unique(output_index*C+input_index).size==N
np.savez(folder/'layout.npz',input=input_index,output=output_index)
print(json.dumps({'input_channels':C,'output_channels':OC,'connections':N,'bijection':True,'coefficients':'not fitted'}))
