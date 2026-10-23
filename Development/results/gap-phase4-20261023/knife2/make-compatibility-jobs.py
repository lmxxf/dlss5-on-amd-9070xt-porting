#!/usr/bin/env python3
from pathlib import Path
import json,re,ntpath
r=Path(__file__).resolve().parent;base=json.loads((r/'gold-corrected-jobs.json').read_text());jobs=[]
for kind,geoms in [('switch',['900','720','free1072']),('ae1',['900','720','free1072']),('mode1',['free1072'])]:
 for geom in geoms:
  for arm in ['baseline','candidate']:
   key={'900':'small900','720':'small720','free1072':'smallfree1088'}[geom]
   source=next(x for x in base['jobs'] if f'gold-{key}-p0-{arm}' in x['label'])
   quoted=re.findall(r'"([^"]*)"',source['arguments']);assert len(quoted)==5;assets,_,inp,_,modules=quoted
   label=f'knife2/compat-{kind}-{geom}-{arm}';outroot=ntpath.dirname(source['exe'])+'\\'+label.split('/')[-1]
   frames=10 if kind=='switch' else 6;temporal=int(kind=='mode1');reset=3 if temporal else 0
   over={'DLSS5_MULTI_PASS':'0.5','DLSS5_MULTI_PASS_PREDICT':'0','DLSS5_MULTI_PASS_SKIN_PROTECT':'0','DLSS5_MULTI_PASS_SKIP_BLOCKS':'','DLSS5_SKIP_BLOCKS':'','DLSS5_VIT_ADAPTIVE':'1' if kind=='ae1' else '0','DLSS5_VIT_ADAPTIVE_IDLE_MS':'86400000','DLSS5_TEMPORAL_MODE':'1' if kind=='mode1' else '0','DLSS5_TEMPORAL_HISTORY_EXPERIMENT':'0','DLSS5_FAST_HISTORY':'0','DLSS5_DIRECT_IO':'3','DLSS5_SHOW_FPS':'0','DLSS5_HOT_RELOAD':'0','DLSS5_MULTI_PASS_HOTKEY':'0','DLSS5_VIT_REUSE_HOTKEY':'0'}
   if kind=='ae1':over['DLSS5_VIT_ADAPTIVE_LOG']=outroot+'\\adaptive.csv'
   flags=source['flags']+'\n'+'\n'.join(k+'='+v for k,v in over.items())+'\n';env={'SMALL_FRAME_CAPTURE':'1','SMALL_FRAME_DIRECT_TEST':'1'}
   if kind=='switch':env['SMALL_FRAME_SWITCH']='1'
   patterns=[r'model requested=0\.5x effective=0\.5x blocks=31']
   if kind=='ae1':patterns.append(r'Small AE gain logical_blocks=11\.\.18 asset_roles=31\.\.38')
   if kind=='mode1':patterns.append(r'history model=Small blend=0\.76708984375')
   jobs.append({'label':label,'exe':source['exe'],'copy_exe':True,'copy_assets':False,'asset_path_reason':'Frame/codec/noise/history use explicit ASSETS and recreate_directory; HIP_MODULES explicit; SHOW_FPS0 and native RGBA16 avoid lab-root shader fallbacks','allow_frame_changes':True,'flags':flags,'env':env,'arguments':f'"{assets}" "{{OUT}}\\same.flags" "{inp}" "{{OUT}}\\out" {frames} {temporal} "{modules}" {reset} 0 0 0','frame_count':frames,'edges_only':0,'required_stderr':patterns,'compat_kind':kind,'compat_geometry':geom,'compat_arm':arm,'expected_modes':['0.5','0.5','1','1','2','2','3','3','0.5','0.5'] if kind=='switch' else ['0.5']*frames,'expected_small_processing':{'900':'1600x1024','720':'1344x768','free1072':'1920x1088'}[geom],'required_local_logs':['DLSS5-AMD/logs/native-game-oneshot.txt','DLSS5-AMD/logs/native-hip.txt']+(['DLSS5-AMD/fast-history-mode1.log'] if kind=='mode1' else []),'notes':'Root copy_exe preserves SHA and puts executable beside this case DLSS5-AMD; collect .txt as well as .log/.csv/.json. No single gold/compare_to: analyze paired frame-index hashes.'})
manifest={'name':'small-compact-public-compat','files':base['files'],'jobs':jobs,'post_analysis':'compat-analyze.py --evidence EVIDENCE_ROOT --output REPORT_JSON; requires captured per-frame hashes plus isolated .txt/.log and AE CSV','requires_runner_support':['copy_exe=true copies checked exe to case/frame.exe','allow_frame_changes=true bypasses only changed_first==0; keep frame count/checked/finite gate']};(r/'compat-jobs.json').write_text(json.dumps(manifest,indent=2)+'\n');print(f'{len(jobs)} jobs, {sum(x["frame_count"] for x in jobs)} frames, {len(base["files"])} file gates')
