#!/usr/bin/env python3
"""CPU-only verification of previously captured pair-probe outputs."""
from pathlib import Path
import argparse,hashlib,json
p=argparse.ArgumentParser();p.add_argument('output_directory',type=Path);a=p.parse_args();root=a.output_directory
receipt=json.loads((root/'receipt.json').read_text(encoding='utf-8-sig'));assert receipt['pass'];checks=[];hashes=[]
assert receipt['expanded_weight_bytes']==12583040 and receipt['packed_weight_bytes']==3145856
assert receipt['source_weight_bytes']==(6291520 if receipt['resolved_weight_path'].endswith('.f16') else 12583040)
def read(path):
 b=path.read_bytes();hashes.append({'path':str(path.relative_to(root)),'bytes':len(b),'sha256':hashlib.sha256(b).hexdigest()});return b
def payload(path,size):
 b=read(path);assert len(b)==size+512;assert b[:256]==bytes([165])*256 and b[-256:]==bytes([165])*256;return b[256:-256]
for n in [400,640]:
 d=root/str(n);qsize=n*3072;vbase=2*n*1024;old=payload(d/'qkv-old-guard.bin',qsize);new=payload(d/'qkv-new-guard.bin',qsize);assert old[:vbase]==new[:vbase]
 for t in range(n):
  for c in range(1024):
   ni=vbase+((c//32)*(n//16)+t//16)*512+((t%16)//8*16+c%16)*16+(c%32)//16*8+t%8
   assert old[vbase+t*1024+c]==new[ni],(n,t,c)
 assert read(d/'qkv-new-inverse.bin')==old
 avold=payload(d/'av-old-guard.bin',n*1024);avnew=payload(d/'av-new-guard.bin',n*1024);assert avold==avnew
 assert not any((x&127)==127 for x in old+new+avold+avnew)
 case=next(x for x in receipt['cases'] if x['tokens']==n);assert case['reuse_gate_sentinels_unchanged'] and case['input_unchanged'] and case['packed_weight_unchanged']
 checks.append({'tokens':n,'qk_exact':True,'inverse_v_exact':True,'av_exact':True,'all_guards_intact':True,'reuse_evidence':'GPU probe full-buffer equality recorded in receipt; reuse raw not separately exported'})
(root/'cpu-verified.json').write_text(json.dumps({'pass':True,'checks':checks,'files':hashes},indent=2)+'\n');print('PAIR_CPU_READBACK_PASS 400 640')
