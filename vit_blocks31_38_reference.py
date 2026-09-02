#!/usr/bin/env python3
import argparse,json,sys
from pathlib import Path
import numpy as np
from unpack_vit_matrices import unpack_matrix
from vit_block31_reference import e4,fp8,unswizzle,bit_perm

def encode_e4(values):
    table=e4(np.arange(256,dtype=np.uint8));valid=np.flatnonzero(np.isfinite(table));flat=np.asarray(values,np.float32).ravel();out=np.empty(flat.size,np.uint8)
    for start in range(0,flat.size,4096):
        x=flat[start:start+4096];out[start:start+len(x)]=valid[np.argmin(abs(x[:,None]-table[valid][None,:]),axis=1)]
    return out.reshape(np.shape(values))
def physical(values):
    out=np.zeros(2097152,np.uint8);tbits=[2,6,7,8,14,15];cbits=[0,1,3,4,5,9,10,11,12,13];encoded=encode_e4(values)
    for token in range(64):
        tb=sum(((token>>i)&1)<<bit for i,bit in enumerate(tbits))
        for channel in range(1024):
            offset=tb|sum(((channel>>i)&1)<<bit for i,bit in enumerate(cbits));out[offset]=encoded[token,channel]
    return out
def main():
    p=argparse.ArgumentParser();p.add_argument("arena",type=Path);p.add_argument("index",type=Path);p.add_argument("input",type=Path);p.add_argument("output",type=Path);p.add_argument("--assets",type=Path,default=Path(__file__).parent);p.add_argument("--expand-dir",type=Path);p.add_argument("--physical",type=Path);p.add_argument("--qkv-mode",choices=["mixed","main"],default="main");p.add_argument("--attention-scale",type=float,default=1.0);p.add_argument("--first-block",type=int,default=31);p.add_argument("--last-block",type=int,default=38);a=p.parse_args();arena=a.arena.read_bytes();records=json.loads(a.index.read_text());records=records["records"] if isinstance(records,dict) else records;records={r["name"]:r for r in records};x=unswizzle(a.input.read_bytes()[:65536],[2,6,7,8,14,15],[0,1,3,4,5,9,10,11,12,13]);order=bit_perm((1,8,16,2,4,32,64,128,256,512));expand_dir=a.expand_dir or a.assets
    for block in range(a.first_block,a.last_block+1):
        def payload(layer):
            r=records[f"block{block}.layer{layer}.layer"];return arena[r["arena_offset"]:r["arena_offset"]+r["payload_size"]]
        expand_path=expand_dir/f"block{block}-vit-expand-effective.f16"
        if expand_path.exists():expand=np.fromfile(expand_path,np.float16).astype(np.float32).reshape(1024,4096)
        elif block==31:expand=np.fromfile(a.assets/"block31-vit-expand-effective.f16",np.float16).astype(np.float32).reshape(1024,4096)
        else:expand=unpack_matrix(payload(0)[:1024*4096],1024,4096,"matrix_input","matrix_output").astype(np.float32)
        contract=unpack_matrix(payload(1)[:4096*1024],4096,1024,"matrix_input","matrix_output").astype(np.float32);projection=unpack_matrix(payload(4)[:1024*1024],1024,1024,"matrix_input","matrix_output").astype(np.float32);contract_skip=np.frombuffer(payload(1)[4096*1024:4096*1024+2048],np.float16).astype(np.float32)[order];projection_skip=np.frombuffer(payload(4)[1024*1024:1024*1024+2048],np.float16).astype(np.float32)[order];main_file=a.assets/("block31-qkv-effective.fp8" if block==31 else f"block{block}-qkv-main.fp8");work_file=a.assets/("block31-qkv-work-effective.f16" if block==31 else f"block{block}-qkv-work.f16");qkv=e4(np.fromfile(main_file,np.uint8)).reshape(1024,3,1024);work=np.fromfile(work_file,np.float16).astype(np.float32).reshape(1024,3,1024)
        branch=fp8(x@expand);z=np.clip(branch,-4,4);z=z*(.89453125+z*(.447265625-.055908203125*np.abs(z)));hidden=fp8(z@contract+x*contract_skip);main=np.einsum("ti,igo->tgo",hidden,qkv);scales=[]
        for group in (0,1):
            scale=np.median(np.linalg.norm(qkv[:,group].reshape(1024,32,32),axis=2),axis=0);main[:,group]=(main[:,group].reshape(64,32,32)/(np.linalg.norm(main[:,group].reshape(64,32,32),axis=2,keepdims=True)+1e-9)*scale[None,:,None]).reshape(64,1024);scales.append(scale)
        aux=np.einsum("ti,igo->tgo",hidden,work)
        for group in (0,1):aux[:,group]=(aux[:,group].reshape(64,32,32)/(np.linalg.norm(aux[:,group].reshape(64,32,32),axis=2,keepdims=True)+1e-9)*scales[group][None,:,None]).reshape(64,1024)
        combined=main if a.qkv_mode=="main" else np.concatenate([main[:32],aux[32:]],axis=0);q,k,v=[combined[:,g].reshape(64,32,32) for g in range(3)];scores=np.einsum("thd,shd->hts",q,k)*a.attention_scale;scores-=scores.max(2,keepdims=True);weights=np.exp(scores);weights/=weights.sum(2,keepdims=True);attention=fp8(np.einsum("hts,shd->thd",weights,v).reshape(64,1024));x=fp8(attention@projection+hidden*projection_skip);print(f"block{block} finite={np.isfinite(x).all()} min={x.min():.6g} max={x.max():.6g} std={x.std():.6g}")
    x.astype(np.float32).tofile(a.output)
    if a.physical:physical(x).tofile(a.physical)
if __name__=="__main__":main()
