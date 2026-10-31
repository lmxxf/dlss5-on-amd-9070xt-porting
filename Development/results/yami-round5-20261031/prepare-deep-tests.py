from pathlib import Path
import json,hashlib,copy,re
p=Path(__file__).parent;r=r'D:\DLSSNR-Lab\yami-round5-20261031';src=json.loads((p/'pair-gold.json').read_text());base=copy.deepcopy(next(j for j in src['jobs']if j['label']=='pair-gold/small1080-candidate'));exe=r+'\\deep-cache-frame.exe';files=src['files']+[{'path':exe,'sha256':hashlib.sha256((p/'deep-cache-frame.exe').read_bytes()).hexdigest()}]
arms={'A0':(0,0),'A1_cache':(1,0),'A2_pair':(0,1),'A3_both':(1,1)};gold=[]
for arm,(cache,pair) in arms.items():
 j=copy.deepcopy(base);j['label']='deep-four-gold/'+arm;j['exe']=exe;j['env']={'NR_R5_DEEP_CACHE':str(cache),'NR_R5_VIT_EXP_PAIR':str(pair)};j['required_stderr']=[f'R5_DEEP_CACHE enabled={cache}']
 if cache:j['required_stderr'].append('R5_DEEP_CACHE enabled=1 hits=[1-9]')
 if pair:j['required_stderr'].append('VIT_EXP_PAIR requested=1 available=1 calls=[1-9]')
 else:j['required_stderr'].append('VIT_EXP_PAIR requested=0 available=0 calls=0')
 gold.append(j)
(p/'deep-four-gold.json').write_text(json.dumps({'name':'deep-lookup-pair-four-arm-gold','files':files,'jobs':gold},indent=2));jobs=[]
orders=[['A0','A1_cache','A2_pair','A3_both'],['A1_cache','A3_both','A0','A2_pair'],['A3_both','A2_pair','A1_cache','A0']]
for rnd,o in enumerate(orders,1):
 for slot,arm in enumerate(o+o[::-1]):
  j=copy.deepcopy(next(j for j in gold if j['label'].endswith('/'+arm)));j['label']=f'deep-four-perf/small1080-r{rnd}-{slot}-{arm}';a=re.findall(r'"[^"]*"|\S+',j['arguments']);a[4]='240';a[8]='1';j['arguments']=' '.join(a);j['frame_count']=240;j['edges_only']=1;jobs.append(j)
(p/'deep-four-perf.json').write_text(json.dumps({'name':'deep-lookup-pair-four-arm-perf','files':files,'jobs':jobs},indent=2))
