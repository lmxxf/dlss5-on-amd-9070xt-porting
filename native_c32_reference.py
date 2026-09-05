"""Reference for native FP8-weight C32 blocks; ordinary-block layout transfer test."""
from pathlib import Path
import argparse,json
import numpy as np
from decode_tinlayout_global import e4m3fn
from encode_tinlayout_global import quantize
H=lambda x:np.asarray(x,np.float16).astype(np.float32)
F=lambda x:e4m3fn(quantize(x))

def unpack(path):
 raw=np.fromfile(path,np.uint8);assert raw.size==20672
 ffn=np.load('release/preblock-ffn-byte-layout/layout.npz')
 att=np.load('release/preblock-attention-layout/matrix-layout.npz')
 bm=np.load('release/preblock-attention-layout/bias-layout.npz')
 w1=np.empty((128,32),np.float32);w2=np.empty((32,128),np.float32)
 w1[ffn['w1_hidden'],ffn['w1_input']]=e4m3fn(raw[:4096]);w2[ffn['w2_output'],ffn['w2_hidden']]=e4m3fn(raw[4096:8192])
 matrices=[]
 for offset,kind in [(8288,'v'),(9312,'v'),(10336,'v'),(19568,'projection')]:
  v=np.empty((32,32),np.float32);v[att[kind+'_output'],att[kind+'_input']]=e4m3fn(raw[offset:offset+1024]);matrices.append(v)
 half=raw.view('<f2').astype(np.float32);bias=np.empty((64,64),np.float32);bias[bm['query'],bm['key']]=half[5680:9776]
 skip_map=np.array([0,1,4,5,8,9,12,13,2,3,6,7,10,11,14,15,16,17,20,21,24,25,28,29,18,19,22,23,26,27,30,31])
 fs=np.empty(32,np.float32);fs[skip_map]=half[4104:4136]
 ats=np.empty(32,np.float32);ats[att['skip_channel']]=half[10296:10328]
 scale=np.frombuffer(raw.tobytes(),'<f4',1,19552)[0]
 return w1,w2,*matrices,bias,scale,fs,ats

def block(tiles,w,skip_first=True):
 w1,w2,qw,kw,vw,pw,bias,scale,fs,ats=w
 expanded=H(F(tiles)@w1.T);gate=np.clip(expanded,-4,4)
 poly=H(gate*H(np.abs(gate)*np.float32(-.055908203125)+np.float32(.447265625))+np.float32(.89453125))
 hidden=F(H(expanded*poly));proj=H(tiles*fs) if skip_first else np.zeros_like(tiles)
 for k in range(0,128,32):proj=H(proj+hidden[...,k:k+32]@w2[:,k:k+32].T)
 feature=proj if skip_first else H(proj+tiles*fs)
 q,k,v=[H(F(feature)@m.T) for m in [qw,kw,vw]]
 def norm(a):
  s=H(a*a)
  while s.shape[-1]>1:s=H(s[...,::2]+s[...,1::2])
  return H(a*H(1/np.sqrt(np.maximum(s,6.198883056640625e-5))))
 q=F(H(norm(q)*H(scale)));k=F(norm(k));v=F(v)
 scores=H(q@k.transpose(0,2,1)+bias)
 bits=np.clip(H(scores*np.float32(.044921875)+np.float32(1.30078125)),1.03125,1.5693359375).astype(np.float16).view(np.uint16).astype(np.uint32)
 exp=(((bits<<5)+0x8000)&65535).astype(np.uint16).view(np.float16).astype(np.float32)
 den=exp
 while den.shape[-1]>1:den=H(den[...,::2]+den[...,1::2])
 prob=F(H(exp*H(1/den)));av=np.zeros_like(tiles)
 for k in [0,32]:av=H(av+prob[:,:,k:k+32]@v[:,k:k+32])
 return F(H(H(F(av)@pw.T)+feature*ats))

if __name__=='__main__':
 p=argparse.ArgumentParser();p.add_argument('weights');p.add_argument('input');p.add_argument('oracle');p.add_argument('width',type=int);p.add_argument('height',type=int);p.add_argument('--output-cells',action='store_true');p.add_argument('--input-cells',action='store_true');p.add_argument('--shift-mask',type=int,default=0);p.add_argument('--export-lab',type=Path);a=p.parse_args()
 count=a.width*a.height*32
 source=e4m3fn(np.fromfile(a.input,np.uint8)[:count]).reshape(2,a.height,a.width,16).transpose(1,2,0,3).reshape(a.height,a.width,32)
 if a.input_cells:
  sm=np.fromfile('release/post-skip-basis/matrix.f32','<f4').reshape(2048,2048);cm=np.argmax(np.abs(sm),axis=0).reshape(8,8,32)[:4,:4]
  source=e4m3fn(np.fromfile(a.input,np.uint8)[:count].reshape(-1,512)[:,cm]).reshape(a.height//4,a.width//4,4,4,32).transpose(0,2,1,3,4).reshape(source.shape)
 target_raw=np.fromfile(a.oracle,np.uint8)[:count]
 if a.output_cells:
  sm=np.fromfile('release/post-skip-basis/matrix.f32','<f4').reshape(2048,2048);cm=np.argmax(np.abs(sm),axis=0).reshape(8,8,32)[:4,:4]
  cells=e4m3fn(target_raw.reshape(-1,512)[:,cm])
  target=cells.reshape(a.height//4,a.width//4,4,4,32).transpose(0,2,1,3,4).reshape(source.shape)
 else:target=e4m3fn(target_raw).reshape(2,a.height,a.width,16).transpose(1,2,0,3).reshape(source.shape)
 px=4 if a.shift_mask&1 else 0;py=4 if a.shift_mask&2 else 0
 padded=np.pad(source,((py,py),(px,px),(0,0)));hh,ww=padded.shape[:2]
 tiles=padded.reshape(hh//8,8,ww//8,8,32).transpose(0,2,1,3,4).reshape(-1,64,32)
 if a.export_lab:
  a.export_lab.mkdir(parents=True,exist_ok=True)
  w1,w2,qw,kw,vw,pw,bias,scale,fs,ats=unpack(a.weights)
  np.concatenate([np.zeros(512),w1.ravel(),w2.ravel(),fs]).astype('<f4').tofile(a.export_lab/'ffn.f32')
  np.concatenate([qw.ravel(),kw.ravel(),vw.ravel(),pw.ravel(),bias.ravel(),[scale],ats]).astype('<f4').tofile(a.export_lab/'attention.f32')
  tiles.astype('<f4').tofile(a.export_lab/'input.f32');target.astype('<f4').tofile(a.export_lab/'oracle.f32')
  (a.export_lab/'geometry.json').write_text(json.dumps({'width':a.width,'height':a.height,'work_width':ww,'work_height':hh,'crop_x':px,'crop_y':py})+'\n')
 result=block(tiles,unpack(a.weights)).reshape(hh//8,ww//8,8,8,32).transpose(0,2,1,3,4).reshape(padded.shape)[py:py+a.height,px:px+a.width]
 err=np.abs(result-target)
 print(json.dumps({'correlation':float(np.corrcoef(result.ravel(),target.ravel())[0,1]),'exact_fraction':float(np.mean(result==target)),'mae':float(err.mean()),'max_error':float(err.max())},indent=2))
