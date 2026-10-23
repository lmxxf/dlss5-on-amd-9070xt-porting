import pathlib,json,argparse
p=argparse.ArgumentParser();p.add_argument('trace',type=pathlib.Path);p.add_argument('--out',type=pathlib.Path);a=p.parse_args();d=json.loads(a.trace.read_text(encoding='utf-8-sig'));tiles=d['tiles'];n=tiles*16;rows=[]
assert len(d['edges'])==16
for e in d['edges']:
 pub=e['producer_prepublish'];consumer=e['consumer_first_mma_complete'];assert len(pub)==d['producer_stride'] and len(consumer)==n//2
 good=[];invalid=[]
 for i,t in enumerate(pub):
  wg,wave=divmod(i,2);first=(wg//8)*32+wave*16
  if first<n:assert t>0,(e['slot'],i,'missing producer');good.append(t)
  else:assert t==0,(e['slot'],i,'invalid tail published');invalid.append(i)
 assert len(good)==tiles*8
 assert e['target']%8==0 and all(v==e['target'] for v in e['ready'])
 assert len(e['ready'])==tiles and all(t>0 for t in consumer)
 last=max(good);over=sum(t<last for t in consumer)
 rows.append(dict(slot=e['slot'],block=e['block'],target=e['target'],producer_live=len(good),producer_invalid_tail=len(invalid),consumer_work_markers=len(consumer),last_producer_prepublish=last,earliest_consumer_first_mma_complete=min(consumer),useful_overlap_consumers=over,overlap_ticks=max(0,last-min(consumer))))
r=dict(source=str(a.trace),ordered=d['ordered'],clock=d['clock'],edges=rows,total_useful_overlap_consumers=sum(x['useful_overlap_consumers'] for x in rows),edges_with_useful_overlap=sum(x['useful_overlap_consumers']>0 for x in rows),tick_frequency_not_assumed=True)
if d['ordered']:assert r['total_useful_overlap_consumers']==0,'ordered control shows impossible overlap'
text=json.dumps(r,indent=2)
if a.out:a.out.write_text(text+'\n')
print(text)
