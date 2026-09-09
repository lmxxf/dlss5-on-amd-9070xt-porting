#!/bin/bash
# usage: abn.sh <rounds> <nameA> <runnerA> <nameB> <runnerB>  (dirs must already exist); prints per-stage min over rounds
set -e
N=$1;A=$2;RA=$3;B=$4;RB=$5
for i in $(seq 1 $N); do
 for pair in "$A $RA" "$B $RB"; do set -- $pair
  ssh amd9070 "set DLSS5_TEST_FRAME_COUNT=15&& powershell -ExecutionPolicy Bypass -File D:\\DLSSNR-Lab\\$1\\$2 -Folder D:\\DLSSNR-Lab\\$1 > D:\\DLSSNR-Lab\\$1\\driver.log 2>&1"
  mkdir -p release/$1/rounds; scp -q amd9070:"D:/DLSSNR-Lab/$1/network.stdout.log" release/$1/rounds/r$i.log
 done
done
python3 - "$A" "$B" "$N" <<'PY'
import sys,re,numpy as np
A,B,N=sys.argv[1],sys.argv[2],int(sys.argv[3])
def stages(path):
 out={};cur=[]
 for line in open(path):
  m=re.fullmatch(r'network_gpu_interval stage=(\S+) ms=([\d.]+)\n',line)
  if m:cur.append((m[1],float(m[2])))
  m=re.fullmatch(r'network_gpu_interval total_ms=([\d.]+).*\n',line)
  if m:out.setdefault('__total',[]).append(float(m[1]));[out.setdefault(k,[]).append(v) for k,v in cur];cur=[]
 return {k:np.array(v[1:]) for k,v in out.items()}
def agg(name):
 acc={}
 for i in range(1,N+1):
  for k,v in stages(f'release/{name}/rounds/r{i}.log').items():acc.setdefault(k,[]).append(v.mean())
 return {k:min(v) for k,v in acc.items()}
a,b=agg(A),agg(B)
rows=sorted(((k,a[k],b.get(k,0)) for k in a if k!='__total'),key=lambda r:r[2]-r[1])
print(f"{'stage':30s} {A:>10s} {B:>10s}   delta")
for k,x,y in rows:
 if abs(y-x)>0.02:print(f"{k:30s} {x:10.3f} {y:10.3f} {y-x:+8.3f}")
print('total(min of means)',round(a['__total'],2),round(b['__total'],2),'sum-of-stage-mins',round(sum(a[k] for k in a if k!='__total'),2),round(sum(b[k] for k in b if k!='__total'),2))
PY
