"""Native ViT linear stages with measured packing and ordered half reductions."""
import numpy as np
from native_split_reference import bits
from native_c64_reference import multiply
from native_c32_reference import H,F
from decode_tinlayout_global import e4m3fn
from unpack_vit_matrices import axis_permutation,MATRIX_OUTPUT_TO_RAW

def matrix(raw,inputs,outputs):
 assert (inputs,outputs) in [(1024,4096),(4096,1024),(1024,1024)] and raw.size==inputs*outputs
 ib,ob=inputs.bit_length()-1,outputs.bit_length()-1
 result=np.empty((outputs,inputs),np.float32)
 result[bits(raw.size,[6,3,9,7,8]+list(range(10,ob+5))),bits(raw.size,[0,1,2,4,5]+list(range(ob+5,ib+ob)))]=e4m3fn(raw)
 return result

def unpack_expand(path):
 raw=np.fromfile(path,np.uint8);assert raw.size==4194320 and not np.any(raw[4194304:])
 return matrix(raw[:4194304],1024,4096)

def unpack_residual(path,inputs):
 raw=np.fromfile(path,np.uint8);count=inputs*1024;assert raw.size==count+2048
 skip=raw[count:].view('<f2').astype(np.float32)[axis_permutation(1024,MATRIX_OUTPUT_TO_RAW)]
 return matrix(raw[:count],inputs,1024),skip

def expand(x,weight):
 ex=multiply(x,weight);gate=np.clip(ex,-4,4)
 poly=H(gate*H(abs(gate)*np.float32(-.055908203125)+np.float32(.447265625))+np.float32(.89453125))
 return F(H(ex*poly))

def residual_projection(x,residual,weight,skip):
 assert x.ndim==2 and x.shape[1] in (1024,4096) and residual.shape==(x.shape[0],1024)
 size=x.shape[1]//4;parts=[]
 for part in range(4):
  initial=H(residual*skip) if part==0 else np.zeros_like(residual)
  parts.append(multiply(x[:,part*size:(part+1)*size],weight[:,part*size:(part+1)*size],initial))
 result=parts[0]
 for part in parts[1:]:result=H(result+part)
 return F(result)
