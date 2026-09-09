#!/usr/bin/env python3
# usage: cmpmed.py <A> <B>  -- per-stage MEDIAN over all frames of release/<X>/rounds/r*.log (min-of-mins picks bimodal outliers)
import re,glob,sys,statistics as st
def load(d):
    out={}
    for f in glob.glob(f'release/{d}/rounds/r*.log'):
        for line in open(f):
            m=re.match(r'network_gpu_interval stage=(\S+) ms=([\d.]+)',line)
            if m: out.setdefault(m[1],[]).append(float(m[2]))
    return out
A,B=load(sys.argv[1]),load(sys.argv[2])
rows=sorted((st.median(B[k])-st.median(A[k]),k,st.median(A[k]),st.median(B[k])) for k in A if k in B)
for d,k,a,b in rows:
    if abs(d)>=0.03: print(f'{k:32s} {a:8.3f} {b:8.3f} {d:+.3f}')
top=lambda X:sum(st.median(X[k]) for k in X if 'detail' not in k and 'probe' not in k and not k.startswith('vit') and not k.startswith('decoder_stage') and k not in('decoder_tail_begin',))
print('sum of medians (top-level stages)',round(top(A),2),round(top(B),2))
