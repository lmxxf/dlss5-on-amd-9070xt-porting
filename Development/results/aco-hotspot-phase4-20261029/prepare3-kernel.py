from pathlib import Path
import json,copy,hashlib,ntpath
p=Path(__file__).parent;r=r'D:\DLSSNR-Lab\aco-hotspot-phase4-20261029';source=json.loads((p/'step1-nmmn.json').read_text());exe=ntpath.join(r,'payload','pure-profile-device-final.exe');stamp=ntpath.join(r,'payload','stamp.hsaco')
for role in ['3a','3b']:
 files=json.loads((p/(role+'-gold.json')).read_text())['files'];files += [{'path':exe,'sha256':hashlib.sha256((p/'payload/pure-profile-device-final.exe').read_bytes()).hexdigest()},{'path':stamp,'sha256':hashlib.sha256((p/'payload/stamp.hsaco').read_bytes()).hexdigest()}];jobs=[]
 for profile,num in [('original1088',153),('small1080',83)]:
  template=next(j for j in source['jobs']if j['profile']==profile and j['kind']=='N')
  for n in [profile+'.flags',profile+'-processing.rgba32f']:files.append({'path':ntpath.join(r,'payload',n),'sha256':hashlib.sha256((p/'payload'/n).read_bytes()).hexdigest()})
  for rnd in range(1,4):
   for slot,arm in enumerate(['baseline','candidate','candidate','baseline']):
    j=copy.deepcopy(template);j['exe']=exe;j['label']=f'{role}/kernel-{profile}-r{rnd}-{slot}-{arm}';j['env']={'PHASE4_PROFILE':'1','PHASE4_DISPATCHES':str(num),'PHASE4_STAMP_MODULE':stamp};j['arguments']=j['arguments'].replace(' 80 160 ',f' 80 {num*2} ').replace(r'D:\DLSSNR-Lab\isa-asm-phase3-20261029\payload\HIP\gfx1201',ntpath.join(r,'baseline'if arm=='baseline'else role,'modules'));jobs.append(j)
 (p/(role+'-kernel.json')).write_text(json.dumps({'name':role+'-kernel-device-clock-ABBA','files':files,'jobs':jobs},indent=2))
