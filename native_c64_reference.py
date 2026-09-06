"""Original-byte C64/C128 references; no proxy weights or fitted corrections."""
from pathlib import Path
import json,os,subprocess
import numpy as np
from native_c32_reference import H,F
from native_c32_normalize import normalize
from native_c32_softmax_sum import denominator
from decode_tinlayout_global import e4m3fn

def multiply(a,m,initial=None):
 result=np.zeros((*a.shape[:-1],m.shape[0]),np.float32) if initial is None else initial.copy()
 for offset in range(0,m.shape[1],32):result=H(result+a[...,offset:offset+32]@m[:,offset:offset+32].T)
 return result

def contract(C):
 return {64:(0x7010,0xe0a0,0xe0b0,0xa0a0,0xf0b0),128:(0x18010,0x2c120,0x2c130,0x24120,0x30130),256:(0x58010,0x98220,0x98240,0x88220,0xa8240)}[C]

def unpack(path):
 raw=np.fromfile(path,np.uint8);assert raw.size in (61760,69936,197184,229936,689232,820288)
 C=256 if raw.size in (689232,820288) else 128 if raw.size in (197184,229936) else 64;heads=C//32;hidden=4*C;b2=hidden*C;b3=b2+C*128;end3=b3+C*C
 fs,scale,p_begin,bias_begin,ats=contract(C);root=Path(f'release/native-c{C}')
 fl=np.load(root/'ffn-layout/layout.npz');al=np.load(root/'attention-layout/matrix-layout.npz');bl=np.load(root/'attention-layout/bias-layout.npz')
 ffn={}
 for name,begin,end,shape,ro,co in [('W1',0,b2,(hidden,C),'w1_hidden','w1_input'),('W2',b2,b3,(C,hidden),'w2_output','w2_hidden'),('W3',b3,end3,(C,C),'w3_output','w3_input')]:
  matrix=np.zeros(shape,np.float32);matrix[fl[ro],fl[co]]=e4m3fn(raw[begin:end]);ffn[name]=matrix
 slots=np.arange(C);order=(slots//16)*16+(slots%8)*2+(slots%16//8)
 ffn['skip']=np.empty(C,np.float32);ffn['skip'][order]=raw.view('<f2')[fs//2:fs//2+C]
 qkv=[]
 for delta in (-0x800,-0x400,0):
  matrix=np.empty((C,C),np.float32);matrix[al['v_output'],al['v_input']]=e4m3fn(raw[al['v_offsets']+delta]);qkv.append(matrix)
 projection=np.empty((C,C),np.float32);projection[al['p_output'],al['p_input']]=e4m3fn(raw[p_begin:p_begin+C*C])
 bias=np.empty((heads,64,64),np.float32);bias[bl['head'],bl['query'],bl['key']]=raw.view('<f2')[bias_begin//2:scale//2]
 skip=np.empty(C,np.float32);skip[np.load(root/'attention-layout/skip-channels.npy')]=raw.view('<f2')[ats//2:ats//2+C]
 return ffn,qkv,projection,bias,raw[scale:scale+heads*4].view('<f4'),skip

def block(x,ffn,qkv,projection,bias,scales,skip,raw_output=False):
 expanded=multiply(F(x),ffn['W1']);gate=np.clip(expanded,-4,4)
 poly=H(gate*H(np.abs(gate)*np.float32(-.055908203125)+np.float32(.447265625))+np.float32(.89453125))
 hidden=F(H(expanded*poly));middle=F(multiply(hidden,ffn['W2']))
 # Unlike C32, C64 stores this boundary as FP8 in shared memory; the
 # attention residual must also consume that quantized feature.
 feature=F(multiply(middle,ffn['W3'],H(x*ffn['skip'])))
 q,k,v=[multiply(F(feature),m) for m in qkv]
 combined=[]
 for head in range(x.shape[-1]//32):
  sl=slice(head*32,(head+1)*32)
  qh=F(H(normalize(q[...,sl])*H(scales[head])));kh=F(normalize(k[...,sl]));vh=F(v[...,sl])
  scores=H(qh@kh.transpose(0,2,1)+bias[head])
  affine=np.clip(H(scores*np.float32(.044921875)+np.float32(1.30078125)),1.03125,1.5693359375)
  bits=affine.astype(np.float16).view(np.uint16).astype(np.uint32)
  exp=(((bits<<5)+0x8000)&65535).astype(np.uint16).view(np.float16).astype(np.float32)
  prob=F(H(exp*H(1/denominator(exp))))
  av=H(H(prob[:,:,:32]@vh[:,:32])+prob[:,:,32:]@vh[:,32:])
  combined.append(F(av))
 result=multiply(np.concatenate(combined,axis=-1),projection,H(feature*skip))
 return result if raw_output else F(result)

if __name__=='__main__':
 import argparse
 parser=argparse.ArgumentParser();parser.add_argument('--channels',type=int,choices=(64,128,256),default=64);parser.add_argument('--random',action='store_true');args=parser.parse_args()
 C=args.channels;heads=C//32;index,width,height={64:(5,32,16),128:(9,16,8),256:(15,8,4)}[C];count=width*height*C
 ww,hh=(width+7)//8*8,(height+7)//8*8
 root=Path(f'release/native-c{C}');folder=root/'attention-layout'
 original=np.fromfile(root/f'block{index}.weights',np.uint8);fs,scale,p_begin,bias_begin,ats=contract(C)
 inverse=np.argsort(np.load(root/'view/mapping.npz')['cell_output_to_hwc'])
 weights=np.zeros_like(original);weights.view('<f2')[fs//2:fs//2+C]=1
 weights.view('<f4')[scale//4:scale//4+heads]=1;weights.tofile(folder/'skip-control.weights')
 np.full(64*C,0x38,np.uint8).tofile(folder/'skip-input.fp8')
 env={k:v for k,v in os.environ.items() if not k.startswith('DLSS5_NATIVE_SCAN_')}
 env.update(DLSS5_NATIVE_SCAN_OFFSET=str(ats),DLSS5_NATIVE_SCAN_COUNT=str(2*C))
 cubin=f'/tmp/dlssnr-cubins/dlssnr-{heads.bit_length()-1:02d}.cubin'
 subprocess.run(['/tmp/native-c32-global-oracle',cubin,str(folder/'skip-control.weights'),str(folder/'skip-input.fp8'),str(folder/'skip-output.fp8'),str(folder/'unused.fp8'),f'cc_tinlayout_fused_swin_{heads}h_{C}_{heads}_inpview_fp8','8','8','1','1',str(heads),'7','0'],env=env,check=True,capture_output=True)
 probe=np.fromfile(folder/'skip-output.fp8',np.uint8).reshape(2*C,4,16*C)[1::2,:,inverse].reshape(C,64,C)
 present=np.any(probe!=0,axis=1);assert np.all(present.sum(1)==1)
 skip_channels=np.argmax(present,axis=1);assert np.unique(skip_channels).size==C
 np.save(folder/'skip-channels.npy',skip_channels)
 parameters=unpack(root/f'block{index}.weights');ffn,qkv,projection,bias,scales,skip=parameters
 source={64:'release/native-c32/block4-aux.fp8',128:'release/native-c64/block8-aux.fp8',256:'release/native-c128/block14-aux.fp8'}[C]
 raw=np.fromfile(source,np.uint8)[:count]
 x=e4m3fn(raw).reshape(C//16,height,width,16).transpose(1,2,0,3).reshape(height,width,C)
 c=np.arange(C);perm=(c&~3)|((c&1)<<1)|((c&2)>>1);x=x[...,perm]
 tiles=np.pad(x,((0,hh-height),(0,ww-width),(0,0))).reshape(hh//8,8,ww//8,8,C).transpose(0,2,1,3,4).reshape(-1,64,C)
 y=block(tiles,*parameters).reshape(hh//8,ww//8,8,8,C).transpose(0,2,1,3,4).reshape(hh,ww,C)[:height,:width]
 target=e4m3fn(np.fromfile(root/f'block{index}-output.fp8',np.uint8)[:count].reshape(-1,16*C)[:,inverse]).reshape(height//4,width//4,4,4,C).transpose(0,2,1,3,4).reshape(x.shape)
 err=np.abs(y-target)
 print(json.dumps({'channels':C,'exact_fraction':float(np.mean(y==target)),'mae':float(err.mean()),'max_error':float(err.max()),'correlation':float(np.corrcoef(y.ravel(),target.ravel())[0,1])},indent=2))
 np.savez(folder/'full-matrices.npz',Q=qkv[0],K=qkv[1],V=qkv[2],P=projection,bias=bias,scales=scales,skip=skip)
 assert np.isfinite(y).all() and np.isfinite(target).all()
 assert np.array_equal(y,target), f'complete C{C} arithmetic is not yet exact'
 if args.random:
  from encode_tinlayout_global import quantize
  environment={k:v for k,v in os.environ.items() if not k.startswith('DLSS5_NATIVE_SCAN_')}
  for seed,amplitude in [(113,.25),(127,3.)]:
   x=F(np.random.default_rng(seed).normal(0,amplitude,(height,width,C)).astype(np.float32))
   quantize(x[...,perm]).reshape(height,width,C//16,16).transpose(2,0,1,3).copy().tofile(folder/'full-random-input.fp8')
   subprocess.run(['/tmp/native-c32-global-oracle',cubin,str(root/f'block{index}.weights'),str(folder/'full-random-input.fp8'),str(folder/'full-random-output.fp8'),str(folder/'full-random-aux.fp8'),f'cc_tinlayout_fused_swin_{heads}h_{C}_{heads}_inpview_fp8',str(width),str(height),str(ww//8),str(hh//8),str(heads),'7','0'],env=environment,check=True,capture_output=True)
   tiles=np.pad(x,((0,hh-height),(0,ww-width),(0,0))).reshape(hh//8,8,ww//8,8,C).transpose(0,2,1,3,4).reshape(-1,64,C)
   result=block(tiles,*parameters).reshape(hh//8,ww//8,8,8,C).transpose(0,2,1,3,4).reshape(hh,ww,C)[:height,:width]
   target=e4m3fn(np.fromfile(folder/'full-random-output.fp8',np.uint8)[:count].reshape(-1,16*C)[:,inverse]).reshape(height//4,width//4,4,4,C).transpose(0,2,1,3,4).reshape(x.shape)
   print(json.dumps({'channels':C,'seed':seed,'amplitude':amplitude,'exact_fraction':float(np.mean(result==target)),'max_error':float(np.abs(result-target).max())}),flush=True)
   assert np.array_equal(result,target), 'random full multihead block regression'
