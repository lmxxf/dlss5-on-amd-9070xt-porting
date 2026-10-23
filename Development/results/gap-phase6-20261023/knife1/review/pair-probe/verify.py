#!/usr/bin/env python3
from pathlib import Path
import argparse,json,hashlib
p=argparse.ArgumentParser();p.add_argument('directory',type=Path);p.add_argument('--output',type=Path);a=p.parse_args();root=a.directory;receipt=json.loads((root/'receipt.json').read_text(encoding='utf-8-sig'));assert receipt['pass'];cases=[];files=[]
def read(p):
 b=p.read_bytes();files.append({'path':str(p.relative_to(root)),'bytes':len(b),'sha256':hashlib.sha256(b).hexdigest()});return b
def payload(p,n):
 b=read(p);assert len(b)==n+512 and b[:256]==b'\xa5'*256 and b[-256:]==b'\xa5'*256;return b[256:-256]
for n in [288,400,448,640]:
 d=root/str(n);old=payload(d/'qkv-old-guard.bin',n*3072);new=payload(d/'qkv-new-guard.bin',n*3072);tiles=n//16;checks=0
 # Consumer-oriented independent iteration, not the host's (token,channel) inverse formula.
 for part in range(2):
  for head in range(32):
   for tile in range(tiles):
    for lane in range(32):
     for khalf in range(2):
      for byte in range(8):
       tok=tile*16+(lane&15);channel=head*32+khalf*16+(lane>>4)*8+byte
       oi=part*n*1024+tok*1024+channel;ni=part*n*1024+(head*tiles+tile)*512+lane*16+khalf*8+byte
       assert old[oi]==new[ni],(n,part,head,tile,lane,khalf,byte);checks+=1
 assert checks==2*n*1024 and old[2*n*1024:]==new[2*n*1024:]
 assert read(d/'qkv-new-inverse.bin')==old
 avold=payload(d/'av-old-guard.bin',n*1024);avnew=payload(d/'av-new-guard.bin',n*1024);assert avold==avnew
 assert not any((x&127)==127 for x in old+new+avold+avnew)
 c=next(x for x in receipt['cases'] if x['tokens']==n);assert c['qk_inverse_byte_diff']==c['v_unchanged_byte_diff']==c['av_byte_diff']==c['canary_changed']==0;assert c['reuse_gate_sentinels_unchanged'] and c['input_unchanged'] and c['packed_weight_unchanged']
 cases.append({'tokens':n,'QK_bytes_independently_checked':checks,'QK_consumer_map_equal':True,'V_baseline_headmajor_bytes_unchanged':True,'AV_byte_equal':True,'guards_and_finite_pass':True,'reuse_gate_from_gpu_full_buffer_equality':True})
report={'pass':True,'scope':'phase5 Vfrag baseline vs QKfrag candidate; 4 actual token counts; CPU reread raw','cases':cases,'files':files,'reuse_raw_exported':False,'gpu_rerun_by_verifier':False};out=a.output or root/'cpu-verified.json';out.write_text(json.dumps(report,indent=2)+'\n');print('QK_PAIR_CPU_RAW_PASS 288 400 448 640')
