from pathlib import Path
import re,json,collections,subprocess
P=Path(__file__).parent;src=(P/'work/debug-map/gfx1201/sources/c32-wave1-fast/probe.hip').read_text().splitlines();find=lambda needle,start=0:next(i+1 for i,l in enumerate(src[start:],start) if needle in l)
a=find(' if constexpr(Chain){');b=find('  i2 feature[2];',a);c=find('  for(uint part=0;part<3;part++){',b);d=find('   f8 sum{};if(part<2)',c);e=find('  h8 ex[4];',d);f=find('  f8 sums{};h8 ones{};',e);g=find('  i2 av[2];',f);h=find('  for(uint ci=0;ci<2;ci++){',g);i=find('#if CW_POST_HEAD_VEC || CW_POST_FULL_TILE',h)
ranges=[('input/staging',find(' uint win=bid();'),a-1),('FFN',a,b-1),('FFN residual/quant',b,c-1),('QKV',c,d-1),('Norm/QKV storage/query setup',d,e-1),('Score/Exp',e,f-1),('Sum/prob',f,g-1),('AV',g,h-1),('OutProj/residual',h,i-1),('Post/Down/tail',i,find('#define CW_ENTRY'))]
s=(P/'work/debug-c32-lines.s').read_text();sp=re.split(r'^([0-9a-f]+) <([^>]+)>:\n',s,flags=re.M);out={}
for x in range(1,len(sp),3):
 name=sp[x+1]
 if name not in ['c32_wave1_prefix_b8d_w16','c32_wave1_post_b8_w16','c32_wave1_chain_w16','c32_wave1_finish_b8_w16','c32_wave1_up_b8_w16','c32_wave1_mapped_b8_w16']:continue
 valid=set(re.findall(r'// ([0-9A-Fa-f]+):',(P/'work/c32-wave1-fast'/(name+'.s')).read_text()));line=None;stages=collections.defaultdict(collections.Counter);locations=collections.defaultdict(list)
 for l in sp[x+2].splitlines():
  m=re.match(r'; .*probe\.hip:(\d+)',l)
  if m:line=int(m[1])
  m=re.match(r'\s*((?:s_|v_|ds_|global_)[\w]+).*// ([0-9A-Fa-f]+):',l)
  if not m or m[2] not in valid:continue
  stage=next((n for n,a,b in ranges if line is not None and a<=line<=b),'shared helper/prologue or unmapped');stages[stage][m[1]]+=1
  if len(locations[stage])<4:locations[stage].append({'source_line':line,'pc':m[2],'instruction':l.strip()})
 out[name]={'stages':{n:dict(c) for n,c in stages.items()},'examples':dict(locations)}
(P/'c32-stage-map.json').write_text(json.dumps({'identity':'debug line-table builds have identical code and resources for all64 C32/137 C64 functions','ranges':ranges,'method':'DWARF source line attribution; shared helpers stay separate, no guessed stage attribution','kernels':out},indent=2));print({n:{k:sum(v.values()) for k,v in r['stages'].items()} for n,r in out.items()})
