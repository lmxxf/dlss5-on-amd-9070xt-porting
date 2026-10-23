#!/usr/bin/env python3
from pathlib import Path
import argparse,hashlib,json,struct
p=argparse.ArgumentParser();p.add_argument('directory',type=Path);p.add_argument('--output',type=Path);a=p.parse_args();r=a.directory;receipt=json.loads((r/'receipt.json').read_text(encoding='utf-8-sig'));assert receipt['pass'];nl=receipt['norm_lanes'];pl=receipt['pack_lanes'];assert nl==32768 and pl==65536
files=[]
def read(name):
 b=(r/name).read_bytes();files.append({'file':name,'bytes':len(b),'sha256':hashlib.sha256(b).hexdigest()});return b
def raw(name,words):
 b=read(name);assert len(b)==words*4+512 and b[:256]==bytes([165])*256 and b[-256:]==bytes([165])*256;return b[256:-256]
nr=raw('norm-with-canaries.bin',nl*6);pr=raw('pack-with-canaries.bin',pl*12);tb=read('norm-tags.u32');assert len(tb)==nl*4
nw=struct.unpack('<'+str(nl*6)+'I',nr);tags=struct.unpack('<'+str(nl)+'I',tb);normdiff=lower=upper=mapdiff=invalid=0
for t in range(nl):
 v=nw[t*6:t*6+6];normdiff+=v[0]!=v[1];lower+=v[2]!=v[3];upper+=v[4]!=v[5];base=t&~31;rc=t&15;mapdiff+=v[2]!=tags[base+rc];mapdiff+=v[4]!=tags[base+rc+16]
 for q in v[:2]:invalid+=bool(q&0x80000000) or (q&0x7f800000)==0x7f800000
packdiff=[0,0,0]
for t in range(pl):
 for mode in range(3):
  off=t*48+mode*16;packdiff[mode]+=sum(x!=y for x,y in zip(pr[off:off+8],pr[off+8:off+16]))
assert normdiff==lower==upper==mapdiff==invalid==0 and packdiff==[0,0,0];assert pr[0]==pr[32]==pr[32768*48]==0 and pr[32768*48+32]==0x80
result={'pass':True,'norm_lanes':nl,'norm_float_bits_equal':True,'lower_upper_and_cpu_lane_mapping_equal':True,'norm_nonnegative_finite':True,'fp8_values_per_path':pl*8,'qkv_probability_av_byte_differences':packdiff,'reference_plus_minus_zero_checked':True,'canaries_unchanged':True,'input_immutability_from_gpu_receipt':receipt['inputs_unchanged'],'files':files,'scope':'full EXEC; current NOF1 QKV/prob/AV; finite random norm, all half codes plus sampled FP32 bit patterns/edges for pack'}
o=a.output or r/'cpu-verified.json';o.write_text(json.dumps(result,indent=2)+'\n');print('HELPER_RAW_REVIEW_PASS norm32768lane pack524288perpath')
