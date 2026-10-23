from pathlib import Path
import json,copy,ntpath,hashlib
P=Path(__file__).resolve().parent
src=P/'ae-jobs.json';base=json.loads(src.read_text());jobs=[]
for mode in (2,3):
 for profile in ('original1088','small720'):
  for arm in ('baseline','candidate'):
   j=copy.deepcopy(next(x for x in base['jobs']if x['review_profile']==profile and x['review_arm']==arm))
   name=f'ae-mode{mode}-{profile}-{arm}';j['label']='knife2/'+name
   flaglines=[]
   for line in j['flags'].splitlines():
    if line.startswith('DLSS5_VIT_ADAPTIVE='):line='DLSS5_VIT_ADAPTIVE='+str(mode)
    if line.startswith('DLSS5_VIT_ADAPTIVE_LOG='):
     old=line.split('=',1)[1];remote_root=ntpath.dirname(ntpath.dirname(old));line='DLSS5_VIT_ADAPTIVE_LOG='+ntpath.join(remote_root,name,'adaptive.csv')
    flaglines.append(line)
   j['flags']='\n'.join(flaglines)+'\n';j['review_kind']='ae-mode23';j['review_ae_mode']=mode;j['frame_count']=6;j['edges_only']=0;j['allow_frame_changes']=True
   j.pop('gold',None);j.pop('compare_to',None)
   j['notes']='Mode2 must actually compute every frame; mode3 must show actual compute and reuse. Both arms compare output SHA by frame index plus full AE CSV. No assumption all frames equal first.'
   jobs.append(j)
manifest={k:copy.deepcopy(v)for k,v in base.items()if k not in ('jobs','name','expected_checked_frames','require_compute_and_reuse','post_analysis')}
manifest.update(name='small-original-AE-mode23-minimal',jobs=jobs,expected_checked_frames=48,post_analysis='analyze-mode23.py --evidence EVIDENCE --output REPORT',source_ae_manifest_sha256=hashlib.sha256(src.read_bytes()).hexdigest(),files_identity='Exact current ae-jobs locks reused, including root-selected accepted pair modules; no baseline overwrite')
assert len(jobs)==8 and len({x['label']for x in jobs})==8
assert all(j['frame_count']==6 and 'DLSS5_VIT_ADAPTIVE_IDLE_MS=86400000' in j['flags']for j in jobs)
(P/'ae-mode23-jobs.json').write_text(json.dumps(manifest,indent=2)+'\n')
print('PREPARED 8 distinct mode2/3 jobs / 48 frames; current AE locks preserved; no GPU execution')
