"""Decode original four-record C512 split layers with measured address rules."""
from pathlib import Path
import numpy as np
from native_split_reference import bits,matrix
from decode_tinlayout_global import e4m3fn

def unpack(folder,block):
 raw=[np.fromfile(Path(folder)/f'block{block}-{i}.weights',np.uint8) for i in range(4)]
 assert [len(r) for r in raw]==[524288,263168,917568,263168]
 pre=matrix(raw[0][:262144]);expand=np.empty((8,256,64),np.float32);contract=np.empty((8,64,256),np.float32)
 for g in range(8):
  expand[g,bits(16384,[3,6,7,8,9,10,11,12]),bits(16384,[1,0,4,5,2,13])]=e4m3fn(raw[0][0x40000+g*16384:0x40000+(g+1)*16384])
  contract[g,bits(16384,[3,6,7,8,9,10]),bits(16384,[1,0,4,5,2,11,12,13])]=e4m3fn(raw[0][0x60000+g*16384:0x60000+(g+1)*16384])
 def projection(r):
  c=np.arange(512);order=(c//16)*16+(c%8)*2+(c%16//8)
  skip=np.empty(512,np.float32);skip[order]=r[262144:].view('<f2')
  return {'matrix':matrix(r[:262144]),'skip':skip}
 i=np.arange(262144);v=(i//1024)*3072+2048+i%1024
 qkv=[matrix(raw[2][v+offset]) for offset in (-2048,-1024,0)]
 bias=np.empty((16,64,64),np.float32)
 bias[bits(65536,[12,13,14,15]),bits(65536,[5,6,10,7,1,11]),bits(65536,[0,3,8,4,2,9])]=raw[2][0xc0000:0xe0000].view('<f2')
 return {'pre':pre,'expand':expand,'contract':contract},projection(raw[1]),qkv,bias,raw[2][0xe0000:].view('<f4'),projection(raw[3])
