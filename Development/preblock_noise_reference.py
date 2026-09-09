"""Candidate preblock random fields translated from original sm_120 instructions.
Not a replacement for the live parameter contract; validate seeds and coordinates.
"""
import numpy as np

def fields(width,height,seed,native_steps=False):
 y,x=np.indices((height,width),dtype=np.uint64)
 mask=np.uint64(0xffffffff)
 def mix(v):
  v=v&mask
  v=((v^(v>>((v>>np.uint64(28))+np.uint64(4))))*np.uint64(0x108ef2d9))&mask
  return v^(v>>np.uint64(22))
 h=mix(((x*0x8da6b343)&mask)^((y*0xd8163841)&mask)^np.uint64((seed*0x9e3779b9)&0xffffffff)^np.uint64(0x243f6a88))
 u=[]
 for mul,add in [(0xcaa5b80d,0x21dd796b),(0x2c9277b5,0xac564b05),(0x83232c31,0x3463e0ac),(0xfa6dc5f9,0x4712a88e)]:
  v=(h*np.uint64(mul)+np.uint64(add))&mask
  v=((v^(v>>((v>>np.uint64(28))+np.uint64(4))))*np.uint64(0x108ef2d9))&mask
  v=(v>>np.uint64(30))^(v>>np.uint64(8))
  u.append((v+1).astype(np.float32)*np.float32(2**-24))
 if native_steps:
  # Original SASS 0x960..0xae0: LG2, multiply ln(2), SQRT;
  # angle multiply 2*pi RN, then multiply 1/(2*pi) RZ before MUFU.
  # Mathematical sin/cos below are still candidates, not a MUFU emulation.
  radius0=np.sqrt(np.float32(-2)*np.float32(np.log2(u[0])*np.float32(.6931471824645996)))
  radius1=np.sqrt(np.float32(-2)*np.float32(np.log2(u[1])*np.float32(.6931471824645996)))
  def angle(v):
   product=np.float32(v*np.float32(6.283185482025146)).astype(np.float64)*np.float64(np.float32(.15915493667125702))
   rounded=product.astype(np.float32)
   turns=np.where(rounded.astype(np.float64)>product,np.nextafter(rounded,np.float32(0)),rounded)
   return turns.astype(np.float64)*(2*np.pi)
  t0,t1=angle(u[2]),angle(u[3])
  return np.stack([radius0*np.cos(t0).astype(np.float32),radius1*np.cos(t1).astype(np.float32),radius1*np.sin(t1).astype(np.float32)],-1)
 radius0=np.sqrt(-2*np.log(u[0]));radius1=np.sqrt(-2*np.log(u[1]))
 return np.stack([radius0*np.cos(np.float32(2*np.pi)*u[2]),radius1*np.cos(np.float32(2*np.pi)*u[3]),radius1*np.sin(np.float32(2*np.pi)*u[3])],-1)

if __name__=='__main__':
 import argparse,json
 from decode_tinlayout_global import e4m3fn
 from encode_tinlayout_global import quantize
 p=argparse.ArgumentParser();p.add_argument('scan');p.add_argument('skip_matrix');p.add_argument('--seed',type=lambda s:int(s,0),default=0x3f800000);a=p.parse_args()
 weight=np.fromfile(a.skip_matrix,'<f4').reshape(2048,2048)
 ix=np.argmax(np.abs(weight),axis=0).reshape(8,8,32)
 raw=np.fromfile(a.scan,np.uint8).reshape(512,2048)
 expected=e4m3fn(raw[:,ix])[[8,0,1],:,:,0].transpose(1,2,0)
 candidate=e4m3fn(quantize(fields(8,8,a.seed).astype(np.float16).astype(np.float32)))
 print(json.dumps({'correlation':float(np.corrcoef(candidate.ravel(),expected.ravel())[0,1]),'exact_fraction':float(np.mean(candidate==expected)),'max_error':float(np.abs(candidate-expected).max())},indent=2))
 assert np.array_equal(candidate,expected), 'Random fields do not match original quantized readout'
