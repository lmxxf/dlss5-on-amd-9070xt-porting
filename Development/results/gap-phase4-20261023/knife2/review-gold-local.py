from pathlib import Path
import json,csv,re,hashlib,math
p=Path(__file__).resolve().parent;e=p/'evidence';work=Path('/home/lmxxf/work/dlss5-gap-phase4-20261023/knife2')
def text(f):return f.read_text(encoding='utf-8-sig')
def js(f):return json.loads(text(f))
def fields(l):return dict(re.findall(r'(\w+)=([^\s]+)',l))
def hashes(d,pat):
 entries=js(d/'raw-hashes.json');result={}
 for x in entries:
  m=re.fullmatch(pat,x['name'])
  if m:result[int(m[1])]=x['sha256'].lower()
 return result
report={'status':'PASS_LOCAL_REVIEW','remote_tools_used':False,'performance_reviewed':False,'frame_gold':[],'trace':[],'compatibility':[]}
# Actual final host/production source identities.
bi=js(p/'build-identity.json');ti=js(p/'trace-build-identity.json')
for n,h in {**bi['hosts'],**ti['trace_hosts']}.items():
 f=work/n;assert f.exists(),str(f);assert hashlib.sha256(f.read_bytes()).hexdigest()==h,(n,'host hash')
si=js(p/'source-identity.json')
for role,key in [('baseline','base_header'),('candidate','candidate_header')]:assert hashlib.sha256((work/(role+'-source')/'Development/HIP/hip_reference_network.h').read_bytes()).hexdigest()==si[key]
report['host_and_source_identity']={'frame_hosts':4,'trace_hosts':2,'source_headers_match':True,'candidate_header':si['candidate_header']}
# 24 selected Frame cases: retain but do not double-count superseded first baseline.
for shape in ['small900','small720','smallfree1088','small1080']:
 for pattern in range(3):
  arms={}
  for arm in ['baseline','candidate']:
   name=f'gold-{shape}-p{pattern}-{arm}'
   if name=='gold-small1080-p0-baseline':name+='-checked'
   d=e/name;assert js(d/'exit.json')['exit_code']==0
   rr=list(csv.DictReader((d/'out.csv').open()));assert len(rr)==3 and all(x['checked']=='1'and int(x['invalid'])==0 for x in rr)
   hh=hashes(d,r'out-frame-(\d+)\.f16');assert set(hh)=={0,1,2}
   st=text(d/'stderr.log');assert 'model requested=0.5x effective=0.5x blocks=31' in st;assert 'model requested=0.5x effective=1x'not in st
   vf=re.findall(r'VIT_VFRAG requested=\d+ available=\d+ explicit_ae0=\d+ producers=(\d+) consumers=(\d+)',st);assert vf and all(a==b for a,b in vf)
   expected=8 if shape=='small1080' else 0;assert sum(int(a)for a,b in vf)==expected,(name,vf)
   if expected:assert 'model_prepared mode=1 'in st and 'model requested=1x effective=1x blocks=71'in st
   arms[arm]=hh
  assert arms['baseline']==arms['candidate'],(shape,pattern)
  report['frame_gold'].append({'shape':shape,'pattern':pattern,'paired_frames':3,'sha256':arms['candidate']})
report['frame_gold_count']=72
# Parse actual trace stdout, independent from trace-validation.json summary.
expected_geo={'small900':(1600,1024,448,95,87),'small720':(1344,768,288,95,87),'smallfree1088':(1920,1088,640,91,83),'small1080':(1920,1152,640,83,83)}
for shape,(W,H,n,db,dc) in expected_geo.items():
 arms={};profiles={}
 for arm in ['baseline','candidate']:
  d=e/f'trace-{shape}-{arm}';assert js(d/'exit.json')['exit_code']==0;s=text(d/'stdout.log')
  assert 'finite=1 repeat_bitdiff=0 timing_not_collected=1'in s
  pp=[fields(l)for l in s.splitlines()if l.startswith('SMALL_TRACE_PROFILE ')];cc=[fields(l)for l in s.splitlines()if l.startswith('SMALL_TRACE_COUNT ')];assert len(pp)==len(cc)==3
  dispatch={i:[]for i in range(3)}
  for l in s.splitlines():
   m=re.match(r'SMALL_TRACE_DISPATCH frame=(\d+) index=(\d+) \S+ (\S+)',l)
   if m:assert int(m[2])==len(dispatch[int(m[1])]);dispatch[int(m[1])].append(m[3])
  compact=1 if arm=='candidate'or shape=='small1080'else 0
  for i,(pr,ct)in enumerate(zip(pp,cc)):
   assert [int(pr[k])for k in ['W','H','tokens','small_active','blocks']]==[W,H,n,1,31]
   assert pr['requested']==pr['effective']=='0'
   assert all(int(pr[k])==compact for k in ['compact_contract','compact_projection','contract_w16','projection_w16','contract_bstream','projection_bout','entry_exit_eligible'])
   assert pr['vfrag_producer_delta']==pr['vfrag_consumer_delta']=='0'
   assert int(ct['dispatches'])==(dc if arm=='candidate'else db)==len(dispatch[i])
   assert int(ct['vit_pack_input'])==(0 if compact else 8)and ct['unmapped']=='0'
   att=[x for x in dispatch[i]if 'vit_attention_fused_'in x];assert len(att)==8 and all(x==f'deep_fast:vit_attention_fused_{400 if n<=400 else 640}_bytein_bout'for x in att)
   if shape!='small1080'and arm=='candidate':assert 'deep_fast:decoder_project2x_h16w_w_bin_f8w16'in dispatch[i]and ct['up39_f32_byte_input']=='1'
   if arm=='candidate':assert f'SMALL_TRACE_PAIR frame={i} raw_bitdiff=0'in s
  hh=hashes(d,r'frame-(\d+)\.rgb32f');assert set(hh)=={0,1,2};arms[arm]=hh;profiles[arm]=pp[1]
 assert arms['baseline']==arms['candidate']
 saved=int(profiles['baseline']['weights_bytes'])-int(profiles['candidate']['weights_bytes']);assert saved==(0 if shape=='small1080'else 120*1048576)
 assert profiles['baseline']['pool_count']==profiles['candidate']['pool_count']and profiles['baseline']['pool_bytes']==profiles['candidate']['pool_bytes']
 report['trace'].append({'shape':shape,'actual_processing':[W,H],'actual_tokens':n,'dispatches':[db,dc],'weight_capacity_saved_bytes':saved,'paired_F32_frames':3,'Small_VF_deltas_zero':True})
report['trace_F32_frames']=24
# Public compatibility, independent paired hashes/actual per-frame modes and geometry.
cases={}
for d in sorted(x for x in e.glob('compat-*')if x.is_dir()):
 name=d.name;arm='candidate'if name.endswith('-candidate')else'baseline';key=name.rsplit('-',1)[0];assert js(d/'exit.json')['exit_code']==0
 rows=list(csv.DictReader((d/'out.csv').open()));hh=hashes(d,r'out-frame-(\d+)\.f16');assert set(hh)==set(range(len(rows)));assert all(x['checked']=='1'and int(x['invalid'])==0 for x in rows)
 st=text(d/'stderr.log');so=text(d/'stdout.log');assert 'model requested=0.5x effective=1x'not in st
 modes=re.findall(r'DIRECT mode=(\S+) output_same=\d+ fresh_after_process=1',so)
 hip=text(d/'DLSS5-AMD/logs/native-hip.txt');proc=re.findall(r'\bprocessing=(\d+x\d+)',hip);proc=[v for i,v in enumerate(proc)if i==0 or v!=proc[i-1]]
 val={'hashes':hh,'count':len(rows),'modes':modes,'processing':proc}
 if '-switch-'in key:
  assert len(rows)==10 and modes==['0.5','0.5','1','1','2','2','3','3','0.5','0.5']
  g=key.split('-')[-1];expected={'900':['1600x1024','1600x960','1600x1024'],'720':['1344x768','1280x768','1344x768'],'free1072':['1920x1088','1920x1152','1920x1088']}[g];assert proc==expected
  assert hh[0]==hh[1]==hh[8]==hh[9];assert all(hh[i]==hh[i+1]for i in (2,4,6))
 elif '-ae1-'in key:
  ae=list(csv.reader((d/'adaptive.csv').open()));assert len(rows)==6 and any(x[1]=='1'for x in ae);val['adaptive']=ae
 elif '-mode1-'in key:
  hist=[fields(l)for l in text(d/'DLSS5-AMD/fast-history-mode1.log').splitlines()if l.startswith('mode1 ')];use=[int(x['use_history'])for x in hist];assert use==[0,0,1,0,1,1];assert float(rows[0]['wall_ms'])>250;assert [int(x['reset'])for x in rows]==[1,0,0,1,0,0];assert hh[0]==hh[1]==hh[3];assert all(x['static_fallback']=='1'and x['motion']==x['depth']=='0'for x in hist);assert 'history model=Small blend=0.76708984375'in st;val['history_use']=use
 else:raise AssertionError(name)
 cases.setdefault(key,{})[arm]=val
for key,aa in cases.items():
 assert set(aa)=={'baseline','candidate'}and aa['baseline']==aa['candidate'],key
 report['compatibility'].append({'case':key,'checked_frames_both_arms':2*aa['baseline']['count'],'processing_sequence':aa['baseline']['processing'],'paired_hashes':True,'ae_decisions_equal':'adaptive'in aa['baseline'],'history_use':aa['baseline'].get('history_use')})
assert len(cases)==7;report['compatibility_frames']=sum(x['checked_frames_both_arms']for x in report['compatibility']);assert report['compatibility_frames']==108
# File locks: no target reads, compare runner's locked identities and local manifest.
locksets=[js(p/n)['files']for n in ['gold-jobs.json','gold-corrected-jobs.json','gold-free-remaining-jobs.json','trace-jobs.json','compat-jobs.json']]
common={}
for rows in locksets:
 for x in rows:
  path=x['path'];sha=x['sha256'].lower();assert path not in common or common[path]==sha;common[path]=sha
mod={Path(x['path'].replace('\\','/')).name:x['sha256'].lower()for x in locksets[0]if x['path'].endswith('.hsaco')};assert len(mod)==40
mf=Path(si['module_root'])/'SHA256SUMS';assert hashlib.sha256(mf.read_bytes()).hexdigest()==si['module_manifest_sha256']
manifest={z.split('/',1)[1]:h.lower()for h,z in (l.split()for l in mf.read_text().splitlines())if z.startswith('gfx1201/')};assert all(manifest[n]==h for n,h in mod.items())
report['locked_files']={'common_locked_paths_consistent':len(common),'gfx1201_modules_match_manifest':40,'module_manifest_sha256':si['module_manifest_sha256'],'raw_check_kind':'collected raw SHA receipts plus finite/repeat logs; large raw payloads not recopied'}
report['retained_receipt_corrections']=['small1080 p0 baseline checked rerun selected; original exit0 case retained but not double-counted','smallfree1088 p0 baseline exit0 raw receipts CPU-restored after incorrect warm-VF gate, no GPU rerun','MODE1 second-frame cold reset matches first-frame >250ms; actual history warm on frames2/4/5; no arbitrary reset waiver']
(p/'gold-review.json').write_text(json.dumps(report,indent=2)+'\n');print('PASS',report['frame_gold_count'],report['trace_F32_frames'],report['compatibility_frames'])
