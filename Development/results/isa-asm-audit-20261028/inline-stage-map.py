from pathlib import Path
import re,json,collections,subprocess
P=Path(__file__).parent;old=json.loads((P/'c32-stage-map.json').read_text());ranges=old['ranges'];out={}
for name in old['kernels']:
 ls=(P/'work/c32-wave1-fast'/(name+'.s')).read_text().splitlines();inst=[]
 for line,l in enumerate(ls,1):
  m=re.match(r'\s*((?:s_|v_|ds_|global_)[\w]+).*// ([0-9A-Fa-f]+):',l)
  if m:inst.append({'asm_line':line,'op':m[1],'pc':m[2],'text':l.strip()})
 cmd=['llvm-symbolizer','--obj='+str(P/'work/debug-map/gfx1201/c32-wave1-fast.hsaco'),'--inlines','--output-style=JSON'];r=subprocess.run(cmd,input=''.join('0x'+x['pc']+'\n' for x in inst),capture_output=True,text=True,check=True);syms=[json.loads(l) for l in r.stdout.splitlines()];assert len(syms)==len(inst)
 stages=collections.defaultdict(collections.Counter);examples=collections.defaultdict(list);unmapped=0
 for x,z in zip(inst,syms):
  frames=[f for f in z.get('Symbol',[]) if f.get('FunctionName','').startswith('cw_body<')];src=frames[0]['Line'] if frames else 0;stage=next((n for n,a,b in ranges if a<=src<=b),'prologue/control/unattributed');stages[stage][x['op']]+=1
  if len(examples[stage])<4:examples[stage].append({**x,'body_source_line':src,'inline_chain':[{'function':f['FunctionName'],'line':f['Line']} for f in z.get('Symbol',[])]})
 out[name]={'stages':{n:dict(c) for n,c in stages.items()},'examples':dict(examples),'total':len(inst)}
(P/'c32-inline-stage-map.json').write_text(json.dumps({'identity':'debug build code/resources identical to all64 production C32 functions','method':'llvm-symbolizer DWARF inline call chain -> cw_body callsite source line; no carry-forward guessing','ranges':ranges,'kernels':out},indent=2));print({n:{k:sum(v.values()) for k,v in r['stages'].items()} for n,r in out.items()})
