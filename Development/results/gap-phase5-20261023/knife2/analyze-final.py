from pathlib import Path
import json,csv,re
R=Path(__file__).parent;E=R/'evidence';load=lambda p:json.loads(p.read_text(encoding='utf-8-sig'));read=lambda p:p.read_text(encoding='utf-8-sig');m=load(R/'final-default-jobs.json');out=[]
for j in m['jobs']:
 d=E/j['label'].split('/')[-1];ref=E/j['reference_label'].split('/')[-1];assert load(d/'exit.json')['exit_code']==0
 rows=list(csv.DictReader((d/'out.csv').open(encoding='utf-8-sig')));assert len(rows)==j['frame_count'] and all(x['checked']=='1' and int(x['invalid'])==0 for x in rows)
 hs=lambda p:{x['name']:x['sha256']for x in load(p/'raw-hashes.json')}
 assert hs(d)==hs(ref),(j['label'],'raw outputs differ from measured candidate')
 v=re.search(r'VIT_VFRAG requested=(\d+) available=(\d+) explicit_ae0=(\d+) producers=(\d+) consumers=(\d+)',read(d/'stderr.log'));assert v
 q,a,ae,pr,co=map(int,v.groups());assert pr==co
 if j['expected_vfrag_on']:assert q==a==1 and pr>=8*j['frame_count']
 else:assert q==a==pr==co==0
 if j['review_kind']=='ae':assert read(d/'adaptive.csv')==read(ref/'adaptive.csv')
 flags=read(d/'same.flags');assert 'NR_VIT_VFRAG' not in flags
 out.append(dict(label=j['label'],frames=len(rows),paired_route=pr,reference=j['reference_label'],pass_result=True))
report=dict(pass_result=True,jobs=len(out),frames=sum(x['frames']for x in out),cases=out);(R/'final-default-review.json').write_text(json.dumps(report,indent=2)+'\n');print('PASS final default/off',report['jobs'],'jobs',report['frames'],'frames')
