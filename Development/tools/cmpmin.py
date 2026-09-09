import sys,re,glob,numpy as np
A,B=sys.argv[1],sys.argv[2]
def stages(path):
 out={};cur=[]
 for line in open(path):
  m=re.fullmatch(r'network_gpu_interval stage=(\S+) ms=([\d.]+)\n',line)
  if m:cur.append((m[1],float(m[2])))
  m=re.fullmatch(r'network_gpu_interval total_ms=([\d.]+).*\n',line)
  if m:[out.setdefault(k,[]).append(v) for k,v in cur];cur=[]
 return out
def agg(name):
 acc={}
 for f in glob.glob(f'release/{name}/rounds/r*.log') or [f'release/{name}/network.stdout.log']:
  for k,v in stages(f).items():acc.setdefault(k,[]).extend(v)
 return {k:min(v) for k,v in acc.items()}
a,b=agg(A),agg(B)
rows=sorted(((k,a[k],b.get(k,0)) for k in a),key=lambda r:r[2]-r[1])
print(f"{'stage':30s} {A:>16s} {B:>16s}   delta")
for k,x,y in rows:
 if abs(y-x)>0.015:print(f"{k:30s} {x:16.3f} {y:16.3f} {y-x:+8.3f}")
print('sum of per-stage mins',round(sum(a.values()),2),round(sum(b.values()),2))
