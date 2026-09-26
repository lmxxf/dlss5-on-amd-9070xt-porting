"""Paired ABBA differences from slots.csv (kind,repeat,target,slot,active,frames,ms,hits_per_frame,bitdiff)."""
import csv,sys,collections
for f in sys.argv[1:]:
 rows=list(csv.DictReader(open(f)));g=collections.defaultdict(list);hits={};bad=collections.Counter()
 for r in rows:
  g[(r['kind'],r['target'],r['repeat'])].append((int(r['active']),float(r['ms'])))
  if int(r['active']):hits[(r['kind'],r['target'])]=float(r['hits_per_frame']);bad[(r['kind'],r['target'])]+=int(r['bitdiff'])
 out=collections.defaultdict(list);base=[]
 for (k,t,rep),v in g.items():
  a=[m for x,m in v if not x];b=[m for x,m in v if x];out[(k,t)].append(sum(b)/len(b)-sum(a)/len(a));base+=a
 bm=sum(base)/len(base)
 print(f,'base %.4f ms'%bm)
 for (k,t),v in sorted(out.items(),key=lambda kv:-sum(kv[1])/len(kv[1])):
  m=sum(v)/len(v);print('  %-5s %-58s hits/f %5.1f  diffs %s  mean %+.4f ms (%+.2f%%) %s'%(k,t,hits[(k,t)],' '.join('%+.4f'%x for x in v),m,100*m/bm,'bitdiff(dup)=%d'%bad[(k,t)] if bad[(k,t)] else ''))
