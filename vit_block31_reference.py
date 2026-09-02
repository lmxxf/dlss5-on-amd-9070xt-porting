#!/usr/bin/env python3
import argparse,json
from pathlib import Path
import numpy as np
from unpack_vit_matrices import unpack_matrix

def e4(a):
    a=np.frombuffer(a,np.uint8) if isinstance(a,(bytes,bytearray,memoryview)) else np.asarray(a,np.uint8);s=np.where(a&128,-1.,1.);e=(a>>3)&15;m=a&7
    return (s*np.where(e==0,m/8*2**-6,(1+m/8)*np.exp2(e.astype(np.int16)-7))).astype(np.float32)
def fp8(x):
    x=np.asarray(x,np.float32);s=np.where(x<0,-1.,1.);a=np.abs(x);sub=s*np.round(a*512)/512;e=np.clip(np.floor(np.log2(np.maximum(a,1e-30))),-6,8);m=np.round((a/np.exp2(e)-1)*8);carry=m>=8;e+=carry;m=np.where(carry,0,m);return np.where(a<.015625,sub,s*np.minimum(np.exp2(e)*(1+m/8),448)).astype(np.float32)
def unswizzle(raw,tbits,cbits):
    y=np.empty((1<<len(tbits),1<<len(cbits)),np.float32)
    for off,value in enumerate(e4(raw)):
        token=sum(((off>>bit)&1)<<i for i,bit in enumerate(tbits));channel=sum(((off>>bit)&1)<<i for i,bit in enumerate(cbits));y[token,channel]=value
    return y
def bit_perm(columns):
    return np.array([sum(target for bit,target in enumerate(columns) if value>>bit&1) for value in range(1024)])
def main():
    p=argparse.ArgumentParser();p.add_argument("arena",type=Path);p.add_argument("index",type=Path);p.add_argument("input",type=Path);p.add_argument("oracle",type=Path);p.add_argument("--assets",type=Path,default=Path(__file__).parent);p.add_argument("--output",type=Path);p.add_argument("--case-prefix",type=Path);a=p.parse_args();arena=a.arena.read_bytes();records=json.loads(a.index.read_text());records=records["records"] if isinstance(records,dict) else records;records={r["name"]:r for r in records}
    def payload(layer):
        r=records[f"block31.layer{layer}.layer"];return arena[r["arena_offset"]:r["arena_offset"]+r["payload_size"]]
    contract=unpack_matrix(payload(1)[:4096*1024],4096,1024,"matrix_input","matrix_output").astype(np.float32);projection=unpack_matrix(payload(4)[:1024*1024],1024,1024,"matrix_input","matrix_output").astype(np.float32);order=bit_perm((1,8,16,2,4,32,64,128,256,512));contract_skip=np.frombuffer(payload(1)[4096*1024:4096*1024+2048],np.float16).astype(np.float32)[order];projection_skip=np.frombuffer(payload(4)[1024*1024:1024*1024+2048],np.float16).astype(np.float32)[order]
    x=unswizzle(a.input.read_bytes()[:65536],[2,6,7,8,14,15],[0,1,3,4,5,9,10,11,12,13]);expand=np.fromfile(a.assets/"block31-vit-expand-effective.f16",np.float16).astype(np.float32).reshape(1024,4096);qkv=e4(np.fromfile(a.assets/"block31-qkv-effective.fp8",np.uint8)).reshape(1024,3,1024);work=np.fromfile(a.assets/"block31-qkv-work-effective.f16",np.float16).astype(np.float32).reshape(1024,3,1024)
    branch=fp8(x@expand);z=np.clip(branch,-4,4);z=z*(.89453125+z*(.447265625-.055908203125*np.abs(z)));hidden=fp8(z@contract+x*contract_skip);main=np.einsum("ti,igo->tgo",hidden,qkv);scales=[]
    for group in (0,1):
        scale=np.median(np.linalg.norm(qkv[:,group].reshape(1024,32,32),axis=2),axis=0);main[:,group]=(main[:,group].reshape(64,32,32)/(np.linalg.norm(main[:,group].reshape(64,32,32),axis=2,keepdims=True)+1e-9)*scale[None,:,None]).reshape(64,1024);scales.append(scale)
    aux=np.einsum("ti,igo->tgo",hidden,work)
    for group in (0,1):aux[:,group]=(aux[:,group].reshape(64,32,32)/(np.linalg.norm(aux[:,group].reshape(64,32,32),axis=2,keepdims=True)+1e-9)*scales[group][None,:,None]).reshape(64,1024)
    combined=main;q,k,v=[combined[:,g].reshape(64,32,32) for g in range(3)];scores=np.einsum("thd,shd->hts",q,k);scores-=scores.max(2,keepdims=True);weights=np.exp(scores);weights/=weights.sum(2,keepdims=True);attention=fp8(np.einsum("hts,shd->thd",weights,v).reshape(64,1024));output=fp8(attention@projection+hidden*projection_skip);oracle=unswizzle(a.oracle.read_bytes()[:65536],[2,6,7,8,14,15],[0,1,3,4,5,9,10,11,12,13]);corr=float(np.corrcoef(output.ravel(),oracle.ravel())[0,1]);mae=float(np.mean(abs(output-oracle)));rmse=float(np.sqrt(np.mean((output-oracle)**2)));print(f"correlation={corr:.9f} MAE={mae:.9f} RMSE={rmse:.9f}")
    if a.output:output.tofile(a.output)
    if a.case_prefix:
        for name,value in {"input":x,"branch":branch,"hidden":hidden,"qkv":combined.reshape(64,3,1024),"attention":attention,"output":output,"oracle":oracle}.items():
            value.astype(np.float32).tofile(str(a.case_prefix)+f"-{name}.f32")
if __name__=="__main__":main()
