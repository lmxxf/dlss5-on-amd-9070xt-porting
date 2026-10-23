from pathlib import Path
import json,ntpath,hashlib
P=Path(__file__).resolve().parent;R=P.parents[3];W=Path('/home/lmxxf/work/dlss5-gap-phase5-20261023/knife2')
old=R/'Development/results/gap-phase4-20261023/knife2'
# Resolve repository explicitly; results nesting must not become a guessed source root.
R=Path('/home/lmxxf/work/ai-theorys-study/wechat/assets/297');old=R/'Development/results/gap-phase4-20261023/knife2'
base=json.loads((old/'gold-corrected-jobs.json').read_text());bh=json.loads((P/'build-identity.json').read_text())['hosts']
LAB=r'D:\DLSSNR-Lab\gap-phase5-20261023';REMOTE=LAB+r'\knife2';MODULES=LAB+r'\baseline\modules'
ASSETS=r'D:\DLSSNR-Lab\small-model-20261018\assets';INPUT=r'D:\DLSSNR-Lab\combined-vs041-20261006\input.f16';FREE=r'D:\DLSSNR-Lab\gap-phase4-20261023\knife2\free1072-input.f16'
def norm(x):
 while '\\\\'in x:x=x.replace('\\\\','\\')
 return x
files={}
for f in base['files']:
 path=norm(f['path']);name=ntpath.basename(path)
 if path.endswith('.exe'):
  if name not in bh:continue
  path=ntpath.join(REMOTE,name);sha=bh[name]
 elif path.endswith('.hsaco'):path=ntpath.join(MODULES,name);sha=f['sha256']
 else:sha=f['sha256']
 files[path]=sha
# Pin the existing standard HDR capture too.
files[INPUT]='fa3c18735ac5f105903614a18a9b1475966b9afd9dd4aeb0cd9cad7bc759ec9b'
# Expected Original asset byte identity from the previously validated HT conversion.
# Runner must hash-check these paths before GPU work; a mismatch is not waived.
original=Path('/home/lmxxf/work/dlss5-small-build/old-converted');orig=[]
for f in sorted(original.iterdir()):
 if f.suffix not in ['.f16','.f32'] or f.name=='post70-history-head.f16':continue
 sha=hashlib.sha256(f.read_bytes()).hexdigest();files[ntpath.join(ASSETS,f.name)]=sha;orig.append({'name':f.name,'sha256':sha,'bytes':f.stat().st_size})
(P/'original-assets-expected.json').write_text(json.dumps({'local_reference':str(original),'files':orig,'remote_assertion':'expected locks only; this preparation did not inspect target'},indent=2)+'\n')
flags={}
for line in base['jobs'][0]['flags'].splitlines():
 if '='in line and line.startswith('DLSS5_'):
  k,v=line.split('=',1);flags[k]=v
common={'DLSS5_HIP_GRAPH':'0','DLSS5_FAST_NUMERIC':'1','DLSS5_MULTI_PASS_PREDICT':'0','DLSS5_MULTI_PASS_SKIN_PROTECT':'0','DLSS5_MULTI_PASS_SKIP_BLOCKS':'','DLSS5_SKIP_BLOCKS':'','DLSS5_TEMPORAL_MODE':'0','DLSS5_FAST_HISTORY':'0','DLSS5_TEMPORAL_HISTORY_EXPERIMENT':'0','DLSS5_HOT_RELOAD':'0','DLSS5_MULTI_PASS_HOTKEY':'0','DLSS5_VIT_REUSE_HOTKEY':'0','DLSS5_SHOW_FPS':'0','DLSS5_FRAME_STATS':'0','DLSS5_NET_TIMING':'0','DLSS5_HIP_NET_TIMING':'0','DLSS5_GAME_PROBE':'0','DLSS5_HIP_SPAN_PROBE':'0','DLSS5_HIP_INPUT_POLL':'0','DLSS5_HIP_VIT_VFRAG':'1','DLSS5_VIT_ADAPTIVE_IDLE_MS':'86400000','DLSS5_VIT_REUSE_PERIOD':'4','DLSS5_VIT_REUSE_GLOBAL':'0.22','DLSS5_VIT_REUSE_LOCAL':'1.0','DLSS5_VIT_REUSE_IMAGE':'0.35'}
profiles={
 'small900':('0.5','900','1152','0',1600,1024,448,'standard'),
 'small720':('0.5','720','1152','0',1344,768,288,'standard'),
 'small1080':('0.5','1080','1152','0',1920,1152,640,'standard'),
 'smallfree1088':('0.5','1080','1088','1',1920,1088,640,'free1072'),
 'original900':('1','900','1152','0',1600,960,400,'standard'),
 'original1088':('1','1080','1088','0',1920,1088,640,'standard'),
 'original1080':('1','1080','1152','0',1920,1152,640,'standard')}
def job(label,profile,arm,frames,pattern=0,ae=False,edges=False):
 mode,height,rows,free,w,h,n,geo=profiles[profile];f=dict(flags);f.update(common);f.update({'DLSS5_MULTI_PASS':mode,'DLSS5_NETWORK_HEIGHT':height,'DLSS5_NETWORK_1080_ROWS':rows,'DLSS5_NETWORK_FREE_RES':free,'DLSS5_VIT_ADAPTIVE':'1'if ae else'0'})
 env={'NR_VIT_VFRAG':'1'};case=ntpath.join(REMOTE,label.split('/')[-1]);capture=not edges
 if capture:env['SMALL_FRAME_CAPTURE']='1'
 if ae:f['DLSS5_VIT_ADAPTIVE_LOG']=case+r'\adaptive.csv'
 inp=FREE if geo=='free1072'else INPUT
 j={'label':label,'exe':ntpath.join(REMOTE,f'frame-{arm}-{geo}.exe'),'copy_exe':True,'copy_assets':False,'asset_path_reason':'All assets/input/modules passed explicitly; no game installation access','flags':'\n'.join(k+'='+v for k,v in f.items())+'\n','env':env,'arguments':f'"{ASSETS}" "{{OUT}}\\same.flags" "{inp}" "{{OUT}}\\out" {frames} 0 "{MODULES}" 0 {int(edges)} 0 {pattern}','frame_count':frames,'edges_only':int(edges),'allow_frame_changes':ae,'required_stderr':[r'model requested=0\.5x effective=0\.5x blocks=31'if mode=='0.5'else r'model requested=1x effective=1x blocks=71',r'VIT_VFRAG requested=1 available=1 explicit_ae0=[01] producers=\d+ consumers=\d+'],'review_profile':profile,'review_arm':arm,'review_pattern':pattern,'review_kind':'ae'if ae else'gold','expected_processing':[w,h],'expected_tokens':n,'expected_small':mode=='0.5','route_calls_are_launches_not_compute':True}
 j['required_local_logs']=['DLSS5-AMD/logs/native-hip.txt','DLSS5-AMD/logs/native-game-oneshot.txt']
 if ae:j.update({'required_local_logs':j['required_local_logs']+['adaptive.csv'],'notes':'Post-analyze paired output hashes by frame index and AE CSV; last frame_count AE records must contain both state0=0 compute and state0=1 reuse; do not compare every raw to first frame.'})
 return j
def save(name,jobs,extra=None):
 m={'name':name,'prepared_only':True,'GPU_not_authorized_until_knife1_complete':True,'module_lock_note':'Both arms share phase5 baseline/modules. If knife1 adopted root must update BOTH arms module locks before execution. No stale-lock waiver.','files':[{'path':k,'sha256':v}for k,v in files.items()],'jobs':jobs,'post_analysis':'analyze-correctness.py for gold/AE; performance separate with warmup80'}
 if extra:m.update(extra)
 (P/(name+'.json')).write_text(json.dumps(m,indent=2)+'\n')
gold=[]
for profile in list(profiles)[:4]:
 for pat in range(3):
  for arm in ['baseline','candidate']:
   j=job(f'knife2/gold-{profile}-p{pat}-{arm}',profile,arm,3,pat)
   if arm=='candidate':j['compare_to']=ntpath.join(REMOTE,f'gold-{profile}-p{pat}-baseline','out-first.f16')
   gold.append(j)
save('gold-jobs',gold,{'expected_checked_frames':72})
ae=[]
for profile in list(profiles)[4:]+list(profiles)[:4]:
 for arm in ['baseline','candidate']:ae.append(job(f'knife2/ae-{profile}-{arm}',profile,arm,6,0,True))
save('ae-jobs',ae,{'expected_checked_frames':84,'pair_by_frame_index':True,'require_compute_and_reuse':True})
defaultidle=[]
for profile in ['original1088','small720']:
 for arm in ['baseline','candidate']:
  j=job(f'knife2/ae-default-idle-{profile}-{arm}',profile,arm,6,0,True)
  j['flags']='\n'.join(line for line in j['flags'].splitlines() if not line.startswith('DLSS5_VIT_ADAPTIVE_IDLE_MS='))+'\n'
  j['review_kind']='ae-default-idle'
  j['notes']='No idle override: production default500ms. Record per-frame outputs and actual AE CSV; differing wall-driven decisions are not automatically a layout failure.'
  defaultidle.append(j)
save('ae-default-idle-jobs',defaultidle,{'expected_checked_frames':24,'strict_cross_arm_state_equality_required':False,'post_analysis':'analyze-default-idle.py --evidence EVIDENCE --output REPORT'})

perf=[]
for profile in list(profiles)[:4]:
 for rnd in range(1,4):
  for slot,arm in enumerate(['baseline','candidate','candidate','baseline']):
   j=job(f'knife2/perf-{profile}-r{rnd}-{slot}-{arm}',profile,arm,240,0,False,True);j.update({'review_kind':'perf','round':rnd,'slot':slot,'warmup':80,'steady_samples':160,'compare_to':ntpath.join(REMOTE,f'gold-{profile}-p0-baseline','out-first.f16')});perf.append(j)
save('perf-jobs',perf,{'steady_samples_per_arm_per_profile':960,'expected_jobs':48})
(P/'jobs-summary.json').write_text(json.dumps({'files_locked':len(files),'Original_asset_files_expected':len(orig),'gold_jobs':len(gold),'gold_frames':72,'AE_jobs':len(ae),'AE_frames':84,'perf_jobs':len(perf),'module_root':MODULES,'prepared_only':True},indent=2)+'\n')
print('PREPARED',len(gold),len(ae),len(perf),'jobs;',len(files),'file locks; NO GPU execution')
