from pathlib import Path
import json,hashlib,copy,re
p=Path(__file__).parent;r=r'D:\DLSSNR-Lab\yami-round5-20261031';old=r+'\\candidate\\modules';roles={'dotseq':'c32-dot2','dotpar':'c32-dotpar','wmma2':'c32-wmma2','wmma4':'c32-wmma4'};base=json.loads((p/'r2d-gold.json').read_text());files={f['path']:f for f in base['files']if old not in f['path']};gold=[];setup=["$ErrorActionPreference='Stop'","if(Test-Path 'D:\\DLSSNR-Lab\\gpu.lock'){throw 'busy'}"]
for arm,role in roles.items():
 new=r+'\\'+role+'\\modules'
 for f0 in base['files']:
  if old not in f0['path']:continue
  f=copy.deepcopy(f0);f['path']=f['path'].replace(old,new);name=f['path'].split('\\')[-1];source=p/role/'build/gfx1201'/name if name in ['c32-wave1-fast.hsaco','c32-wave1-fast-norm900.hsaco']else Path('Development/results/ksearch-round4-20261031/payload/HIP/gfx1201')/name;f['sha256']=hashlib.sha256(source.read_bytes()).hexdigest();files[f['path']]=f
 for j0 in base['jobs']:
  if not j0['label'].endswith('-candidate'):continue
  j=copy.deepcopy(j0);prof=j['label'].split('/')[-1].removesuffix('-candidate');j['label']='c32-reduction-gold/'+arm+'-'+prof;j['arguments']=j['arguments'].replace(old,new);j['method']=arm;gold.append(j)
 setup += [f'New-Item -ItemType Directory -Force "{new}"|Out-Null',f'Copy-Item "D:\\DLSSNR-Lab\\ksearch-round4-20261031\\payload\\HIP\\gfx1201\\*" "{new}"']
 for mod in ['c32-wave1-fast','c32-wave1-fast-norm900']:setup.append(f'Copy-Item "{r}\\{role}-{mod}.hsaco" "{new}\\{mod}.hsaco"')
(p/'c32-reduction-setup.ps1').write_text('\n'.join(setup)+'\n');g=dict(name='c32-reduction-gold',files=list(files.values()),jobs=gold);(p/'c32-reduction-gold.json').write_text(json.dumps(g,indent=2)+'\n')
jobs=[];arms=['stock']+list(roles)
for rnd in [1,2,3]:
 seq=arms[rnd-1:]+arms[:rnd-1];seq+=seq[::-1]
 for slot,arm in enumerate(seq):
  j=copy.deepcopy(next(j for j in base['jobs']if j['label']=='r2d-gold/small1080-baseline'))if arm=='stock'else copy.deepcopy(next(j for j in gold if j['label']=='c32-reduction-gold/'+arm+'-small1080'))
  j['label']=f'c32-reduction-screen/perf-small1080-r{rnd}-{slot}-{arm}';a=re.findall(r'"[^"]*"|\S+',j['arguments']);a[4]='240';a[8]='1';j['arguments']=' '.join(a);j['frame_count']=240;j['edges_only']=1;jobs.append(j)
(p/'c32-reduction-screen.json').write_text(json.dumps(dict(name='c32-reduction-screen',files=list(files.values()),jobs=jobs),indent=2)+'\n')
(p/'c32-reduction-screen-plan.md').write_text('C32 exact denominator K-search, fixed before measurements\n\nFour candidates:guardeddot2serial;guardeddot2fourindependentaccumulators;guarded2-accumulatorWMMA;guarded4independentWMMA. Originalreductionretainedforunsafeexactnessguard. Simpleproducer/reductionloopfusioncompiledidenticalfunctionbytes+metadataandisnotGPUrerun. Allmethodsneedprimitiveverification(actualCOMGRandproductionLLVM23,including24-bitprecisionedgeCPUoracles),then7staticGoldhashprofilesusingstockhost.\n\nSmall1080screen:5arms(stock+4methods),3rotatedsymmetric10-slotrounds,240framesdiscard80,960samples/arm. Require3/3meanimprovementandpooledp99improvementtoadvance;rankpassingcandidatesbymean. ThisisSCREENING,notpromotion. Bestcandidategetsindependent4-profileABBA andfinalpayloadvalidation. Keepallresults,norepeattoselectwins.\n')
