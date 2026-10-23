#!/usr/bin/env python3
"""CPU-only analysis of root-run public Frame compatibility evidence."""
from pathlib import Path
import argparse,csv,json,re,hashlib,math
p=argparse.ArgumentParser();p.add_argument('--evidence',type=Path,required=True);p.add_argument('--manifest',type=Path,default=Path(__file__).with_name('compat-jobs.json'));p.add_argument('--output',type=Path,required=True);a=p.parse_args();m=json.loads(a.manifest.read_text());results=[];pair={}
def read(p):return p.read_text(encoding='utf-8-sig')
def fields(line):return dict(re.findall(r'(\w+)=([^\s]+)',line))
def same_adjacent_compress(seq):return [x for i,x in enumerate(seq) if i==0 or x!=seq[i-1]]
for job in m['jobs']:
 name=job['label'].split('/')[-1];d=a.evidence/name;kind=job['compat_kind'];geom=job['compat_geometry'];arm=job['compat_arm'];count=job['frame_count'];assert json.loads(read(d/'exit.json'))['exit_code']==0,(name,'exit')
 rows=list(csv.DictReader((d/'out.csv').open()));assert len(rows)==count and [int(x['frame']) for x in rows]==list(range(count)),(name,'frame count/order');assert all(x['checked']=='1' and int(x['invalid'])==0 and math.isfinite(float(x['wall_ms'])) for x in rows),(name,'finite/all-frame readback')
 stdout=read(d/'stdout.log');stderr=read(d/'stderr.log');direct=re.findall(r'DIRECT mode=(\S+) output_same=\d+ fresh_after_process=1',stdout);assert direct==job['expected_modes'],(name,'direct mode sequence',direct)
 models=re.findall(r'model requested=(\S+) effective=(\S+) blocks=(\d+)',stderr);small=[x for x in models if x[0]=='0.5x'];assert small and all(x[1:] == ('0.5x','31') for x in small),(name,'Small fell back or absent',small)
 assert not re.search(r'model requested=0\.5x effective=1x',stderr),(name,'silent fallback')
 entries=json.loads(read(d/'raw-hashes.json'));entries=entries if isinstance(entries,list) else [entries];hashes={}
 for x in entries:
  mm=re.fullmatch(r'out-frame-(\d+)\.f16',x['name'])
  if mm:
   idx=int(mm[1]);hashes[idx]=x['sha256'].lower();payload=d/x['name']
   if payload.exists():assert hashlib.sha256(payload.read_bytes()).hexdigest()==hashes[idx],(name,'local raw mismatch',idx)
 assert set(hashes)==set(range(count)),(name,'per-frame hash count')
 lab=d/'DLSS5-AMD';step=read(lab/'logs/native-game-oneshot.txt');hip=read(lab/'logs/native-hip.txt');processing=same_adjacent_compress(re.findall(r'\bprocessing=(\d+x\d+)',hip));smallproc=job['expected_small_processing'];result={'case':name,'kind':kind,'geometry':geom,'arm':arm,'checked_frames':count,'direct_modes':direct,'processing_sequence':processing,'actual_small_records':len(small),'frame_sha256':hashes}
 if kind=='switch':
  orig={'900':'1600x960','720':'1280x768','free1072':'1920x1152'}[geom];assert processing==[smallproc,orig,smallproc],(name,'expected public geometry rebuild',processing);assert step.count('model/geometry rebuild completed')==2,(name,'rebuild count');assert len(small)>=2
  for mode in ['1x','2x','3x']:assert (mode,mode,'71') in models,(name,'Original live mode missing',mode)
  assert len({hashes[i] for i in [0,1,8,9]})==1,(name,'Small roundtrip differs')
  for i in [2,4,6]:assert hashes[i]==hashes[i+1],(name,'same-mode repeat differs',i)
 else:
  assert processing==[smallproc],(name,'wrong Small geometry',processing)
 if kind=='ae1':
  assert 'Small AE gain logical_blocks=11..18 asset_roles=31..38' in stderr,(name,'Small AE gain absent');ae=list(csv.reader((d/'adaptive.csv').open()));assert len(ae)>=count and all(len(x)==7 for x in ae),(name,'AE CSV absent/truncated');assert any(x[1]=='1' for x in ae),(name,'AE reuse was never exercised');result['adaptive_csv']=ae
 if kind=='mode1':
  assert 'history model=Small blend=0.76708984375' in stderr,(name,'Small history head/blend absent');hist=[fields(x) for x in read(lab/'fast-history-mode1.log').splitlines() if x.startswith('mode1 ')];assert len(hist)==count,(name,'history row count',len(hist));use=[int(x['use_history']) for x in hist];explicit_reset=[int(x['reset']) for x in rows];assert explicit_reset==[1,0,0,1,0,0],(name,'explicit reset schedule changed',explicit_reset)
  if use==[0,1,1,0,1,1]:history_reason='nominal cold/warm/reset/warm'
  elif use==[0,0,1,0,1,1]:
   first_ms=float(rows[0]['wall_ms']);assert first_ms>250,(name,'extra second-frame cold reset lacks long first-frame evidence',first_ms)
   assert hashes[0]==hashes[1]==hashes[3],(name,'cold/reset images disagree')
   history_reason='first cold frame exceeds fixed 250ms history idle reset; frame1 resets internally; frames2/4/5 truly warm'
   result['first_frame_wall_ms']=first_ms;result['history_idle_threshold_ms']=250
  else:raise AssertionError((name,'unsupported history sequence; do not waive arbitrary resets',use))
  assert all(x['static_fallback']=='1' and x['motion']=='0' and x['depth']=='0' for x in hist),(name,'unexpected guide scope');result['history_use']=use;result['history_reason']=history_reason;result['history_mode']='static fallback, real temporal history'
 pair.setdefault((kind,geom),{})[arm]=result;results.append(result)
for key,arms in pair.items():
 assert set(arms)=={'baseline','candidate'},(key,'missing arm');assert arms['baseline']['frame_sha256']==arms['candidate']['frame_sha256'],(key,'paired output differs')
 if key[0]=='ae1':assert arms['baseline']['adaptive_csv']==arms['candidate']['adaptive_csv'],(key,'AE decisions or metrics differ')
 if key[0]=='mode1':assert arms['baseline']['history_use']==arms['candidate']['history_use']
report={'pass':True,'paired_cases':len(pair),'jobs':len(results),'checked_frames':sum(x['checked_frames'] for x in results),'note':'No per-mode or history frames were required to equal an unrelated first-frame SHA. Switch compares repeated same-mode frames and final Small roundtrip. No performance use.','cases':results};a.output.write_text(json.dumps(report,indent=2)+'\n');print(f'COMPAT_PASS pairs={len(pair)} jobs={len(results)} frames={report["checked_frames"]}')
