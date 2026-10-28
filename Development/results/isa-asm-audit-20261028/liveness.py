from pathlib import Path
import re,json,csv
P=Path(__file__).parent
names=[('c64-wave2-fast','c64_wave2_bi_bo'),('c64-wave2-fast','c128_wave2_bi_bo'),('c64-wave2-fast','c256_wave2_bi_bo_w16')]
def regs(s):
 r=set()
 for m in re.finditer(r'\bv(?:\[(\d+):(\d+)\]|(\d+))',s):
  r.update(range(int(m[1]),int(m[2])+1) if m[1] else [int(m[3])])
 return sum(1<<n for n in r)
res={}
for mod,name in names:
 f=P/'work'/mod/(name+'.s');ins=[]
 for line,l in enumerate(f.read_text().splitlines(),1):
  m=re.search(r'^\s*(\S+)\s*(.*?)\s*// ([0-9A-Fa-f]+):',l)
  if not m:continue
  op,args,pc=m[1],m[2],int(m[3],16);defs=0;uses=0
  for part in (op+' '+args).split('::'):
   q=part.strip().split(None,1)
   if len(q)<2:continue
   opc,oper=q;dst=oper.split(',',1)[0];d=regs(dst) if (opc.startswith(('v_','ds_load','global_load','buffer_load','flat_load')) and not opc.startswith(('v_cmp','v_readfirstlane','v_writelane'))) else 0
   defs|=d;uses|=regs(oper.split(',',1)[1] if d and ',' in oper else oper)
  ins.append({'line':line,'pc':pc,'op':op,'args':args,'defs':defs,'uses':uses,'text':l.strip()})
 pcidx={x['pc']:i for i,x in enumerate(ins)};succ=[]
 for i,x in enumerate(ins):
  s=[] if x['op'] in ['s_endpgm','s_setpc_b64'] else ([i+1] if i+1<len(ins) else [])
  if x['op']=='s_branch' or x['op'].startswith('s_cbranch'):
   n=int(x['args'].split()[0],0);n=n-65536 if n>=32768 else n;target=x['pc']+4+4*n
   if x['op']=='s_branch':s=[]
   if target in pcidx:s.append(pcidx[target])
   else:raise ValueError((name,x['text'],hex(target)))
  succ.append(s)
 live=[0]*len(ins);changed=True;iterations=0
 while changed:
  changed=False;iterations+=1
  for i in range(len(ins)-1,-1,-1):
   out=0
   for s in succ[i]:out|=live[s]
   now=ins[i]['uses']|(out&~ins[i]['defs'])
   if now!=live[i]:live[i]=now;changed=True
 peak=max(n.bit_count() for n in live);idx=[i for i,n in enumerate(live) if n.bit_count()==peak];barriers=[x['line'] for x in ins if 'barrier' in x['op']]
 out=P/'work'/(name+'-liveness.csv')
 with out.open('w') as f:
  w=csv.writer(f);w.writerow(['line','pc','live_in_count','live_registers','instruction'])
  for x,n in zip(ins,live):w.writerow([x['line'],hex(x['pc']),n.bit_count(),' '.join('v'+str(i) for i in range(256) if n>>i&1),x['text']])
 first=idx[0];res[name]={'peak_registers':peak,'peak_lines':[ins[i]['line'] for i in idx],'first_peak_excerpt':[x['text'] for x in ins[max(0,first-4):first+8]],'barriers':barriers,'barrier_live':[{'line':x['line'],'count':live[j].bit_count(),'registers':[k for k in range(256) if live[j]>>k&1]} for j,x in enumerate(ins) if 'barrier' in x['op']],'backedges':[{'from_line':ins[j]['line'],'to_line':ins[t]['line'],'live_count':live[t].bit_count(),'registers':[k for k in range(256) if live[t]>>k&1]} for j,ss in enumerate(succ) for t in ss if t<=j],'iterations':iterations,'method':'CFG bitset may-liveness of physical VGPRs; partial lane writes/implicit exec semantics not lane-exact; not source variable names','allocated_max_register':max((regs(x['args']).bit_length() for x in ins),default=0)}
(P/'liveness-summary.json').write_text(json.dumps(res,indent=2));print(json.dumps({k:{x:v for x,v in r.items() if x not in ['peak_lines','first_peak_excerpt']} for k,r in res.items()},indent=2))
