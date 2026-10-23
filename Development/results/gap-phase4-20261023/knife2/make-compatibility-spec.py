#!/usr/bin/env python3
"""Local-only job specification generator. Does not invoke a target or GPU.

The output is a design manifest for root's runner, NOT blindly runnable by the
current repeat-only run-jobs.ps1: mode/history sequences intentionally vary.
"""
from pathlib import Path
import argparse,json,hashlib
p=argparse.ArgumentParser();p.add_argument('--assets',required=True,help='Combined asset root with original weights, small/, shaders, noise/history')
p.add_argument('--modules',default=r'D:\DLSSNR-Lab\gap-phase4-20261023\knife1\modules')
p.add_argument('--remote-root',default=r'D:\DLSSNR-Lab\gap-phase4-20261023\knife2')
p.add_argument('--standard-input',default=r'D:\DLSSNR-Lab\combined-vs041-20261006\input.f16')
p.add_argument('--output',type=Path,required=True);a=p.parse_args()
local=Path('/home/lmxxf/work/dlss5-gap-phase4-20261023/knife2');jobs=[]
for kind,geoms in [('switch',['900','720','free1072']),('ae1',['900','720','free1072']),('mode1',['free1072'])]:
 for geom in geoms:
  for arm in ['baseline','candidate']:
   hostgeom='free1072' if geom=='free1072' else 'standard';name=f'frame-{arm}-{hostgeom}.exe';host=local/name
   label=f'compat-{kind}-{geom}-{arm}';remote=a.remote_root+'\\'+label
   is_switch=kind=='switch';frames=10 if is_switch else 6;temporal=1 if kind=='mode1' else 0;reset=3 if kind=='mode1' else 0
   flags={'DLSS5_HIP_FAST':'1','DLSS5_FAST_NUMERIC':'1','DLSS5_MULTI_PASS':'0.5','DLSS5_MULTI_PASS_PREDICT':'0','DLSS5_MULTI_PASS_SKIN_PROTECT':'0','DLSS5_MULTI_PASS_SKIP_BLOCKS':'','DLSS5_SKIP_BLOCKS':'','DLSS5_VIT_ADAPTIVE':'1' if kind=='ae1' else '0','DLSS5_VIT_ADAPTIVE_IDLE_MS':'86400000','DLSS5_TEMPORAL_MODE':'1' if kind=='mode1' else '0','DLSS5_TEMPORAL_HISTORY_EXPERIMENT':'0','DLSS5_FAST_HISTORY':'0','DLSS5_OVERLAP':'0','DLSS5_HIP_GRAPH':'0','DLSS5_DIRECT_IO':'3','DLSS5_SHOW_FPS':'0','DLSS5_HOT_RELOAD':'0','DLSS5_MULTI_PASS_HOTKEY':'0','DLSS5_VIT_REUSE_HOTKEY':'0','DLSS5_NETWORK_FREE_RES':'1' if geom=='free1072' else '0','DLSS5_NETWORK_HEIGHT':'1080' if geom=='free1072' else geom,'DLSS5_NETWORK_1080_ROWS':'1152','DLSS5_STYLE':'1','DLSS5_FRAME_STATS':'0','DLSS5_NET_TIMING':'0','DLSS5_GAME_PROBE':'0'}
   if kind=='ae1':flags['DLSS5_VIT_ADAPTIVE_LOG']=remote+'\\adaptive.csv'
   inp=a.remote_root+'\\free1072-input.f16' if geom=='free1072' else a.standard_input
   env={'SMALL_FRAME_CAPTURE':'1','SMALL_FRAME_DIRECT_TEST':'1'}
   if is_switch:env['SMALL_FRAME_SWITCH']='1'
   expected_small={'900':{'processing':'1600x1024','tokens':448},'720':{'processing':'1344x768','tokens':288},'free1072':{'processing':'1920x1088','tokens':640}}[geom]
   jobs.append({'label':label,'arm':arm,'kind':kind,'geometry':geom,'exe':a.remote_root+'\\'+name,'local_exe':str(host),'exe_sha256':hashlib.sha256(host.read_bytes()).hexdigest() if host.exists() else None,'assets':a.assets,'input':inp,'modules':a.modules,'env':env,'flag_overrides':flags,'arguments':f'"{a.assets}" "{{OUT}}\\same.flags" "{inp}" "{{OUT}}\\out" {frames} {temporal} "{a.modules}" {reset} 0 0 0','expected_frames':frames,'expected_small':expected_small,'required_stderr':['model requested=0.5x effective=0.5x blocks=31','Small AE gain logical_blocks=11..18' if kind=='ae1' else ''],'pair_rule':'match out-frame-i.f16 to other arm at same i; never one SHA for entire sequence','small_roundtrip_indices':[0,1,8,9] if is_switch else None,'direct_modes':['0.5','0.5','1','1','2','2','3','3','0.5','0.5'] if is_switch else ['0.5']*frames,'do_not_set_runner_frame_count':True,'do_not_set_runner_compare_to':True})
a.output.write_text(json.dumps({'scope':'CPU-only specification; merge flag_overrides onto an already validated production flag set','guard_owner':'root only; existing GPU lock/game-check/disk/watchdog','runner_note':'current runner repeat gate cannot process these sequences; omit its frame_count/gold/compare_to and validate every-frame CSV+paired raw before accepting','jobs':jobs},indent=2)+'\n');print(f'{len(jobs)} jobs / {sum(j["expected_frames"] for j in jobs)} checked frames; no target operations')
